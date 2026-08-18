#!/usr/bin/env python3
"""Market-wide Form 4 — what insiders across every issuer did in the last few days.

The per-ticker view answers "what did insiders do at this company". This
answers the question that comes first: "where are insiders buying at all". You
arrive without a ticker and leave with one.

WHY THIS IS A DAILY SCAN AND NOT A BULK DOWNLOAD
------------------------------------------------
The SEC publishes quarterly Insider Transactions Data Sets, and using them here
would be a mistake. Form 4 is due within two business days of the trade, and
that promptness is the entire reason the data is worth reading. A quarterly file
lands up to three months after the fact, by which point the trade is history.
So the recent window is read from the EDGAR daily index, which is current the
same day.

WHAT IS RANKED, AND WHY IT IS NOT EVERY TRANSACTION
---------------------------------------------------
Around a thousand Form 4 rows are filed daily and most of them mean nothing.
The filters below are not tidying — each one removes a category that research
has repeatedly found carries no signal:

  * OPEN-MARKET PURCHASES ONLY (code P). Grants (A), option exercises (M), tax
    withholding (F) and gifts (G) are compensation mechanics, not decisions
    about price. An "insider buying" screen that counts grants shows relentless
    buying at every company in every market.
  * BUYS, NOT SELLS. Insiders sell to diversify, to pay tax, to buy a house, on
    a schedule set a year ahead. They buy for one reason. Sells are collected
    and shown, but they do not drive the ranking, because a sell leaderboard
    mostly ranks companies by how much stock their executives were granted.
  * CLUSTERS ARE FLAGGED. Several insiders at one issuer buying within days of
    each other is a materially stronger signal than one person buying, and it
    is the pattern this view exists to surface. Joint filers on a single form —
    a fund and its managing member — are ONE participant, not two: counting
    them separately would manufacture clusters out of single decisions.

Values are as filed: shares times the reported price per share. A Form 4 with no
price (some gifts, some plans) contributes shares but no value, and is never
valued at a price taken from somewhere else.
"""

import json
import os
import re
import sqlite3
import sys
import time
from datetime import date, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sec_ownership_data import (_get, parse_form4, UA,  # noqa: E402
                                owner_filing_dates, ROUTINE_MIN_YEARS)

BASE = "https://www.sec.gov/Archives"
# EDGAR asks for <=10 req/s across everything using this User-Agent, and other
# finterm scripts can be scanning at the same time, so this sits well under it.
REQ_PAUSE = 0.13


def db_path():
    root = os.environ.get("FINCEPT_DATA_DIR")
    if not root:
        root = os.path.join(os.path.expanduser("~"), ".local", "share",
                            "com.fincept.terminal")
    os.makedirs(root, exist_ok=True)
    return os.path.join(root, "form4.sqlite")


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    # A scan and a classify pass can be writing at the same time — the app
    # chains one after the other and a user can start another. Without this a
    # concurrent writer fails outright instead of waiting its turn.
    con.execute("PRAGMA busy_timeout=15000")
    con.execute("""CREATE TABLE IF NOT EXISTS tx (
        accession TEXT, filed TEXT, tx_date TEXT, symbol TEXT, issuer TEXT,
        insider TEXT, insider_cik TEXT, roles TEXT, code TEXT, direction TEXT,
        shares REAL, price REAL, value REAL, open_market INTEGER,
        derivative INTEGER, source_url TEXT, held_after REAL)""")
    # Added after the first stores were written; ALTER is the migration.
    cols = {r[1] for r in con.execute("PRAGMA table_info(tx)")}
    if "held_after" not in cols:
        con.execute("ALTER TABLE tx ADD COLUMN held_after REAL")
        # Rows stored before the column existed carry NULL, and scan() never
        # revisits an accession it has already seen. Drop those accessions from
        # the seen set so the next scan reads them again, rather than leaving a
        # permanently blank column that claims the filings were silent.
        con.execute("""DELETE FROM seen WHERE accession IN (
                         SELECT accession FROM tx GROUP BY accession
                          HAVING COUNT(held_after) = 0)""")
        con.execute("DELETE FROM tx WHERE held_after IS NULL")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_filed ON tx(filed)")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_symbol ON tx(symbol)")
    # One row per accession so a rescan is incremental rather than a refetch of
    # everything already read.
    con.execute("CREATE TABLE IF NOT EXISTS seen (accession TEXT PRIMARY KEY, filed TEXT)")
    # Routine-vs-opportunistic needs YEARS of an insider's filings, which a
    # few days of daily index cannot supply — it is fetched per owner from
    # EDGAR and cached here, because it changes about as often as a person's
    # trading habits do.
    con.execute("""CREATE TABLE IF NOT EXISTS owner_pattern (
        insider_cik TEXT PRIMARY KEY, pattern TEXT, years INTEGER,
        trades INTEGER, checked TEXT)""")
    con.commit()
    return con


def business_days(n):
    out, d = [], date.today()
    while len(out) < n:
        if d.weekday() < 5:
            out.append(d)
        d -= timedelta(days=1)
    return out


