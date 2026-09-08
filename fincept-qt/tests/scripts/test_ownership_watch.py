#!/usr/bin/env python3
"""Ownership where the reader already is — the watchlist rows and the calendar.

Runs the SHIPPED functions from ownership_watch against throwaway stores and
a stubbed IPO calendar. No network. What is pinned:

  • A symbol the 13F or FINRA stores have never seen gets no holder or
    short figures — never a zero. An insider count is present (possibly 0)
    only where the Form 4 store has read a day inside the window.
  • The insider-buy count applies the two exclusions (10% owners, 10b5-1
    plans) and counts distinct buyers.
  • 13F due dates are 45 days after quarter end, moved forward off a weekend;
    FINRA publication dates are ten business days after a settlement, and a
    settlement already past whose publication is still ahead is listed.
  • Lock-ups are 180 days after pricing, SPACs are skipped, and only
    expiries inside the window are returned.
"""

import os
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
    tmp = tempfile.mkdtemp(prefix="own_watch")
    os.environ["FINCEPT_DATA_DIR"] = tmp
    import ownership_watch as w
    import sec_form4_market as form4
    import finra_short_interest as finra
    import sec_13f_bulk as bulk

    # ── stores: a 13F quarter pair, a FINRA date, a few Form 4 rows ─────────
    con = bulk.connect()
    con.execute("INSERT INTO quarters VALUES ('2026-03-31','test',2,2,datetime('now'),0)")
    con.execute("INSERT INTO quarters VALUES ('2025-12-31','test',1,1,datetime('now'),0)")
    for acc, q, cik, mgr, shares in (("a1", "2026-03-31", "0001", "Fund One", 1000.0),
                                     ("a2", "2026-03-31", "0002", "Fund Two", 500.0),
                                     ("p1", "2025-12-31", "0001", "Fund One", 900.0)):
        con.execute("INSERT INTO filings VALUES (?,?,?,?,0)", (acc, q, cik, mgr))
        con.execute("INSERT INTO holdings VALUES (?,?,?,?,?,?,?,?)",
                    (acc, q, "111111111", "WIDGET CO", "COM", "", shares * 10.0, shares))
        con.execute("INSERT INTO books VALUES (?,?,?,?,?,?)", (acc, q, cik, mgr, shares * 10.0, 3))
    con.execute("INSERT INTO cusip_ticker VALUES ('111111111','WDGT','WIDGET CO','test')")
    con.commit()
    con.close()
    bulk.MIN_BOOK_VALUE, bulk.MIN_BOOK_POSITIONS = 0.0, 0

    fc = finra.connect()
    fc.execute("INSERT INTO si VALUES ('2026-08-14','WDGT',300.0,200.0,50.0,6.0,'NNM')")
    fc.execute("INSERT INTO complete_dates VALUES ('2026-08-14', 1, datetime('now'))")
    fc.commit()
    fc.close()

    con4 = form4.connect()
    today = date.today()
    d = lambda n: (today - timedelta(days=n)).isoformat()
    rows = [("x1", d(3), d(5), "WDGT", "Widget Co", "Ann", "11", "Director", "P", "acquired", 100, 10.0, 1, 0, 0, 0),
            ("x2", d(2), d(4), "WDGT", "Widget Co", "Bob", "12", "CEO", "P", "acquired", 200, 10.0, 1, 0, 0, 0),
            ("x3", d(2), d(4), "WDGT", "Widget Co", "Bob", "12", "CEO", "P", "acquired", 50, 10.0, 1, 0, 0, 0),
            ("x4", d(1), d(2), "WDGT", "Widget Co", "HoldCo", "13", "10% owner", "P", "acquired", 9000, 10.0, 1, 0, 1, 0),
            ("x5", d(1), d(2), "WDGT", "Widget Co", "Cy", "14", "Director", "P", "acquired", 100, 10.0, 1, 0, 0, 1),
            ("x6", d(1), d(2), "WDGT", "Widget Co", "Dee", "15", "Director", "S", "disposed", 100, 10.0, 1, 0, 0, 0)]
    for acc, filed, tx, sym, iss, ins, cik, roles, code, direction, sh, px, om, deriv, ten, plan in rows:
        con4.execute("INSERT INTO tx (accession, filed, tx_date, symbol, issuer, insider, insider_cik, roles, code, "
                     "direction, shares, price, value, open_market, derivative, source_url, held_after, ten_pct, plan) "
                     "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                     (acc, filed, tx, sym, iss, ins, cik, roles, code, direction, sh, px, sh * px, om, deriv, "u", None, ten, plan))
    con4.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (d(1),))
    con4.commit()
    con4.close()

    # The once-a-day look at FINRA's index: stub it to list the date the store
    # already holds, and count the calls — the second rows() must not ask again.
    probes = []
    def fake_dates():
        probes.append(1)
        return {"dates": ["2026-08-14", "2026-07-31"]}
    finra.dates = fake_dates

    r = w.rows(["wdgt", "NOPE"])
    row = r["rows"]["WDGT"]
    check("rows: holders and shares from the 13F totals", row.get("holders") == 2, str(row))
    check("rows: holder change against the prior quarter (one filer in both)", row.get("delta_holders") == 1, str(row))
    check("rows: SI ÷ 13F shares = 300 / 1500", abs(row.get("sirio", 0) - 0.2) < 1e-9, str(row.get("sirio")))
    check("rows: days to cover carried", row.get("dtc") == 6.0, str(row))
    check("rows: scorable insider buys — three rows by two buyers; 10% owner, plan and sell excluded",
          row.get("insider_buys") == 3 and row.get("insider_buyers") == 2, str(row))
    check("rows: buy value summed", abs(row.get("insider_buy_value", 0) - 3500.0) < 1e-9, str(row.get("insider_buy_value")))
    # The 13F and FINRA stores have never seen NOPE: no holder, no short
    # figure. The Form 4 store reads EVERY issuer's filings, so "no scorable
    # buy in a window it has read" is a true zero for NOPE as well.
    nope = r["rows"]["NOPE"]
    check("rows: an unknown symbol has no holder or short figures, never zeros",
          "holders" not in nope and "short" not in nope and "sirio" not in nope, str(nope))
    check("rows: …but a zero insider count, because the store read the window", nope.get("insider_buys") == 0, str(nope))
    check("rows: the three dates travel", r["quarter"] == "2026-03-31" and r["settlement"] == "2026-08-14"
          and r["published_after"] == "2026-08-28" and r["form4_scanned_to"] == d(1), str(r))
    w.rows(["WDGT"])
    check("rows: FINRA's index is asked once a day, not once per watchlist load", len(probes) == 1, str(len(probes)))

    # A symbol the 13F store knows but with no scorable buy: zero, stated,
    # because the store HAS read a day inside the window.
    con4 = form4.connect()
    con4.execute("INSERT INTO tx (accession, filed, tx_date, symbol, issuer, insider, insider_cik, roles, code, "
                 "direction, shares, price, value, open_market, derivative, source_url, held_after, ten_pct, plan) "
                 "VALUES ('y1',?,?,'QUIET','Quiet Co','Q','21','Director','S','disposed',10,1.0,10.0,1,0,'u',NULL,0,0)",
                 (d(1), d(2)))
    con4.commit()
    con4.close()
    r2 = w.rows(["WDGT", "QUIET"])
    check("rows: a name with no buys in a window the store has read shows 0, not absent",
          r2["rows"]["QUIET"].get("insider_buys") == 0, str(r2["rows"]["QUIET"]))
    # …and with nothing read inside the window, the count is absent for everyone.
    con4 = form4.connect()
    con4.execute("DELETE FROM scanned_days")
    con4.execute("INSERT INTO scanned_days VALUES (?)", (d(60),))
    con4.commit()
    con4.close()
    r3 = w.rows(["WDGT"])
    check("rows: nothing read inside the window -> no insider count at all, never a zero",
          "insider_buys" not in r3["rows"]["WDGT"] and r3["form4_scanned_to"] == d(60), str(r3["rows"]["WDGT"]))
    con4 = form4.connect()
    con4.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (d(1),))
    con4.commit()
    con4.close()

    a = w.alerts(["WDGT", "NOPE"], days=7)
    check("alerts: one issuer row, the two scorable buyers", len(a["rows"]) == 1 and a["rows"][0]["insiders"] == 2, str(a["rows"]))

    # ── calendar date math ──────────────────────────────────────────────────
    t = date(2026, 9, 7)   # a Monday
    f = w.form13f_deadlines(t, 90)
    check("13F: Q3 2026 is due 16 Nov (45 days after 30 Sep lands on a Saturday)",
          f == [{"quarter_end": "2026-09-30", "due": "2026-11-16"}], str(f))
    fin = w.finra_settlements(t, 30)
    check("FINRA: the 31 Aug settlement, already past, still publishes ahead (14 Sep)",
          fin and fin[0] == {"settlement": "2026-08-31", "published": "2026-09-14"}, str(fin[:1]))
    check("FINRA: the 15 Sep settlement publishes 29 Sep",
          {"settlement": "2026-09-15", "published": "2026-09-29"} in fin, str(fin))
    check("FINRA: nothing beyond the window", all(x["published"] <= "2026-10-07" for x in fin), str(fin))

    def fake_fetch(yyyymm):
        return {
            "2026-03": [{"proposedTickerSymbol": "ABC", "companyName": "Abc Inc", "pricedDate": "3/12/2026"},
                        {"proposedTickerSymbol": "SPCU", "companyName": "Some Acquisition Corp", "pricedDate": "3/13/2026"},
                        {"proposedTickerSymbol": "OLD", "companyName": "Old Co", "pricedDate": "3/1/2026"}],
            "2026-06": [{"proposedTickerSymbol": "LATE", "companyName": "Late Ltd", "pricedDate": "6/30/2026"}],
        }.get(yyyymm, [])
    lk = w.lockup_expiries(t, 90, fetch=fake_fetch)
    syms = [x["symbol"] for x in lk]
    check("lockups: 180 days after pricing, inside the window", "ABC" in syms and
          next(x for x in lk if x["symbol"] == "ABC")["expiry"] == "2026-09-08", str(lk))
    check("lockups: a SPAC is skipped", "SPCU" not in syms, str(syms))
    check("lockups: an expiry before today is not listed", "OLD" not in syms, str(syms))
    check("lockups: an expiry beyond the window is not listed", "LATE" not in syms, str(syms))

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED: " + ", ".join(FAILURES))
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
