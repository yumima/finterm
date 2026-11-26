#!/usr/bin/env python3
"""
The whole 13F universe, locally — SEC's quarterly data sets into SQLite.

WHY THIS REPLACES THE CURATED LIST
    Bloomberg's HDS does not curate. It shows every institutional holder of a
    security because Bloomberg ingests every 13F filing and serves the inverted
    index. One quarter of SEC's own data set carries 10,671 distinct filers and
    3.8 million positions, so a hand-picked list of twenty firms covers 0.2% of
    the universe and silently answers "who owns this" with "of the twenty firms
    I happened to choose, these".

    SEC publishes the same filings as bulk quarterly TSVs, so the complete index
    is buildable locally. Ingest once per quarter, then every lookup is a local
    query with no network at all — which is also what makes it fast enough to
    load on a symbol change instead of behind a button.

JOINING ON CUSIP, NOT ON NAMES
    13F reports CUSIPs. Matching a ticker to a holding by ISSUER NAME is a
    heuristic and it fails in the worst direction: "Apple Inc." normalises to
    APPLE, which is a prefix of APPLE HOSPITALITY REIT, so a REIT position gets
    reported as an AAPL position with a real share count. CUSIP is exact.

    Ticker to CUSIP comes from OpenFIGI — Bloomberg's own open symbology, free
    and keyless at low volume. The FIGI column in SEC's own file is populated on
    under 10% of rows, so it cannot be the join key; OpenFIGI can.

WHAT IS DELIBERATELY NOT COUNTED AS A HOLDING
    Option positions live in the same table, distinguished only by PUTCALL. A
    put is a bearish position. They are stored, labelled, and excluded from the
    stock position and from every weight — a manager holding puts must never
    read as long the underlying.

ACTIONS
    status   {}                        what is ingested locally
    ingest   {"url"?, "quarter"?}      download + index one quarterly data set
    holders  {"ticker"|"cusip", "limit"?, "sort"?: value|weight}
    movers   {"limit"?, "sort"?, "min_holders"?}   breadth change per security
    ticker_totals {"quarter"?}         13F shares and holder count per ticker
    book     {"cik"|"manager"}         one manager's whole book from the index
    resolve  {"ticker"}                ticker -> CUSIP via OpenFIGI
"""

import io
import json
import os
import re as _re
import sqlite3
import sys
import time
import urllib.request
import zipfile
import sec_http  # the one SEC client: contact identity, shared pacing, 404 vs refusal
from finterm_paths import market_data_path

DATASET_INDEX = "https://www.sec.gov/dera/data/form-13f"
OPENFIGI_URL = "https://api.openfigi.com/v3/mapping"
# Optional and per-user. Keyless OpenFIGI allows 25 requests a minute, which
# puts the full 22,820-CUSIP map at about 95 minutes; a free key raises that to
# 25 requests per 6 seconds and turns it into a couple of minutes. Never
# bundled — read from the environment the app injects.
OPENFIGI_KEY = os.environ.get("OPENFIGI_API_KEY", "")


def db_path():
    """Where the index lives: the app's market_data folder (finterm_paths)."""
    return market_data_path("form13f.sqlite")


def _pad_cik(cik):
    """EDGAR keys CIKs as ten zero-padded digits."""
    return str(cik).lstrip("0").zfill(10)


def yahoo_ticker(raw):
    """An OpenFIGI ticker as the rest of the terminal spells it (Yahoo).

    OpenFIGI writes share classes with a slash — BRK/B, BF/B, HEI/A — where
    Yahoo, and so Equity Research, the charts and every lookup by symbol, use a
    dash: BRK-B. And for convertible notes it returns a bond description
    ("BA 6 10/15/27", "ORCL 6.5 01/15/29 D") in the ticker field; that is not a
    tradable symbol, and opening it anywhere fails, so it maps to no ticker.
    No real ticker contains a space.
    """
    t = (raw or "").upper().strip().rstrip("*")  # "EA*": OpenFIGI's mark on a delisted line
    if not t or " " in t:
        return ""
    return t.replace("/", "-")


def _normalise_tickers(con):
    """One-off rewrite of a map built before yahoo_ticker() existed — the map
    itself and ticker_totals, which keeps its own copy of the ticker per
    quarter and is never rebuilt once a quarter has rows. Checks first, so the
    common case (nothing to fix) never takes a write lock."""
    tables = ["cusip_ticker"]
    if con.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='ticker_totals'").fetchone():
        tables.append("ticker_totals")
    stale = [t for t in tables
             if con.execute(f"SELECT 1 FROM {t} WHERE ticker LIKE '%/%' OR ticker LIKE '% %' "
                            "OR ticker LIKE '%*' LIMIT 1").fetchone()]
    if not stale:
        return
    for t in stale:
        con.execute(f"UPDATE {t} SET ticker='' WHERE ticker LIKE '% %'")
        con.execute(f"UPDATE {t} SET ticker=REPLACE(ticker, '/', '-') WHERE ticker LIKE '%/%'")
        con.execute(f"UPDATE {t} SET ticker=RTRIM(ticker, '*') WHERE ticker LIKE '%*'")
    con.commit()


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA synchronous=NORMAL")
    # A reader on the watchlist path can arrive while an ingest is writing;
    # it should wait its turn, not fail on the first locked page.
    con.execute("PRAGMA busy_timeout=15000")
    con.executescript("""
        CREATE TABLE IF NOT EXISTS quarters (
            quarter TEXT PRIMARY KEY,     -- e.g. 2026-03-31
            source  TEXT,
            filers  INTEGER,
            rows    INTEGER,
            ingested_at TEXT,
            -- 1 when the quarter holds only the filers pulled directly from
            -- EDGAR ahead of the bulk data set. A partial quarter must never
            -- be served as the default: AAPL has 5,716 institutional holders,
            -- and answering with the 20 that happen to be loaded would be a
            -- complete-looking answer that is wrong by two orders of magnitude.
            partial INTEGER DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS filings (
            accession TEXT PRIMARY KEY,
            quarter   TEXT,
            cik       TEXT,          -- the stable filer id, from SUBMISSION.tsv
            manager   TEXT,
            is_amendment INTEGER
        );
        CREATE INDEX IF NOT EXISTS ix_filings_cik ON filings(cik, quarter);
        CREATE TABLE IF NOT EXISTS holdings (
            accession TEXT,
            quarter   TEXT,
            cusip     TEXT,
            issuer    TEXT,
            class     TEXT,
            put_call  TEXT,          -- '', 'PUT', 'CALL'
            value     REAL,
            shares    REAL
        );
        CREATE INDEX IF NOT EXISTS ix_hold_cusip ON holdings(cusip, quarter);
        CREATE INDEX IF NOT EXISTS ix_hold_acc   ON holdings(accession);
        -- (accession, cusip) turns the prior-quarter lookup from a scan per
        -- holder into a seek. Without it the join over 7,850 holders took 36s.
        CREATE INDEX IF NOT EXISTS ix_hold_acc_cusip ON holdings(accession, cusip);
        CREATE INDEX IF NOT EXISTS ix_filings_q  ON filings(quarter);
        CREATE TABLE IF NOT EXISTS cusip_ticker (
            cusip  TEXT PRIMARY KEY,
            ticker TEXT,
            name   TEXT,
            source TEXT
        );
        CREATE INDEX IF NOT EXISTS ix_ct_ticker ON cusip_ticker(ticker);
        -- Book totals precomputed at ingest. Deriving them per query with a
        -- correlated subquery over 3.3m rows made a holder lookup take seconds,
        -- which is the difference between loading on a symbol change and hiding
        -- behind a button.
        CREATE TABLE IF NOT EXISTS books (
            accession TEXT PRIMARY KEY,
            quarter   TEXT,
            cik       TEXT,
            manager   TEXT,
            stock_value REAL,      -- options excluded: an equity weight needs
            stock_count INTEGER    -- an equity denominator
        );
        CREATE INDEX IF NOT EXISTS ix_books_q ON books(quarter, stock_value);
        CREATE INDEX IF NOT EXISTS ix_books_cik ON books(cik, quarter);
    """)
    cols = {r[1] for r in con.execute("PRAGMA table_info(quarters)")}
    if "partial" not in cols:
        con.execute("ALTER TABLE quarters ADD COLUMN partial INTEGER DEFAULT 0")
    # The day a filing reached SEC: what "how current is this" is measured in.
    if "filed" not in {r[1] for r in con.execute("PRAGMA table_info(filings)")}:
        con.execute("ALTER TABLE filings ADD COLUMN filed TEXT")
    con.commit()
    _normalise_tickers(con)
    return con


# ── Ticker <-> CUSIP ────────────────────────────────────────────────────────

def resolve_ticker(ticker, con=None):
    """Ticker -> CUSIP, exactly, via OpenFIGI. Cached in the local index.

    OpenFIGI is Bloomberg's open symbology service: free, keyless at low volume,
    and authoritative for this mapping. It is used rather than issuer-name
    matching because a name match cannot distinguish Apple Inc. from Apple
    Hospitality REIT, and getting that wrong attributes another company's
    position to your ticker.
    """
    own = con is None
    con = con or connect()
    try:
        # The map is in Yahoo form (BRK-B); accept the other spellings of a
        # share class callers arrive with (BRK.B, BRK/B).
        t = yahoo_ticker(str(ticker).replace(".", "-"))
        if not t:
            # "BRK B", a bond description, blank: not a ticker. Querying for ''
            # would match an unresolved row and hand back some other security
            # — attributing its holders to this symbol.
            return {"error": f"{str(ticker).strip()!r} is not a ticker symbol",
                    "ticker": str(ticker), "unresolved": True}
        row = con.execute(
            "SELECT cusip, name FROM cusip_ticker WHERE ticker=? LIMIT 1", (t,)).fetchone()
        if row:
            return {"ticker": t, "cusip": row[0], "name": row[1], "source": "cache"}

        # Not found means not resolved YET, and that is reported as such. There
        # is deliberately no name-matching fallback: the map is built by
        # resolve_cusips() from CUSIP to ticker, which is exact, and a guess
        # here would undo the whole point of having it.
        return {"error": f"{t} is not in the CUSIP map yet — run the symbol resolver",
                "ticker": t, "unresolved": True}
    finally:
        if own:
            con.close()


# Bloomberg exchange codes of US venues (NYSE UN, Nasdaq UQ/UW/UR, NYSE
# American UA, NYSE Arca UP, Cboe UF/UD, IEX/others, OTC UV). Listed, not
# "starts with U": UY is Uruguay and UG Uganda.
US_VENUES = frozenset(("UA", "UB", "UC", "UD", "UF", "UJ", "UL", "UM", "UN", "UO", "UP", "UQ",
                       "UR", "UT", "UU", "UV", "UW", "UX", "UZ"))


def us_listing(recs):
    """The US line among OpenFIGI's records for one CUSIP, or None.

    The US composite ("US") first. A security whose composite line is gone —
    Exxon Mobil's old CUSIP after its 2026 reorganisation lists only single
    venues — still has its US venues, all exchange codes beginning with "U"
    (UN NYSE, UQ Nasdaq, UA, UP, UZ ...); their most common ticker is the US
    one. Never the first record regardless: that was a Colombian line, and
    "EXMOC" stood in for Exxon on every screen (2026-11-16)."""
    recs = recs or []
    comp = next((r for r in recs if (r.get("exchCode") or "").upper() == "US" and r.get("ticker")), None)
    if comp:
        return comp
    us = [r for r in recs if (r.get("exchCode") or "").upper() in US_VENUES and r.get("ticker")]
    if not us:
        return None
    counts = {}
    for r in us:
        counts[r["ticker"]] = counts.get(r["ticker"], 0) + 1
    best = max(counts, key=lambda t: (counts[t], -len(t)))
    return next(r for r in us if r["ticker"] == best)


def recheck_tickers(limit=3000, batch=10, progress=None):
    """Re-resolve the most valuable CUSIPs once with us_listing(). Mappings
    made before it could hold a foreign line's ticker (EXMOC for Exxon,
    HONGBP for Honeywell). Marked done, so it runs once per index."""
    con = connect()
    try:
        con.execute("CREATE TABLE IF NOT EXISTS map_fixes (name TEXT PRIMARY KEY, done_at TEXT)")
        if con.execute("SELECT 1 FROM map_fixes WHERE name='us_listing'").fetchone():
            return {"skipped": "already rechecked"}
        todo = [c for (c,) in con.execute("""
            SELECT ct.cusip FROM cusip_ticker ct JOIN holdings h ON h.cusip = ct.cusip
             WHERE ct.source = 'openfigi' AND h.put_call = ''
             GROUP BY ct.cusip ORDER BY SUM(h.value) DESC LIMIT ?""", (int(limit),))]
        changed = []
        failed_chunks = 0
        for i in range(0, len(todo), batch):
            chunk = todo[i:i + batch]
            body = json.dumps([{"idType": "ID_CUSIP", "idValue": c} for c in chunk]).encode()
            hdrs = {"Content-Type": "application/json"}
            if OPENFIGI_KEY:
                hdrs["X-OPENFIGI-APIKEY"] = OPENFIGI_KEY
            try:
                data = json.loads(urllib.request.urlopen(
                    urllib.request.Request(OPENFIGI_URL, data=body, headers=hdrs), timeout=45).read())
            except Exception:
                failed_chunks += 1  # the run is not marked done: asked again next time
                time.sleep(6)
                continue
            for cusip, res in zip(chunk, data):
                recs = res.get("data") or []
                if not recs:
                    continue  # nothing better known: keep what is there
                pick = us_listing(recs)
                if not pick:
                    # No US line any more (acquired, delisted: EA, WBD): the old
                    # ticker still names the quarters it was held in — unless it
                    # IS one of the foreign lines, the very pick being undone.
                    old_t = con.execute("SELECT ticker FROM cusip_ticker WHERE cusip=?", (cusip,)).fetchone()[0]
                    foreign = {yahoo_ticker(r.get("ticker")) for r in recs}
                    if old_t and old_t in foreign:
                        con.execute("UPDATE cusip_ticker SET ticker='', source='openfigi-nonus' WHERE cusip=?",
                                    (cusip,))
                        changed.append((cusip, old_t, ""))
                    continue
                new_t = yahoo_ticker(pick.get("ticker"))
                old_t = con.execute("SELECT ticker FROM cusip_ticker WHERE cusip=?", (cusip,)).fetchone()[0]
                if new_t and new_t != old_t:
                    con.execute("UPDATE cusip_ticker SET ticker=?, source='openfigi' WHERE cusip=?",
                                (new_t, cusip))
                    changed.append((cusip, old_t, new_t))
            con.commit()
            if progress:
                progress(i + len(chunk), len(todo))
            time.sleep(0.25 if OPENFIGI_KEY else 2.5)
        if changed and con.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name='ticker_totals'").fetchone():
            for cusip, _o, new_t in changed:
                con.execute("UPDATE ticker_totals SET ticker=? WHERE cusip=?", (new_t, cusip))
        if not failed_chunks:
            con.execute("INSERT OR REPLACE INTO map_fixes VALUES ('us_listing', datetime('now'))")
        con.commit()
        return {"checked": len(todo), "changed": changed, "failed_chunks": failed_chunks}
    finally:
        con.close()


def resolve_cusips(limit=2000, quarter=None, batch=10, progress=None):
    """Build the CUSIP -> ticker map from the index, exactly, via OpenFIGI.

    This is what removes the last heuristic. Matching a ticker to a holding by
    ISSUER NAME cannot distinguish Apple Inc. from Apple Hospitality REIT, and
    getting it wrong reports another company's position under your ticker with a
    real share count. OpenFIGI answers CUSIP -> ticker authoritatively.

    Resolved in descending order of aggregate value held, so the names anyone
    actually looks up are covered first. A CUSIP OpenFIGI does not recognise is
    recorded with an empty ticker so it is not retried every run — an unresolved
    identifier is a known gap, not a reason to start guessing.
    """
    con = connect()
    try:
        if not quarter:
            row = con.execute(
                "SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 1").fetchone()
            if not row:
                return {"error": "no 13F data ingested yet"}
            quarter = row[0]
        rows = con.execute("""
            SELECT h.cusip, SUM(h.value) v FROM holdings h
             WHERE h.quarter=? AND h.put_call=''
               AND h.cusip NOT IN (SELECT cusip FROM cusip_ticker)
             GROUP BY h.cusip ORDER BY v DESC LIMIT ?
        """, (quarter, int(limit))).fetchall()
        todo = [r[0] for r in rows]
        if not todo:
            return {"quarter": quarter, "resolved": 0, "remaining": 0,
                    "note": "every CUSIP in this quarter is already mapped"}

        resolved = failed = 0
        for i in range(0, len(todo), batch):
            chunk = todo[i:i + batch]
            body = json.dumps([{"idType": "ID_CUSIP", "idValue": c} for c in chunk]).encode()
            hdrs = {"Content-Type": "application/json"}
            if OPENFIGI_KEY:
                hdrs["X-OPENFIGI-APIKEY"] = OPENFIGI_KEY
            req = urllib.request.Request(OPENFIGI_URL, data=body, headers=hdrs)
            try:
                data = json.loads(urllib.request.urlopen(req, timeout=45).read())
            except Exception:
                # OpenFIGI rate-limits without a key. Back off rather than
                # hammering it; the run is resumable because resolved CUSIPs are
                # committed as they land.
                time.sleep(6)
                continue
            for cusip, res in zip(chunk, data):
                recs = res.get("data") or []
                if recs:
                    # One CUSIP maps to every listing of the security, including
                    # foreign lines — Philip Morris comes back as "4I1" on a
                    # German exchange before "PM". A US-listed record is what a
                    # US ticker lookup has to find, so prefer one; falling back
                    # to the first record only when there is no US listing.
                    pick = us_listing(recs)
                    con.execute("INSERT OR REPLACE INTO cusip_ticker VALUES (?,?,?,?)",
                                (cusip, yahoo_ticker(pick.get("ticker")) if pick else "",
                                 (pick or recs[0]).get("name") or "",
                                 "openfigi" if pick else "openfigi-nonus"))
                    resolved += 1
                else:
                    con.execute("INSERT OR REPLACE INTO cusip_ticker VALUES (?,?,?,?)",
                                (cusip, "", "", "openfigi-miss"))
                    failed += 1
            con.commit()
            if progress:
                progress(resolved + failed, len(todo))
            # 25 requests a minute keyless; 25 per 6 seconds with a key.
            time.sleep(0.25 if OPENFIGI_KEY else 2.5)

        remaining = con.execute("""
            SELECT COUNT(*) FROM (SELECT DISTINCT cusip FROM holdings
             WHERE quarter=? AND put_call=''
               AND cusip NOT IN (SELECT cusip FROM cusip_ticker))
        """, (quarter,)).fetchone()[0]
        return {"quarter": quarter, "resolved": resolved, "unrecognised": failed,
                "remaining": remaining}
    finally:
        con.close()


