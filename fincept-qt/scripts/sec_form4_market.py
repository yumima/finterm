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
from sec_ownership_data import _get, parse_form4  # noqa: E402

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
    # Two flags the evidence turns on: a 10% owner's buys rank inverted, and a
    # 10b5-1 plan trade was decided months before it printed. NULL on rows
    # read before the columns existed — unknown, not false.
    if "ten_pct" not in cols:
        con.execute("ALTER TABLE tx ADD COLUMN ten_pct INTEGER")
    if "plan" not in cols:
        con.execute("ALTER TABLE tx ADD COLUMN plan INTEGER")
        # Deliberately NOT deleting the rows that predate this column. They
        # carry NULL and show a blank cell, which is a small cost; deleting
        # them to force a re-read would destroy the store, because scan() only
        # ever walks back from today and cannot reach filings older than its
        # window. Weeks of accumulated filings would be unrecoverable.
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_filed ON tx(filed)")
    con.execute("CREATE INDEX IF NOT EXISTS ix_tx_symbol ON tx(symbol)")
    # One row per accession so a rescan is incremental rather than a refetch of
    # everything already read.
    con.execute("CREATE TABLE IF NOT EXISTS seen (accession TEXT PRIMARY KEY, filed TEXT)")
    # Days already walked, including ones the index had nothing for. Without
    # this a market holiday is never recorded in `seen` (no accessions to
    # record), so every scan would re-walk it and spend its budget on a day
    # that will never have filings.
    con.execute("CREATE TABLE IF NOT EXISTS scanned_days (filed TEXT PRIMARY KEY)")
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


def scan(days=3, limit=None, max_new_days=None):
    """Read Form 4 filings into the local store, oldest-unread day first.

    A lock held by another writer — a second scan, an orphaned one — is
    reported, not raised: what was read before it is committed, and the
    caller retries later rather than treating the pass as a crash.

    @days is the window the reader is looking at; @max_new_days bounds how much
    of it one press will fetch, because a day is around 500 submission fetches.
    Days already present in the store are skipped, so pressing again genuinely
    reaches further back instead of re-walking the same few days — which is what
    a "last 30 days" selector implies is reachable.
    """
    con = connect()
    parent_at_start = os.getppid()
    try:
        fetched = parsed = skipped = 0
        # A day with any filing recorded has been walked; the index for a given
        # day never changes once published.
        done_days = {r[0] for r in con.execute(
            "SELECT filed FROM scanned_days").fetchall()}
        done_days |= {r[0] for r in con.execute(
            "SELECT DISTINCT filed FROM seen").fetchall()}
        wanted = [d for d in business_days(int(days)) if d.isoformat() not in done_days]
        budget = int(max_new_days) if max_new_days else len(wanted)
        days_left = budget
        for d in wanted:
            if days_left <= 0:
                break
            # An orphan keeps the write lock for the rest of its walk. If the
            # process that started this pass is gone — the parent pid has
            # changed, to init or to a subreaper — stop at the day boundary:
            # everything read so far is committed and the next pass resumes.
            if os.getppid() != parent_at_start:
                break
            filed = d.isoformat()
            idx = day_index(d)
            if not idx:
                # No index for this date — a holiday, or today before EDGAR
                # publishes. Record it only when it is in the past: today's
                # index will appear later and must not be written off.
                if d < date.today():
                    con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)",
                                (filed,))
                    con.commit()
                # Not counted against the budget: nothing was read. Today's
                # index in particular does not exist until EDGAR publishes, and
                # spending the day's allowance on it would stall the walk.
                continue
            days_left -= 1
            have = {r[0] for r in con.execute(
                "SELECT accession FROM seen WHERE filed=?", (filed,)).fetchall()}
            todo = [(a, p) for a, p in idx.items() if a not in have]
            skipped += len(idx) - len(todo)
            if limit:
                todo = todo[:int(limit)]
            for n, (acc, path) in enumerate(todo, 1):
                r = _get(f"{BASE}/{path}")
                time.sleep(REQ_PAUSE)
                fetched += 1
                # Commit in small batches. One transaction per day held the
                # write lock for minutes at a time, and any other process
                # touching the store — the scan tab's own query, a second
                # scan — waited out its busy timeout and died with
                # "database is locked". Twenty-five filings is a few seconds.
                if n % 25 == 0:
                    con.commit()
                con.execute("INSERT OR REPLACE INTO seen VALUES (?,?)", (acc, filed))
                if not r or not getattr(r, "ok", False):
                    continue
                xml = _extract_xml(r.text)
                if not xml:
                    continue
                for t in parse_form4(xml, f"{BASE}/{path}"):
                    parsed += 1
                    plan = t.get("plan_10b5_1")
                    con.execute(
                        "INSERT INTO tx (accession, filed, tx_date, symbol, issuer, insider, "
                        "insider_cik, roles, code, direction, shares, price, value, "
                        "open_market, derivative, source_url, held_after, ten_pct, plan) "
                        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                        (acc, filed, t.get("date", ""), clean_symbol(t.get("symbol")),
                         t.get("issuer", ""), t.get("insider", ""),
                         t.get("insider_cik", ""), ", ".join(t.get("roles") or []),
                         t.get("code", ""), t.get("direction", ""),
                         t.get("shares"), t.get("price"), t.get("value"),
                         1 if t.get("open_market") else 0,
                         1 if t.get("derivative") else 0, t.get("source_url", ""),
                         t.get("shares_held_after"),
                         1 if t.get("ten_percent_owner") else 0,
                         None if plan is None else (1 if plan else 0)))
            con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (filed,))
            con.commit()
        remaining_days = max(0, len(wanted) - budget)
        return {"fetched": fetched, "transactions": parsed,
                "already_had": skipped, "days": int(days),
                "days_read": budget - days_left,
                # >0 means the window asked for is not fully covered yet and
                # pressing again will fetch more.
                "days_remaining": remaining_days}
    except sqlite3.OperationalError as e:
        if "locked" not in str(e).lower():
            raise
        con.rollback()
        return {"fetched": fetched, "transactions": parsed, "days": int(days),
                "days_read": 0, "days_remaining": max(1, len(wanted)),
                "error": "the Form 4 store is locked by another scan — will retry"}
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