def day_index(d):
    """Form 4 accessions filed on one day, with the path to the full submission.

    A Form 4 is indexed under both the issuer and every reporting owner, so the
    same accession appears several times; the dict collapses them.
    """
    q = (d.month - 1) // 3 + 1
    url = f"{BASE}/edgar/daily-index/{d.year}/QTR{q}/form.{d.strftime('%Y%m%d')}.idx"
    r = _get(url)
    if not r or not getattr(r, "ok", False):
        return {}
    out = {}
    for line in r.text.splitlines():
        if not line.startswith("4 "):
            continue
        m = re.search(r"(edgar/data/\d+/(\d{10}-\d{2}-\d{6})\.txt)", line)
        if m:
            out[m.group(2)] = m.group(1)
    return out


# Some filers put a literal placeholder in issuerTradingSymbol rather than
# leaving it empty. Treated as a ticker it becomes a row labelled "NONE" that
# aggregates unrelated companies and offers a drill-through to a symbol that
# does not exist.
_NOT_A_SYMBOL = {"NONE", "N/A", "NA", "N-A", "NULL", "-", "--", "[NONE]"}


def clean_symbol(sym):
    s = (sym or "").strip().upper()
    return "" if s in _NOT_A_SYMBOL else s


def _extract_xml(text):
    """The ownership XML out of a complete submission text file."""
    i = text.find("<ownershipDocument")
    if i < 0:
        return None
    j = text.find("</ownershipDocument>", i)
    if j < 0:
        return None
    return text[i:j + len("</ownershipDocument>")].encode("utf-8", "replace")


def scan(days=3, limit=None):
    """Read the last @days business days of Form 4 filings into the local store."""
    con = connect()
    try:
        fetched = parsed = skipped = 0
        for d in business_days(int(days)):
            filed = d.isoformat()
            idx = day_index(d)
            if not idx:
                continue
            have = {r[0] for r in con.execute(
                "SELECT accession FROM seen WHERE filed=?", (filed,)).fetchall()}
            todo = [(a, p) for a, p in idx.items() if a not in have]
            skipped += len(idx) - len(todo)
            if limit:
                todo = todo[:int(limit)]
            for acc, path in todo:
                r = _get(f"{BASE}/{path}")
                time.sleep(REQ_PAUSE)
                fetched += 1
                con.execute("INSERT OR REPLACE INTO seen VALUES (?,?)", (acc, filed))
                if not r or not getattr(r, "ok", False):
                    continue
                xml = _extract_xml(r.text)
                if not xml:
                    continue
                for t in parse_form4(xml, f"{BASE}/{path}"):
                    parsed += 1
                    con.execute(
                        "INSERT INTO tx VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (acc, filed, t.get("date", ""), clean_symbol(t.get("symbol")),
                         t.get("issuer", ""), t.get("insider", ""),
                         t.get("insider_cik", ""), ", ".join(t.get("roles") or []),
                         t.get("code", ""), t.get("direction", ""),
                         t.get("shares"), t.get("price"), t.get("value"),
                         1 if t.get("open_market") else 0,
                         1 if t.get("derivative") else 0, t.get("source_url", ""),
                         t.get("shares_held_after")))
            con.commit()
        return {"fetched": fetched, "transactions": parsed,
                "already_had": skipped, "days": int(days)}
    finally:
        con.close()


def status():
    con = connect()
    try:
        n = con.execute("SELECT COUNT(*) FROM seen").fetchone()[0]
        rows = con.execute("SELECT COUNT(*) FROM tx").fetchone()[0]
        span = con.execute("SELECT MIN(filed), MAX(filed) FROM seen").fetchone()
        return {"filings": n, "transactions": rows,
                "first_filed": span[0], "last_filed": span[1],
                "ready": n > 0}
    finally:
        con.close()