# ── Ingest ──────────────────────────────────────────────────────────────────

def available_datasets():
    """Quarterly data-set URLs published by SEC, newest first."""
    import re
    r, _ = sec_http.get(DATASET_INDEX, timeout=60)
    if r is None:
        return []
    hits = re.findall(r'href="([^"]*form-13f-data-sets/[^"]*\.zip)"', r.text)
    return ["https://www.sec.gov" + h if h.startswith("/") else h for h in hits]


def ingest(url=None, progress=None):
    """Download one quarterly data set and index it. Returns a summary."""
    if not url:
        urls = available_datasets()
        if not urls:
            return {"error": "could not list SEC 13F data sets"}
        url = urls[0]

    r, _ = sec_http.get(url, timeout=600)
    if r is None:
        return {"error": f"download failed: {url}"}

    con = connect()
    try:
        with zipfile.ZipFile(io.BytesIO(r.content)) as z:
            names = {n.upper(): n for n in z.namelist()}
            if "COVERPAGE.TSV" not in names or "INFOTABLE.TSV" not in names:
                return {"error": "data set is missing COVERPAGE/INFOTABLE"}

            # SUBMISSION carries the CIK, which is the only stable identifier
            # for a filer across quarters — accession numbers change every
            # filing, and matching on the manager NAME would reintroduce exactly
            # the guessing this index exists to remove (firms rename, and two
            # unrelated filers can share a name fragment).
            acc_cik = {}
            acc_filed = {}
            with z.open(names["SUBMISSION.TSV"]) as f:
                head = None
                for line in io.TextIOWrapper(f, encoding="utf-8", errors="replace"):
                    parts = line.rstrip("\n").split("\t")
                    if head is None:
                        head = {k: i for i, k in enumerate(parts)}
                        continue
                    # An ABSENT cik must stay absent. zfill on an empty string
                    # produces "0000000000", which looks like a real CIK: two
                    # unrelated filers missing a CIK then collide on it, the
                    # per-filer dedup keeps one and the other disappears from
                    # the index entirely. Only pad a value that exists.
                    raw = parts[head["CIK"]].strip()
                    acc_cik[parts[head["ACCESSION_NUMBER"]]] = raw.zfill(10) if raw else ""
                    if "FILING_DATE" in head:
                        acc_filed[parts[head["ACCESSION_NUMBER"]]] = _iso_quarter(parts[head["FILING_DATE"]])

            # Cover pages carry the manager name and the quarter reported for.
            acc_meta = {}
            with z.open(names["COVERPAGE.TSV"]) as f:
                head = None
                for line in io.TextIOWrapper(f, encoding="utf-8", errors="replace"):
                    parts = line.rstrip("\n").split("\t")
                    if head is None:
                        head = {k: i for i, k in enumerate(parts)}
                        continue
                    acc = parts[head["ACCESSION_NUMBER"]]
                    acc_meta[acc] = (
                        _iso_quarter(parts[head["REPORTCALENDARORQUARTER"]]),
                        parts[head["FILINGMANAGER_NAME"]],
                        1 if parts[head["ISAMENDMENT"]].strip().upper() == "Y" else 0,
                        acc_cik.get(acc, ""),
                    )

            quarters = {}
            for m in acc_meta.values():
                quarters[m[0]] = quarters.get(m[0], 0) + 1
            # A data set spans filings received in a window, so it can carry a
            # tail of late filings for older quarters. Index the dominant one.
            quarter = max(quarters, key=quarters.get) if quarters else ""

            con.execute("DELETE FROM holdings WHERE quarter=?", (quarter,))
            con.execute("DELETE FROM filings WHERE quarter=?", (quarter,))
            # One filing per (filer, quarter). An amendment restates or adds to
            # the original, so counting both double-counts the book; the
            # original 13F-HR is preferred and an amendment is used only when
            # there is no original in this data set.
            chosen = {}
            for a, m in acc_meta.items():
                if m[0] != quarter:
                    continue
                key = m[3] or a
                prev = chosen.get(key)
                if prev is None or (prev[1][2] == 1 and m[2] == 0):
                    chosen[key] = (a, m)
            keep = {a for a, _ in chosen.values()}
            con.executemany(
                "INSERT OR REPLACE INTO filings (accession, quarter, cik, manager, is_amendment, filed) "
                "VALUES (?,?,?,?,?,?)",
                [(a, m[0], m[3], m[1], m[2], acc_filed.get(a)) for a, m in chosen.values()])

            n_rows = 0
            batch = []
            with z.open(names["INFOTABLE.TSV"]) as f:
                head = None
                for line in io.TextIOWrapper(f, encoding="utf-8", errors="replace"):
                    parts = line.rstrip("\n").split("\t")
                    if head is None:
                        head = {k: i for i, k in enumerate(parts)}
                        continue
                    acc = parts[head["ACCESSION_NUMBER"]]
                    if acc not in keep:
                        continue
                    # Share lines only; a principal amount is a bond and cannot
                    # be added to a share count.
                    if parts[head["SSHPRNAMTTYPE"]].strip().upper() != "SH":
                        continue
                    try:
                        value = float(parts[head["VALUE"]] or 0)
                        shares = float(parts[head["SSHPRNAMT"]] or 0)
                    except ValueError:
                        continue
                    batch.append((
                        acc, quarter,
                        parts[head["CUSIP"]].strip().upper(),
                        parts[head["NAMEOFISSUER"]].strip(),
                        parts[head["TITLEOFCLASS"]].strip(),
                        parts[head["PUTCALL"]].strip().upper(),
                        value, shares,
                    ))
                    n_rows += 1
                    if len(batch) >= 50000:
                        con.executemany(
                            "INSERT INTO holdings VALUES (?,?,?,?,?,?,?,?)", batch)
                        batch.clear()
                        if progress:
                            progress(n_rows)
            if batch:
                con.executemany("INSERT INTO holdings VALUES (?,?,?,?,?,?,?,?)", batch)

        # Collapse duplicate rows for the same security within one filing.
        #
        # A filer reports the SAME CUSIP on several rows, one per internal
        # manager or discretion type — Berkshire files Ally Financial three
        # times. The XML parser aggregates for exactly this reason and the bulk
        # path did not, so AAPL showed 8,404 "holders" across 6,005 filings, and
        # the prior-quarter join multiplied that to 74,973. Sums are unaffected;
        # per-holder rows were not.
        con.execute("""
            CREATE TEMP TABLE _agg AS
            SELECT accession, quarter, cusip,
                   MIN(issuer) AS issuer, MIN(class) AS class, put_call,
                   SUM(value) AS value, SUM(shares) AS shares
              FROM holdings WHERE quarter=?
             GROUP BY accession, cusip, put_call
        """, (quarter,))
        con.execute("DELETE FROM holdings WHERE quarter=?", (quarter,))
        con.execute("INSERT INTO holdings SELECT accession, quarter, cusip, issuer, class, "
                    "put_call, value, shares FROM _agg")
        n_rows = con.execute("SELECT COUNT(*) FROM holdings WHERE quarter=?",
                             (quarter,)).fetchone()[0]
        con.execute("DROP TABLE _agg")

        # Units. SEC moved 13F VALUE from thousands to whole dollars for 2023
        # onward, and the bulk files carry whatever the filing used — so an
        # older data set would be off by 1000x in every weight with nothing on
        # screen to show it. The date decides, checked against the implied price
        # per share across the quarter, because a units error is invisible.
        med = con.execute("""
            SELECT value / shares FROM holdings
             WHERE quarter=? AND put_call='' AND shares > 0
             ORDER BY value / shares LIMIT 1
            OFFSET (SELECT COUNT(*) / 2 FROM holdings
                     WHERE quarter=? AND put_call='' AND shares > 0)
        """, (quarter, quarter)).fetchone()
        implied = med[0] if med else 0.0
        if implied and implied < 1.0:
            # Real equities do not trade below a dollar across a whole market.
            con.execute("UPDATE holdings SET value = value * 1000 WHERE quarter=?", (quarter,))
            value_basis = "thousands (scaled to dollars)"
        else:
            value_basis = "whole dollars"

        con.execute("DELETE FROM books WHERE quarter=?", (quarter,))
        con.execute("""
            INSERT INTO books (accession, quarter, cik, manager, stock_value, stock_count)
            SELECT h.accession, h.quarter, f.cik, f.manager, SUM(h.value), COUNT(*)
              FROM holdings h JOIN filings f ON f.accession=h.accession
             WHERE h.quarter=? AND h.put_call=''
             GROUP BY h.accession
        """, (quarter,))

        filers = con.execute(
            "SELECT COUNT(DISTINCT manager) FROM filings WHERE quarter=?", (quarter,)).fetchone()[0]
        con.execute("INSERT OR REPLACE INTO quarters VALUES (?,?,?,?,datetime('now'),0)",
                    (quarter, url, filers, n_rows))
        con.commit()
        return {"quarter": quarter, "filers": filers, "rows": n_rows, "source": url,
                "value_basis": value_basis, "implied_price_median": implied,
                "db": db_path()}
    finally:
        con.close()


def _iso_quarter(s):
    """'31-MAR-2026' -> '2026-03-31'."""
    months = {"JAN": "01", "FEB": "02", "MAR": "03", "APR": "04", "MAY": "05", "JUN": "06",
              "JUL": "07", "AUG": "08", "SEP": "09", "OCT": "10", "NOV": "11", "DEC": "12"}
    try:
        d, m, y = s.strip().split("-")
        return f"{y}-{months[m.upper()[:3]]}-{int(d):02d}"
    except Exception:
        return s.strip()


# ── Queries ─────────────────────────────────────────────────────────────────

def newer_available():
    """Is SEC publishing a quarter this index does not have?

    Without this the index silently ages: it was built once, it still answers,
    and nothing says the answers are a quarter behind.
    """
    qs = status().get("quarters", [])
    # Only COMPLETE quarters count as ingested. A partial one was pulled from
    # EDGAR ahead of its data set, so the data set is still unseen.
    have = {q["quarter"] for q in qs if not q.get("partial")}
    urls = available_datasets()
    return {"indexed": sorted(have, reverse=True),
            "partial": sorted((q["quarter"] for q in qs if q.get("partial")), reverse=True),
            "newest_dataset": urls[0].rsplit("/", 1)[-1] if urls else "",
            "datasets_available": len(urls),
            # Sets whose filing window ends after the newest one ingested —
            # not every historical set the index never meant to hold (that
            # count read "52 newer data sets" on an index one set behind).
            "unseen_datasets": _unseen_datasets(urls)}


def _unseen_datasets(urls):
    con = connect()
    try:
        names = {(s or "").rsplit("/", 1)[-1] for (s,) in con.execute(
            "SELECT source FROM quarters WHERE COALESCE(partial,0)=0")}
        if con.execute("SELECT 1 FROM sqlite_master WHERE name='datasets_in'").fetchone():
            names |= {n for (n,) in con.execute("SELECT name FROM datasets_in")}
    finally:
        con.close()
    ends = [dataset_window(n)[1] for n in names if dataset_window(n)]
    newest = max(ends, default="")
    return sum(1 for u in urls if (dataset_window(u.rsplit("/", 1)[-1]) or ("", ""))[1] > newest)


def status():
    con = connect()
    try:
        rows = con.execute(
            "SELECT quarter, filers, rows, ingested_at, COALESCE(partial,0) "
            "FROM quarters ORDER BY quarter DESC").fetchall()
        # `partial` travels with every quarter. The whole point of the column is
        # that a partial quarter must not look like a bulk one, and a status
        # listing that omits it presents "20 filers" the same way it presents
        # "10,647".
        return {"db": db_path(),
                "quarters": [{"quarter": r[0], "filers": r[1], "rows": r[2],
                              "ingested_at": r[3], "partial": bool(r[4])} for r in rows]}
    finally:
        con.close()


# A "% of book" ranking is meaningless without a floor on the book. 10,647
# filers include single-position family accounts and shells, and one of those
# holding nothing but Apple scores 100% — ranking above Berkshire's 22%. These
# are stated, returned with the result, and adjustable, rather than hidden.
MIN_BOOK_VALUE = 50_000_000.0   # a book too small to be a firm's equity book
MIN_BOOK_POSITIONS = 5          # a book too narrow for a weight to mean anything


# A book this wide is not expressing a view on any one name — it is tracking a
# benchmark or running client mandates. BlackRock files 5,413 positions. Leaving
# them in a demand signal measures index rebalancing, not conviction.
MAX_DISCRETIONARY_POSITIONS = 1000


# Below this book size a "% of their book" ranking surfaces two-person
# advisers with one name in the account, not conviction. Bloomberg's HDS sorts
# by position size; the weight sort is the second view, and it needs a floor
# an order of magnitude above the one the value sort uses.
MIN_BOOK_FOR_WEIGHT_SORT = 1_000_000_000.0

# The five passive complexes, by name. Breadth (stock_count) already labels a
# 40,000-name book as broad; the names are here so an index arm is called what
# it is rather than "broad". Kept short and visible: it is a label, not a filter.
PASSIVE_COMPLEXES = ("blackrock", "vanguard", "state street", "geode", "ssga")


def holder_tier(manager, stock_count):
    """'index' for a named passive complex, 'broad' for a book too wide to hold
    a view on any one name, 'focused' otherwise. 13F cannot tell a hedge fund
    from a pension, so this never claims to."""
    m = (manager or "").lower()
    if any(k in m for k in PASSIVE_COMPLEXES):
        return "index"
    if (stock_count or 0) >= MAX_DISCRETIONARY_POSITIONS:
        return "broad"
    return "focused"