def recent(days=30, min_insiders=1, min_value=25_000.0, exclude_ten_pct=True,
           exclude_plan=True, limit=200, direction="buy", symbols=None):
    """Issuers with open-market insider purchases in the window, one row per
    issuer, the cluster stated as a count.

    The shape is OpenInsider's: a cluster is not a constant but a group-by with
    a minimum insider count the reader sets. The two exclusions are the
    evidence-backed ones — 10% owners (whose buy ranking inverts) and 10b5-1
    plan trades (decided months before they print) — and both are switches,
    not silent filters. Sells are available for reading and never ranked.
    """
    con = connect()
    try:
        cutoff = (date.today() - timedelta(days=int(days))).isoformat()
        buying = direction != "sell"
        want_code = "P" if buying else "S"
        want = "acquired" if buying else "disposed"
        conds = ["t.open_market=1", "t.derivative=0", "t.code=?", "t.direction=?",
                 "t.filed>=?"]
        args = [want_code, want, cutoff]
        if exclude_ten_pct:
            conds.append("COALESCE(t.ten_pct,0)=0")
        if exclude_plan:
            conds.append("COALESCE(t.plan,0)=0")
        if symbols:
            syms = sorted({str(x).upper() for x in symbols if x})
            conds.append("t.symbol IN (%s)" % ",".join("?" * len(syms)))
            args.extend(syms)
        rows = con.execute(f"""
            SELECT COALESCE(NULLIF(t.symbol,''), t.issuer) AS key,
                   MAX(t.symbol), MAX(t.issuer),
                   COUNT(DISTINCT COALESCE(NULLIF(t.insider_cik,''), t.insider)) AS people,
                   COUNT(*) AS trades,
                   SUM(COALESCE(t.value,0)) AS v,
                   SUM(COALESCE(t.shares,0)) AS sh,
                   SUM(CASE WHEN t.value IS NOT NULL THEN t.shares ELSE 0 END) AS priced_sh,
                   MIN(t.tx_date), MAX(t.tx_date), MAX(t.filed),
                   GROUP_CONCAT(DISTINCT t.roles),
                   -- How much the buy grew the buyer's own holding: shares
                   -- bought over shares held before. Purchases only — after a
                   -- sale the same arithmetic means nothing.
                   MAX(CASE WHEN t.held_after > t.shares AND t.shares > 0
                            THEN t.shares / (t.held_after - t.shares) END),
                   MAX(COALESCE(t.plan,0)), MAX(COALESCE(t.ten_pct,0)),
                   SUM(CASE WHEN t.plan IS NULL THEN 1 ELSE 0 END)
              FROM tx t
             WHERE {" AND ".join(conds)}
             GROUP BY key
            HAVING people >= ? AND v >= ?
             ORDER BY v DESC LIMIT ?
        """, args + [int(min_insiders), float(min_value), int(limit)]).fetchall()
        out = []
        for (key, sym, issuer, people, trades, v, sh, priced_sh, first, last, filed,
             roles, stake, any_plan, any_ten, plan_unknown) in rows:
            roles_list = []
            for r in (roles or "").split(","):
                r = r.strip()
                if r and r not in roles_list:
                    roles_list.append(r)
            out.append({
                "symbol": sym or "", "issuer": issuer or "",
                "insiders": people or 0, "trades": trades or 0,
                "value": v or 0.0, "shares": sh or 0.0,
                "avg_price": (v / priced_sh) if priced_sh else None,
                "first_trade": first or "", "last_trade": last or "", "last_filed": filed or "",
                "roles": roles_list[:5],
                "stake_increase": stake,
                "any_plan": bool(any_plan), "any_ten_pct": bool(any_ten),
                "plan_unknown": int(plan_unknown or 0),
            })
        covered = con.execute(
            "SELECT COUNT(*), MAX(filed) FROM scanned_days WHERE filed>=?", (cutoff,)).fetchone()
        wanted = sum(1 for d in business_days(int(days) * 7 // 5 + 3)
                     if d.isoformat() >= cutoff)
        return {"rows": out, "days": int(days), "direction": direction,
                "min_insiders": int(min_insiders), "min_value": float(min_value),
                "exclude_ten_pct": bool(exclude_ten_pct), "exclude_plan": bool(exclude_plan),
                "days_scanned": covered[0] or 0, "days_wanted": wanted,
                "last_scanned": covered[1] or ""}
    finally:
        con.close()


def handle_action(action, p):
    if action == "scan":
        return scan(p.get("days") or 3, p.get("limit"), p.get("max_new_days"))
    if action == "status":
        return status()
    if action == "recent":
        return recent(p.get("days") or 30, p.get("min_insiders") or 1,
                      p.get("min_value") if p.get("min_value") is not None else 25_000.0,
                      p.get("exclude_ten_pct", True), p.get("exclude_plan", True),
                      p.get("limit") or 200, p.get("direction") or "buy", p.get("symbols"))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "status"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, payload), indent=2, default=str))
