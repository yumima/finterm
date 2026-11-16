#!/usr/bin/env python3
"""Which followed companies have listed (pre_ipo_listings.py) — the join only,
no network. A CIK SEC lists as traded is listed under its primary ticker; one
it does not list is simply absent (still private)."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pre_ipo_listings as pl  # needs requests
except ImportError:
    sys.exit(77)

FAILS = []


def check(name, got, want):
    if got != want:
        FAILS.append(f"{name}: got {got!r}, want {want!r}")


doc = {
    "0": {"cik_str": 1181412, "ticker": "SPCX", "title": "SPACE EXPLORATION TECHNOLOGIES CORP"},
    "1": {"cik_str": 1067983, "ticker": "BRK-B", "title": "BERKSHIRE HATHAWAY INC"},
    "2": {"cik_str": 1067983, "ticker": "BRK-A", "title": "BERKSHIRE HATHAWAY INC"},
    "3": {"cik_str": 320193, "ticker": "", "title": "NO TICKER"},
}
check("pad", pl.pad("1181412"), "0001181412")
check("pad junk", pl.pad("n/a"), "")
got = pl.listed_from(doc, ["0001181412", "1067983", "0000320193", "0009999999", ""])
check("listed", got, {"0001181412": "SPCX", "0001067983": "BRK-B"})  # first row is the primary class
check("none asked", pl.listed_from(doc, []), {})
check("no ciks, no request", pl.listings([]), {"listed": {}, "checked": 0})

if FAILS:
    print("\n".join(FAILS))
    sys.exit(1)
print("ok")