def holders(ticker=None, cusip=None, limit=60, quarter=None,
            min_book=MIN_BOOK_VALUE, min_positions=MIN_BOOK_POSITIONS,
            max_positions=None, sort="value"):
    """Filers holding this security whose 13F book passes the floors.

    Only filers with an equity book of at least `min_book` dollars and at
    least `min_positions` stock positions (and, when set, at most
    `max_positions`) are counted — see MIN_BOOK_VALUE / MIN_BOOK_POSITIONS.
    `sort` is "value" — position size, the HDS default — or "weight", the
    position as a share of the filer's own book, which answers a different
    question and raises the book-size floor so the answer is Berkshire rather
    than a shell. Totals, counts and concentration are computed over every
    holder that passes those floors, before the display limit is applied.
    The floors are returned with the result so the UI can state them.
    """
    con = connect()
    try:
        if sort == "weight":
            min_book = max(float(min_book), MIN_BOOK_FOR_WEIGHT_SORT)
        name = ""
        if not cusip:
            res = resolve_ticker(ticker, con)
            if res.get("error"):
                return res
            cusip, name = res["cusip"], res.get("name", "")
        # A complete quarter by default. The partial one is newer and is worth
        # knowing about, but "who owns this" answered from the 400 filers that
        # happen to be loaded is a confident wrong answer.
        newer_partial = None
        if not quarter:
            row = con.execute(
                "SELECT quarter FROM quarters WHERE COALESCE(partial,0)=0 "
                "ORDER BY quarter DESC LIMIT 1").fetchone()
            if not row:
                return {"error": "no complete 13F quarter ingested yet", "cusip": cusip}
            quarter = row[0]
            p = con.execute(
                "SELECT quarter, filers FROM quarters WHERE COALESCE(partial,0)=1 "
                "AND quarter > ? ORDER BY quarter DESC LIMIT 1", (quarter,)).fetchone()
            if p:
                newer_partial = {"quarter": p[0], "filers": p[1]}

        # Book totals exclude options: an equity weight measured against a
        # denominator that includes option notionals is not an equity weight.
        # Left-join the prior quarter on CIK — the only stable filer id — so
        # every holder carries what they did, not just what they hold. Joining
        # on the manager NAME would put one filer's history under another's.
        prior = con.execute(
            "SELECT quarter FROM quarters WHERE quarter < ? ORDER BY quarter DESC LIMIT 1",
            (quarter,)).fetchone()
        prior_q = prior[0] if prior else None
        # The prior quarter in today's terms: the same holding before a split
        # is fewer shares, and before a CUSIP change it sat under another CUSIP.
        prior_cusip, prior_factor = cusip, 1.0
        if prior_q:
            splits, links = _adjustments(con, prior_q, quarter, compute=False)
            if cusip in links:
                prior_cusip, prior_factor = links[cusip]
            else:
                prior_factor = splits.get(cusip, 1.0)
        # Not reportable last quarter at all: holders now are holders who
        # became visible (an IPO), not buyers.
        newly_listed = bool(prior_q) and prior_cusip not in prior_universe(con, prior_q)

        rows = con.execute("""
            SELECT b.manager, h.accession, h.issuer, h.class, h.put_call,
                   h.value, h.shares, b.stock_value, b.stock_count, b.cik,
                   ph.shares AS prior_shares,
                   pb.accession AS filed_prior
            FROM holdings h
            JOIN books b       ON b.accession = h.accession
            -- b.cik <> '' matters: a filer missing from SUBMISSION.tsv has an
            -- empty CIK, and an empty-to-empty join matches every other such
            -- filer — which inflated 7,850 holders of AAPL to 74,973.
            LEFT JOIN books pb ON pb.cik = b.cik AND b.cik <> '' AND pb.quarter = ?
            LEFT JOIN holdings ph ON ph.accession = pb.accession
                                 AND ph.cusip = ? AND ph.put_call = ''
            WHERE h.cusip = ? AND h.quarter = ?
              AND b.stock_value >= ? AND b.stock_count >= ?
              AND (? = 0 OR b.stock_count <= ?)
            ORDER BY h.value DESC
        """, (prior_q or "", prior_cusip, cusip, quarter,
              float(min_book), int(min_positions),
              int(max_positions or 0), int(max_positions or 0))).fetchall()

        out = []
        for (m, acc, issuer, cls, pc, value, shares, book_total, book_count, cik,
             prior_shares, filed_prior) in rows:
            weight = (value / book_total) if (book_total and not pc) else None
            rec = {
                "manager": m, "accession": acc, "cik": cik, "issuer": issuer, "class": cls,
                "put_call": pc, "is_derivative": bool(pc),
                "value": value, "shares": shares,
                "weight": weight, "book_total": book_total, "position_count": book_count,
                "tier": holder_tier(m, book_count),
            }
            if prior_q and not pc:
                if prior_shares is None:
                    # A filer with no prior-quarter FILING has not opened a
                    # position; we simply could not see them. Saying "new" there
                    # manufactures a decision out of a missing filing — which is
                    # exactly what Vanguard's entity reshuffle looks like.
                    rec["action"] = ("listed" if newly_listed else "new") if filed_prior else "first seen"
                    rec["shares_delta"] = shares
                    if not filed_prior:
                        rec["note"] = ("no prior-quarter filing from this filer, so an opening "
                                       "position cannot be told apart from a first appearance")
                else:
                    prior_shares = prior_shares * prior_factor
                    delta = shares - prior_shares
                    rec["shares_delta"] = delta
                    rec["pct_change"] = (delta / prior_shares) if prior_shares else None
                    rec["action"] = ("held" if (prior_shares > 0
                                                and abs(delta) / prior_shares < 0.01)
                                     else ("added" if delta > 0 else "trimmed"))
                rec["prior_shares"] = prior_shares
            out.append(rec)
        if sort == "weight":
            out.sort(key=lambda r: (r["weight"] or 0), reverse=True)
        else:
            sort = "value"
            out.sort(key=lambda r: (r["value"] or 0), reverse=True)
        stock = [r for r in out if not r["is_derivative"]]

        # Concentration, over every holder rather than the rows shown. The
        # first version of this screen computed "top five" over the 80 rows it
        # displayed and printed 93% for Apple; the figure across all 6,004
        # filers is 39%. A number computed over a slice must not be labelled
        # as the register's.
        by_value = sorted((r["value"] or 0.0 for r in stock), reverse=True)
        value_all = sum(by_value)
        top10_share = (sum(by_value[:10]) / value_all) if value_all > 0 else None
        broad_value = sum((r["value"] or 0.0) for r in stock if r["tier"] != "focused")
        broad_share = (broad_value / value_all) if value_all > 0 else None

        # Filers who held this last quarter and do not now. They cannot appear
        # in the query above — it is driven by CURRENT holdings — but "twelve
        # filers got out" is half of what changed, and counting it here avoids a
        # second round trip. Only filers who DID file this quarter count: one
        # who stopped filing has not sold, we simply cannot see them.
        exited = 0
        exited_shares = 0.0
        if prior_q:
            # pb.cik <> '' and COUNT(DISTINCT) are both load-bearing, for the
            # same reason the main query above carries the guard: thousands of
            # filers are missing from SUBMISSION.tsv, an empty CIK matches every
            # other empty CIK, and COUNT(*) then counts prior-filing x
            # current-book PAIRS rather than filers. A four-filer fixture
            # returned 70 without this.
            exited_shares = con.execute("""
                SELECT COALESCE(SUM(ph.shares), 0) FROM holdings ph
                  JOIN books pb ON pb.accession = ph.accession AND pb.cik <> ''
                  JOIN books cb ON cb.cik = pb.cik AND cb.quarter = ?
                 WHERE ph.cusip = ? AND ph.quarter = ? AND ph.put_call = ''
                   AND pb.stock_value >= ? AND pb.stock_count >= ?
                   AND (? = 0 OR pb.stock_count <= ?)
                   AND NOT EXISTS (SELECT 1 FROM holdings ch
                                    WHERE ch.accession = cb.accession
                                      AND ch.cusip = ph.cusip AND ch.put_call = '')
            """, (quarter, cusip, prior_q, float(min_book), int(min_positions),
                  int(max_positions or 0), int(max_positions or 0))).fetchone()[0]
            exited = con.execute("""
                SELECT COUNT(DISTINCT pb.cik) FROM holdings ph
                  JOIN books pb ON pb.accession = ph.accession AND pb.cik <> ''
                  JOIN books cb ON cb.cik = pb.cik AND cb.quarter = ?
                 WHERE ph.cusip = ? AND ph.quarter = ? AND ph.put_call = ''
                   AND pb.stock_value >= ? AND pb.stock_count >= ?
                   AND (? = 0 OR pb.stock_count <= ?)
                   AND NOT EXISTS (SELECT 1 FROM holdings ch
                                    WHERE ch.accession = cb.accession
                                      AND ch.cusip = ph.cusip AND ch.put_call = '')
            """, (quarter, cusip, prior_q, float(min_book), int(min_positions),
                  int(max_positions or 0), int(max_positions or 0))).fetchone()[0]
        buyers = sum(1 for r in stock if r.get("action") == "added")
        new = sum(1 for r in stock if r.get("action") == "new")
        sellers = sum(1 for r in stock if r.get("action") == "trimmed")
        # Totals across every filer that passes the book-size / position-count
        # floors, not just the rows returned. This is what makes an
        # institutional-ownership percentage computable from the filings
        # themselves; filers below the floors are excluded, so it is a floor.
        total_shares = sum(r["shares"] or 0 for r in stock)
        total_value = sum(r["value"] or 0 for r in stock)
        return {"ticker": (ticker or "").upper(), "cusip": cusip, "company": name,
                "quarter": quarter,
                "holder_count": len(stock),
                "option_holder_count": len(out) - len(stock),
                "total_shares_held": total_shares,
                "total_value_held": total_value,
                "prior_quarter": prior_q,
                "buyers": buyers, "new": new, "sellers": sellers, "exited": exited,
                "exited_shares": exited_shares,
                "top10_share": top10_share, "broad_share": broad_share,
                "sort": sort,
                "newer_partial": newer_partial,
                "min_book_value": min_book, "min_book_positions": min_positions,
                "max_book_positions": max_positions or 0,
                "holders": out[:int(limit)]}
    finally:
        con.close()


def ingest_recent(quarters=2, progress=None):
    """Ingest the newest `quarters` data sets that are not already indexed.

    Two is the minimum that makes the feature work: a single quarter is a
    photograph, and the question people actually ask — who built, who exited —
    needs the frame before it.
    """
    urls = available_datasets()
    if not urls:
        return {"error": "could not list SEC 13F data sets"}
    have = {q["quarter"] for q in status().get("quarters", [])}
    done = []
    for url in urls:
        if len(done) >= int(quarters):
            break
        r = ingest(url, progress)
        if r.get("error"):
            return {"error": r["error"], "ingested": done}
        done.append({"quarter": r["quarter"], "filers": r["filers"], "rows": r["rows"]})
        have.add(r["quarter"])
    return {"ingested": done, "quarters_present": sorted(have, reverse=True)}


def quarter_pair(con):
    """The two most recent indexed quarters, newest first, or fewer."""
    return [r[0] for r in con.execute(
        "SELECT quarter FROM quarters WHERE COALESCE(partial,0)=0 "
        "ORDER BY quarter DESC LIMIT 2").fetchall()]


def firms(query="", limit=40, quarter=None):
    """Filers matching `query`, largest book first.

    The dropdown cannot list 10,647 firms, so this is a search. Matching is on
    the filer's own reported name, which is safe here in a way it is not for
    securities: the user is picking from what the index actually contains, and
    the CIK travels with each row so the selection is exact from then on.
    """
    con = connect()
    try:
        q = (query or "").strip()
        if not quarter:
            if not con.execute("SELECT 1 FROM quarters LIMIT 1").fetchone():
                return {"error": "no 13F data ingested yet"}
            # Each filer at its newest filing. The newest quarter alone is
            # often a partial EDGAR pull of the largest filers: searching only
            # there, Berkshire Hathaway — not yet filed for it — was not found.
            floor = (quarter_pair(con) or [""])[0]
            rows = con.execute("""
                SELECT b.cik, b.manager, b.stock_value, b.stock_count FROM books b
                  JOIN (SELECT cik, MAX(quarter) AS q FROM books WHERE cik <> '' GROUP BY cik) n
                    ON n.cik = b.cik AND n.q = b.quarter
                 WHERE b.quarter >= ? AND b.manager LIKE ?
                 ORDER BY b.stock_value DESC LIMIT ?""", (floor, f"%{q}%", int(limit))).fetchall()
            return {"quarter": None, "query": q,
                    "firms": [{"cik": r[0], "manager": r[1], "book_value": r[2],
                               "position_count": r[3]} for r in rows]}
        if q:
            rows = con.execute(
                "SELECT cik, manager, stock_value, stock_count FROM books "
                "WHERE quarter=? AND manager LIKE ? ORDER BY stock_value DESC LIMIT ?",
                (quarter, f"%{q}%", int(limit))).fetchall()
        else:
            rows = con.execute(
                "SELECT cik, manager, stock_value, stock_count FROM books "
                "WHERE quarter=? ORDER BY stock_value DESC LIMIT ?",
                (quarter, int(limit))).fetchall()
        return {"quarter": quarter, "query": q,
                "firms": [{"cik": r[0], "manager": r[1], "book_value": r[2],
                           "position_count": r[3]} for r in rows]}
    finally:
        con.close()


def _book_moves(con, accession, prior_accession, compute=True):
    """Position-level moves between two of one filer's filings: counts of new,
    added, trimmed and exited common-stock positions, the dollar flow those
    moves imply, and the largest buy and sell. Options are excluded — a put
    is not a holding. Splits and CUSIP changes between the two filings are
    undone first (see compare_positions).
    """
    if not prior_accession:
        return None
    splits, links = _adjustments(con, _accession_quarter(con, prior_accession),
                                 _accession_quarter(con, accession), compute=compute)
    prior_q = _accession_quarter(con, prior_accession)
    books = dict(con.execute("SELECT accession, stock_value FROM books WHERE accession IN (?,?)",
                             (accession, prior_accession)).fetchall())
    if not comparable_books(books.get(accession), books.get(prior_accession)):
        return None  # a change of filing entity or units: its "moves" are not trades
    m = {"new": 0, "listed": 0, "added": 0, "trimmed": 0, "held": 0, "exited": 0,
         "bought_value": 0.0, "sold_value": 0.0, "top_buy": None, "top_sell": None}
    for _cusip, issuer, ticker, kind, _s, _p, flow, _v in compare_positions(
            _positions(con, accession), _positions(con, prior_accession), splits, links,
            prior_universe(con, prior_q) if prior_q else None):
        m[kind] += 1
        if kind in ("held", "listed") or not plausible_move(flow, books.get(accession), books.get(prior_accession)):
            continue
        key = "top_buy" if flow >= 0 else "top_sell"
        m["bought_value" if flow >= 0 else "sold_value"] += abs(flow)
        if m[key] is None or abs(flow) > abs(m[key]["value"]):
            m[key] = {"issuer": issuer, "ticker": ticker or "", "value": flow}
    return m


TOP_FIRMS = 20   # the ranking's length; the user's own funds are added below it


def top_firms(limit=TOP_FIRMS, ciks=None):
    """The largest 13F filers, each with what it did since its previous filing,
    then the filers the user added to the list (`ciks`) that are not among
    them, each marked `mine`.

    Ranked by each filer's NEWEST indexed book, which may be a quarter the bulk
    sets have not reached yet (ingest_current pulls those straight from EDGAR
    for the largest filers). Every row therefore carries its own quarter and
    prior quarter, and its moves compare those two filings of that filer — a
    refreshed filer is never compared against an unrefreshed neighbour's dates.

    13F covers US-listed long equity of managers with SEC filing obligations.
    That includes most of the world's largest asset managers (and sovereign
    funds such as Norges Bank) but only their US-listed stock books.
    """
    con = connect()
    try:
        if not con.execute("SELECT 1 FROM books LIMIT 1").fetchone():
            return {"error": "no 13F data ingested yet"}
        # Newest book per filer, then rank. Two indexed quarters plus any
        # partial current-quarter pull: a few thousand rows to scan.
        # Only filers still filing: a book whose newest filing predates the
        # newest complete quarter belongs to a filer that stopped (merged,
        # closed, or restructured — Vanguard Group Inc gave way to Vanguard
        # Capital Management and Vanguard Portfolio Management in Q1 2026, and
        # ranking its stale book beside theirs listed Vanguard twice).
        qs = quarter_pair(con)
        floor = qs[0] if qs else ""
        newest = con.execute("""
            SELECT b.cik, b.manager, b.quarter, b.accession, b.stock_value, b.stock_count
              FROM books b
              JOIN (SELECT cik, MAX(quarter) AS q FROM books WHERE cik <> '' GROUP BY cik) n
                ON n.cik = b.cik AND n.q = b.quarter
             WHERE b.quarter >= ?
             ORDER BY b.stock_value DESC LIMIT ?
        """, (floor, int(limit))).fetchall()
        mine = _norm_ciks(ciks)
        ranked = {r[0] for r in newest}
        # The user's own funds: each at its newest filing, whatever its size,
        # and whether or not it is still filing (it is theirs to remove).
        extra = []
        for cik in mine:
            if cik in ranked:
                continue
            r = con.execute("SELECT cik, manager, quarter, accession, stock_value, stock_count FROM books "
                            "WHERE cik=? ORDER BY quarter DESC LIMIT 1", (cik,)).fetchone()
            if r:
                extra.append(r)
        extra.sort(key=lambda r: -(r[4] or 0))
        firms_out = []
        corrected, pending = {}, False
        sync_pairs = set(_screen_pairs(con, limit, mine))  # exactly the pairs sync() keeps corrected
        for cik, manager, quarter, accession, value, count in list(newest) + extra:
            prior = con.execute(
                "SELECT quarter, accession, stock_value, stock_count FROM books "
                "WHERE cik=? AND quarter<? ORDER BY quarter DESC LIMIT 1",
                (cik, quarter)).fetchone()
            row = {"cik": cik, "manager": manager, "quarter": quarter,
                   "book_value": value, "position_count": count,
                   "mine": cik in mine, "ranked": cik in ranked}
            if prior:
                row.update({"prior_quarter": prior[0], "prior_book_value": prior[2],
                            "prior_position_count": prior[3]})
                # Stored corrections only. Computing them is market-wide work
                # with network calls, due again after every filing the sync
                # stores — started from here it ran past the screen's time
                # limit and the ranking came back empty. The sync keeps them
                # current and the screen reloads when a pass ends.
                # No corrections stored for this pair yet (a new index, a
                # quarter just read): without them a 10:1 split reads as a
                # tenfold purchase, so the moves wait for the sync as well.
                pair = (prior[0], quarter)
                if pair not in corrected:
                    corrected[pair] = _adjustments_stored(con, *pair)
                if corrected[pair]:
                    row["moves"] = _book_moves(con, accession, prior[1], compute=False)
                elif pair in sync_pairs:
                    pending = True  # one the sync computes: ask for a pass
            firms_out.append(row)
        newest_q = max((f["quarter"] for f in firms_out), default=None)
        partial = {r[0] for r in con.execute(
            "SELECT quarter FROM quarters WHERE COALESCE(partial,0)=1").fetchall()}
        return {"firms": firms_out, "newest_quarter": newest_q, "top": int(limit),
                "partial_quarters": sorted(partial), "pending": pending}
    finally:
        con.close()


