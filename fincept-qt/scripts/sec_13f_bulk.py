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
import sqlite3
import sys
import time
import urllib.request
import zipfile

try:
    import requests
except ImportError:
    requests = None

UA = {"User-Agent": "FinceptTerminal research@hanlexon.com",
      "Accept-Encoding": "gzip, deflate"}

DATASET_INDEX = "https://www.sec.gov/dera/data/form-13f"
OPENFIGI_URL = "https://api.openfigi.com/v3/mapping"
# Optional and per-user. Keyless OpenFIGI allows 25 requests a minute, which
# puts the full 22,820-CUSIP map at about 95 minutes; a free key raises that to
# 25 requests per 6 seconds and turns it into a couple of minutes. Never
# bundled — read from the environment the app injects.
OPENFIGI_KEY = os.environ.get("OPENFIGI_API_KEY", "")

_LAST_REQ = 0.0
_MIN_REQ_GAP = 0.4


def db_path():
    """Where the index lives. Mirrors the app data directory the C++ side uses."""
    env = os.environ.get("FINCEPT_DATA_DIR")
    if env:
        base = env
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support/com.fincept.terminal")
    elif os.name == "nt":
        base = os.path.join(os.environ.get("LOCALAPPDATA", ""), "com.fincept.terminal")
    else:
        base = os.path.expanduser("~/.local/share/com.fincept.terminal")
    os.makedirs(base, exist_ok=True)
    return os.path.join(base, "form13f.sqlite")


def _get(url, timeout=300):
    global _LAST_REQ
    if requests is None:
        return None
    elapsed = time.time() - _LAST_REQ
    if elapsed < _MIN_REQ_GAP:
        time.sleep(_MIN_REQ_GAP - elapsed)
    try:
        r = requests.get(url, timeout=timeout, headers=UA)
        _LAST_REQ = time.time()
        r.raise_for_status()
        return r
    except Exception:
        _LAST_REQ = time.time()
        return None


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
    t = (raw or "").upper().strip()
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
                            "LIMIT 1").fetchone()]
    if not stale:
        return
    for t in stale:
        con.execute(f"UPDATE {t} SET ticker='' WHERE ticker LIKE '% %'")
        con.execute(f"UPDATE {t} SET ticker=REPLACE(ticker, '/', '-') WHERE ticker LIKE '%/%'")
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
                    pick = next((r for r in recs
                                 if (r.get("exchCode") or "").upper() == "US"), recs[0])
                    con.execute("INSERT OR REPLACE INTO cusip_ticker VALUES (?,?,?,?)",
                                (cusip, yahoo_ticker(pick.get("ticker")),
                                 pick.get("name") or "", "openfigi"))
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
    r = _get(DATASET_INDEX, timeout=60)
    if r is None:
        return []
    hits = re.findall(r'href="([^"]*form-13f-data-sets/[^"]*\.zip)"', r.text)
    return ["https://www.sec.gov" + h if h.startswith("/") else h for h in hits]


