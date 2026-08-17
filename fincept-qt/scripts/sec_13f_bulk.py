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
    holders  {"ticker"|"cusip", "limit"?, "min_weight"?}
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


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA synchronous=NORMAL")
    con.executescript("""
        CREATE TABLE IF NOT EXISTS quarters (
            quarter TEXT PRIMARY KEY,     -- e.g. 2026-03-31
            source  TEXT,
            filers  INTEGER,
            rows    INTEGER,
            ingested_at TEXT
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
        t = str(ticker).upper().strip()
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
            req = urllib.request.Request(
                OPENFIGI_URL, data=body, headers={"Content-Type": "application/json"})
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
                                (cusip, (pick.get("ticker") or "").upper(),
                                 pick.get("name") or "", "openfigi"))
                    resolved += 1
                else:
                    con.execute("INSERT OR REPLACE INTO cusip_ticker VALUES (?,?,?,?)",
                                (cusip, "", "", "openfigi-miss"))
                    failed += 1
            con.commit()
            if progress:
                progress(resolved + failed, len(todo))
            # Keyless OpenFIGI allows 25 requests a minute.
            time.sleep(2.5)

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
                    acc_cik[parts[head["ACCESSION_NUMBER"]]] = \
                        parts[head["CIK"]].strip().zfill(10)

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
        con.execute("INSERT OR REPLACE INTO quarters VALUES (?,?,?,?,datetime('now'))",
                    (quarter, url, filers, n_rows))
        con.commit()
        return {"quarter": quarter, "filers": filers, "rows": n_rows, "source": url,
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

def status():
    con = connect()
    try:
        rows = con.execute(
            "SELECT quarter, filers, rows, ingested_at FROM quarters ORDER BY quarter DESC"
        ).fetchall()
        return {"db": db_path(),
                "quarters": [{"quarter": r[0], "filers": r[1], "rows": r[2],
                              "ingested_at": r[3]} for r in rows]}
    finally:
        con.close()


# A "% of book" ranking is meaningless without a floor on the book. 10,647
# filers include single-position family accounts and shells, and one of those
# holding nothing but Apple scores 100% — ranking above Berkshire's 22%. These
# are stated, returned with the result, and adjustable, rather than hidden.
MIN_BOOK_VALUE = 50_000_000.0   # a book too small to be a firm's equity book
MIN_BOOK_POSITIONS = 5          # a book too narrow for a weight to mean anything


def holders(ticker=None, cusip=None, limit=60, quarter=None,
            min_book=MIN_BOOK_VALUE, min_positions=MIN_BOOK_POSITIONS):
    """Every filer holding this security, ranked by weight in their own book."""
    con = connect()
    try:
        name = ""
        if not cusip:
            res = resolve_ticker(ticker, con)
            if res.get("error"):
                return res
            cusip, name = res["cusip"], res.get("name", "")
        if not quarter:
            row = con.execute("SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 1"
                              ).fetchone()
            if not row:
                return {"error": "no 13F data ingested yet", "cusip": cusip}
            quarter = row[0]

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
            ORDER BY h.value DESC
        """, (prior_q or "", cusip, quarter,
              float(min_book), int(min_positions))).fetchall()

        out = []
        for (m, acc, issuer, cls, pc, value, shares, book_total, book_count, cik,
             prior_shares, filed_prior) in rows:
            weight = (value / book_total) if (book_total and not pc) else None
            rec = {
                "manager": m, "accession": acc, "cik": cik, "issuer": issuer, "class": cls,
                "put_call": pc, "is_derivative": bool(pc),
                "value": value, "shares": shares,
                "weight": weight, "book_total": book_total, "position_count": book_count,
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
        out.sort(key=lambda r: (r["weight"] or 0), reverse=True)
        stock = [r for r in out if not r["is_derivative"]]
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
                "min_book_value": min_book, "min_book_positions": min_positions,
                "holders": out[:int(limit)]}
    finally:
        con.close()


def manager_book(manager=None, accession=None, quarter=None, limit=200):
    """One filer's whole disclosed book from the local index."""
    con = connect()
    try:
        if not accession:
            if not quarter:
                row = con.execute(
                    "SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 1").fetchone()
                if not row:
                    return {"error": "no 13F data ingested yet"}
                quarter = row[0]
            row = con.execute(
                "SELECT accession, manager FROM filings "
                "WHERE quarter=? AND manager LIKE ? ORDER BY is_amendment ASC LIMIT 1",
                (quarter, f"%{manager}%")).fetchone()
            if not row:
                return {"error": f"no 13F filing found for '{manager}' in {quarter}"}
            accession, manager = row
        total = con.execute(
            "SELECT SUM(value), COUNT(*) FROM holdings WHERE accession=? AND put_call=''",
            (accession,)).fetchone()
        book_total = total[0] or 0.0
        rows = con.execute(
            "SELECT issuer, class, cusip, put_call, value, shares FROM holdings "
            "WHERE accession=? ORDER BY value DESC LIMIT ?", (accession, int(limit))).fetchall()
        positions = [{
            "issuer": r[0], "class": r[1], "cusip": r[2],
            "put_call": r[3], "is_derivative": bool(r[3]),
            "value": r[4], "shares": r[5],
            "weight": (r[4] / book_total) if (book_total and not r[3]) else None,
        } for r in rows]
        return {"manager": manager, "accession": accession, "quarter": quarter,
                "total_value": book_total, "position_count": total[1] or 0,
                "positions": positions}
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
    done, skipped = [], []
    for url in urls:
        if len(done) + len(have) >= int(quarters) + len(have) and len(done) >= int(quarters):
            break
        if len(done) >= int(quarters):
            break
        r = ingest(url, progress)
        if r.get("error"):
            return {"error": r["error"], "ingested": done}
        if r["quarter"] in have:
            skipped.append(r["quarter"])
        done.append({"quarter": r["quarter"], "filers": r["filers"], "rows": r["rows"]})
        have.add(r["quarter"])
    return {"ingested": done, "quarters_present": sorted(have, reverse=True)}


