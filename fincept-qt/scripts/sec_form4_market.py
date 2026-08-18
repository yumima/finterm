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
from sec_ownership_data import _get, parse_form4, UA  # noqa: E402

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
    con.execute("""CREATE TABLE IF NOT EXISTS tx (
        accession TEXT, filed TEXT, tx_date TEXT, symbol TEXT, issuer TEXT,
        insider TEXT, insider_cik TEXT, roles TEXT, code TEXT, direction TEXT,
        shares REAL, price REAL, value REAL, open_market INTEGER,
        derivative INTEGER, source_url TEXT)""")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_filed ON tx(filed)")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_symbol ON tx(symbol)")
    # One row per accession so a rescan is incremental rather than a refetch of
    # everything already read.
    con.execute("CREATE TABLE IF NOT EXISTS seen (accession TEXT PRIMARY KEY, filed TEXT)")
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
                        "INSERT INTO tx VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (acc, filed, t.get("date", ""), clean_symbol(t.get("symbol")),
                         t.get("issuer", ""), t.get("insider", ""),
                         t.get("insider_cik", ""), ", ".join(t.get("roles") or []),
                         t.get("code", ""), t.get("direction", ""),
                         t.get("shares"), t.get("price"), t.get("value"),
                         1 if t.get("open_market") else 0,
                         1 if t.get("derivative") else 0, t.get("source_url", "")))
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
            SELECT symbol, issuer,
                   SUM(COALESCE(value,0)) AS v,
                   SUM(COALESCE(shares,0)) AS sh,
                   COUNT(DISTINCT insider_cik) AS people,
                   COUNT(*) AS trades,
                   MAX(tx_date) AS latest,
                   GROUP_CONCAT(DISTINCT roles)
              FROM tx
             WHERE open_market=1 AND derivative=0 AND code='P'
               AND direction=? AND filed>=?
             GROUP BY COALESCE(NULLIF(symbol,''), issuer)
            HAVING v >= ?
             ORDER BY v DESC LIMIT ?
        """, (want, cutoff, float(min_value), int(limit))).fetchall()
        out = []
        for sym, issuer, v, sh, people, trades, latest, roles in rows:
            out.append({
                "symbol": sym or "", "issuer": issuer or "",
                "value": v or 0.0, "shares": sh or 0.0,
                "insiders": people or 0, "trades": trades or 0,
                "latest": latest or "",
                # The pattern the view exists to surface, stated as a fact about
                # the filings rather than as a rating.
                "cluster": (people or 0) >= 2,
                "roles": [r for r in (roles or "").split(",") if r.strip()][:4],
            })
        return {"leaders": out, "days": int(days), "direction": direction}
    finally:
        con.close()


def detail(symbol=None, issuer=None, days=30):
    """Every open-market insider transaction behind one issuer's ranking."""
    con = connect()
    try:
        cutoff = (date.today() - timedelta(days=int(days) * 2)).isoformat()
        key, val = ("symbol", symbol) if symbol else ("issuer", issuer)
        rows = con.execute(f"""
            SELECT tx_date, insider, roles, code, direction, shares, price, value,
                   source_url, issuer, symbol
              FROM tx WHERE {key}=? AND filed>=? AND derivative=0
             ORDER BY tx_date DESC, ABS(COALESCE(value,0)) DESC LIMIT 200
        """, (val, cutoff)).fetchall()
        return {"symbol": symbol or "", "issuer": issuer or "",
                "transactions": [
                    {"date": r[0], "insider": r[1], "roles": r[2], "code": r[3],
                     "direction": r[4], "shares": r[5], "price": r[6],
                     "value": r[7], "url": r[8], "issuer": r[9], "symbol": r[10]}
                    for r in rows]}
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
    if action == "detail":
        return detail(p.get("symbol"), p.get("issuer"), p.get("days") or 30)
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, payload), indent=2, default=str))
