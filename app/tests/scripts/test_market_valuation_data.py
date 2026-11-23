#!/usr/bin/env python3
"""market_valuation_data — the CAPE sheet and the Strategy 8-K "BTC Update".

Offline (no network): the parsers only.
  - Shiller's Date is a float year.month: 2026.1 is OCTOBER, 2026.01 January;
    a blank / NA CAPE is absent, never 0; the header row is found by name.
  - The BTC Update's fourth number is the aggregate holding; a misread (out
    of range, a decimal) is no answer, never a figure.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import requests  # noqa: F401
except ImportError:
    sys.exit(77)

import market_valuation_data as mv  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


def main():
    hdr = ["Date", "P", "D", "E", "CPI", "Fraction", "Rate GS10", "Price", "Dividend", "Price", "Earnings",
           "Earnings", "CAPE"]
    rows = [["junk"], hdr,
            [2026.09, 7669.4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40.2683],
            [2026.1, 7726.7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40.1174],
            [2026.11, 7619.4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "NA"],
            [2026.01, 7000.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 38.5]]
    pts = mv.parse_cape_rows(rows)
    check("2026.1 is October", ["2026-10-01", 40.1174] in pts, pts)
    check("2026.01 is January", pts[0] == ["2026-01-01", 38.5], pts)
    check("NA CAPE is skipped, not 0", all(p[0] != "2026-11-01" for p in pts), pts)
    check("no CAPE column → nothing", mv.parse_cape_rows([["Date", "P"], [2026.1, 1.0]]) == [])
    check("link found", mv._shiller_link('<a href="//img1.wsimg.com/x/ie_data.xls?ver=1">') ==
          "https://img1.wsimg.com/x/ie_data.xls?ver=1")

    doc = ("<p>BTC Update</p><p>On November 16, 2026, Strategy announced updates with respect to its bitcoin "
           "holdings:</p><table><tr><td>During Period November 9, 2026 to November 15, 2026</td><td>As of "
           "November 15, 2026</td></tr><tr><td>BTC Purchased (1)</td><td>Aggregate Purchase Price (in millions) (2)"
           "</td><td>Average Purchase Price (2)</td><td>Aggregate BTC Holdings</td><td>Aggregate Purchase Price "
           "(in billions) (2)</td><td>Average Purchase Price (2)</td></tr><tr><td>485</td><td>$ 33.4</td><td>$ "
           "68,887</td><td>850,849</td><td>$ 64.20</td><td>$ 75,452</td></tr></table>")
    check("holding read", mv.parse_btc_update(doc) == (850849, "2026-11-15"), mv.parse_btc_update(doc))
    check("an implausible holding is no answer", mv.parse_btc_update(doc.replace("850,849", "12")) is None)
    check("no table → None", mv.parse_btc_update("<p>nothing here</p>") is None)

    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