def classify(days=10, limit=60):
    """Label the insiders behind recent open-market buys routine or opportunistic.

    Cohen, Malloy and Pomorski found the distinction is most of the signal in
    Form 4: an insider who trades the same month every year is following a plan
    and predicts nothing, while the same trade from someone with no such pattern
    does. The test needs several years of that person's filings, so it is a
    per-owner fetch from EDGAR — done for the insiders actually on screen, and
    cached, rather than for all ten thousand filers.

    An insider without enough history is left UNCLASSIFIED. Calling them
    opportunistic because no pattern was visible in two years of filings would
    put the strongest label in the dataset on the weakest evidence.
    """
    con = connect()
    try:
        cutoff = (date.today() - timedelta(days=int(days) * 2)).isoformat()
        rows = con.execute("""
            SELECT DISTINCT t.insider_cik
              FROM tx t
              LEFT JOIN owner_pattern p ON p.insider_cik = t.insider_cik
             WHERE t.open_market=1 AND t.derivative=0 AND t.code='P'
               AND t.direction='acquired' AND t.filed>=? AND t.insider_cik<>''
               AND p.insider_cik IS NULL
             LIMIT ?
        """, (cutoff, int(limit))).fetchall()
        done = 0
        for (cik,) in rows:
            dates = owner_filing_dates(cik) or []
            time.sleep(REQ_PAUSE)
            years, months = set(), {}
            for d in dates:
                try:
                    y, m = int(d[0:4]), int(d[5:7])
                except (ValueError, IndexError):
                    continue
                years.add(y)
                months.setdefault(m, set()).add(y)
            span = len(years)
            if span < ROUTINE_MIN_YEARS:
                pattern = "unclassified"
            elif any(len(ys) >= ROUTINE_MIN_YEARS for ys in months.values()):
                pattern = "routine"
            else:
                pattern = "opportunistic"
            con.execute("INSERT OR REPLACE INTO owner_pattern VALUES (?,?,?,?,?)",
                        (cik, pattern, span, len(dates), date.today().isoformat()))
            done += 1
        con.commit()
        remaining = con.execute("""
            SELECT COUNT(DISTINCT t.insider_cik)
              FROM tx t
              LEFT JOIN owner_pattern p ON p.insider_cik = t.insider_cik
             WHERE t.open_market=1 AND t.derivative=0 AND t.code='P'
               AND t.direction='acquired' AND t.filed>=? AND t.insider_cik<>''
               AND p.insider_cik IS NULL
        """, (cutoff,)).fetchone()[0]
        return {"classified": done, "remaining_unknown": remaining}
    finally:
        con.close()


def leaders(days=5, limit=40, min_value=0.0, direction="buy"):
    """Issuers ranked by open-market insider conviction in the window.

    Ranked on VALUE, with the distinct-insider count carried alongside rather
    than folded in: one executive buying $5m and five buying $1m each are
    different events, and which one matters is the reader's call, not a
    weighting hidden in a score.
    """
    con = connect()
    try:
        cutoff = (date.today() - timedelta(days=int(days) * 2)).isoformat()
        want = "acquired" if direction == "buy" else "disposed"
        # Code P only: see the module docstring on why grants and exercises are
        # excluded rather than merely de-emphasised.
        rows = con.execute("""
            SELECT t.symbol, t.issuer,
                   SUM(COALESCE(t.value,0)) AS v,
                   SUM(COALESCE(t.shares,0)) AS sh,
                   COUNT(DISTINCT t.insider_cik) AS people,
                   COUNT(*) AS trades,
                   MAX(t.tx_date) AS latest,
                   GROUP_CONCAT(DISTINCT t.roles),
                   COUNT(DISTINCT CASE WHEN p.pattern='opportunistic'
                                       THEN t.insider_cik END),
                   COUNT(DISTINCT CASE WHEN p.pattern='routine'
                                       THEN t.insider_cik END),
                   -- How much a buy grew the insider's own holding. A $50k
                   -- purchase by a director already holding $50m is noise; the
                   -- same purchase from someone holding $200k is not, and only
                   -- this ratio can tell them apart. Purchases ONLY: after a
                   -- sale held_after is what is left, and the same arithmetic
                   -- yields a number that means nothing.
                   MAX(CASE WHEN t.direction='acquired' AND t.held_after > t.shares
                                 AND t.shares > 0
                            THEN t.shares / (t.held_after - t.shares) END)
              FROM tx t
              LEFT JOIN owner_pattern p ON p.insider_cik = t.insider_cik
             WHERE open_market=1 AND derivative=0 AND code='P'
               AND direction=? AND filed>=?
             GROUP BY COALESCE(NULLIF(t.symbol,''), t.issuer)
            HAVING v >= ?
             ORDER BY v DESC LIMIT ?
        """, (want, cutoff, float(min_value), int(limit))).fetchall()
        out = []
        for (sym, issuer, v, sh, people, trades, latest, roles,
             opportunistic, routine, stake) in rows:
            out.append({
                "symbol": sym or "", "issuer": issuer or "",
                "value": v or 0.0, "shares": sh or 0.0,
                "insiders": people or 0, "trades": trades or 0,
                "latest": latest or "",
                # The pattern the view exists to surface, stated as a fact about
                # the filings rather than as a rating.
                "cluster": (people or 0) >= 2,
                "roles": [r for r in (roles or "").split(",") if r.strip()][:4],
                "opportunistic": opportunistic or 0,
                "routine": routine or 0,
                # None when no filing reported holdings after the trade.
                "stake_increase": stake,
            })
        return {"leaders": out, "days": int(days), "direction": direction}
    finally:
        con.close()


def handle_action(action, p):
    if action == "scan":
        return scan(p.get("days") or 3, p.get("limit"))
    if action == "status":
        return status()
    if action == "leaders":
        return leaders(p.get("days") or 5, p.get("limit") or 40,
                       p.get("min_value") or 0.0, p.get("direction") or "buy")
    if action == "classify":
        return classify(p.get("days") or 10, p.get("limit") or 60)
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, payload), indent=2, default=str))