def quarter_pair(con):
    """The two most recent indexed quarters, newest first, or fewer."""
    return [r[0] for r in con.execute(
        "SELECT quarter FROM quarters ORDER BY quarter DESC LIMIT 2").fetchall()]


def holder_moves(ticker=None, cusip=None, limit=60,
                 min_book=None, min_positions=None):
    """Who built, trimmed, entered or exited this security last quarter.

    Filers are joined across quarters on CIK — the only stable identifier. The
    manager NAME is display only; firms rename, and matching on a name would put
    one filer's position under another's.

    A filer present in the newer quarter but absent from the older one is only
    reported as NEW when they actually filed that older quarter. A firm that did
    not file at all cannot be said to have opened a position, and calling it
    "new" would manufacture activity out of a missing filing.
    """
    con = connect()
    try:
        qs = quarter_pair(con)
        if len(qs) < 2:
            return {"error": "only one quarter is indexed — a diff needs two",
                    "quarters": qs}
        cur_q, prev_q = qs[0], qs[1]

        name = ""
        if not cusip:
            res = resolve_ticker(ticker, con)
            if res.get("error"):
                return res
            cusip, name = res["cusip"], res.get("name", "")

        mb = MIN_BOOK_VALUE if min_book is None else float(min_book)
        mp = MIN_BOOK_POSITIONS if min_positions is None else int(min_positions)

        def positions(q):
            rows = con.execute("""
                SELECT b.cik, b.manager, h.shares, h.value, b.stock_value, b.stock_count
                  FROM holdings h JOIN books b ON b.accession=h.accession
                 WHERE h.cusip=? AND h.quarter=? AND h.put_call=''
                   AND b.stock_value>=? AND b.stock_count>=?
            """, (cusip, q, mb, mp)).fetchall()
            return {r[0]: {"cik": r[0], "manager": r[1], "shares": r[2], "value": r[3],
                           "book_total": r[4], "position_count": r[5]} for r in rows if r[0]}

        cur, prev = positions(cur_q), positions(prev_q)
        filed_prev = {r[0] for r in con.execute(
            "SELECT cik FROM books WHERE quarter=?", (prev_q,)).fetchall()}

        moves = []
        for cik, p in cur.items():
            before = prev.get(cik)
            weight = (p["value"] / p["book_total"]) if p["book_total"] else None
            if before is None:
                moves.append({**p, "action": "new" if cik in filed_prev else "first seen",
                              "shares_delta": p["shares"], "pct_change": None,
                              "weight": weight,
                              "note": None if cik in filed_prev
                                      else "this filer has no prior-quarter filing indexed, "
                                           "so an opening position cannot be distinguished "
                                           "from a first appearance"})
                continue
            delta = p["shares"] - before["shares"]
            if before["shares"] > 0 and abs(delta) / before["shares"] < 0.01:
                continue  # drift, not a decision
            moves.append({**p, "action": "added" if delta > 0 else "trimmed",
                          "shares_delta": delta,
                          "pct_change": delta / before["shares"] if before["shares"] else None,
                          "weight": weight})
        for cik, before in prev.items():
            if cik in cur:
                continue
            # Only an exit if they filed this quarter. A filer who stopped
            # filing has not sold; we simply cannot see them.
            still_filing = con.execute(
                "SELECT 1 FROM books WHERE quarter=? AND cik=? LIMIT 1",
                (cur_q, cik)).fetchone()
            moves.append({"cik": cik, "manager": before["manager"], "shares": 0.0,
                          "value": 0.0, "book_total": before["book_total"],
                          "position_count": before["position_count"],
                          "action": "exited" if still_filing else "stopped filing",
                          "shares_delta": -before["shares"], "pct_change": -1.0,
                          "weight": 0.0,
                          "note": None if still_filing
                                  else "this filer has no current-quarter filing indexed, so "
                                       "an exit cannot be distinguished from a missing filing"})
        moves.sort(key=lambda m: abs(m.get("shares_delta") or 0), reverse=True)

        added = sum(1 for m in moves if m["action"] in ("added", "new"))
        cut = sum(1 for m in moves if m["action"] in ("trimmed", "exited"))
        net = sum((m.get("shares_delta") or 0) for m in moves
                  if m["action"] in ("added", "trimmed", "new", "exited"))
        return {"ticker": (ticker or "").upper(), "cusip": cusip, "company": name,
                "quarter": cur_q, "prior_quarter": prev_q,
                "holders_now": len(cur), "holders_prior": len(prev),
                "buyers": added, "sellers": cut, "net_share_change": net,
                "min_book_value": mb, "min_book_positions": mp,
                "moves": moves[:int(limit)]}
    finally:
        con.close()


def handle_action(action, payload):
    if action == "status":
        return status()
    if action == "datasets":
        return {"datasets": available_datasets()[:12]}
    if action == "ingest":
        return ingest(payload.get("url"))
    if action == "ingest_recent":
        return ingest_recent(int(payload.get("quarters") or 2))
    if action == "moves":
        return holder_moves(payload.get("ticker"), payload.get("cusip"),
                            payload.get("limit") or 60,
                            payload.get("min_book"), payload.get("min_positions"))
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
                       int(payload.get("min_positions") or MIN_BOOK_POSITIONS))
    if action == "book":
        return manager_book(payload.get("manager"), payload.get("accession"),
                            payload.get("quarter"), payload.get("limit") or 200)
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
