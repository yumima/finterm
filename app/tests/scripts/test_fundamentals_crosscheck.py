#!/usr/bin/env python3
"""Yahoo's statements against the company's own SEC filings — the comparison.

fundamentals_crosscheck.check_frames is what decides "Yahoo agrees with what
the company filed" or "it does not". Both mistakes cost: a false MISMATCH
accuses a correct figure, a false MATCH certifies a wrong one. These cases
drive the pure comparison with hand-built documents (no network).
"""

import os
import sys
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import fundamentals_crosscheck as fx  # noqa: E402


def facts(**tags):
    """{tag: [(start, end, val, form)]} -> a company-facts document."""
    gaap = {}
    for tag, rows in tags.items():
        unit = "USD/shares" if tag.startswith("EarningsPerShare") else "USD"
        gaap[tag] = {"units": {unit: [
            {"start": s, "end": e, "val": v, "form": f, "filed": e, "accn": "x"} if s else
            {"end": e, "val": v, "form": f, "filed": e, "accn": "x"}
            for (s, e, v, f) in rows]}}
    return {"facts": {"us-gaap": gaap}}


def one(rows, line, period=None):
    hits = [r for r in rows if r["line"] == line and (period is None or r["period"] == period)]
    assert len(hits) == 1, hits
    return hits[0]


def test_match_on_any_of_the_tags_a_line_is_filed_under():
    f = facts(RevenueFromContractWithCustomerExcludingAssessedTax=[("2023-10-01", "2024-09-28", 391035e6, "10-K")])
    r = one(fx.check_frames("X", {"income": {"2024-09-30": {"Total Revenue": 391035e6}}}, f), "revenue")
    assert r["status"] == "match" and r["sec_tag"].startswith("RevenueFromContract")


def test_fiscal_year_ending_on_a_weekday_is_the_same_period():
    # Yahoo stamps the month end; a 52/53-week year ends on the last Saturday.
    f = facts(NetIncomeLoss=[("2023-10-01", "2024-09-28", 93736e6, "10-K")])
    assert one(fx.check_frames("X", {"income": {"2024-09-30": {"Net Income": 93736e6}}}, f), "net_income")["status"] == "match"


def test_a_quarter_sharing_the_year_end_is_not_the_year():
    # Q4 and the nine months end the same day as the fiscal year. Only the
    # twelve-month figure may be compared with an annual line.
    f = facts(Revenues=[("2024-07-01", "2024-09-30", 25e9, "10-K"), ("2024-01-01", "2024-09-30", 75e9, "10-K"),
                        ("2023-10-01", "2024-09-30", 100e9, "10-K")])
    r = one(fx.check_frames("X", {"income": {"2024-09-30": {"Total Revenue": 100e9}}}, f), "revenue")
    assert r["status"] == "match" and r["candidates"] == 1


def test_restated_or_as_reported_either_is_what_the_company_filed():
    f = facts(NetIncomeLoss=[("2023-01-01", "2023-12-31", 500e6, "10-K"), ("2023-01-01", "2023-12-31", 480e6, "10-K/A")])
    assert one(fx.check_frames("X", {"income": {"2023-12-31": {"Net Income": 480e6}}}, f), "net_income")["status"] == "match"
    assert one(fx.check_frames("X", {"income": {"2023-12-31": {"Net Income": 500e6}}}, f), "net_income")["status"] == "match"


def test_a_real_disagreement_is_a_mismatch_with_its_size():
    f = facts(Assets=[(None, "2023-12-31", 1000e6, "10-K")])
    r = one(fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": 1100e6}}}, f), "total_assets")
    assert r["status"] == "mismatch" and abs(r["diff_pct"] - 10.0) < 1e-6 and r["sec"] == 1000e6


def test_rounding_is_not_a_disagreement_but_one_percent_is():
    f = facts(Assets=[(None, "2023-12-31", 1000e6, "10-K")])
    ok = one(fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": 1000.4e6}}}, f), "total_assets")
    bad = one(fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": 1010e6}}}, f), "total_assets")
    assert ok["status"] == "match" and bad["status"] == "mismatch"


def test_eps_is_compared_to_the_cent():
    f = facts(EarningsPerShareDiluted=[("2023-01-01", "2023-12-31", 6.13, "10-K")])
    ok = one(fx.check_frames("X", {"income": {"2023-12-31": {"Diluted EPS": 6.13}}}, f), "eps_diluted")
    bad = one(fx.check_frames("X", {"income": {"2023-12-31": {"Diluted EPS": 6.31}}}, f), "eps_diluted")
    assert ok["status"] == "match" and bad["status"] == "mismatch"


def test_a_total_filed_in_two_parts_is_their_sum():
    # A filer with a discontinued business tags continuing and discontinued
    # operating cash flow and no total; Yahoo reports the whole company.
    f = facts(NetCashProvidedByUsedInOperatingActivitiesContinuingOperations=[("2021-10-01", "2022-09-30", 193e6, "10-K")],
              CashProvidedByUsedInOperatingActivitiesDiscontinuedOperations=[("2021-10-01", "2022-09-30", -406e6, "10-K")])
    r = one(fx.check_frames("X", {"cashflow": {"2022-09-30": {"Operating Cash Flow": -213e6}}}, f), "operating_cash_flow")
    assert r["status"] == "match" and " + " in r["sec_tag"]


def test_a_filed_zero_has_no_percentage_and_stays_json():
    import json
    f = facts(Assets=[(None, "2023-12-31", 0, "10-K")])
    r = one(fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": 5e6}}}, f), "total_assets")
    assert r["status"] == "mismatch" and r["diff_pct"] is None
    json.loads(json.dumps(r, allow_nan=False))


def test_a_refused_directory_is_not_an_unknown_ticker():
    import types
    fx._CIK.clear()
    sys.modules["sec_xbrl_data"] = types.SimpleNamespace(get_company_tickers=lambda: {"error": "403"})
    try:
        r = fx.check("AAPL")
    finally:
        del sys.modules["sec_xbrl_data"]
    assert "unavailable" in r["error"] and "registrant" not in r["error"] and not fx._CIK


def test_nothing_filed_is_not_a_mismatch():
    # A foreign filer, or a line the company does not report: "cannot check",
    # which must never be counted as either agreement or disagreement.
    r = one(fx.check_frames("X", {"income": {"2023-12-31": {"Total Revenue": 5e9}}}, {"facts": {}}), "revenue")
    assert r["status"] == "no_sec" and "sec" not in r


def test_a_line_yahoo_does_not_report_is_not_checked():
    f = facts(Assets=[(None, "2023-12-31", 1000e6, "10-K")])
    assert fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": float("nan")}}}, f) == []


def test_summary_rates_are_over_what_could_be_compared():
    f = facts(Assets=[(None, "2023-12-31", 1000e6, "10-K"), (None, "2022-12-31", 900e6, "10-K")])
    rows = fx.check_frames("X", {"balance": {"2023-12-31": {"Total Assets": 1000e6},
                                             "2022-12-31": {"Total Assets": 990e6},
                                             "2021-12-31": {"Total Assets": 800e6}}}, f)
    s = fx.summarize([{"symbol": "X", "rows": rows}])
    a = s["lines"]["total_assets"]
    assert (a["match"], a["mismatch"], a["no_sec"], a["compared"]) == (1, 1, 1, 2)
    assert a["mismatch_rate"] == 0.5 and len(s["cases"]) == 1


def main():
    failures = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except Exception:
                traceback.print_exc()
                failures.append(name)
    if failures:
        print(f"{len(failures)} failure(s): {', '.join(failures)}")
        return 1
    print("all fundamentals cross-check tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