def firm_book(cik=None, quarter=None, limit=250):
    """One filer's whole book from the index, with its quarter-over-quarter moves.

    Keyed on CIK, never on the manager name — firms rename between quarters and
    a name join would splice one filer's history onto another's.
    """
    con = connect()
    try:
        if not cik:
            return {"error": "cik required"}
        qs = quarter_pair(con)
        if not qs:
            return {"error": "no 13F data ingested yet"}
        cur_q = quarter or qs[0]
        prior_q = next((x for x in qs if x < cur_q), None)

        head = con.execute(
            "SELECT accession, manager, stock_value, stock_count FROM books "
            "WHERE cik=? AND quarter=? LIMIT 1", (cik, cur_q)).fetchone()
        if not head:
            return {"error": f"no 13F filing indexed for CIK {cik} in {cur_q}", "cik": cik}
        accession, manager, book_total, book_count = head

        rows = con.execute("""
            SELECT h.issuer, h.class, h.cusip, h.put_call, h.value, h.shares,
                   ct.ticker, ph.shares AS prior_shares, pb.accession AS filed_prior
              FROM holdings h
              LEFT JOIN cusip_ticker ct ON ct.cusip = h.cusip
              LEFT JOIN books pb ON pb.cik = ? AND pb.quarter = ?
              LEFT JOIN holdings ph ON ph.accession = pb.accession
                                   AND ph.cusip = h.cusip AND ph.put_call = ''
             WHERE h.accession = ?
             ORDER BY h.value DESC LIMIT ?
        """, (cik, prior_q or "", accession, int(limit))).fetchall()

        # Corporate actions between the two filings, undone (see compare_positions).
        prior_acc_row = con.execute(
            "SELECT accession FROM books WHERE cik=? AND quarter=? LIMIT 1",
            (cik, prior_q)).fetchone() if prior_q else None
        prior_pos = _positions(con, prior_acc_row[0]) if prior_acc_row else {}
        splits, links = _adjustments(con, prior_q, cur_q, compute=False) if prior_q else ({}, {})
        # Over the whole book, not the rows shown: a successor below the
        # display limit still means its retired CUSIP is not an exit.
        held_now = {c for (c,) in con.execute(
            "SELECT cusip FROM holdings WHERE accession=? AND put_call=''", (accession,))}
        linked_old = {links[c][0] for c in held_now if c in links and links[c][0] in prior_pos}
        universe = prior_universe(con, prior_q) if prior_q else None

        positions = []
        for issuer, cls, cusip, pc, value, shares, ticker, prior_shares, filed_prior in rows:
            if not pc:
                if cusip in links and links[cusip][0] in prior_pos:
                    prior_shares = prior_pos[links[cusip][0]][2] * links[cusip][1]
                elif prior_shares is not None:
                    prior_shares = prior_shares * splits.get(cusip, 1.0)
            rec = {"issuer": issuer, "class": cls, "cusip": cusip, "ticker": ticker or "",
                   "put_call": pc, "is_derivative": bool(pc),
                   "value": value, "shares": shares,
                   "weight": (value / book_total) if (book_total and not pc) else None}
            if prior_q and not pc:
                if prior_shares is None:
                    rec["action"] = (("listed" if universe is not None and cusip not in universe else "new")
                                     if filed_prior else "first seen")
                    rec["shares_delta"] = shares
                else:
                    delta = shares - prior_shares
                    rec["shares_delta"] = delta
                    rec["pct_change"] = (delta / prior_shares) if prior_shares else None
                    rec["action"] = ("held" if (prior_shares > 0
                                                and abs(delta) / prior_shares < 0.01)
                                     else ("added" if delta > 0 else "trimmed"))
            positions.append(rec)

        # Exits: held last quarter, absent now. Only meaningful when the filer
        # actually filed this quarter, which they did — we are reading it.
        exits = []
        if prior_q:
            prior_acc = con.execute(
                "SELECT accession FROM books WHERE cik=? AND quarter=? LIMIT 1",
                (cik, prior_q)).fetchone()
            if prior_acc:
                exits = [{"issuer": r[0], "cusip": r[1], "ticker": r[3] or "",
                          "shares": 0.0, "shares_delta": -r[2], "action": "exited",
                          "weight": 0.0, "value": 0.0, "is_derivative": False}
                         for r in con.execute("""
                            SELECT ph.issuer, ph.cusip, ph.shares, ct.ticker
                              FROM holdings ph
                              LEFT JOIN cusip_ticker ct ON ct.cusip = ph.cusip
                             WHERE ph.accession=? AND ph.put_call=''
                               AND ph.cusip NOT IN (SELECT cusip FROM holdings
                                                     WHERE accession=? AND put_call='')
                             ORDER BY ph.value DESC LIMIT 40
                         """, (prior_acc[0], accession)).fetchall()
                         if r[1] not in linked_old]  # moved to a new CUSIP: not an exit

        return {"cik": cik, "manager": manager, "quarter": cur_q, "prior_quarter": prior_q,
                "accession": accession, "total_value": book_total,
                "position_count": book_count, "positions": positions, "exits": exits}
    finally:
        con.close()


# ── Current quarter, straight from EDGAR ────────────────────────────────────
#
# The bulk data sets publish only after their filing window closes, so they run
# a full quarter behind: Q2 13Fs were due 14 August and were on EDGAR that day,
# while the newest bulk set still covered Q1. Everything below closes that gap
# by reading the filings directly for the filers that matter, which is the
# difference between ownership data that is 45 days old and 135.

from xml.etree import ElementTree


def _local(tag):
    """Tag without its namespace. The information table declares a default
    namespace, so a bare tag lookup matches nothing — silently."""
    return tag.rsplit("}", 1)[-1]


def parse_information_table(xml_bytes):
    """Positions from one information table, aggregated by (CUSIP, put/call).

    Aggregation is not an optimisation: a filer reports the same security on
    several rows, one per internal manager or discretion type, and a per-row
    read understates the position and every weight derived from it. Options
    share the underlying's CUSIP and are kept apart — a put is bearish and is
    not a holding.
    """
    try:
        root = ElementTree.fromstring(xml_bytes)
    except ElementTree.ParseError:
        return []
    agg = {}
    for node in root:
        if _local(node.tag) != "infoTable":
            continue
        row = {}
        for child in node:
            tag = _local(child.tag)
            if tag == "shrsOrPrnAmt":
                for g in child:
                    row[_local(g.tag)] = (g.text or "").strip()
            else:
                row[tag] = (child.text or "").strip()
        cusip = (row.get("cusip") or "").upper()
        if not cusip:
            continue
        if (row.get("sshPrnamtType") or "SH").upper() != "SH":
            continue  # a principal amount is a bond, not a share count
        try:
            value = float(row.get("value") or 0)
            shares = float(row.get("sshPrnamt") or 0)
        except ValueError:
            continue
        pc = (row.get("putCall") or "").strip().upper()
        key = (cusip, pc)
        if key not in agg:
            agg[key] = {"cusip": cusip, "issuer": row.get("nameOfIssuer") or "",
                        "class": row.get("titleOfClass") or "", "put_call": pc,
                        "value": 0.0, "shares": 0.0}
        agg[key]["value"] += value
        agg[key]["shares"] += shares
    return list(agg.values())


def _newest_13f(cik, after_quarter):
    """The filer's newest 13F-HR reporting a quarter later than `after_quarter`."""
    r, _ = sec_http.get(f"https://data.sec.gov/submissions/CIK{_pad_cik(cik)}.json", timeout=30)
    if r is None:
        return None
    try:
        rec = (r.json().get("filings") or {}).get("recent") or {}
    except Exception:
        return None
    acc, frm, fdt, rdt = (rec.get("accessionNumber") or [], rec.get("form") or [],
                          rec.get("filingDate") or [], rec.get("reportDate") or [])
    best = None
    for i in range(len(acc)):
        # Originals only. An amendment restates and would double the book.
        if (frm[i] if i < len(frm) else "").upper() != "13F-HR":
            continue
        period = rdt[i] if i < len(rdt) else ""
        if not period or period <= after_quarter:
            continue
        if best is None or period > best["period"]:
            best = {"accession": acc[i], "period": period,
                    "filed": fdt[i] if i < len(fdt) else ""}
    return best


