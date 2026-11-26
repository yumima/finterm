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
import time
import sys
from datetime import date, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sec_http  # noqa: E402  (the one SEC client)
from sec_ownership_data import parse_form4  # noqa: E402
from finterm_paths import market_data_path

BASE = "https://www.sec.gov/Archives"


def db_path():
    return market_data_path("form4.sqlite")


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


def coverage(days, con=None):
    """Which business days of the trailing @days-calendar-day window have been
    read to the end. Today is not expected: its index may not exist yet.

    Returns (read, expected, missing) — the one definition both the app's
    coverage note and the weekly brief's completeness gate use.
    """
    today = date.today()
    cutoff = today - timedelta(days=int(days))
    expected = [d.isoformat() for d in business_days(int(days) + 3) if cutoff <= d < today]
    own = con is None
    con = con or connect()
    try:
        done = {r[0] for r in con.execute(
            "SELECT filed FROM scanned_days WHERE filed >= ?", (cutoff.isoformat(),)).fetchall()}
    finally:
        if own:
            con.close()
    missing = sorted(d for d in expected if d not in done)
    return len(expected) - len(missing), len(expected), missing


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

    {} means EDGAR has no index for the day (404: a market holiday, or today
    before it publishes). None means the index could not be read — refused,
    throttled, offline — and the day must NOT be recorded as walked: a refusal
    written off as a holiday is never fetched again.
    """
    q = (d.month - 1) // 3 + 1
    url = f"{BASE}/edgar/daily-index/{d.year}/QTR{q}/form.{d.strftime('%Y%m%d')}.idx"
    r, status = sec_http.get(url)
    if r is None:
        return {} if status == 404 else None
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


EDGAR_GAP = 0.2   # at most 5 requests a second (SEC allows 10 in all)
READERS = 4       # at once; the gap still spaces their starts


def _read_all(paths):
    """(accession, path) -> (accession, path, answer, status), in order, a few
    read at once. A day is walked over them one by one, so what is committed
    when is as it was; a walk that stops early leaves the rest unread."""
    import itertools
    from concurrent.futures import ThreadPoolExecutor

    fetch = lambda p: sec_http.get(f"{BASE}/{p[1]}", gap=EDGAR_GAP)
    with ThreadPoolExecutor(max_workers=READERS) as pool:
        waiting, rest = [], iter(paths)
        for item in itertools.islice(rest, READERS * 2):
            waiting.append((item, pool.submit(fetch, item)))
        while waiting:
            item, job = waiting.pop(0)
            for nxt in itertools.islice(rest, 1):
                waiting.append((nxt, pool.submit(fetch, nxt)))
            yield (*item, *job.result())


class _ScanLock:
    """One scan at a time, across the app, a brief's build and the read-ahead:
    two would read the same filings and count their trades twice."""

    def __init__(self, wait_s=0, waiting=None):
        self.wait_s, self.waiting, self.f = wait_s, waiting, None

    def __enter__(self):
        import file_lock

        self.f = file_lock.open_lock_file(f"{db_path()}.scan.lock")
        until, told = time.monotonic() + self.wait_s, False
        while True:
            if file_lock.try_lock(self.f):
                return True
            if time.monotonic() >= until:
                return False
            if self.waiting and not told:
                self.waiting()
                told = True
            time.sleep(1)

    def __exit__(self, *exc):
        if self.f:
            self.f.close()  # lets go of the lock


LOCKED = "the Form 4 store is locked by another scan — will retry"


def scan(days=3, limit=None, max_new_days=None, progress=None, wait_s=0, waiting=None, deadline_s=None):
    """_scan, one at a time (_ScanLock). Another scan running: this one waits
    @wait_s for it (@waiting() is called once), else answers as for a locked
    store."""
    with _ScanLock(wait_s, waiting) as mine:
        if not mine:
            return {"fetched": 0, "transactions": 0, "days": int(days), "days_read": 0, "days_remaining": 1,
                    "error": LOCKED}
        return _scan(days, limit, max_new_days, progress, deadline_s)


def _scan(days=3, limit=None, max_new_days=None, progress=None, deadline_s=None):
    """Read Form 4 filings into the local store, oldest-unread day first.

    A lock held by another writer — a second scan, an orphaned one — is
    reported, not raised: what was read before it is committed, and the
    caller retries later rather than treating the pass as a crash.

    @days is the window the reader is looking at; @max_new_days bounds how much
    of it one press will fetch, because a day is around 500 submission fetches.
    Days already present in the store are skipped, so pressing again genuinely
    reaches further back instead of re-walking the same few days — which is what
    a "last 30 days" selector implies is reachable.

    @progress(day, done, total, days_left): called as a day's filings are read.
    @deadline_s: stop after that long, mid-day if need be: the day stays
    unrecorded and the next pass resumes it.
    """
    until = time.monotonic() + deadline_s if deadline_s else None
    con = connect()
    parent_at_start = os.getppid()
    try:
        fetched = parsed = skipped = failed = 0
        failed_days = []
        # Only a day walked to its end is done. A day with some filings in
        # `seen` but no `scanned_days` row was cut off part-way (the app quit
        # mid-walk) and is resumed: its already-read filings are skipped, so
        # the cost is one index fetch. Counting such days as done left them
        # partial forever — 199 of a day's ~500 filings, never revisited.
        done_days = {r[0] for r in con.execute(
            "SELECT filed FROM scanned_days").fetchall()}
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
            if idx is None:
                # Unreadable, not empty: leave the day unrecorded so the next
                # pass retries it, and stop — the other days' indexes sit on
                # the same server that just refused or failed.
                failed_days.append(filed)
                break
            if not idx:
                # No index for this date — a holiday, or today before EDGAR
                # publishes. Record it only when it is in the past: today's
                # index will appear later and must not be written off.
                # A few days back: the newest index can be missing only
                # because EDGAR has not published it yet.
                if (date.today() - d).days >= 3:
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
            # A pass capped by @limit read only part of the day: it is not done.
            truncated = bool(limit) and len(todo) > int(limit)
            if truncated:
                todo = todo[:int(limit)]
            day_failed = 0
            for n, (acc, path, r, status) in enumerate(_read_all(todo), 1):
                if progress:
                    progress(d, n, len(todo), days_left)
                if until and time.monotonic() > until:
                    truncated = True
                    days_left = 0
                    break
                fetched += 1
                # Commit in small batches. One transaction per day held the
                # write lock for minutes at a time, and any other process
                # touching the store — the scan tab's own query, a second
                # scan — waited out its busy timeout and died with
                # "database is locked". Twenty-five filings is a few seconds.
                if n % 25 == 0:
                    con.commit()
                if r is None and status == 404:
                    # Gone for good (withdrawn or removed): nothing to read,
                    # and retrying would hold the day open forever.
                    con.execute("INSERT OR REPLACE INTO seen VALUES (?,?)", (acc, filed))
                    continue
                if r is None:
                    # Refused, throttled, offline: not marked seen, so the
                    # next pass fetches it again rather than losing it.
                    day_failed += 1
                    continue
                # In the transaction of its trades: a filing another scan has
                # stored meanwhile is not counted again.
                if con.execute("INSERT OR IGNORE INTO seen VALUES (?,?)", (acc, filed)).rowcount == 0:
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
            if day_failed:
                failed += day_failed
                failed_days.append(filed)
            elif not truncated:
                con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (filed,))
            con.commit()
        # Whatever is still unrecorded — not reached, failed, or cut short by
        # @limit — is still to do. Counted from the store, not inferred.
        recorded = {r[0] for r in con.execute("SELECT filed FROM scanned_days").fetchall()}
        remaining_days = sum(1 for d in wanted if d.isoformat() not in recorded and d < date.today())
        out = {"fetched": fetched, "transactions": parsed,
                "already_had": skipped, "days": int(days),
                "days_read": budget - days_left,
                # >0 means the window asked for is not fully covered yet and
                # pressing again will fetch more.
                "days_remaining": remaining_days,
                # What could not be read this pass (refused, throttled,
                # offline) — retried next pass, never written off.
                "failed_days": sorted(set(failed_days)), "failed_filings": failed}
        if failed_days:
            # The caller's soft-error path: pause and retry later rather than
            # hammering a server that is refusing or throttling us.
            n = len(set(failed_days))
            out["error"] = (f"EDGAR did not answer for {n} day{'s' if n != 1 else ''} "
                            f"({failed} filing{'s' if failed != 1 else ''} unread) — they are retried, not skipped")
        return out
    except sqlite3.OperationalError as e:
        if "locked" not in str(e).lower():
            raise
        con.rollback()
        return {"fetched": fetched, "transactions": parsed, "days": int(days),
                "days_read": 0, "days_remaining": max(1, len(wanted)),
                "error": LOCKED}
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
        last = con.execute(
            "SELECT MAX(filed) FROM scanned_days WHERE filed>=?", (cutoff,)).fetchone()
        read, wanted, _missing = coverage(days, con)
        return {"rows": out, "days": int(days), "direction": direction,
                "min_insiders": int(min_insiders), "min_value": float(min_value),
                "exclude_ten_pct": bool(exclude_ten_pct), "exclude_plan": bool(exclude_plan),
                "days_scanned": read, "days_wanted": wanted,
                "last_scanned": last[0] or ""}
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
