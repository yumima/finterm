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
    os.environ["FINCEPT_DATA_DIR"] = tmp

    # ── an old-schema store, as shipped before the flags existed ───────────
    old = sqlite3.connect(os.path.join(tmp, "form4.sqlite"))
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
    con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (d(1),))
    con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (d(2),))
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

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED: " + ", ".join(FAILURES))
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