def _information_table_url(cik, accession):
    """Locate the info table inside an accession — its filename is assigned by
    the filing agent, so the directory listing is the only way to find it."""
    a = accession.replace("-", "")
    r, _ = sec_http.get(f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{a}/index.json", timeout=30)
    if r is None:
        return None
    try:
        items = r.json()["directory"]["item"]
    except Exception:
        return None
    base = f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{a}/"
    for it in items:
        n = (it.get("name") or "").lower()
        if n.endswith(".xml") and "primary_doc" not in n:
            return base + it["name"]
    return None


def ingest_current(top=400, progress=None, ciks=None):
    """Pull the current quarter straight from EDGAR for the largest filers.

    Ranked by book size in the newest indexed quarter, because that is the only
    ordering available before the new quarter exists — and because a weight in a
    large book is what the screen is for. Filers outside the cut keep their last
    indexed quarter, and the result says how many that is rather than implying
    the quarter is complete.
    """
    con = connect()
    try:
        qs = quarter_pair(con)
        if not qs:
            return {"error": "no 13F data ingested yet — build the index first"}
        latest = qs[0]
        rows = con.execute(
            "SELECT cik, manager FROM books WHERE quarter=? AND cik<>'' "
            "ORDER BY stock_value DESC LIMIT ?", (latest, int(top))).fetchall()
        # And the funds the user added to the list, whatever their size.
        have = {r[0] for r in rows}
        for cik in _norm_ciks(ciks):
            if cik not in have:
                r = con.execute("SELECT cik, manager FROM books WHERE cik=? ORDER BY quarter DESC LIMIT 1",
                                (cik,)).fetchone()
                if r:
                    rows.append(r)

        added = skipped = failed = 0
        new_quarter = ""
        touched = set()      # every period written, not only the newest
        per_quarter = {}     # period -> filers added, so counts are not mixed
        for idx, (cik, manager) in enumerate(rows):
            f = _newest_13f(cik, latest)
            if not f:
                skipped += 1
                continue
            url = _information_table_url(cik, f["accession"])
            if not url:
                failed += 1
                continue
            r, _ = sec_http.get(url, timeout=45)
            if r is None:
                failed += 1
                continue
            positions = parse_information_table(r.content)
            if not positions:
                failed += 1
                continue
            q = f["period"]
            new_quarter = max(new_quarter, q)
            touched.add(q)
            # Replace the filer's whole quarter, not just this accession. The
            # bulk path picks one filing per filer-quarter and deletes the
            # quarter first; deleting by accession alone leaves a second
            # original for the same period in place and counts the filer twice
            # in every holder total.
            con.execute("DELETE FROM holdings WHERE cusip IS NOT NULL AND accession IN "
                        "(SELECT accession FROM filings WHERE cik=? AND quarter=?)",
                        (_pad_cik(cik), q))
            con.execute("DELETE FROM books WHERE cik=? AND quarter=?", (_pad_cik(cik), q))
            con.execute("DELETE FROM filings WHERE cik=? AND quarter=?", (_pad_cik(cik), q))
            con.execute("INSERT OR REPLACE INTO filings (accession, quarter, cik, manager, is_amendment, filed) "
                        "VALUES (?,?,?,?,?,?)", (f["accession"], q, _pad_cik(cik), manager, 0, f.get("filed")))
            con.executemany(
                "INSERT INTO holdings VALUES (?,?,?,?,?,?,?,?)",
                [(f["accession"], q, p["cusip"], p["issuer"], p["class"], p["put_call"],
                  p["value"], p["shares"]) for p in positions])
            stock = [p for p in positions if not p["put_call"]]
            con.execute("INSERT OR REPLACE INTO books VALUES (?,?,?,?,?,?)",
                        (f["accession"], q, _pad_cik(cik), manager,
                         sum(p["value"] for p in stock), len(stock)))
            added += 1
            per_quarter[q] = per_quarter.get(q, 0) + 1
            if added % 25 == 0:
                con.commit()
                if progress:
                    progress(idx + 1, len(rows))
        con.commit()

        # Register EVERY period written, not just the newest. A stale index can
        # be two quarters behind, and a filer whose newest filing is for the
        # intermediate one would otherwise land in holdings under a quarter with
        # no `quarters` row — invisible to holders, quarter_pair and status, and
        # never cleaned up.
        for q in sorted(touched):
            # PARTIAL: a row that looks like a bulk one would imply the whole
            # universe is present, and this is the largest filers only.
            con.execute("INSERT OR REPLACE INTO quarters VALUES (?,?,?,?,datetime('now'),1)",
                        (q, f"EDGAR direct (top {int(top)} filers)", per_quarter.get(q, 0),
                         con.execute("SELECT COUNT(*) FROM holdings WHERE quarter=?",
                                     (q,)).fetchone()[0]))
        con.commit()
        return {"quarter": new_quarter,
                "filers_added": per_quarter.get(new_quarter, 0),
                "filers_added_total": added,
                "quarters_written": sorted(touched),
                "no_newer_filing": skipped, "failed": failed,
                "requested": len(rows), "partial": True,
                "note": "current quarter for the largest filers only — the bulk data set "
                        "for it has not been published yet"}
    finally:
        con.close()


# ── Whole-universe aggregates: ticker totals and breadth ────────────────────
#
# Two market-wide questions need one pass over every holding in a quarter:
# "how many 13F shares are out there for each ticker" (the denominator of
# short interest ÷ institutional shares) and "how many filers hold each name,
# and how did that change" (breadth of ownership — Chen, Hong and Stein, and
# one of the two ownership signals that replicates cleanly). Both are
# computed once per quarter into cache tables, because a GROUP BY over 3.3m
# rows is seconds, not milliseconds, and the answer does not change until the
# next data set is ingested.

def _ensure_totals(con, quarter):
    con.execute("""CREATE TABLE IF NOT EXISTS ticker_totals (
        quarter TEXT, cusip TEXT, ticker TEXT, holders INTEGER, shares REAL, value REAL,
        focused_holders INTEGER, focused_shares REAL, top10_share REAL,
        PRIMARY KEY (quarter, cusip))""")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tt_ticker ON ticker_totals(quarter, ticker)")
    if con.execute("SELECT 1 FROM ticker_totals WHERE quarter=? LIMIT 1", (quarter,)).fetchone():
        return
    con.execute("DROP TABLE IF EXISTS _q")
    # Every filer counts here, blank CIK or not: a filing missing from
    # SUBMISSION.tsv still holds real shares. Only the cross-quarter
    # comparison below needs the CIK, because only it has to match filers.
    con.execute("""CREATE TEMP TABLE _q AS
        SELECT h.cusip, b.accession, h.shares, h.value,
               CASE WHEN b.stock_count < ? THEN 1 ELSE 0 END AS focused
          FROM holdings h JOIN books b ON b.accession = h.accession
         WHERE h.quarter = ? AND h.put_call = ''
           AND b.stock_value >= ? AND b.stock_count >= ?""",
        (MAX_DISCRETIONARY_POSITIONS, quarter, MIN_BOOK_VALUE, MIN_BOOK_POSITIONS))
    con.execute("CREATE INDEX _q_cusip ON _q(cusip, value)")
    con.execute("""INSERT INTO ticker_totals
        SELECT ?, q.cusip, ct.ticker, COUNT(DISTINCT q.accession), SUM(q.shares), SUM(q.value),
               SUM(q.focused), SUM(CASE WHEN q.focused THEN q.shares ELSE 0 END), NULL
          FROM _q q LEFT JOIN cusip_ticker ct ON ct.cusip = q.cusip
         GROUP BY q.cusip""", (quarter,))
    # Top-10 share per CUSIP: a window function over the temp table.
    con.execute("""WITH ranked AS (
            SELECT cusip, value,
                   ROW_NUMBER() OVER (PARTITION BY cusip ORDER BY value DESC) AS rn
              FROM _q),
         top AS (SELECT cusip, SUM(value) AS v10 FROM ranked WHERE rn <= 10 GROUP BY cusip)
        UPDATE ticker_totals SET top10_share =
            (SELECT top.v10 / ticker_totals.value FROM top WHERE top.cusip = ticker_totals.cusip)
         WHERE quarter = ? AND value > 0""", (quarter,))
    con.execute("DROP TABLE IF EXISTS _q")
    con.commit()


# 13F issuer names for the things that are not companies. An ETF's short
# interest is a hedge, not a view on a business, and the short-side evidence
# is about stocks; a levered ETF tops any SI ÷ 13F ranking by construction.
# Matched on the filers' own issuer strings, which is a heuristic and is said
# so wherever the exclusion is applied.
#
# Whole words: as substrings, "ETF" took out NETFLIX and "MUNI" every
# COMMUNICATIONS and COMMUNITIES company (2026-11-25).
FUND_WORDS = {"ETF", "ETFS", "ETN", "ETNS", "ET", "ISHARES", "PROSHARES", "SPDR", "FUND", "FUNDS", "FD", "FDS",
              "GRANITESHARES", "VANECK", "IPATH", "VIX", "2X", "3X", "LEVERAGED", "INVERSE", "MUNI", "MUNICIPAL",
              "VANGUARD", "YIELDMAX", "MICROSECTORS"}
FUND_PREFIXES = ("DIREX", "ISHARES", "PROSHARES", "MUNIY", "MUNIH", "MUNIV", "MUNIE")
# Words a company can carry too: a fund only without a company's own word.
FUND_LIKE = {"INVESCO", "TRUST", "TR", "INDEX", "PORTFOLIO", "PORTFOLIOS", "PHYSICAL", "WISDOMTREE", "ARK", "PACER",
             "ULTRA", "BULL", "BEAR"}
FUND_PHRASES = ("GLOBAL X", "SCHWAB STRATEGIC", "JPMORGAN EXCHANGE", "STRATEGIC INV", "CLOSED-END", "CLOSED END",
                "SELECT SECTOR", "FIRST TRUST EXCHANGE", "FIRST TR EXCHANGE", "EXCHANGE TRADED", "EXCHANGE-TRADED",
                "EXCH TRADED", "EXCH TRD", "MANAGER DIRECTED", "STATE STREET COMMUNICATION")
COMPANY_WORDS = {"INC", "CORP", "CORPORATION", "CO", "COMPANY", "LTD", "PLC", "HLDGS", "HOLDINGS", "HLDG", "GROUP",
                 "REIT", "RLTY", "REALTY", "PPTY", "PPTYS", "PROPERTY", "PROPERTIES", "RESIDENTIAL", "APARTMENT",
                 "HOSPITALITY", "INDUSTRIAL", "LODGING", "OFFICE", "HEALTHCARE", "STORAGE", "BANCORP", "BANCSHARES", "BANK", "HOTEL",
                 "HOTELS", "MORTGAGE", "MTG", "THERAPY", "PROLOGIS"}


def is_fund(name):
    text = " ".join(_re.sub(r"[^A-Z0-9&+\-]+", " ", (name or "").upper()).split())
    words = set(text.replace("-", " ").split())
    if words & FUND_WORDS or any(w.startswith(FUND_PREFIXES) for w in words) or any(p in text for p in FUND_PHRASES):
        return True
    return bool(words & FUND_LIKE) and not words & COMPANY_WORDS


def shares_by_ticker(quarter=None):
    """{ticker: {shares, holders, name, fund}} for the newest complete quarter."""
    con = connect()
    try:
        if not quarter:
            row = con.execute(
                "SELECT quarter FROM quarters WHERE COALESCE(partial,0)=0 "
                "ORDER BY quarter DESC LIMIT 1").fetchone()
            if not row:
                return {"error": "no complete 13F quarter ingested yet", "by_ticker": {}}
            quarter = row[0]
        _ensure_totals(con, quarter)
        out = {}
        for ticker, holders_n, shares, name in con.execute(
                "SELECT t.ticker, SUM(t.holders), SUM(t.shares), MAX(ct.name) "
                "FROM ticker_totals t LEFT JOIN cusip_ticker ct ON ct.cusip = t.cusip "
                "WHERE t.quarter=? AND t.ticker IS NOT NULL AND t.ticker<>'' "
                "GROUP BY t.ticker", (quarter,)):
            out[ticker] = {"shares": shares, "holders": holders_n, "name": name or "",
                           "fund": is_fund(name)}
        return {"quarter": quarter, "by_ticker": out}
    finally:
        con.close()


def _ensure_breadth(con, cur_q, prior_q):
    """Per-CUSIP change between two quarters, among filers who filed in BOTH.

    A filer that stopped filing has not sold, and one that started has not
    bought — they were simply not visible. Counting either as a decision is
    the mistake this join exists to avoid.
    """
    con.execute("""CREATE TABLE IF NOT EXISTS breadth (
        quarter TEXT, prior_quarter TEXT, cusip TEXT,
        holders_prior INTEGER, new_holders INTEGER, closed INTEGER,
        added INTEGER, reduced INTEGER,
        focused_net_shares REAL,
        PRIMARY KEY (quarter, prior_quarter, cusip))""")
    # A run marker, not "any row present": a quarter pair with no comparable
    # filer at all is a legitimate (empty) answer and must not be recomputed
    # on every call.
    con.execute("""CREATE TABLE IF NOT EXISTS breadth_runs (
        quarter TEXT, prior_quarter TEXT, computed_at TEXT, PRIMARY KEY (quarter, prior_quarter))""")
    # v2: prior holdings restated for splits and CUSIP changes. Results of the
    # unadjusted version are dropped once.
    con.execute("CREATE TABLE IF NOT EXISTS breadth_version (v INTEGER)")
    if (con.execute("SELECT v FROM breadth_version").fetchone() or (0,))[0] < 2:
        con.execute("DELETE FROM breadth")
        con.execute("DELETE FROM breadth_runs")
        con.execute("DELETE FROM breadth_version")
        con.execute("INSERT INTO breadth_version VALUES (2)")
        con.commit()
    if con.execute("SELECT 1 FROM breadth_runs WHERE quarter=? AND prior_quarter=?",
                   (cur_q, prior_q)).fetchone():
        return
    adj_status = {"complete": True}
    splits, links = _adjustments(con, prior_q, cur_q, status=adj_status)
    for tag, q in (("_c", cur_q), ("_p", prior_q)):
        con.execute(f"DROP TABLE IF EXISTS {tag}")
        con.execute(f"""CREATE TEMP TABLE {tag} AS
            SELECT h.cusip, b.cik, h.shares,
                   CASE WHEN b.stock_count < ? THEN 1 ELSE 0 END AS focused
              FROM holdings h JOIN books b ON b.accession = h.accession
             WHERE h.quarter = ? AND h.put_call = '' AND b.cik <> ''
               AND b.stock_value >= ? AND b.stock_count >= ?""",
            (MAX_DISCRETIONARY_POSITIONS, q, MIN_BOOK_VALUE, MIN_BOOK_POSITIONS))
        con.execute(f"CREATE INDEX {tag}_k ON {tag}(cusip, cik)")
    # The prior quarter in today's terms: split shares multiplied out, a
    # retired CUSIP's holdings moved onto the one that replaced it.
    for cusip, f in splits.items():
        con.execute("UPDATE _p SET shares = shares * ? WHERE cusip = ?", (f, cusip))
    for new_c, (old_c, ratio) in links.items():
        con.execute("UPDATE _p SET cusip = ?, shares = shares * ? WHERE cusip = ?", (new_c, ratio, old_c))
    con.execute("DROP TABLE IF EXISTS _both")
    con.execute("""CREATE TEMP TABLE _both AS
        SELECT cik FROM books WHERE quarter=? AND cik<>''
        INTERSECT SELECT cik FROM books WHERE quarter=? AND cik<>''""", (cur_q, prior_q))
    con.execute("CREATE INDEX _both_k ON _both(cik)")
    con.execute("""INSERT INTO breadth
        SELECT ?, ?, cusip,
               SUM(had), SUM(is_new), SUM(is_closed), SUM(is_added), SUM(is_reduced),
               SUM(focused_delta)
          FROM (
            SELECT cusip,
                   CASE WHEN p_cik IS NOT NULL THEN 1 ELSE 0 END AS had,
                   CASE WHEN c_cik IS NOT NULL AND p_cik IS NULL THEN 1 ELSE 0 END AS is_new,
                   CASE WHEN c_cik IS NULL AND p_cik IS NOT NULL THEN 1 ELSE 0 END AS is_closed,
                   CASE WHEN c_cik IS NOT NULL AND p_cik IS NOT NULL
                             AND c_shares > p_shares * 1.01 THEN 1 ELSE 0 END AS is_added,
                   CASE WHEN c_cik IS NOT NULL AND p_cik IS NOT NULL
                             AND c_shares < p_shares * 0.99 THEN 1 ELSE 0 END AS is_reduced,
                   CASE WHEN COALESCE(c_focused, p_focused) = 1
                        THEN COALESCE(c_shares, 0) - COALESCE(p_shares, 0) ELSE 0 END
                        AS focused_delta
              -- _c FULL OUTER JOIN _p, written so SQLite before 3.39 runs it
              -- (Ubuntu 22.04's Python has 3.37): every _c row with its _p
              -- match or none, then the _p rows no _c row matched.
              FROM (SELECT c.cusip AS cusip, c.cik AS c_cik, c.shares AS c_shares, c.focused AS c_focused,
                           p.cik AS p_cik, p.shares AS p_shares, p.focused AS p_focused
                      FROM _c c LEFT JOIN _p p ON p.cusip = c.cusip AND p.cik = c.cik
                    UNION ALL
                    SELECT p.cusip, NULL, NULL, NULL, p.cik, p.shares, p.focused
                      FROM _p p
                     WHERE NOT EXISTS (SELECT 1 FROM _c c WHERE c.cusip = p.cusip AND c.cik = p.cik))
             WHERE COALESCE(c_cik, p_cik) IN (SELECT cik FROM _both))
         GROUP BY cusip""", (cur_q, prior_q))
    for tag in ("_c", "_p", "_both"):
        con.execute(f"DROP TABLE IF EXISTS {tag}")
    if adj_status["complete"]:
        # Not kept as final when a split lookup went unanswered: the next call
        # recomputes rather than serving a split as a wave of buying for good.
        con.execute("INSERT OR REPLACE INTO breadth_runs VALUES (?,?,datetime('now'))", (cur_q, prior_q))
    con.commit()


def movers(limit=100, sort="breadth_up", min_holders=20, quarter=None, include_funds=False):
    """Securities by how their holder base changed between the two indexed
    quarters. `sort`: breadth_up, breadth_down, new, closed, focused_buying,
    focused_selling, concentration. Funds are dropped by name unless asked
    for: VTI gaining 146 holders is flow into an index, not a view on one."""
    con = connect()
    try:
        qs = quarter_pair(con)
        if len(qs) < 2:
            return {"error": "two complete 13F quarters are needed for a comparison"}
        cur_q = quarter or qs[0]
        prior_q = next((x for x in qs if x < cur_q), None)
        if not prior_q:
            return {"error": "no prior quarter to compare against"}
        _ensure_totals(con, cur_q)
        _ensure_breadth(con, cur_q, prior_q)
        order = {
            "breadth_up": "(t.holders - b.holders_prior) DESC",
            "breadth_down": "(t.holders - b.holders_prior) ASC",
            "new": "b.new_holders DESC",
            "closed": "b.closed DESC",
            "focused_buying": "b.focused_net_shares DESC",
            "focused_selling": "b.focused_net_shares ASC",
            "concentration": "t.top10_share DESC",
        }.get(sort)
        if not order:
            sort, order = "breadth_up", "(t.holders - b.holders_prior) DESC"
        # LEFT JOIN: a security with no comparable filer has zero change, not
        # no row. The COALESCEs make the ORDER BY well-defined for those.
        order = order.replace("b.holders_prior", "COALESCE(b.holders_prior,0)") \
                     .replace("b.new_holders", "COALESCE(b.new_holders,0)") \
                     .replace("b.closed", "COALESCE(b.closed,0)") \
                     .replace("b.focused_net_shares", "COALESCE(b.focused_net_shares,0)")
        rows = con.execute(f"""
            SELECT t.cusip, t.ticker, ct.name, t.holders, COALESCE(b.holders_prior,0),
                   COALESCE(b.new_holders,0), COALESCE(b.closed,0), COALESCE(b.added,0),
                   COALESCE(b.reduced,0), t.shares, t.value, t.focused_holders,
                   b.focused_net_shares, t.top10_share
              FROM ticker_totals t
              LEFT JOIN breadth b ON b.cusip = t.cusip AND b.quarter = t.quarter
                                  AND b.prior_quarter = ?
              LEFT JOIN cusip_ticker ct ON ct.cusip = t.cusip
             WHERE t.quarter = ? AND t.holders >= ? AND t.ticker IS NOT NULL AND t.ticker <> ''
             ORDER BY {order} LIMIT ?""", (prior_q, cur_q, int(min_holders),
                                          int(limit) * (1 if include_funds else 3))).fetchall()
        funds_dropped = 0
        if not include_funds:
            kept = [r for r in rows if not is_fund(r[2])]
            funds_dropped = len(rows) - len(kept)
            rows = kept[:int(limit)]
        out = [{"cusip": r[0], "ticker": r[1], "name": r[2] or "",
                "fund": is_fund(r[2]),
                "holders": r[3], "holders_prior": r[4],
                "delta_holders": (r[3] or 0) - (r[4] or 0),
                "new": r[5], "closed": r[6], "added": r[7], "reduced": r[8],
                "shares": r[9], "value": r[10], "focused_holders": r[11],
                "focused_net_shares": r[12], "top10_share": r[13]} for r in rows]
        return {"quarter": cur_q, "prior_quarter": prior_q, "sort": sort,
                "min_holders": int(min_holders), "funds_dropped": funds_dropped, "rows": out}
    finally:
        con.close()



# ── Corporate actions between two filings ───────────────────────────────────
#
# 13F reports share counts, and two filings a quarter apart are compared on
# them. Two corporate actions make that comparison lie (2026-11-16, LARGEST
# FUNDS listed KLAC as nearly every big manager's biggest buy):
#   * a split — KLA's 10-for-1 on 2026-06-12 multiplied every holder's shares
#     by ten, and an unchanged stake read as a purchase of nine times itself;
#   * a new CUSIP — Exxon Mobil's reorganisation into ExxonMobil Holdings
#     retired one CUSIP and issued another, and every holder "sold" one line
#     and "bought" the other.
# A split is taken from the ANNOUNCED ratio, never inferred from the size of a
# price gap; a CUSIP change from the same holders moving from a retiring
# security to a new one under the same issuer name.

SPLIT_GAP_LO, SPLIT_GAP_HI = 0.6, 1.67   # price-per-share ratios worth checking for a split

# Bump when the rules below change: stored corrections and moves carry it, so
# a change of rule recomputes them instead of serving the old answer (the
# Thomson Reuters CUSIP change stayed unlinked after the name rule learned
# "Sh Par", because the stored result predated it).
ADJ_RULES_VERSION = 5  # 5: funds by whole words (is_fund)


def split_factor_between(splits, prior_q, cur_q):
    """Product of the split ratios dated after `prior_q` and on or before
    `cur_q` (ISO dates); 1.0 when none. `splits` is [(iso_date, ratio)], a
    ratio of 10 meaning ten new shares for one, 0.1 a 1-for-10 reverse."""
    f = 1.0
    for d, ratio in splits or []:
        try:
            r = float(ratio)
        except (TypeError, ValueError):
            continue
        if prior_q < str(d)[:10] <= cur_q and is_split_ratio(r):
            f *= r
    return f


_NAME_SUFFIXES = {"INC", "CORP", "CORPORATION", "CO", "COMPANY", "HOLDINGS", "HOLDING", "HLDGS", "HLDG",
                  "LTD", "PLC", "LLC", "LP", "NV", "SA", "AG", "SE", "GROUP", "GRP", "THE", "NEW", "COM",
                  "CL", "CLASS", "A", "B", "C", "ORD", "SHS", "COMMON", "STOCK", "ADR", "SPONSORED",
                  # share descriptions some filers append: "... Ordinary Shares", "COM NPV"
                  "ORDINARY", "SHARES", "SHARE", "SH", "STK", "CAP", "NPV", "PAR", "VALUE", "REGISTERED",
                  "REG", "USD", "ADS", "UNITS", "NEW1"}


def issuer_key(name):
    """An issuer name with its legal and share-class words removed and the
    rest run together: "EXXON MOBIL CORP" and "EXXONMOBIL HOLDINGS CORP" both
    give EXXONMOBIL, while two banks that merely both start "FIRST" do not
    meet. Only ever used together with the same holders moving across."""
    words = "".join(ch if ch.isalnum() else " " for ch in (name or "").upper()).split()
    return "".join(w for w in words if w not in _NAME_SUFFIXES)


def is_split_ratio(r):
    """A share split is a ratio of small whole numbers (2, 3:2, 1:10). Yahoo
    also files spin-off price adjustments as "splits" (FedEx 1.241 for its
    Freight spin-off): those change no share count and are not used."""
    if not (r and r > 0):
        return False
    for x in (r, 1.0 / r):
        for k in (1, 2, 3, 4):
            v = x * k
            if round(v) >= 1 and abs(v - round(v)) < 0.002:
                return True
    return False


def _announced_splits(con, ticker):
    """A ticker's split history, from Yahoo, kept for a day: several quarter
    pairs ask about the same names."""
    con.execute("CREATE TABLE IF NOT EXISTS split_cache (ticker TEXT PRIMARY KEY, day TEXT, splits TEXT)")
    today = time.strftime("%Y-%m-%d")
    row = con.execute("SELECT day, splits FROM split_cache WHERE ticker=?", (ticker,)).fetchone()
    if row and row[0] == today:
        return json.loads(row[1])
    try:
        import yfinance as yf
        s = yf.Ticker(ticker).splits
        out = [(ts.date().isoformat(), float(v)) for ts, v in s.items()] if s is not None else []
    except Exception:
        return None  # unknown — not "no splits"
    con.execute("INSERT OR REPLACE INTO split_cache VALUES (?,?,?)", (ticker, today, json.dumps(out)))
    con.commit()  # a one-row write: never hold the lock past it
    return out


def _ticker_stamp(con):
    """A digest of the whole CUSIP->ticker map: any remap, even to a ticker of
    the same length, changes it."""
    import hashlib
    h = hashlib.md5()
    for c, t in con.execute("SELECT cusip, ticker FROM cusip_ticker ORDER BY cusip"):
        h.update(f"{c}={t};".encode())
    return h.hexdigest()


def snap_ratio(r):
    """A CUSIP-change exchange ratio as the whole ratio it almost certainly is
    (1:1, 2:1, 1-for-10) when the holders' median is within 2.5% of one — the
    median also carries the median holder's own trading, which must not be
    absorbed into the conversion. A merger's odd ratio (0.343) is kept."""
    if not (r and r > 0):
        return r
    for x, inv in ((r, False), (1.0 / r, True)):
        v = round(x)
        if v >= 1 and abs(x - v) / x < 0.025:
            return 1.0 / v if inv else float(v)
    return r


def _quarter_stamp(con, *quarters):
    rows = con.execute(
        f"SELECT quarter, filers, ingested_at FROM quarters WHERE quarter IN ({','.join('?' * len(quarters))}) "
        "ORDER BY quarter", quarters).fetchall()
    return "|".join(f"{q}:{n}:{t}" for q, n, t in rows)


def _adj_tables(con):
    con.executescript("""
        CREATE TABLE IF NOT EXISTS adj_runs (prior_quarter TEXT, quarter TEXT, stamp TEXT,
            PRIMARY KEY (prior_quarter, quarter));
        CREATE TABLE IF NOT EXISTS split_adj (prior_quarter TEXT, quarter TEXT, cusip TEXT,
            factor REAL, PRIMARY KEY (prior_quarter, quarter, cusip));
        CREATE TABLE IF NOT EXISTS cusip_link (prior_quarter TEXT, quarter TEXT, new_cusip TEXT,
            old_cusip TEXT, ratio REAL, PRIMARY KEY (prior_quarter, quarter, new_cusip));
    """)


def _ensure_adjustments(con, prior_q, cur_q):
    """Split factors and CUSIP links for one quarter pair, computed once per
    state of the index (an ingest that changes either quarter recomputes).
    Returns whether the result is complete — False when a split lookup could
    not be answered, in which case nothing built on it should be kept.

    Computed in memory and written in one short transaction at the end: the
    computation makes network calls, and holding SQLite's write lock through
    them would stall the background sync and every other writer."""
    _adj_tables(con)
    stamp = f"r{ADJ_RULES_VERSION}|" + _quarter_stamp(con, prior_q, cur_q) + "|" + _ticker_stamp(con)
    row = con.execute("SELECT stamp FROM adj_runs WHERE prior_quarter=? AND quarter=?",
                      (prior_q, cur_q)).fetchone()
    if row and row[0] == stamp:
        return True
    split_rows, link_rows = [], []
    # Only filers present in both quarters: a partial quarter holds a few
    # dozen, and set against ten thousand everything would look retired.
    con.execute("DROP TABLE IF EXISTS _adj_both")
    con.execute("""CREATE TEMP TABLE _adj_both AS
        SELECT cik FROM books WHERE quarter=? AND cik<>''
        INTERSECT SELECT cik FROM books WHERE quarter=? AND cik<>''""", (cur_q, prior_q))
    per = {}
    for tag, q in (("c", cur_q), ("p", prior_q)):
        for cusip, issuer, holders, value, shares in con.execute("""
                SELECT h.cusip, MAX(h.issuer), COUNT(DISTINCT b.cik), SUM(h.value), SUM(h.shares)
                  FROM holdings h JOIN books b ON b.accession = h.accession
                 WHERE h.quarter=? AND h.put_call='' AND b.cik IN (SELECT cik FROM _adj_both)
                 GROUP BY h.cusip""", (q,)):
            per.setdefault(cusip, {})[tag] = (issuer, holders, value, shares)
    # The name most filers give a security, not the alphabetically largest:
    # one filer entered its own name ("Woodbridge Co Ltd/The") as the issuer
    # of Thomson Reuters, and that hid the CUSIP change.
    for tag, q in (("c", cur_q), ("p", prior_q)):
        best = {}
        for cusip, issuer, n in con.execute(
                "SELECT cusip, issuer, COUNT(*) FROM holdings WHERE quarter=? AND put_call='' "
                "GROUP BY cusip, issuer", (q,)):
            if cusip in per and tag in per[cusip] and n > best.get(cusip, ("", 0))[1]:
                best[cusip] = (issuer, n)
        for cusip, (issuer, _n) in best.items():
            _old, h, v, sh = per[cusip][tag]
            per[cusip][tag] = (issuer, h, v, sh)
    tick = dict(con.execute("SELECT cusip, ticker FROM cusip_ticker WHERE ticker<>''").fetchall())

    # Splits: a held-through security whose price per share OR share count
    # moved by a split-sized step (a 3:2 split, or a 2:1 in a stock that also
    # rose, keeps the price inside the band — the share count still jumps),
    # confirmed by an announced split in between.
    incomplete = False
    for cusip, d in per.items():
        if "c" not in d or "p" not in d or not tick.get(cusip):
            continue
        (_, hc, vc, sc), (_, hp, vp, sp) = d["c"], d["p"]
        if min(hc, hp) < 5 or not (sc and sp and vc and vp):
            continue
        price_ratio = (vc / sc) / (vp / sp)
        share_ratio = sc / sp
        if SPLIT_GAP_LO <= price_ratio <= SPLIT_GAP_HI and 0.74 <= share_ratio <= 1.35:
            continue
        splits = _announced_splits(con, tick[cusip])
        if splits is None:
            incomplete = True  # asked again next time: an unknown is not "no split"
            continue
        f = split_factor_between(splits, prior_q, cur_q)
        if f != 1.0:
            split_rows.append((prior_q, cur_q, cusip, f))

    # CUSIP changes: a security its holders left wholesale and a new one they
    # arrived in, under the same issuer name.
    # A change dated near a quarter end straddles it: Exxon's new CUSIP was
    # already reported by 399 holders at 30 June while 3,885 still reported
    # the old one. So "most holders left" and "holders at least doubled", not
    # "all gone" and "all new".
    retiring = {c: d["p"] for c, d in per.items()
                if "p" in d and d["p"][1] >= 5 and d.get("c", (None, 0))[1] <= 0.25 * d["p"][1]}
    arriving = {c: d["c"] for c, d in per.items()
                if "c" in d and d["c"][1] >= 5 and d.get("p", (None, 0))[1] * 2 <= d["c"][1]}
    by_key = {}
    for c, (issuer, *_rest) in retiring.items():
        if not is_fund(issuer) and len(issuer_key(issuer)) >= 4:
            by_key.setdefault(issuer_key(issuer), []).append(c)
    candidates = []
    for new_c, (issuer, n_new, *_r) in arriving.items():
        if is_fund(issuer):
            continue
        best = None
        for old_c in by_key.get(issuer_key(issuer), []):
            pairs = con.execute("""
                SELECT c.shares, p.shares
                  FROM holdings c JOIN books bc ON bc.accession = c.accession
                  JOIN books bp ON bp.cik = bc.cik AND bp.quarter = ?
                  JOIN holdings p ON p.accession = bp.accession AND p.cusip = ? AND p.put_call=''
                 WHERE c.cusip = ? AND c.quarter = ? AND c.put_call='' AND bc.cik <> ''
                   AND p.shares > 0""", (prior_q, old_c, new_c, cur_q)).fetchall()
            if len(pairs) < 0.5 * n_new:
                continue
            ratios = sorted(cs / ps for cs, ps in pairs)
            if not best or len(pairs) > best[1]:
                best = (old_c, len(pairs), snap_ratio(ratios[len(ratios) // 2]))
        if best:
            candidates.append((best[1], new_c, best[0], best[2]))
    # One-to-one: a retired CUSIP feeds one successor — the one most of its
    # holders moved into — or a class A and class B arriving together would
    # both be measured against the same old position.
    taken = set()
    for _n, new_c, old_c, ratio in sorted(candidates, reverse=True):
        if old_c in taken:
            continue
        taken.add(old_c)
        link_rows.append((prior_q, cur_q, new_c, old_c, ratio))
    con.execute("DROP TABLE IF EXISTS _adj_both")
    con.execute("DELETE FROM split_adj WHERE prior_quarter=? AND quarter=?", (prior_q, cur_q))
    con.execute("DELETE FROM cusip_link WHERE prior_quarter=? AND quarter=?", (prior_q, cur_q))
    con.executemany("INSERT OR REPLACE INTO split_adj VALUES (?,?,?,?)", split_rows)
    con.executemany("INSERT OR REPLACE INTO cusip_link VALUES (?,?,?,?,?)", link_rows)
    # Always a row: it is what says corrections for this pair exist, which the
    # fund ranking waits for. An incomplete run never equals the index's
    # stamp, so the next call computes again.
    con.execute("INSERT OR REPLACE INTO adj_runs VALUES (?,?,?)",
                (prior_q, cur_q, ("incomplete|" + stamp) if incomplete else stamp))
    con.commit()
    return not incomplete


_universe_cache = {}


def _universe_tables(con):
    con.executescript("""
        CREATE TABLE IF NOT EXISTS universe (quarter TEXT, cusip TEXT, PRIMARY KEY (quarter, cusip)) WITHOUT ROWID;
        CREATE TABLE IF NOT EXISTS universe_runs (quarter TEXT PRIMARY KEY, stamp TEXT);
    """)


def _universe_stamp(con, q):
    r = con.execute("SELECT filers, ingested_at FROM quarters WHERE quarter=?", (q,)).fetchone()
    n = con.execute("SELECT COUNT(*) FROM books WHERE quarter=?", (q,)).fetchone()[0]
    return f"{r[0] if r else ''}:{r[1] if r else ''}:{n}"


def _ensure_universe(con, q):
    """Store `q`'s universe for the screens — the sync's job. Reading it from
    the holdings is a scan of every row of the quarter (5 s on a full index),
    which the fund ranking and every fund's page paid on each open."""
    _universe_tables(con)
    stamp = _universe_stamp(con, q)
    row = con.execute("SELECT stamp FROM universe_runs WHERE quarter=?", (q,)).fetchone()
    if row and row[0] == stamp:
        return False
    cusips = [(q, c) for (c,) in con.execute(
        "SELECT DISTINCT cusip FROM holdings WHERE quarter=? AND put_call=''", (q,))]
    con.execute("DELETE FROM universe WHERE quarter=?", (q,))
    con.executemany("INSERT OR IGNORE INTO universe VALUES (?,?)", cusips)
    con.execute("INSERT OR REPLACE INTO universe_runs VALUES (?,?)", (q, stamp))
    con.commit()
    _universe_cache.pop(q, None)
    return True


def prior_universe(con, prior_q):
    """Every security any filer reported in `prior_q` (a complete quarter).
    One absent from it was not reportable then — a new listing — so a
    position in it now is a holding that became visible, not a purchase:
    Fidelity's pre-IPO SpaceX stake read as a $36B buy (2026-11-16).

    Read from the copy the sync stores when there is one, even if a filing
    has arrived since: the universe only grows, and a security reported for
    the first time by that late filing is one no screen is waiting on. With
    no copy (a new index), from the holdings themselves."""
    if prior_q not in _universe_cache:
        stored = None
        try:
            if con.execute("SELECT 1 FROM universe_runs WHERE quarter=?", (prior_q,)).fetchone():
                stored = {c for (c,) in con.execute("SELECT cusip FROM universe WHERE quarter=?", (prior_q,))}
        except sqlite3.OperationalError:
            stored = None
        _universe_cache[prior_q] = stored if stored is not None else {c for (c,) in con.execute(
            "SELECT DISTINCT cusip FROM holdings WHERE quarter=? AND put_call=''", (prior_q,))}
    return _universe_cache[prior_q]


def _norm_ciks(ciks):
    """CIKs as the index keys them: ten digits, zero-padded."""
    out = []
    for c in ciks or []:
        d = "".join(ch for ch in str(c) if ch.isdigit())
        if d and d.zfill(10) not in out:
            out.append(d.zfill(10))
    return out


def _screen_pairs(con, limit=TOP_FIRMS, ciks=None):
    """The quarter pairs the screens read corrections for: the recent
    consecutive ones, and the (previous, newest) pair of each of the largest
    filers and of each filer the user added to the list (one that skipped a
    quarter compares across the gap)."""
    recent = [q for (q,) in con.execute("SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 3")]
    pairs = list(zip(recent[1:], recent[:-1]))
    floor = (quarter_pair(con) or [""])[0]
    newest = [r for r in con.execute("""
            SELECT b.cik, b.quarter FROM books b
              JOIN (SELECT cik, MAX(quarter) AS q FROM books WHERE cik <> '' GROUP BY cik) n
                ON n.cik = b.cik AND n.q = b.quarter
             WHERE b.quarter >= ? ORDER BY b.stock_value DESC LIMIT ?""", (floor, int(limit))).fetchall()]
    for cik in _norm_ciks(ciks):
        r = con.execute("SELECT cik, quarter FROM books WHERE cik=? ORDER BY quarter DESC LIMIT 1", (cik,)).fetchone()
        if r:
            newest.append(r)
    for cik, quarter in newest:
        prior = con.execute("SELECT quarter FROM books WHERE cik=? AND quarter<? ORDER BY quarter DESC LIMIT 1",
                            (cik, quarter)).fetchone()
        if prior and (prior[0], quarter) not in pairs:
            pairs.append((prior[0], quarter))
    return pairs


def _adj_stamps(con, pairs):
    _adj_tables(con)
    return [con.execute("SELECT stamp FROM adj_runs WHERE prior_quarter=? AND quarter=?", p).fetchone()
            for p in pairs]


def _adjustments_stored(con, prior_q, cur_q):
    """Whether corrections for this pair have ever been computed (current or not)."""
    _adj_tables(con)
    return con.execute("SELECT 1 FROM adj_runs WHERE prior_quarter=? AND quarter=?",
                       (prior_q, cur_q)).fetchone() is not None


def _adjustments(con, prior_q, cur_q, compute=True, status=None):
    """({cusip: split factor}, {new_cusip: (old_cusip, ratio)}) for a pair.

    `compute=False` reads what is stored, even if a newer filing has since
    made it due for recomputation: the per-stock and per-fund lookups wait on
    a 60-second limit and must not start minutes of work. The sync and the
    market-wide scans keep the stored corrections current. `status`, a dict,
    gets "complete": False when a split lookup went unanswered."""
    if not prior_q or not cur_q:
        return {}, {}
    if not compute:
        _adj_tables(con)  # a fresh index has none yet: then there is nothing to undo
    if compute:
        ok = _ensure_adjustments(con, prior_q, cur_q)
        if status is not None and not ok:
            status["complete"] = False
    splits = dict(con.execute("SELECT cusip, factor FROM split_adj WHERE prior_quarter=? AND quarter=?",
                              (prior_q, cur_q)).fetchall())
    links = {n: (o, r) for n, o, r in con.execute(
        "SELECT new_cusip, old_cusip, ratio FROM cusip_link WHERE prior_quarter=? AND quarter=?",
        (prior_q, cur_q)).fetchall()}
    return splits, links


def compare_positions(cur, prior, splits, links, universe=None):
    """One filer's positions now against its previous filing, corporate
    actions undone. `cur` and `prior` are {cusip: (issuer, value, shares,
    ticker)}. Yields (cusip, issuer, ticker, kind, shares, prior_shares, flow,
    value) with kind new/listed/added/trimmed/held/exited — "listed" a
    position in a security that was not reportable last quarter at all
    (`universe`, see prior_universe). Prior shares are restated
    in today's shares; flow is the shares changed at the price the filing
    carrying the position implies — an estimate, as 13F holds quarter-end
    snapshots, not trades."""
    used_prior = set()
    for cusip, (issuer, value, shares, ticker) in cur.items():
        px = value / shares if shares else 0.0
        if cusip in prior and cusip in links and links[cusip][0] in prior:
            # Held under both CUSIPs at the last filing (a change that
            # straddled the quarter end): the position was their sum.
            old, ratio = links[cusip]
            p_shares = prior[cusip][2] * splits.get(cusip, 1.0) + prior[old][2] * ratio
            used_prior.update((cusip, old))
        elif cusip in prior:
            p_shares = prior[cusip][2] * splits.get(cusip, 1.0)
            used_prior.add(cusip)
        elif cusip in links and links[cusip][0] in prior:
            old, ratio = links[cusip]
            p_shares = prior[old][2] * ratio
            used_prior.add(old)
        else:
            listed = universe is not None and cusip not in universe
            yield cusip, issuer, ticker, ("listed" if listed else "new"), shares, 0.0, \
                (0.0 if listed else value), value
            continue
        delta = shares - p_shares
        if p_shares > 0 and abs(delta) / p_shares < 0.01:
            yield cusip, issuer, ticker, "held", shares, p_shares, 0.0, value
            continue
        yield cusip, issuer, ticker, ("added" if delta > 0 else "trimmed"), shares, p_shares, delta * px, value
    for cusip, (issuer, value, shares, ticker) in prior.items():
        if cusip not in used_prior:
            yield cusip, issuer, ticker, "exited", 0.0, shares, -(value or 0.0), 0.0


def _positions(con, accession):
    return {c: (i, v or 0.0, s or 0.0, t or "") for c, i, v, s, t in con.execute("""
        SELECT h.cusip, h.issuer, h.value, h.shares, ct.ticker
          FROM holdings h LEFT JOIN cusip_ticker ct ON ct.cusip = h.cusip
         WHERE h.accession = ? AND h.put_call = ''""", (accession,))}


def _accession_quarter(con, accession):
    r = con.execute("SELECT quarter FROM books WHERE accession=?", (accession,)).fetchone()
    return r[0] if r else None


BIG_MOVE_FLOOR = 25_000_000.0   # a stake change smaller than this is not a "big move"
REPORTING_CHANGE = 3.0          # a book this many times bigger or smaller changed how it is filed


def comparable_books(book, prior_book):
    """Two filings of one filer can be compared position by position only if
    they describe the same book. One three times larger or smaller is a
    change of filing entity or units, not trading: JPMorgan's CIK "exited"
    $85B of NVDA when its holdings moved to another filer (2026-11-16)."""
    if not (book and prior_book and book > 0 and prior_book > 0):
        return False
    r = book / prior_book
    return 1.0 / REPORTING_CHANGE <= r <= REPORTING_CHANGE


def plausible_move(flow, book, prior_book):
    """No single position can move more than the whole book it sits in; one
    that does is a filing error (a value in the wrong units, a share class
    mislabelled — $265B of BRK-A from a $57B book)."""
    return abs(flow) <= max(book or 0.0, prior_book or 0.0)


INCOMPLETE = "incomplete|"   # stamp prefix: stored, but a split lookup went unanswered


def _big_moves_stored(con):
    """Whether a big-moves scan in today's layout is stored, current or not.
    The stamp row says so — every finished scan leaves one, even a scan that
    found no moves or could not finish its split lookups."""
    try:
        cols = [r[1] for r in con.execute("PRAGMA table_info(big_moves)")]
        if "filed" not in cols:
            return False  # no table, or one from before the filing date was kept: the scan rebuilds it
        return con.execute("SELECT 1 FROM big_moves_run LIMIT 1").fetchone() is not None
    except sqlite3.OperationalError:
        return False


def _big_moves_stamp(con):
    stamp = "|".join(f"{q}:{n}:{t}" for q, n, t in con.execute(
        "SELECT quarter, filers, ingested_at FROM quarters ORDER BY quarter"))
    stamp += "|" + _ticker_stamp(con) + f"|r{ADJ_RULES_VERSION}"
    stamp += "|" + str(con.execute("SELECT COUNT(filed) FROM filings").fetchone()[0])
    return stamp


def _big_moves_overdue(con):
    """No stored scan, or one from another state of the index — an earlier
    pass was cut short before its scan, tickers were rechecked, or the rules
    changed. A scan stored as incomplete for THIS state is not overdue: its
    lookups are tried again when the next filing arrives, not on every pass."""
    if not _big_moves_stored(con):
        return True
    row = con.execute("SELECT stamp FROM big_moves_run").fetchone()
    return row[0] not in (_big_moves_stamp(con), INCOMPLETE + _big_moves_stamp(con))


def _ensure_big_moves(con, progress=None):
    """Every filer's stake changes of BIG_MOVE_FLOOR or more, newest filing
    against the one before it, corporate actions undone. Kept per state of the
    index; recomputed when a quarter is (re)ingested or tickers are resolved."""
    con.executescript("""
        CREATE TABLE IF NOT EXISTS big_moves_run (stamp TEXT);
    """)
    # v2 adds the prior book (the base of an exit's weight); an older table is rebuilt.
    cols = [r[1] for r in con.execute("PRAGMA table_info(big_moves)")]
    if cols and "filed" not in cols:
        con.execute("DROP TABLE big_moves")
        con.execute("DELETE FROM big_moves_run")
    con.execute("""CREATE TABLE IF NOT EXISTS big_moves (
            cik TEXT, manager TEXT, quarter TEXT, prior_quarter TEXT, cusip TEXT, issuer TEXT,
            ticker TEXT, kind TEXT, shares REAL, prior_shares REAL, flow REAL, value REAL,
            book_value REAL, position_count INTEGER, prior_book_value REAL,
            accession TEXT, filed TEXT)""")
    stamp = _big_moves_stamp(con)
    row = con.execute("SELECT stamp FROM big_moves_run").fetchone()
    if row and row[0] == stamp:
        return False
    # Collected in memory and written in ONE transaction at the end. The scan
    # used to DELETE first and INSERT filer by filer, but the corrections it
    # asks for along the way commit — which published the half-done scan and
    # let a second scan (the screen's, beside the sync's) add its own copy of
    # every row: each move listed twice.
    found = []
    qs = quarter_pair(con)
    floor = qs[0] if qs else ""
    newest = con.execute("""
        SELECT b.cik, b.manager, b.quarter, b.accession, b.stock_value, b.stock_count
          FROM books b
          JOIN (SELECT cik, MAX(quarter) AS q FROM books WHERE cik <> '' GROUP BY cik) n
            ON n.cik = b.cik AND n.q = b.quarter
         WHERE b.quarter >= ? AND b.stock_value >= ? AND b.stock_count >= ?""",
        (floor, MIN_BOOK_VALUE, MIN_BOOK_POSITIONS)).fetchall()
    filed_on = dict(con.execute("SELECT accession, filed FROM filings WHERE filed IS NOT NULL"))
    adj = {}
    adj_status = {"complete": True}
    for n, (cik, manager, quarter, accession, book_value, count) in enumerate(newest):
        prior = con.execute("SELECT quarter, accession, stock_value FROM books WHERE cik=? AND quarter<? "
                            "ORDER BY quarter DESC LIMIT 1", (cik, quarter)).fetchone()
        if progress and n % 500 == 0:
            progress(n, len(newest))
        if not prior:
            continue  # a first filing: nothing it did can be seen
        if not comparable_books(book_value, prior[2]):
            continue
        key = (prior[0], quarter)
        if key not in adj:
            adj[key] = _adjustments(con, *key, status=adj_status)
        splits, links = adj[key]
        out = []
        for cusip, issuer, ticker, kind, shares, p_shares, flow, value in compare_positions(
                _positions(con, accession), _positions(con, prior[1]), splits, links,
                prior_universe(con, prior[0])):
            if (kind not in ("held", "listed") and abs(flow) >= BIG_MOVE_FLOOR
                    and plausible_move(flow, book_value, prior[2])):
                out.append((cik, manager, quarter, prior[0], cusip, issuer, ticker, kind, shares,
                            p_shares, flow, value, book_value, count, prior[2], accession,
                            filed_on.get(accession)))
        found.extend(out)
    con.commit()  # nothing of ours is pending; start the write from a clean state
    con.execute("BEGIN IMMEDIATE")  # two scans finishing together: one waits, then replaces
    con.execute("DELETE FROM big_moves")
    con.executemany("INSERT INTO big_moves VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", found)
    con.execute("DELETE FROM big_moves_run")
    # Always a stamp: its presence is what says a scan is stored. An
    # incomplete one never equals the index's stamp, so the next call that is
    # allowed to scan does.
    con.execute("INSERT INTO big_moves_run VALUES (?)",
                (stamp if adj_status["complete"] else INCOMPLETE + stamp,))
    con.commit()
    return True


def price_ranges(con, wanted, today=None):
    """{(ticker, start, end): (low, high)} — each stock's lowest low and
    highest high after `start` up to and including `end`: the prices a stake
    that changed between two filings changed hands within. 13F says how many
    shares, never when or at what price; this is the honest bound.

    Yahoo's history is split-adjusted; a split after `end` is multiplied back
    out so the range is in the shares the filing counts. Kept for a day."""
    import json as _json
    con.execute("CREATE TABLE IF NOT EXISTS price_range (ticker TEXT, start TEXT, end TEXT, day TEXT, "
                "low REAL, high REAL, PRIMARY KEY (ticker, start, end))")
    today = today or _date.today().isoformat()
    out, todo = {}, {}
    for t, a, b in wanted:
        row = con.execute("SELECT low, high FROM price_range WHERE ticker=? AND start=? AND end=? AND day=?",
                          (t, a, b, today)).fetchone()
        if row:
            out[(t, a, b)] = (row[0], row[1])
        else:
            todo.setdefault((a, b), []).append(t)
    if not todo:
        return out
    try:
        import yfinance as yf
    except Exception:
        return out
    # One Ticker.history per stock, never yf.download: download's shared state
    # hands one caller's rows to another when calls overlap (see the daemon's
    # gate), and a wrong range would sit in the cache all day.
    from concurrent.futures import ThreadPoolExecutor

    def one(job):
        t, a, b = job
        try:
            h = yf.Ticker(t).history(start=(_date.fromisoformat(a) + _td(days=1)).isoformat(),
                                     end=(_date.fromisoformat(b) + _td(days=1)).isoformat(),
                                     auto_adjust=False)
            lo, hi = float(h["Low"].min()), float(h["High"].max())
        except Exception:
            return job, None
        return job, (lo, hi) if (lo == lo and hi == hi and lo > 0) else None

    jobs = [(t, a, b) for (a, b), ts in todo.items() for t in sorted(set(ts))]
    with ThreadPoolExecutor(max_workers=3) as pool:  # gentle: Yahoo rate-limits bursts
        results = list(pool.map(one, jobs))
    for (t, a, b), lo_hi in results:
        if not lo_hi:
            continue
        splits = _announced_splits(con, t) or []
        f = split_factor_between(splits, b, today)
        lo, hi = lo_hi[0] * f, lo_hi[1] * f
        con.execute("INSERT OR REPLACE INTO price_range VALUES (?,?,?,?,?,?)", (t, a, b, today, lo, hi))
        out[(t, a, b)] = (lo, hi)
    con.execute("DELETE FROM price_range WHERE day < ?", (today,))  # yesterday's are never read again
    con.commit()
    return out


def big_moves(limit=200, side="both", sort="value", active_only=True, include_funds=False,
              filed_within=0, symbols=None, readonly=False):
    """The largest single stake changes — one manager, one stock — across
    every 13F filer, newest filing against the one before it.

    `sort`: "value" (estimated dollars) or "weight" (the move as a share of the
    manager's own book: conviction, which a dollar ranking hides under the
    index giants). `active_only` drops books of more than
    MAX_DISCRETIONARY_POSITIONS names: a 4,000-stock book moves with its
    inflows, not its views. Funds and ETFs as holdings are dropped by name
    unless asked for."""
    con = connect()
    try:
        if not con.execute("SELECT 1 FROM books LIMIT 1").fetchone():
            return {"error": "no 13F data ingested yet"}
        if readonly:
            # An alert check: what is stored, never minutes of recomputation
            # (the sync keeps it current after every pass that read filings).
            con.execute("""CREATE TABLE IF NOT EXISTS big_moves (
                cik TEXT, manager TEXT, quarter TEXT, prior_quarter TEXT, cusip TEXT, issuer TEXT,
                ticker TEXT, kind TEXT, shares REAL, prior_shares REAL, flow REAL, value REAL,
                book_value REAL, position_count INTEGER, prior_book_value REAL, accession TEXT, filed TEXT)""")
        elif not _big_moves_stored(con):
            # Nothing stored yet (a new index, or a table from an older
            # layout). The scan takes minutes on a full index — longer than
            # this screen waits — so it is the sync's to run, never ours;
            # `pending` asks the app to start a pass now.
            return {"rows": [], "pending": True,
                    "error": "Big moves are being worked out from the 13F index — a few minutes on a "
                             "new index. This tab fills in when the scan finishes."}
        where = ["1=1"]
        if side == "buys":
            where.append("flow > 0")
        elif side == "sells":
            where.append("flow < 0")
        if active_only:
            where.append(f"position_count <= {int(MAX_DISCRETIONARY_POSITIONS)}")
        if int(filed_within or 0) > 0:
            since = (_date.today() - _td(days=int(filed_within))).isoformat()
            where.append(f"filed >= '{since}'")
        args = []
        if symbols:
            syms = sorted({str(x).upper() for x in symbols if x})
            where.append(f"ticker IN ({','.join('?' * len(syms))})")
            args += syms
        # A buy is weighed against the book it went into, a sale against the
        # book it came out of.
        base = "(CASE WHEN flow < 0 THEN prior_book_value ELSE book_value END)"
        order = f"ABS(flow) / {base} DESC" if sort == "weight" else "ABS(flow) DESC"
        rows = con.execute(f"""
            SELECT cik, manager, quarter, prior_quarter, cusip, issuer, ticker, kind, shares,
                   prior_shares, flow, value, book_value, position_count, prior_book_value,
                   accession, filed
              FROM big_moves WHERE {' AND '.join(where)} ORDER BY {order} LIMIT ?""",
            args + [int(limit) * (1 if include_funds else 3)]).fetchall()
        out = []
        for r in rows:
            if not include_funds and is_fund(r[5]):
                continue
            out.append({"cik": r[0], "manager": r[1], "quarter": r[2], "prior_quarter": r[3],
                        "cusip": r[4], "issuer": r[5], "ticker": r[6] or "", "kind": r[7],
                        "shares": r[8], "prior_shares": r[9], "flow": r[10], "value": r[11],
                        "book_value": r[12], "position_count": r[13], "prior_book_value": r[14],
                        "filed": r[16] or "", "url": filing_url(r[0], r[15]) if r[15] else "",
                        "book_pct": (abs(r[10]) / (r[14] if r[10] < 0 else r[12]) * 100.0)
                                    if (r[14] if r[10] < 0 else r[12]) else None})
            if len(out) >= int(limit):
                break
        # The price range each move happened in: between the two filings.
        ranges = {} if readonly else price_ranges(con, {(r["ticker"], r["prior_quarter"], r["quarter"])
                                                        for r in out if r["ticker"]})
        for r in out:
            lo_hi = ranges.get((r["ticker"], r["prior_quarter"], r["quarter"]))
            if lo_hi:
                r["range_low"], r["range_high"] = lo_hi
        partial = [q for (q,) in con.execute("SELECT quarter FROM quarters WHERE COALESCE(partial,0)=1")]
        return {"rows": out, "side": side, "sort": sort, "active_only": bool(active_only),
                "filed_within": int(filed_within or 0),
                "floor": BIG_MOVE_FLOOR, "max_positions": MAX_DISCRETIONARY_POSITIONS,
                "partial_quarters": sorted(partial)}
    finally:
        con.close()



# A US listing: up to five letters, a share class after a dot or dash. The
# ticker lookup leaves a foreign line here and there ("WV8" for AvalonBay).
_US_TICKER = _re.compile(r"^[A-Z]{1,5}([.-][A-Z]{1,2})?$")


SCAN_COVERS = 0.98


def _filers(con, quarter):
    return con.execute("SELECT COUNT(DISTINCT cik) FROM books WHERE quarter=?", (quarter,)).fetchone()[0]


def stock_flows(limit=10, today=None, symbols=()):
    """Institutions' net change by stock: the stored big moves of the newest
    quarter, summed per stock. Reads only what the sync stored — never a scan,
    never a price lookup.

    Only a quarter whose filings are in (past the deadline, as many filers as
    before: quarter_is_complete) — halfway, the sums are the early filers' and
    flip: on 8 Nov 2026 MU was +$2.3B, it ended −$7.3B. During the season the
    quarter before cannot stand in, because its big moves are replaced filer
    by filer; then the answer is {"error"} saying so."""
    today = today or _date.today().isoformat()
    con = connect()
    try:
        if not con.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='big_moves'").fetchone() \
                or not _big_moves_stored(con):
            return {"error": "the 13F index has not been read yet"}
        # (A filing that mistypes a future period is not a quarter.)
        q = con.execute("SELECT MAX(quarter) FROM books WHERE quarter < ?", (today,)).fetchone()[0]
        prior = con.execute("SELECT MAX(quarter) FROM books WHERE quarter < ?", (q or "",)).fetchone()[0]
        if not q or not prior:
            return {"error": "the 13F index holds no two quarters to compare"}
        n, before = _filers(con, q), _filers(con, prior)
        if not quarter_is_complete(q, n, before, today):
            return {"error": f"13F season: {n:,} of about {before:,} managers have filed for "
                             f"{_date.fromisoformat(q):%b %-d, %Y}"}
        # The index can be ahead of the scan (a sync cut short): its sums are
        # then the early filers' again. The scan's stamp says how many filings
        # of the quarter it saw; a few stragglers since change nothing.
        stamp = con.execute("SELECT stamp FROM big_moves_run").fetchone()[0].removeprefix(INCOMPLETE)
        seen = next((int(p.split(":")[1]) for p in stamp.split("|") if p.startswith(q + ":")), 0)
        now = con.execute("SELECT filers FROM quarters WHERE quarter=?", (q,)).fetchone()
        # (`filers` is written when a sync ends; `books` filing by filing.)
        if seen < SCAN_COVERS * max(n, now[0] if now and now[0] else 0):
            return {"error": "the 13F scan is behind its filings (the next sync brings it up to date)"}
        by, filed_to = {}, ""
        for manager, issuer, ticker, flow, filed, kind in con.execute(
                "SELECT manager, issuer, ticker, flow, filed, kind FROM big_moves WHERE quarter = ? AND "
                "position_count <= ? AND ticker <> ''", (q, MAX_DISCRETIONARY_POSITIONS)):
            filed_to = max(filed_to, filed or "")
            # A filer's own parent or subsidiary: a restructuring, not a trade.
            if issuer_key(manager) == issuer_key(issuer) or not _US_TICKER.match(ticker):
                continue
            d = by.setdefault(ticker, {"ticker": ticker, "net": 0.0, "adds": 0, "trims": 0, "old": 0, "fund": 0})
            d["net"] += flow
            d["adds" if flow > 0 else "trims"] += 1
            d["old"] += kind != "new"
            # One verdict per stock, whatever each filer's spelling of it.
            d["fund"] += 1 if is_fund(issuer) else -1
        # Nobody held it before: a listing the prior quarter's few early
        # filers hid from the new-listing rule (SK Hynix's ADS: 100 "new"
        # positions, no other move).
        rank = sorted((d for d in by.values() if d["fund"] < 0 and (d["old"] or d["adds"] < 5)),
                      key=lambda d: (d["net"], d["ticker"]))
        if not rank:
            return {"error": "the 13F scan holds no moves"}
        return {"quarter": q, "filed_to": filed_to, "managers": n,
                "by": {d["ticker"]: d for d in rank if d["ticker"] in set(symbols)},
                "added": [d for d in reversed(rank) if d["net"] > 0][:int(limit)],
                "trimmed": [d for d in rank if d["net"] < 0][:int(limit)]}
    finally:
        con.close()


# ── Keeping the index current ───────────────────────────────────────────────
#
# Two SEC sources, used together so the index is as new as the filings are:
#   * the bulk data sets — every 13F in a three-month FILING window, published
#     some weeks after the window closes; complete, and the authority;
#   * EDGAR's daily index — every 13F-HR filed each day, from the day after the
#     newest data set's window up to yesterday. Each filing is read from its
#     full submission file: one request carries the period, the manager and
#     the holdings table.
# sync() runs both, resumably: a data set is recorded when it is in, a day
# when every filing on it is, a filing when it is stored. A quarter whose
# filings pass ~90% of the previous quarter's filers after the deadline is
# treated as complete, so the screens move to it without waiting for the data
# set; the data set then replaces it wholesale.

from datetime import date as _date, timedelta as _td

EDGAR_GAP = 0.12            # ~8 requests a second: inside SEC's 10/s fair-access limit
COMPLETE_SHARE = 0.9        # of the previous quarter's filers, after the deadline
FILING_DEADLINE_DAYS = 45


def dataset_window(name):
    """'01jun2026-31aug2026_form13f.zip' -> ('2026-06-01', '2026-08-31')."""
    m = _re.search(r"(\d{2})([a-z]{3})(\d{4})-(\d{2})([a-z]{3})(\d{4})", (name or "").lower())
    if not m:
        return None
    mon = {k: i for i, k in enumerate(("jan", "feb", "mar", "apr", "may", "jun", "jul", "aug",
                                       "sep", "oct", "nov", "dec"), 1)}
    try:
        a = _date(int(m.group(3)), mon[m.group(2)], int(m.group(1)))
        b = _date(int(m.group(6)), mon[m.group(5)], int(m.group(4)))
    except (KeyError, ValueError):
        return None
    return a.isoformat(), b.isoformat()


def quarter_deadline(period):
    """The day a quarter's 13F is due: 45 days after the quarter end."""
    return (_date.fromisoformat(period) + _td(days=FILING_DEADLINE_DAYS)).isoformat()


def quarter_is_complete(period, filers, prior_filers, today):
    return (today > quarter_deadline(period) and bool(prior_filers)
            and filers >= COMPLETE_SHARE * prior_filers)


def parse_submission(text):
    """(period ISO, manager, information-table XML bytes) from a full 13F
    submission file, or None. The header says CONFORMED PERIOD OF REPORT and
    the filer's conformed name; the table is the embedded XML document."""
    per = _re.search(r"CONFORMED PERIOD OF REPORT:\s*(\d{8})", text)
    name = _re.search(r"COMPANY CONFORMED NAME:\s*(.+)", text)
    tab = _re.search(r"<(?:\w+:)?informationTable[\s>].*?</(?:\w+:)?informationTable>", text, _re.S)
    if not per or not tab:
        return None
    p = per.group(1)
    return f"{p[:4]}-{p[4:6]}-{p[6:]}", (name.group(1).strip() if name else ""), tab.group(0).encode()


def _day_13f(d):
    """13F-HR originals filed on one day: [(cik, path)]; [] on a day with no
    index (404: weekend, holiday, not yet published); None when the index could
    not be read — the day is then not recorded and is asked again."""
    q = (d.month - 1) // 3 + 1
    url = f"https://www.sec.gov/Archives/edgar/daily-index/{d.year}/QTR{q}/form.{d.strftime('%Y%m%d')}.idx"
    r, code = sec_http.get(url, timeout=60, gap=EDGAR_GAP)
    if code == 404:
        # No index for the day (sec_http reads EDGAR's S3 AccessDenied as 404):
        # a day SEC was closed — or one not published yet, which only a day a
        # few days back can rule out.
        return [] if (_date.today() - d).days >= 3 else None
    if r is None:
        return None
    out = []
    for line in r.text.splitlines():
        if not line.startswith("13F-HR ") or line.startswith("13F-HR/A"):
            continue
        m = _re.search(r"edgar/data/(\d+)/(\d{10}-\d{2}-\d{6})\.txt", line)
        if m:
            out.append((m.group(1), m.group(0), m.group(2)))
    return out


def _store_filing(con, accession, period, cik, manager, positions, filed=None):
    """One filer's quarter, replaced by this filing (as ingest_current does:
    deleting by accession alone would leave a second original in place)."""
    cik = _pad_cik(cik)
    con.execute("DELETE FROM holdings WHERE accession IN "
                "(SELECT accession FROM filings WHERE cik=? AND quarter=?)", (cik, period))
    con.execute("DELETE FROM books WHERE cik=? AND quarter=?", (cik, period))
    con.execute("DELETE FROM filings WHERE cik=? AND quarter=?", (cik, period))
    con.execute("INSERT OR REPLACE INTO filings (accession, quarter, cik, manager, is_amendment, filed) "
                "VALUES (?,?,?,?,?,?)", (accession, period, cik, manager, 0, filed))
    con.executemany("INSERT INTO holdings VALUES (?,?,?,?,?,?,?,?)",
                    [(accession, period, p["cusip"], p["issuer"], p["class"], p["put_call"],
                      p["value"], p["shares"]) for p in positions])
    stock = [p for p in positions if not p["put_call"]]
    con.execute("INSERT OR REPLACE INTO books VALUES (?,?,?,?,?,?)",
                (accession, period, cik, manager, sum(p["value"] for p in stock), len(stock)))


def _register_quarter(con, period, source):
    """The quarters row for an EDGAR-filled quarter: its filer count, and
    whether it is complete enough to be served as the default."""
    filers = con.execute("SELECT COUNT(DISTINCT cik) FROM books WHERE quarter=? AND cik<>''",
                         (period,)).fetchone()[0]
    prior = con.execute("SELECT filers FROM quarters WHERE quarter<? AND COALESCE(partial,0)=0 "
                        "ORDER BY quarter DESC LIMIT 1", (period,)).fetchone()
    complete = quarter_is_complete(period, filers, prior[0] if prior else 0, _date.today().isoformat())
    rows = con.execute("SELECT COUNT(*) FROM holdings WHERE quarter=?", (period,)).fetchone()[0]
    con.execute("INSERT OR REPLACE INTO quarters VALUES (?,?,?,?,datetime('now'),?)",
                (period, source, filers, rows, 0 if complete else 1))
    return filers, complete


def filing_url(cik, accession):
    """The filing's index page on EDGAR — the source behind a row."""
    try:
        return (f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/"
                f"{accession.replace('-', '')}/{accession}-index.htm")
    except (TypeError, ValueError):
        return ""


def backfill_filed_dates():
    """Filing dates for filings indexed before they were kept: EDGAR's
    quarterly full index lists every 13F-HR with the day it was filed — one
    file per calendar quarter of filing, read once."""
    con = connect()
    try:
        # Once per quarter: a filing the index never lists (an amendment, an
        # odd form type) would otherwise send every sync to download it again.
        con.execute("CREATE TABLE IF NOT EXISTS filed_backfill (quarter TEXT PRIMARY KEY, at TEXT)")
        periods = [q for (q,) in con.execute("""
            SELECT DISTINCT quarter FROM filings WHERE filed IS NULL
               AND quarter NOT IN (SELECT quarter FROM filed_backfill)
             ORDER BY quarter DESC LIMIT 4""")]
        if not periods:
            return {"filled": 0}
    finally:
        con.close()
    # A quarter's 13Fs are filed in the next calendar quarter (late ones in the one after).
    wanted = set()
    for p in periods:
        y, m = int(p[:4]), int(p[5:7])
        for k in (1, 2):
            q = (m - 1) // 3 + 1 + k
            yy = y + (q - 1) // 4
            wanted.add((yy, (q - 1) % 4 + 1))
    dates = {}
    for y, q in sorted(wanted):
        if _date(y, (q - 1) * 3 + 1, 1) > _date.today():
            continue
        r, _ = sec_http.get(f"https://www.sec.gov/Archives/edgar/full-index/{y}/QTR{q}/form.idx", timeout=300)
        if r is None:
            continue
        for line in r.text.splitlines():
            if not line.startswith("13F-HR"):
                continue
            m = _re.search(r"(\d{4}-\d{2}-\d{2})\s+edgar/data/\d+/(\d{10}-\d{2}-\d{6})\.txt", line)
            if m:
                dates[m.group(2)] = m.group(1)
    con = connect()
    try:
        con.executemany("UPDATE filings SET filed=? WHERE accession=? AND filed IS NULL",
                        [(d, a) for a, d in dates.items()])
        if dates:  # an unreadable index is tried again next time
            con.executemany("INSERT OR REPLACE INTO filed_backfill VALUES (?, datetime('now'))",
                            [(q,) for q in periods])
        con.commit()
        return {"filled": con.total_changes, "index_entries": len(dates)}
    finally:
        con.close()


def sync(progress=None, max_filings=None, ciks=None):
    """Bring the index up to the newest filing SEC has. Safe to run often:
    everything already in is skipped."""
    # SEC refuses automated requests that do not name a contact (since
    # 2026-10). Without one every step below fails, and quietly: say so.
    if not sec_http.has_contact():
        return {"error": "SEC needs a contact email for automated access — set \"Contact email\" "
                         "in Settings › API keys (Your identity)", "transient": False}
    out = {"datasets": [], "days": 0, "filings": 0, "failed": 0, "quarters": {}}
    con = connect()
    try:
        con.executescript("""
            CREATE TABLE IF NOT EXISTS datasets_in (name TEXT PRIMARY KEY, quarter TEXT, at TEXT);
            CREATE TABLE IF NOT EXISTS edgar_days (day TEXT PRIMARY KEY, filings INTEGER, at TEXT);
        """)
        # Data sets already in, including those ingested before this table.
        for (src,) in con.execute("SELECT source FROM quarters WHERE COALESCE(partial,0)=0"):
            if src and src.endswith(".zip"):
                con.execute("INSERT OR IGNORE INTO datasets_in VALUES (?,?,datetime('now'))",
                            (src.rsplit("/", 1)[-1], ""))
        con.commit()
    finally:
        con.close()

    # 1. Data sets newer than the newest one in.
    urls = available_datasets()
    if not urls:
        out["warning"] = "could not list the SEC data sets; EDGAR only this time"
    con = connect()
    try:
        have = {n for (n,) in con.execute("SELECT name FROM datasets_in")}
        newest_in = max((dataset_window(n)[1] for n in have if dataset_window(n)), default="")
    finally:
        con.close()
    for url in reversed(urls):  # oldest first, so a quarter is never overwritten by an older set
        name = url.rsplit("/", 1)[-1]
        w = dataset_window(name)
        if name in have or not w or w[1] <= newest_in:
            continue
        r = ingest(url)
        if r.get("error"):
            out["datasets"].append({"name": name, "error": r["error"]})
            break
        con = connect()
        con.execute("INSERT OR REPLACE INTO datasets_in VALUES (?,?,datetime('now'))", (name, r["quarter"]))
        # Ingesting a data set replaces its quarter wholesale, late EDGAR
        # filings for it included: the days after its window are read again.
        con.execute("DELETE FROM edgar_days WHERE day > ?", (w[1],))
        con.commit()
        con.close()
        newest_in = w[1]
        out["datasets"].append({"name": name, "quarter": r["quarter"], "filers": r["filers"]})

    # 2. EDGAR, day by day, from the day after that window to yesterday.
    # Paced at EDGAR_GAP (sec_http's per-request `gap`), not the 0.4 s default.
    con = connect()
    try:
        start = (_date.fromisoformat(newest_in) + _td(days=1)) if newest_in else _date.today() - _td(days=90)
        done = {d for (d,) in con.execute("SELECT day FROM edgar_days")}
        floor_q = (quarter_pair(con) or [""])[-1]  # nothing older than the comparison base
        known = {a for (a,) in con.execute("SELECT accession FROM filings")}
        touched = set()
        d = start
        today = _date.today()
        while d < today:
            if d.isoformat() in done or d.weekday() >= 5:
                d += _td(days=1)
                continue
            day = _day_13f(d)
            if day is None:
                out["warning"] = f"EDGAR's index for {d} could not be read; it is asked again next time"
                out["refused"] = True
                break
            stored_all = True
            for cik, path, acc in day:
                if acc in known:
                    continue
                if max_filings and out["filings"] >= int(max_filings):
                    stored_all = False
                    out["more"] = True  # a pass is capped; the next one carries on
                    break
                r, code = sec_http.get(f"https://www.sec.gov/Archives/{path}", timeout=60, gap=EDGAR_GAP)
                for wait in (3, 10):
                    # A large submission file times out now and then (a read
                    # timeout or a 5xx, which sec_http does not retry: it
                    # retries only connection failures) — try it again first.
                    if r is not None or code not in (None, 429, 500, 502, 503, 504):
                        break
                    time.sleep(wait)
                    r, code = sec_http.get(f"https://www.sec.gov/Archives/{path}", timeout=120, gap=EDGAR_GAP)
                if r is None and code not in (404, 410):
                    stored_all = False  # refused or offline: this day is not finished
                    out["failed"] += 1
                    out["refused"] = True  # asked again soon, not in six hours
                    out["warning"] = f"SEC refused a filing request on {d} (HTTP {code}); resuming shortly"
                    break
                parsed = parse_submission(r.text) if r is not None else None
                if not parsed:
                    out["failed"] += 1  # withdrawn, or not a 13F we can read: skipped, not retried forever
                    continue
                period, manager, xml = parsed
                if period < floor_q:
                    continue  # a late filing for a quarter this index no longer compares
                positions = parse_information_table(xml)
                if not positions:
                    out["failed"] += 1
                    continue
                _store_filing(con, acc, period, cik, manager, positions, d.isoformat())
                known.add(acc)
                touched.add(period)
                out["filings"] += 1
                con.commit()  # one filing per transaction: readers never wait long
                if progress and out["filings"] % 50 == 0:
                    progress(out["filings"], d.isoformat())
            if not stored_all:
                break
            con.execute("INSERT OR REPLACE INTO edgar_days VALUES (?,?,datetime('now'))", (d.isoformat(), len(day)))
            con.commit()
            out["days"] += 1
            d += _td(days=1)
        # The last day read in full — not one a capped pass stopped inside.
        last = con.execute("SELECT MAX(day) FROM edgar_days").fetchone()[0]
        out["through"] = last or (start - _td(days=1)).isoformat()
        for period in sorted(touched):
            # Only a quarter the bulk sets have not covered: their rows are the authority.
            row = con.execute("SELECT source FROM quarters WHERE quarter=?", (period,)).fetchone()
            if row and row[0] and str(row[0]).endswith(".zip"):
                continue
            filers, complete = _register_quarter(con, period, f"EDGAR daily index through {out['through']}")
            out["quarters"][period] = {"filers": filers, "complete": complete}
        con.commit()
    finally:
        con.close()

    # 3. Name the new holdings, and correct tickers picked before us_listing().
    # Housekeeping against outside services: whatever goes wrong here must not
    # keep step 4 from running — it is the only place the screens' scans are made.
    try:
        if out["filings"] or out["datasets"]:
            newest = max(list(out["quarters"]) + [x.get("quarter", "") for x in out["datasets"]] or [""])
            if newest:
                r = resolve_cusips(limit=1500, quarter=newest)
                out["resolved"] = r.get("resolved", 0)
        out["recheck"] = recheck_tickers()
        out["filed_dates"] = backfill_filed_dates()
    except Exception as e:
        # Its own key: `warning` is what turns a quiet pass into "SEC did not answer".
        out["upkeep_warning"] = f"ticker and filing-date upkeep failed: {type(e).__name__}: {str(e)[:120]}"

    # 4. The scans the screens read. Last, because the tickers and filing dates
    # settled in step 3 are part of what they are stamped with — refreshed
    # before it, the scan was already due again when the pass ended.
    # After a pass that read filings — and after one that did not, when the
    # stored scan belongs to another state of the index: the screens no longer
    # recompute, so a pass cut short before this step (the app closed), a
    # ticker recheck or a rules change has to be caught up here.
    con = connect()
    try:
        # Corrections first, every pass: for the recent quarter pairs and for
        # whatever pair each of the largest filers compares (one that skipped
        # a quarter compares across the gap). A current pair costs one stamp
        # comparison; one with an unanswered split lookup is tried again.
        pairs = _screen_pairs(con, TOP_FIRMS, ciks)
        # The universes the screens compare against: every prior quarter of
        # those pairs. Cheap when current (a stamp), 5 s each when not.
        for q in sorted({a for a, _b in pairs}):
            _ensure_universe(con, q)
        before = _adj_stamps(con, pairs)
        for a, b in pairs:
            _ensure_adjustments(con, a, b)
        # Only a change counts: a pair recomputed to the same incomplete
        # state is not news, and must not make the screens reload every pass.
        corrected = _adj_stamps(con, pairs) != before
        scanned = False
        if out["filings"] or out["datasets"] or _big_moves_overdue(con):
            # what the screens and the alerts read, current with the filings just stored
            scanned = bool(_ensure_big_moves(con))
        out["refreshed"] = scanned or corrected
    finally:
        con.close()
    if not urls and not out["days"] and not out["filings"] and out.get("warning"):
        # Neither source answered: a failure, not "up to date".
        out["error"] = "SEC did not answer (data-set list and EDGAR index): " + out["warning"]
        out["transient"] = True
    return out


def fund_filings(ciks):
    """Each fund's newest 13F: when it was filed, for which quarter, and its
    biggest buy and sell against its previous filing — what a "fund you
    follow filed" alert says. Reads stored corrections only (an alert check
    must not start minutes of work)."""
    con = connect()
    try:
        out = []
        for cik in ciks or []:
            c = _pad_cik(cik)
            # The original before an amendment, then the earliest: one answer
            # every pass, so a filing is announced once.
            row = con.execute("""SELECT b.accession, b.quarter, b.manager, f.filed, b.stock_value
                                   FROM books b JOIN filings f ON f.accession = b.accession
                                  WHERE b.cik=? ORDER BY b.quarter DESC, COALESCE(f.is_amendment,0) ASC,
                                        f.filed ASC, b.accession ASC LIMIT 1""", (c,)).fetchone()
            if not row:
                continue
            prior = con.execute("SELECT accession FROM books WHERE cik=? AND quarter<? ORDER BY quarter DESC LIMIT 1",
                                (c, row[1])).fetchone()
            m = _book_moves(con, row[0], prior[0], compute=False) if prior else None
            out.append({"cik": c, "accession": row[0], "quarter": row[1], "manager": row[2],
                        "filed": row[3] or "", "book_value": row[4],
                        "url": filing_url(c, row[0]),
                        "top_buy": (m or {}).get("top_buy"), "top_sell": (m or {}).get("top_sell"),
                        "new": (m or {}).get("new"), "exited": (m or {}).get("exited")})
        return {"funds": out}
    finally:
        con.close()


def handle_action(action, payload):
    if action == "status":
        return status()
    if action == "newer":
        return newer_available()
    if action == "datasets":
        return {"datasets": available_datasets()[:12]}
    if action == "ingest":
        return ingest(payload.get("url"))
    if action == "ingest_current":
        return ingest_current(int(payload.get("top") or 400), None, payload.get("ciks"))
    if action == "ingest_recent":
        return ingest_recent(int(payload.get("quarters") or 2))
    if action == "resolve":
        return resolve_ticker(payload.get("ticker") or "")
    if action == "resolve_symbols":
        return resolve_cusips(int(payload.get("limit") or 2000), payload.get("quarter"))
    if action == "resolve_all":
        # Run to completion. 22,820 CUSIPs at OpenFIGI's keyless rate is about
        # 95 minutes, and the top 2,000 by value already cover 96% of all
        # institutional value — so the caller sees a usable map within minutes
        # and the tail fills in behind it. Resumable: every batch commits, so an
        # interrupted run picks up where it stopped.
        total = {"resolved": 0, "unrecognised": 0}
        while True:
            r = resolve_cusips(int(payload.get("chunk") or 500), payload.get("quarter"))
            if r.get("error"):
                return {**total, "error": r["error"]}
            total["resolved"] += r.get("resolved", 0)
            total["unrecognised"] += r.get("unrecognised", 0)
            total["remaining"] = r.get("remaining", 0)
            if not r.get("remaining") or r.get("resolved", 0) + r.get("unrecognised", 0) == 0:
                break
        return total
    if action == "holders":
        return holders(payload.get("ticker"), payload.get("cusip"),
                       payload.get("limit") or 60, payload.get("quarter"),
                       float(payload.get("min_book") or MIN_BOOK_VALUE),
                       int(payload.get("min_positions") or MIN_BOOK_POSITIONS),
                       payload.get("max_positions"), payload.get("sort") or "value")
    if action == "movers":
        return movers(int(payload.get("limit") or 100), payload.get("sort") or "breadth_up",
                      int(payload.get("min_holders") or 20), payload.get("quarter"),
                      bool(payload.get("include_funds", False)))
    if action == "ticker_totals":
        return shares_by_ticker(payload.get("quarter"))
    if action == "firms":
        return firms(payload.get("query") or "", int(payload.get("limit") or 40),
                     payload.get("quarter"))
    if action == "sync":
        def prog(n, day):
            print(f"PROGRESS {n} {day}", file=sys.stderr, flush=True)
        return sync(prog, payload.get("max_filings"), payload.get("ciks"))
    if action == "recheck_tickers":
        return recheck_tickers(int(payload.get("limit") or 3000))
    if action == "big_moves":
        return big_moves(int(payload.get("limit") or 200), payload.get("side") or "both",
                         payload.get("sort") or "value", bool(payload.get("active_only", True)),
                         bool(payload.get("include_funds", False)), int(payload.get("filed_within") or 0),
                         payload.get("symbols"), bool(payload.get("readonly", False)))
    if action == "fund_filings":
        return fund_filings(payload.get("ciks") or [])
    if action == "top_firms":
        return top_firms(int(payload.get("limit") or TOP_FIRMS), payload.get("ciks"))
    if action == "book":
        return firm_book(payload.get("cik"), payload.get("quarter"),
                         int(payload.get("limit") or 250))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
