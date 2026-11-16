#!/usr/bin/env python3
"""Futures forward curve and COT shaping (futures_curve.py, futures_cot.py).

Pinned: which symbols are asked for, that a contract with no price is no row,
that an absent open interest / expiry stays None (never 0), that expired and
foreign-root rows are dropped, that an old print is flagged stale, and that a
past curve only carries months that traded that exact session.
"""

import os
import sys
from datetime import date

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

import futures_curve as fc  # noqa: E402

FAILS = []


def check(name, got, want):
    if got != want:
        FAILS.append(f"{name}: got {got!r}, want {want!r}")


check("split", fc.split_contract("CLF27.NYM"), ("CL", 2027, 1, "NYM"))
check("split digit root", fc.split_contract("6EZ26.CME"), ("6E", 2026, 12, "CME"))
check("split 3-letter root", fc.split_contract("SR3H27.CME"), ("SR3", 2027, 3, "CME"))
check("split continuous", fc.split_contract("CL=F"), None)
check("split bad month", fc.split_contract("CLA27.NYM"), None)
check("split none", fc.split_contract(None), None)

c = fc.candidate_symbols("CL", "NYM", date(2026, 11, 16), 4)
check("candidates", c, ["CLX26.NYM", "CLZ26.NYM", "CLF27.NYM", "CLG27.NYM"])

NOW = 1_800_000_000
q = {"symbol": "CLF27.NYM", "regularMarketPrice": 85.6, "regularMarketPreviousClose": 87.29,
     "regularMarketTime": NOW - 60, "regularMarketVolume": 86464, "openInterest": 257222,
     "expireDate": 1797811200, "bid": 85.55, "ask": 85.57, "currency": "USD"}
row = fc.contract_row(q, NOW)
check("row month", row["month"], "2027-01")
check("row change", round(row["change"], 4), -1.69)
check("row pct", round(row["pct"], 4), round((85.6 - 87.29) / 87.29 * 100, 4))
check("row expiry", row["expiry"], "2026-12-21")
check("row fresh", row["stale"], False)

bare = fc.contract_row({"symbol": "CLG27.NYM", "regularMarketPrice": 84.2}, NOW)
check("absent oi is None", bare["oi"], None)
check("absent volume is None", bare["volume"], None)
check("absent prev is None", bare["prev_close"], None)
check("absent change is None", bare["change"], None)
check("absent expiry is None", bare["expiry"], None)
check("no print time is stale", bare["stale"], True)

# Staleness is measured against the curve's newest print, in trading days.
from datetime import datetime  # noqa: E402
from zoneinfo import ZoneInfo  # noqa: E402
_ET = ZoneInfo("America/New_York")


def et(y, m, d, hh, mm=0):
    return datetime(y, m, d, hh, mm, tzinfo=_ET).timestamp()


FRI_PM, SUN_EVE, MON_AM = et(2026, 11, 13, 15), et(2026, 11, 15, 19), et(2026, 11, 16, 10)
check("sunday evening belongs to monday", fc.trading_day(SUN_EVE), date(2026, 11, 16))
check("friday print, monday curve: fresh", fc.is_stale(FRI_PM, MON_AM), False)
check("thursday print, monday curve: stale", fc.is_stale(et(2026, 11, 12, 15), MON_AM), True)
check("whole curve last printed friday: nothing stale", fc.is_stale(FRI_PM, FRI_PM), False)
check("same session", fc.is_stale(MON_AM - 3600, MON_AM), False)
check("zero oi stays zero", fc.contract_row(dict(q, openInterest=0), NOW)["oi"], 0.0)
check("no price is no row", fc.contract_row({"symbol": "CLH27.NYM", "regularMarketPrice": None}, NOW), None)
check("zero price is no row", fc.contract_row({"symbol": "CLH27.NYM", "regularMarketPrice": 0}, NOW), None)
check("old print is stale", fc.contract_row(dict(q, regularMarketTime=NOW - 6 * 86400), NOW)["stale"], True)

quotes = [
    dict(q, symbol="CLG27.NYM", expireDate=1800403200),
    q,
    dict(q, symbol="CLX26.NYM", expireDate=1790000000),          # expired, still served
    dict(q, symbol="MCLF27.NYM"),                                 # another root
    {"symbol": "CLH27.NYM"},                                      # unpriced
]
curve = fc.shape_curve("CL", quotes, "CLF27.NYM", NOW, today=date(2026, 11, 16))
check("curve order", [r["symbol"] for r in curve["contracts"]], ["CLF27.NYM", "CLG27.NYM"])
check("curve front", curve["front"], "CLF27.NYM")
check("curve currency", curve["currency"], "USD")

past = fc.curve_on_date({"A": {"2026-11-09": 10.0, "2026-11-10": 11.0}, "B": {"2026-11-10": 5.0},
                         "C": {"2026-11-09": float("nan")}}, "2026-11-09")
check("past curve exact session only", past, {"A": 10.0})

try:
    import futures_cot  # noqa: E402  (needs requests)
except ImportError:
    sys.exit(77)

market, rows = futures_cot.shape([
    {"report_date_as_yyyy_mm_dd": "2026-11-03T00:00:00.000", "market_and_exchange_names": "GOLD - COMEX",
     "open_interest_all": "538190", "noncomm_positions_long_all": "212570",
     "noncomm_positions_short_all": "41169", "comm_positions_long_all": "106402"},
    {"report_date_as_yyyy_mm_dd": "2026-10-27T00:00:00.000", "open_interest_all": "518785"},
    {"report_date_as_yyyy_mm_dd": ""},
])
check("cot market", market, "GOLD - COMEX")
check("cot sorted", [r["date"] for r in rows], ["2026-10-27", "2026-11-03"])
check("cot value", rows[1]["nc_long"], 212570)
check("cot absent is None", rows[1]["c_short"], None)
check("cot bad code", "error" in futures_cot.fetch("gold; drop"), True)

if FAILS:
    print("\n".join(FAILS))
    sys.exit(1)
print("ok")
