#!/usr/bin/env python3
"""Market-wide Form 4 store — the cluster group-by and its two exclusions.

Runs the SHIPPED functions from sec_form4_market against a throwaway store.
No network: rows are inserted the way scan() inserts them. What is pinned:

  • A store written before the ten_pct / plan columns existed migrates in
    place, with NULL (unknown) in the old rows — never dropped, never 0.
  • recent() groups by issuer, counts DISTINCT buyers, and ranks by value.
  • Buys by 10% owners and 10b5-1 plan trades are excluded by default and
    come back when the switch is off; rows with an UNKNOWN plan flag are
    kept either way.
  • Sells never reach the buy ranking; grants never do.
  • Joint filers on one filing (same insider name, no CIK) count once.
  • A day cut off part-way (filings in `seen`, no `scanned_days` row) is
    resumed by the next scan — only its unread filings are fetched — and
    then counts as covered; a completed day is not walked again.
"""

import os
import sqlite3
import sys
import tempfile
from datetime import date, timedelta

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def main():
    tmp = tempfile.mkdtemp(prefix="form4m")
    os.environ["FINTERM_DATA_DIR"] = tmp

    # ── an old-schema store, as shipped before the flags existed ───────────
    # Where the store lives (market_data/ since the 2026-10-14 layout move) —
    # planted at the old root, the migration under test never saw it.
    import finterm_paths
    old = sqlite3.connect(finterm_paths.market_data_path("form4.sqlite"))
    old.execute("""CREATE TABLE tx (
        accession TEXT, filed TEXT, tx_date TEXT, symbol TEXT, issuer TEXT,
        insider TEXT, insider_cik TEXT, roles TEXT, code TEXT, direction TEXT,
        shares REAL, price REAL, value REAL, open_market INTEGER,
        derivative INTEGER, source_url TEXT, held_after REAL)""")
    today = date.today()
    d = lambda n: (today - timedelta(days=n)).isoformat()
    old.execute("INSERT INTO tx VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                ("acc-old", d(3), d(5), "OLD", "Old Filer Inc", "Legacy Buyer", "0001", "Director",
                 "P", "acquired", 1000.0, 10.0, 10000.0, 1, 0, "u", 5000.0))
    old.commit()
    old.close()

    import sec_form4_market as m
    con = m.connect()
    cols = {r[1] for r in con.execute("PRAGMA table_info(tx)")}
    check("migration: ten_pct and plan columns added", {"ten_pct", "plan"} <= cols, str(cols))
    row = con.execute("SELECT ten_pct, plan FROM tx WHERE accession='acc-old'").fetchone()
    check("migration: the old row survives with unknown flags", row == (None, None), str(row))

    def ins(acc, sym, issuer, insider, cik, roles, code, direction, shares, price, filed_ago,
            ten_pct=0, plan=0, held=None, open_market=1, derivative=0):
        con.execute(
            "INSERT INTO tx (accession, filed, tx_date, symbol, issuer, insider, insider_cik, roles, "
            "code, direction, shares, price, value, open_market, derivative, source_url, held_after, "
            "ten_pct, plan) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (acc, d(filed_ago), d(filed_ago + 2), sym, issuer, insider, cik, roles, code, direction,
             shares, price, shares * price, open_market, derivative, "u", held, ten_pct, plan))

    # A real cluster: three distinct insiders at ABC.
    ins("a1", "ABC", "ABC Corp", "Alice", "0011", "Chief Executive Officer", "P", "acquired", 1000, 20.0, 4, held=3000)
    ins("a2", "ABC", "ABC Corp", "Bob", "0012", "Director", "P", "acquired", 500, 21.0, 3)
    ins("a3", "ABC", "ABC Corp", "Alice", "0011", "Chief Executive Officer", "P", "acquired", 200, 22.0, 2)
    ins("a4", "ABC", "ABC Corp", "Carol", "0013", "Chief Financial Officer", "P", "acquired", 300, 20.5, 1)
    # A 10% owner and a plan trade at DEF — one real buyer besides them.
    ins("d1", "DEF", "DEF Inc", "HoldCo LP", "0021", "10% owner", "P", "acquired", 50000, 5.0, 2, ten_pct=1)
    ins("d2", "DEF", "DEF Inc", "Dan", "0022", "Director", "P", "acquired", 1000, 5.0, 2, plan=1)
    ins("d3", "DEF", "DEF Inc", "Eve", "0023", "Director", "P", "acquired", 2000, 5.0, 1)
    # Sells and a grant at GHI: never a buy.
    ins("g1", "GHI", "GHI Co", "Gil", "0031", "Director", "S", "disposed", 1000, 50.0, 1)
    ins("g2", "GHI", "GHI Co", "Hal", "0032", "Director", "A", "acquired", 1000, 0.0, 1, open_market=0)
    # Joint filers with no CIK at JKL — one decision, filed under one name.
    ins("j1", "JKL", "JKL Ltd", "Fund and GP", "", "10% owner", "P", "acquired", 100, 10.0, 1)
    ins("j2", "JKL", "JKL Ltd", "Fund and GP", "", "10% owner", "P", "acquired", 100, 10.0, 1)
    # Older than the window.
    ins("z1", "ZZZ", "Zed", "Zoe", "0041", "Director", "P", "acquired", 9000, 9.0, 45)
    # Coverage counts BUSINESS days, so the two scanned days must be business
    # days: "yesterday and the day before" made this test fail every Sunday
    # and Monday, when one or both of those are a weekend.
    past = [x for x in m.business_days(30) if x < today]  # coverage never counts today
    for day in sorted(past, reverse=True)[:2]:
        con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (day.isoformat(),))
    con.commit()
    con.close()

    r = m.recent(days=30, min_insiders=2, min_value=0)
    by = {x["symbol"]: x for x in r["rows"]}
    check("recent: ABC is a cluster of three distinct buyers", by.get("ABC", {}).get("insiders") == 3, str(by.get("ABC")))
    check("recent: ABC counts four trades", by["ABC"]["trades"] == 4, str(by["ABC"]))
    check("recent: value summed as shares x price",
          abs(by["ABC"]["value"] - (1000 * 20 + 500 * 21 + 200 * 22 + 300 * 20.5)) < 1e-6, str(by["ABC"]["value"]))
    check("recent: average price is value-weighted", abs(by["ABC"]["avg_price"] - by["ABC"]["value"] / 2000) < 1e-9)
    check("recent: stake increase from held_after — 1000 bought over 2000 held before",
          abs(by["ABC"]["stake_increase"] - 0.5) < 1e-9, str(by["ABC"].get("stake_increase")))
    check("recent: roles de-duplicated", by["ABC"]["roles"].count("Director") == 1, str(by["ABC"]["roles"]))
    check("recent: DEF has one scorable buyer, so it is not a cluster", "DEF" not in by, str(list(by)))
    check("recent: sells and grants never rank", "GHI" not in by, str(list(by)))
    check("recent: joint filers under one name count once", "JKL" not in by, str(list(by)))
    check("recent: the old row is outside the window", "ZZZ" not in by, str(list(by)))
    check("recent: coverage reported", r["days_scanned"] == 2 and r["days_wanted"] > 2, str((r["days_scanned"], r["days_wanted"])))

    r1 = m.recent(days=30, min_insiders=1, min_value=0)
    by1 = {x["symbol"]: x for x in r1["rows"]}
    check("recent: with the floor at one, DEF shows its one real buyer", by1.get("DEF", {}).get("insiders") == 1, str(by1.get("DEF")))
    check("recent: the OLD row with unknown flags is kept, and says so",
          by1.get("OLD", {}).get("plan_unknown") == 1, str(by1.get("OLD")))

    r2 = m.recent(days=30, min_insiders=2, min_value=0, exclude_ten_pct=False, exclude_plan=False)
    by2 = {x["symbol"]: x for x in r2["rows"]}
    check("recent: switches off, DEF becomes a cluster of three and is flagged",
          by2.get("DEF", {}).get("insiders") == 3 and by2["DEF"]["any_plan"] and by2["DEF"]["any_ten_pct"],
          str(by2.get("DEF")))
    check("recent: ranked by value, DEF's $265K above ABC's $47K", [x["symbol"] for x in r2["rows"]][:2] == ["DEF", "ABC"],
          str([x["symbol"] for x in r2["rows"]]))

    rs = m.recent(days=30, min_insiders=1, min_value=0, direction="sell")
    check("recent: the sell view lists GHI's sale and nothing else",
          [x["symbol"] for x in rs["rows"]] == ["GHI"], str([x["symbol"] for x in rs["rows"]]))

    # ── an interrupted day is resumed, not written off ─────────────────────
    con = m.connect()
    marked = {r[0] for r in con.execute("SELECT filed FROM scanned_days")}
    days = [x.isoformat() for x in m.business_days(8) if x < today and x.isoformat() not in marked][:2]
    partial, complete = days
    con.execute("INSERT OR REPLACE INTO seen VALUES (?,?)", (f"{partial}-read", partial))
    con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (complete,))
    con.commit()
    con.close()
    walked, fetched = [], []
    m.day_index = lambda dd: (walked.append(dd.isoformat()) or
                              {f"{dd}-read": f"a/{dd}-read.txt", f"{dd}-new": f"a/{dd}-new.txt"})
    class Ok:
        ok, text = True, "no ownership xml here"
    m._get_status = lambda url: (fetched.append(url) or (Ok(), 200))
    m.REQ_PAUSE = 0
    res = m.scan(days=8)
    check("scan: the interrupted day is walked again", partial in walked, str(walked))
    check("scan: a completed day is not", complete not in walked, str(walked))
    check("scan: only the unread filing of the interrupted day is fetched",
          any(u.endswith(f"{partial}-new.txt") for u in fetched)
          and not any(u.endswith(f"{partial}-read.txt") for u in fetched),
          str(fetched))
    con = m.connect()
    check("scan: the resumed day is now recorded as covered",
          con.execute("SELECT 1 FROM scanned_days WHERE filed=?", (partial,)).fetchone() is not None, str(res))
    con.close()

    # ── a failed filing is not read; a vanished one is; a refusal stops ────
    con = m.connect()
    marked = {r[0] for r in con.execute("SELECT filed FROM scanned_days")}
    con.close()
    left = [x.isoformat() for x in m.business_days(12) if x < today and x.isoformat() not in marked]
    flaky, gone_day, refused, after = left[0], left[1], left[2], left[3]
    m.day_index = lambda dd: ({f"{dd}-ok": f"a/{dd}-ok.txt", f"{dd}-bad": f"a/{dd}-bad.txt"}
                              if dd.isoformat() == flaky else
                              {f"{dd}-ok": f"a/{dd}-ok.txt", f"{dd}-404": f"a/{dd}-404.txt"}
                              if dd.isoformat() == gone_day else
                              None if dd.isoformat() == refused else {})
    m._get_status = lambda url: ((None, 503) if url.endswith("-bad.txt") else
                                 (None, 404) if url.endswith("-404.txt") else (Ok(), 200))
    res = m.scan(days=12)
    con = m.connect()
    rec = {r[0] for r in con.execute("SELECT filed FROM scanned_days")}
    seen = {r[0] for r in con.execute("SELECT accession FROM seen")}
    con.close()
    check("scan: a day with an unreadable filing stays unrecorded", flaky not in rec, str(res))
    check("scan: the unreadable filing is not marked seen", f"{flaky}-bad" not in seen and f"{flaky}-ok" in seen)
    check("scan: a filing gone for good (404) does not hold its day open",
          gone_day in rec and f"{gone_day}-404" in seen, str(res))
    check("scan: a refused index leaves its day unrecorded", refused not in rec, str(res))
    check("scan: a refusal ends the pass — older days are not hammered", after not in rec, str(res))
    check("scan: failures are reported as a soft error with the days",
          refused in res.get("failed_days", []) and flaky in res.get("failed_days", []) and "error" in res, str(res))
    check("scan: every unrecorded past day counts as still to do",
          res.get("days_remaining", 0) >= 3, str(res))
    read, expected, missing = m.coverage(20)
    check("coverage: the failed days are reported missing", flaky in missing and refused in missing, str(missing))
    check("coverage: today is never expected", today.isoformat() not in missing)

    # day_index itself: 404 = no filings that day; anything else = unknown.
    real = __import__("importlib").reload(m)
    real._get_status = lambda url: (None, 404)
    check("day_index: 404 is an empty day", real.day_index(today) == {})
    real._get_status = lambda url: (None, 403)
    check("day_index: a refusal is None, not empty", real.day_index(today) is None)
    real._get_status = lambda url: (None, None)
    check("day_index: no answer is None, not empty", real.day_index(today) is None)

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED: " + ", ".join(FAILURES))
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
