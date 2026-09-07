#!/usr/bin/env python3
"""FINRA consolidated short interest — the decode, the lag, the ranking.

Runs the SHIPPED functions from finra_short_interest against a throwaway
store with the network stubbed out. What is pinned:

  • A FINRA row decodes with absent numbers ABSENT, never zero — "no days to
    cover reported" and "zero days to cover" are different facts.
  • The publication lag is stated as a date: ten business days after
    settlement, so a Friday settlement publishes on a Friday two weeks on.
  • history() serves the local store when FINRA is unreachable and says so.
  • rank() joins to 13F shares, computes SI ÷ 13F shares, drops funds by
    name and thin names by holder count, and reports what it dropped.
  • A date with only one symbol's rows in the store is NOT treated as a
    complete settlement file for the ranking.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def main():
    tmp = tempfile.mkdtemp(prefix="finra_si")
    os.environ["FINCEPT_DATA_DIR"] = tmp
    import finra_short_interest as si

    # ── row decode ──────────────────────────────────────────────────────────
    r = si._row({"settlementDate": "2026-08-14", "symbolCode": "aapl",
                 "currentShortPositionQuantity": 116327753, "previousShortPositionQuantity": 141606163,
                 "averageDailyVolumeQuantity": 46065396, "daysToCoverQuantity": 2.53,
                 "marketClassCode": "NNM"})
    check("decode: symbol upper-cased", r["symbol"] == "AAPL", r["symbol"])
    check("decode: numbers are floats", r["short"] == 116327753.0 and r["dtc"] == 2.53, str(r))
    r2 = si._row({"settlementDate": "2026-08-14", "symbolCode": "X",
                  "currentShortPositionQuantity": 10, "daysToCoverQuantity": None})
    check("decode: an absent days-to-cover stays None, not 0", r2["dtc"] is None, str(r2))
    check("decode: an absent prior stays None", r2["prior"] is None, str(r2))

    # ── publication lag ─────────────────────────────────────────────────────
    check("lag: ten business days after a Friday settlement is the Friday two weeks on",
          si.published_after("2026-08-14") == "2026-08-28", si.published_after("2026-08-14"))
    check("lag: a bad date yields an empty string, not an exception",
          si.published_after("not-a-date") == "")

    # ── history: network first, store as fallback ───────────────────────────
    fixture = [
        {"settlementDate": "2026-08-14", "symbolCode": "TST", "currentShortPositionQuantity": 200_000,
         "previousShortPositionQuantity": 160_000, "averageDailyVolumeQuantity": 20_000,
         "daysToCoverQuantity": 10.0, "marketClassCode": "NNM"},
        {"settlementDate": "2026-07-31", "symbolCode": "TST", "currentShortPositionQuantity": 160_000,
         "previousShortPositionQuantity": 180_000, "averageDailyVolumeQuantity": 20_000,
         "daysToCoverQuantity": 8.0, "marketClassCode": "NNM"},
        {"settlementDate": "2024-01-15", "symbolCode": "TST", "currentShortPositionQuantity": 5,
         "previousShortPositionQuantity": 5, "averageDailyVolumeQuantity": 1,
         "daysToCoverQuantity": 5.0, "marketClassCode": "NNM"},
    ]
    si._post = lambda body, timeout=90: fixture
    h = si.history("tst", months=12)
    check("history: newest first", h["rows"][0]["settlement"] == "2026-08-14", str(h.get("rows")))
    check("history: the two-year-old row is outside the window", len(h["rows"]) == 2, str(len(h["rows"])))
    check("history: change vs prior computed", abs(h["latest"]["change_pct"] - 0.25) < 1e-9,
          str(h["latest"].get("change_pct")))
    check("history: source says finra", h["source"] == "finra", h["source"])

    si._post = lambda body, timeout=90: None   # network gone
    h2 = si.history("TST", months=12)
    check("history offline: served from the store", len(h2.get("rows", [])) == 2, str(h2))
    check("history offline: labelled as the cache", h2.get("source") == "local cache", str(h2.get("source")))
    h3 = si.history("NOPE", months=12)
    check("history offline: an unknown symbol says offline rather than 'no rows'",
          "offline" in h3.get("error", ""), str(h3))

    # ── rank: the join, the exclusions, the completeness guard ──────────────
    # The store now holds TST for 2026-08-14 from the per-symbol fetch. That
    # must not make the date look complete.
    con = si.connect()
    done = con.execute("SELECT COUNT(*) FROM complete_dates").fetchone()[0]
    con.close()
    check("rank: a per-symbol fetch does not mark the date complete", done == 0, str(done))

    page = [
        {"settlementDate": "2026-08-14", "symbolCode": "TST", "currentShortPositionQuantity": 200_000,
         "previousShortPositionQuantity": 160_000, "averageDailyVolumeQuantity": 20_000,
         "daysToCoverQuantity": 10.0, "marketClassCode": "NNM"},
        {"settlementDate": "2026-08-14", "symbolCode": "BIG", "currentShortPositionQuantity": 5_000_000,
         "previousShortPositionQuantity": 4_000_000, "averageDailyVolumeQuantity": 1_000_000,
         "daysToCoverQuantity": 5.0, "marketClassCode": "NYSE"},
        {"settlementDate": "2026-08-14", "symbolCode": "ETF1", "currentShortPositionQuantity": 9_000_000,
         "previousShortPositionQuantity": 9_000_000, "averageDailyVolumeQuantity": 9_000_000,
         "daysToCoverQuantity": 1.0, "marketClassCode": "ARCA"},
        {"settlementDate": "2026-08-14", "symbolCode": "THIN", "currentShortPositionQuantity": 2_000_000,
         "previousShortPositionQuantity": 2_000_000, "averageDailyVolumeQuantity": 100_000,
         "daysToCoverQuantity": 20.0, "marketClassCode": "NNM"},
        {"settlementDate": "2026-08-14", "symbolCode": "NOIDX", "currentShortPositionQuantity": 3_000_000,
         "previousShortPositionQuantity": 3_000_000, "averageDailyVolumeQuantity": 300_000,
         "daysToCoverQuantity": 10.0, "marketClassCode": "NNM"},
    ]
    si._post = lambda body, timeout=90: page
    si.dates = lambda: {"dates": ["2026-08-14", "2026-07-31"]}

    class FakeBulk:
        @staticmethod
        def shares_by_ticker(quarter=None):
            return {"quarter": "2026-03-31", "by_ticker": {
                "TST": {"shares": 100_000.0, "holders": 120, "name": "TEST CO", "fund": False},
                "BIG": {"shares": 50_000_000.0, "holders": 900, "name": "BIG CORP", "fund": False},
                "ETF1": {"shares": 1_000_000.0, "holders": 300, "name": "SOME INDEX ETF", "fund": True},
                "THIN": {"shares": 1_000_000.0, "holders": 12, "name": "THIN INC", "fund": False},
            }}
    sys.modules["sec_13f_bulk"] = FakeBulk()

    r = si.rank(limit=10)
    syms = [x["symbol"] for x in r["rows"]]
    check("rank: the settlement date was paged in and marked complete", r["complete"] is True, str(r))
    check("rank: SI ÷ 13F shares computed", abs(next(x for x in r["rows"] if x["symbol"] == "TST")["sirio"] - 2.0) < 1e-12)
    check("rank: sorted by SI ÷ 13F, highest first", syms[0] == "TST" and syms[1] == "BIG", str(syms))
    check("rank: the fund is dropped by name and counted", "ETF1" not in syms and r["funds_dropped"] == 1,
          str((syms, r.get("funds_dropped"))))
    check("rank: a name with too few holders is dropped", "THIN" not in syms, str(syms))
    check("rank: a symbol with no 13F denominator is kept without a ratio",
          any(x["symbol"] == "NOIDX" and "sirio" not in x for x in r["rows"]), str(r["rows"]))
    check("rank: publication lag carried", r["published_after"] == "2026-08-28", r["published_after"])

    # Percentile: TST has the higher ratio of the two joined stocks, and the
    # universe is the ranking's (the same short-position floor applies).
    p = si.sirio_percentile("TST")
    check("percentile: the top name sits above the other joined stock",
          p.get("percentile") == 0.5 and p.get("universe") == 2, str(p))
    p2 = si.sirio_percentile("NOIDX")
    check("percentile: a name without a denominator has none", p2.get("percentile") is None, str(p2))

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED: " + ", ".join(FAILURES))
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