def ingest(url=None, progress=None):
    """Download one quarterly data set and index it. Returns a summary."""
    if requests is None:
        return {"error": "requests not available"}
    if not url:
        urls = available_datasets()
        if not urls:
            return {"error": "could not list SEC 13F data sets"}
        url = urls[0]

    r = _get(url, timeout=600)
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
                "INSERT OR REPLACE INTO filings VALUES (?,?,?,?,?)",
                [(a, m[0], m[3], m[1], m[2]) for a, m in chosen.values()])

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
            # The data set is named by filing window, not by reported quarter,
            # so this cannot be resolved to a quarter without downloading it.
            # The honest signal is "there is a set you have not ingested".
            "unseen_datasets": max(0, len(urls) - len(have))}


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
    """Every filer holding this security.

    `sort` is "value" — position size, the HDS default — or "weight", the
    position as a share of the filer's own book, which answers a different
    question and carries a book-size floor so the answer is Berkshire rather
    than a shell. Totals, counts and concentration are computed over EVERY
    holder before the display limit is applied.
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
                                 AND ph.cusip = h.cusip AND ph.put_call = ''
            WHERE h.cusip = ? AND h.quarter = ?
              AND b.stock_value >= ? AND b.stock_count >= ?
              AND (? = 0 OR b.stock_count <= ?)
            ORDER BY h.value DESC
        """, (prior_q or "", cusip, quarter,
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
                    rec["action"] = "new" if filed_prior else "first seen"
                    rec["shares_delta"] = shares
                    if not filed_prior:
                        rec["note"] = ("no prior-quarter filing from this filer, so an opening "
                                       "position cannot be told apart from a first appearance")
                else:
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
        # Totals across EVERY filer, not just the rows returned. This is what
        # makes an institutional-ownership percentage computable from the
        # filings themselves rather than taken from a vendor aggregate.
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
        if not quarter:
            row = con.execute(
                "SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 1").fetchone()
            if not row:
                return {"error": "no 13F data ingested yet"}
            quarter = row[0]
        q = (query or "").strip()
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


def _book_moves(con, accession, prior_accession):
    """Position-level moves between two of one filer's filings: counts of new,
    added, trimmed and exited common-stock positions, the dollar flow those
    moves imply, and the largest buy and sell. Options are excluded — a put
    is not a holding.

    Flow is shares changed × the price implied by the filing that carries the
    position (value ÷ shares): the current filing for new/added/trimmed, the
    prior one for exits. It is an estimate — 13F shows quarter-end snapshots,
    not trades — and is labelled as one wherever it is shown.
    """
    if not prior_accession:
        return None
    rows = con.execute("""
        SELECT h.cusip, h.issuer, h.value, h.shares, ph.shares, ct.ticker
          FROM holdings h
          LEFT JOIN holdings ph ON ph.accession = ? AND ph.cusip = h.cusip AND ph.put_call = ''
          LEFT JOIN cusip_ticker ct ON ct.cusip = h.cusip
         WHERE h.accession = ? AND h.put_call = ''
    """, (prior_accession, accession)).fetchall()
    m = {"new": 0, "added": 0, "trimmed": 0, "held": 0, "exited": 0,
         "bought_value": 0.0, "sold_value": 0.0, "top_buy": None, "top_sell": None}

    def note(kind, issuer, ticker, flow):
        key = "top_buy" if kind == "buy" else "top_sell"
        cur = m[key]
        if cur is None or abs(flow) > abs(cur["value"]):
            m[key] = {"issuer": issuer, "ticker": ticker or "", "value": flow}

    for cusip, issuer, value, shares, prior_shares, ticker in rows:
        px = (value / shares) if shares else 0.0
        if prior_shares is None:
            m["new"] += 1
            flow = value
        else:
            delta = shares - prior_shares
            if prior_shares > 0 and abs(delta) / prior_shares < 0.01:
                m["held"] += 1
                continue
            m["added" if delta > 0 else "trimmed"] += 1
            flow = delta * px
        if flow >= 0:
            m["bought_value"] += flow
            note("buy", issuer, ticker, flow)
        else:
            m["sold_value"] += -flow
            note("sell", issuer, ticker, flow)
    for issuer, value, ticker in con.execute("""
        SELECT ph.issuer, ph.value, ct.ticker
          FROM holdings ph
          LEFT JOIN cusip_ticker ct ON ct.cusip = ph.cusip
         WHERE ph.accession = ? AND ph.put_call = ''
           AND ph.cusip NOT IN (SELECT cusip FROM holdings WHERE accession = ? AND put_call = '')
    """, (prior_accession, accession)).fetchall():
        m["exited"] += 1
        m["sold_value"] += value or 0.0
        note("sell", issuer, ticker, -(value or 0.0))
    return m


def top_firms(limit=50):
    """The largest 13F filers, each with what it did since its previous filing.

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
        firms_out = []
        for cik, manager, quarter, accession, value, count in newest:
            prior = con.execute(
                "SELECT quarter, accession, stock_value, stock_count FROM books "
                "WHERE cik=? AND quarter<? ORDER BY quarter DESC LIMIT 1",
                (cik, quarter)).fetchone()
            row = {"cik": cik, "manager": manager, "quarter": quarter,
                   "book_value": value, "position_count": count}
            if prior:
                row.update({"prior_quarter": prior[0], "prior_book_value": prior[2],
                            "prior_position_count": prior[3]})
                row["moves"] = _book_moves(con, accession, prior[1])
            firms_out.append(row)
        newest_q = max((f["quarter"] for f in firms_out), default=None)
        partial = {r[0] for r in con.execute(
            "SELECT quarter FROM quarters WHERE COALESCE(partial,0)=1").fetchall()}
        return {"firms": firms_out, "newest_quarter": newest_q,
                "partial_quarters": sorted(partial)}
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

        positions = []
        for issuer, cls, cusip, pc, value, shares, ticker, prior_shares, filed_prior in rows:
            rec = {"issuer": issuer, "class": cls, "cusip": cusip, "ticker": ticker or "",
                   "put_call": pc, "is_derivative": bool(pc),
                   "value": value, "shares": shares,
                   "weight": (value / book_total) if (book_total and not pc) else None}
            if prior_q and not pc:
                if prior_shares is None:
                    rec["action"] = "new" if filed_prior else "first seen"
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
                         """, (prior_acc[0], accession)).fetchall()]

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
    r = _get(f"https://data.sec.gov/submissions/CIK{_pad_cik(cik)}.json", timeout=30)
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
    r = _get(f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{a}/index.json", timeout=30)
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


def ingest_current(top=400, progress=None):
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
            r = _get(url, timeout=45)
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
            con.execute("INSERT OR REPLACE INTO filings VALUES (?,?,?,?,?)",
                        (f["accession"], q, _pad_cik(cik), manager, 0))
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
FUND_WORDS = ("ETF", "ETN", " ET ", "ISHARES", "PROSHARES", "DIREX", "SPDR", "INVESCO",
              "VANGUARD", "TRUST", " TR ", "FUND", "INDEX", "PORTFOLIO", "PHYSICAL",
              "GRANITESHARES", "WISDOMTREE", "GLOBAL X", "ARK ", "VANECK",
              "FIRST TRUST", "SCHWAB STRATEGIC", "JPMORGAN EXCHANGE", "PACER",
              "ULTRA", "BULL", "BEAR", " 2X", " 3X", "LEVERAGED", "INVERSE", "IPATH", " VIX",
              "STRATEGIC INV", "CLOSED-END", "CLOSED END", "MUNI", "SELECT SECTOR")


def is_fund(name):
    n = " " + (name or "").upper() + " "
    return any(w in n for w in FUND_WORDS)


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
    if con.execute("SELECT 1 FROM breadth_runs WHERE quarter=? AND prior_quarter=?",
                   (cur_q, prior_q)).fetchone():
        return
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
            SELECT COALESCE(c.cusip, p.cusip) AS cusip,
                   CASE WHEN p.cik IS NOT NULL THEN 1 ELSE 0 END AS had,
                   CASE WHEN c.cik IS NOT NULL AND p.cik IS NULL THEN 1 ELSE 0 END AS is_new,
                   CASE WHEN c.cik IS NULL AND p.cik IS NOT NULL THEN 1 ELSE 0 END AS is_closed,
                   CASE WHEN c.cik IS NOT NULL AND p.cik IS NOT NULL
                             AND c.shares > p.shares * 1.01 THEN 1 ELSE 0 END AS is_added,
                   CASE WHEN c.cik IS NOT NULL AND p.cik IS NOT NULL
                             AND c.shares < p.shares * 0.99 THEN 1 ELSE 0 END AS is_reduced,
                   CASE WHEN COALESCE(c.focused, p.focused) = 1
                        THEN COALESCE(c.shares, 0) - COALESCE(p.shares, 0) ELSE 0 END
                        AS focused_delta
              FROM _c c
              FULL OUTER JOIN _p p ON p.cusip = c.cusip AND p.cik = c.cik
             WHERE COALESCE(c.cik, p.cik) IN (SELECT cik FROM _both))
         GROUP BY cusip""", (cur_q, prior_q))
    for tag in ("_c", "_p", "_both"):
        con.execute(f"DROP TABLE IF EXISTS {tag}")
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
        return ingest_current(int(payload.get("top") or 400))
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
    if action == "top_firms":
        return top_firms(int(payload.get("limit") or 50))
    if action == "book":
        return firm_book(payload.get("cik"), payload.get("quarter"),
                         int(payload.get("limit") or 250))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
