#!/usr/bin/env python3
"""Statement lines replaced by the figure the company filed — and only those.

sec_statement_overlay.apply_overlay changes numbers the user will read, so the
cases here are mostly about what it must NOT do: touch a line that already
agrees, fill a cell Yahoo left empty, or repair equity that is merely
different rather than zero.
"""

import os
import sys
import time
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import sec_statement_overlay as so  # noqa: E402


def facts(**tags):
    gaap = {}
    for tag, rows in tags.items():
        gaap[tag] = {"units": {"USD": [
            ({"start": s, "end": e, "val": v, "form": "10-K", "filed": f, "accn": f} if s else
             {"end": e, "val": v, "form": "10-K", "filed": f, "accn": f}) for (s, e, v, f) in rows]}}
    return {"facts": {"us-gaap": gaap}}


def payload(**kw):
    p = {"income_statement": {}, "quarterly_income_statement": {}, "balance_sheet": {}, "ttm": {}}
    p.update(kw)
    return p


def test_yahoos_adjusted_operating_income_becomes_the_filed_one():
    p = payload(income_statement={"2024-12-31 00:00:00": {"Operating Income": 30e6, "Total Revenue": 4e9}})
    done = so.apply_overlay(p, facts(OperatingIncomeLoss=[("2024-01-01", "2024-12-31", -56e6, "2025-02-27")]))
    cells = p["income_statement"]["2024-12-31 00:00:00"]
    assert cells["Operating Income"] == -56e6 and cells["Total Revenue"] == 4e9
    assert done == [{"statement": "income_statement", "period": "2024-12-31", "line": "Operating Income",
                     "yahoo": 30e6, "sec": -56e6, "basis": "OperatingIncomeLoss"}]


def test_a_line_that_already_agrees_is_untouched():
    p = payload(income_statement={"2024-09-30": {"Operating Income": 123216e6}})
    f = facts(OperatingIncomeLoss=[("2023-10-01", "2024-09-28", 123216e6, "2024-11-01")])
    assert so.apply_overlay(p, f) == [] and p["income_statement"]["2024-09-30"]["Operating Income"] == 123216e6


def test_yahoo_matching_the_original_filing_is_not_overwritten_by_a_restatement():
    # Either is what the company filed; there is no error to correct.
    p = payload(income_statement={"2023-12-31": {"Operating Income": 500e6}})
    f = facts(OperatingIncomeLoss=[("2023-01-01", "2023-12-31", 500e6, "2024-02-01"),
                                   ("2023-01-01", "2023-12-31", 480e6, "2025-02-01")])
    assert so.apply_overlay(p, f) == []


def test_a_restatement_is_what_replaces_a_figure_that_matches_neither():
    p = payload(income_statement={"2023-12-31": {"Operating Income": 650e6}})
    f = facts(OperatingIncomeLoss=[("2023-01-01", "2023-12-31", 500e6, "2024-02-01"),
                                   ("2023-01-01", "2023-12-31", 480e6, "2025-02-01")])
    so.apply_overlay(p, f)
    assert p["income_statement"]["2023-12-31"]["Operating Income"] == 480e6


def test_a_cell_yahoo_left_empty_stays_empty():
    p = payload(income_statement={"2021-12-31": {"Total Revenue": 1e9}})
    f = facts(OperatingIncomeLoss=[("2021-01-01", "2021-12-31", 192e6, "2022-02-01")])
    assert so.apply_overlay(p, f) == []
    assert "Operating Income" not in p["income_statement"]["2021-12-31"]


def test_nothing_filed_means_nothing_changed():
    p = payload(income_statement={"2024-12-31": {"Operating Income": 30e6}})
    assert so.apply_overlay(p, {"facts": {}}) == [] and p["income_statement"]["2024-12-31"]["Operating Income"] == 30e6


def test_annual_columns_switch_together_or_not_at_all():
    # 2022 has nothing filed. Switching 2023 and 2024 alone would put filed
    # and adjusted figures side by side under one label.
    inc = {"2024-12-31": {"Operating Income": 30e6}, "2023-12-31": {"Operating Income": 67e6},
           "2022-12-31": {"Operating Income": 220e6}}
    p = payload(income_statement=inc)
    f = facts(OperatingIncomeLoss=[("2024-01-01", "2024-12-31", -56e6, "2025-02-27"),
                                   ("2023-01-01", "2023-12-31", 39e6, "2024-02-20")])
    assert so.apply_overlay(p, f) == [] and inc["2024-12-31"]["Operating Income"] == 30e6


QUARTERS = {"2025-12-31": {"Operating Income": -9e6}, "2025-09-30": {"Operating Income": 10e6},
            "2025-06-30": {"Operating Income": 5e6}, "2025-03-31": {"Operating Income": -20e6}}
FILED = [("2025-01-01", "2025-12-31", -79e6, "2026-02-09"), ("2025-01-01", "2025-09-30", -37e6, "2025-11-05"),
         ("2025-07-01", "2025-09-30", 10e6, "2025-11-05"), ("2025-04-01", "2025-06-30", 5e6, "2025-08-05"),
         ("2025-01-01", "2025-03-31", -52e6, "2025-05-05")]


def quarters():
    return {k: dict(v) for k, v in QUARTERS.items()}


def test_fourth_quarter_is_the_filed_year_minus_the_filed_nine_months():
    q = quarters()
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -14e6, "revenue": 5e9,
                                                   "quarters": sorted(q, reverse=True)})
    done = so.apply_overlay(p, facts(OperatingIncomeLoss=FILED))
    assert q["2025-12-31"]["Operating Income"] == -42e6           # -79 - (-37)
    assert q["2025-03-31"]["Operating Income"] == -52e6           # Yahoo's -20 was not the filed quarter
    assert q["2025-09-30"]["Operating Income"] == 10e6 and len(done) == 2
    # TTM is the four quarters as they now stand, and other TTM lines are not touched.
    assert p["ttm"]["operating_income"] == -42e6 + 10e6 + 5e6 - 52e6 and p["ttm"]["revenue"] == 5e9


def test_quarters_and_ttm_stay_yahoos_when_one_quarter_has_nothing_filed():
    # No nine-month figure: Q4 cannot be derived. Three filed quarters plus
    # one adjusted quarter would be a TTM on neither basis.
    q = quarters()
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -14e6, "quarters": sorted(q, reverse=True)})
    f = facts(OperatingIncomeLoss=[r for r in FILED if r[1] != "2025-09-30" or r[0] != "2025-01-01"])
    assert so.apply_overlay(p, f) == []
    assert q == QUARTERS and p["ttm"]["operating_income"] == -14e6


def test_a_derived_quarter_from_disagreeing_vintages_is_not_used():
    # The nine months were refiled recast; the year was not yet. Year minus
    # nine months is then two different numbers, and neither was filed.
    q = quarters()
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -14e6, "quarters": sorted(q, reverse=True)})
    f = facts(OperatingIncomeLoss=FILED + [("2025-01-01", "2025-09-30", -30e6, "2026-11-05")])
    assert so.apply_overlay(p, f) == [] and q == QUARTERS


def test_a_quarter_matching_its_original_filing_is_not_overwritten_by_a_recast():
    q = quarters()
    q["2025-03-31"]["Operating Income"] = -52e6
    q["2025-12-31"]["Operating Income"] = -42e6
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -79e6, "quarters": sorted(q, reverse=True)})
    f = facts(OperatingIncomeLoss=FILED + [("2025-01-01", "2025-03-31", -60e6, "2026-05-05")])
    assert so.apply_overlay(p, f) == [] and q["2025-03-31"]["Operating Income"] == -52e6


def test_equity_zero_is_replaced_by_the_filed_parent_equity():
    p = payload(balance_sheet={"2022-12-31": {"Stockholders Equity": 0.0, "Common Stock Equity": 0.0},
                               "2023-12-31": {"Stockholders Equity": 700e6}, "2021-12-31": {"Total Assets": 1e9}})
    f = facts(StockholdersEquity=[(None, "2022-12-31", 75e6, "2023-02-01"), (None, "2023-12-31", 607e6, "2024-02-01"),
                                  (None, "2021-12-31", 50e6, "2022-02-01")])
    done = so.apply_overlay(p, f)
    assert p["balance_sheet"]["2022-12-31"]["Stockholders Equity"] == 75e6
    assert p["balance_sheet"]["2022-12-31"]["Common Stock Equity"] == 0.0     # a different line: not this module's
    assert p["balance_sheet"]["2023-12-31"]["Stockholders Equity"] == 700e6   # differs, but unclassified: left alone
    assert "Stockholders Equity" not in p["balance_sheet"]["2021-12-31"]     # absent stays absent
    assert [d["period"] for d in done] == ["2022-12-31"]


def test_equity_zero_against_only_a_total_is_removed_not_guessed():
    # Total equity including minorities was filed; the parent-only figure this
    # row means was not. The zero is wrong, and the total is not the answer.
    p = payload(balance_sheet={"2022-12-31": {"Stockholders Equity": 0.0, "Total Assets": 3e8}})
    f = facts(StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest=[(None, "2022-12-31", 75.9e6, "2024-11-12")])
    done = so.apply_overlay(p, f)
    assert p["balance_sheet"]["2022-12-31"] == {"Total Assets": 3e8}
    assert done[0]["sec"] is None and done[0]["yahoo"] == 0.0


def test_the_series_form_follows_the_same_rule():
    f = facts(OperatingIncomeLoss=[("2024-01-01", "2024-12-31", -56e6, "2025-02-27"),
                                   ("2023-01-01", "2023-12-31", 39e6, "2024-02-20")])
    s = [{"date": "2023-12-31", "value": 67e6}, {"date": "2024-12-31", "value": 30e6}]
    assert so.filed_annual_operating_income(f, s) == [{"date": "2023-12-31", "value": 39e6},
                                                      {"date": "2024-12-31", "value": -56e6}]
    assert so.filed_annual_operating_income(f, s + [{"date": "2022-12-31", "value": 220e6}]) is None


def test_no_derived_quarter_in_a_year_with_discontinued_operations():
    # The 10-K recasts the year to continuing operations; the nine months are
    # not recast until next year's Q3. One filing of each, so "they all agree"
    # is trivially true — and the difference is a number nobody filed.
    q = quarters()
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -14e6, "quarters": sorted(q, reverse=True)})
    f = facts(OperatingIncomeLoss=FILED,
              IncomeLossFromDiscontinuedOperationsNetOfTax=[("2025-01-01", "2025-12-31", -30e6, "2026-02-09")])
    assert so.apply_overlay(p, f) == [] and q == QUARTERS and p["ttm"]["operating_income"] == -14e6


def test_an_older_quarter_with_nothing_filed_keeps_the_whole_table_on_one_basis():
    # Five columns, as yfinance usually returns. Switching the newest four and
    # leaving the fifth would compare a filed quarter with an adjusted one.
    q = quarters()
    q["2024-12-31"] = {"Operating Income": 3e6}
    p = payload(quarterly_income_statement=q, ttm={"operating_income": -14e6})
    assert so.apply_overlay(p, facts(OperatingIncomeLoss=FILED)) == []
    assert q["2025-03-31"]["Operating Income"] == -20e6


class _Net:
    """so's network and cache, replaced: a temp directory and a scripted SEC."""

    def __init__(self, answers):
        import tempfile
        self.answers, self.calls, self.dir = answers, [], tempfile.mkdtemp()

    def __enter__(self):
        self.saved = (so._get, so._cache_path)
        so._get = self.get
        so._cache_path = lambda *parts: os.path.join(self.dir, *parts)
        so._backoff_until = 0.0
        so._no_facts_until.clear()
        return self

    def __exit__(self, *exc):
        import shutil
        so._get, so._cache_path = self.saved
        so._backoff_until = 0.0
        so._no_facts_until.clear()
        shutil.rmtree(self.dir, ignore_errors=True)

    def get(self, url):
        self.calls.append(url)
        for key, answer in self.answers.items():
            if key in url:
                if isinstance(answer, Exception):
                    raise answer
                return answer
        raise so._NoSuch(url)


DIRECTORY = {"0": {"cik_str": 320193, "ticker": "AAPL"}, "1": {"cik_str": 1067983, "ticker": "BRK-B"}}
CONCEPT = {"units": {"USD": [{"start": "2024-01-01", "end": "2024-12-31", "val": -56e6, "form": "10-K",
                              "filed": "2025-02-27", "accn": "a"}]}}


def test_a_failing_sec_is_asked_once_not_on_every_statement():
    with _Net({"company_tickers": TimeoutError("slow")}) as net:
        a = so.facts_for("AAPL")
        b = so.facts_for("MSFT")
        assert a[0] is None and b[0] is None and a[1]["status"] == b[1]["status"] == "unavailable"
        assert len(net.calls) == 1


def test_facts_come_tag_by_tag_and_are_cached():
    with _Net({"company_tickers": DIRECTORY, "OperatingIncomeLoss": CONCEPT}) as net:
        facts_, status = so.facts_for("AAPL")
        assert status["status"] == "applied" and status["cik"] == "320193"
        assert list(facts_["facts"]["us-gaap"]) == ["OperatingIncomeLoss"]
        assert not any("companyfacts" in u for u in net.calls)      # never the multi-megabyte document
        n = len(net.calls)
        again, _ = so.facts_for("AAPL")
        assert again == facts_ and len(net.calls) == n               # second call: disk cache only


def test_a_dotted_ticker_finds_its_dashed_registrant():
    with _Net({"company_tickers": DIRECTORY}):
        assert so.cik("BRK.B") == "1067983" and so.cik("ZZZZ") is None


def test_a_registrant_reporting_none_of_the_tags_is_not_a_failure():
    with _Net({"company_tickers": DIRECTORY}) as net:      # every concept request answers 404
        facts_, status = so.facts_for("AAPL")
        assert facts_ is None and status["status"] == "no_facts"
        n = len(net.calls)
        assert so.facts_for("AAPL")[1]["status"] == "no_facts" and len(net.calls) == n   # not asked again
        assert so._backoff_until == 0.0                     # and SEC is not treated as down


def test_a_network_failure_falls_back_to_figures_cached_earlier():
    # Without this the same cell would read the filed figure on one load and
    # Yahoo's adjusted one on the next, whenever SEC happened to be slow.
    with _Net({"company_tickers": DIRECTORY, "OperatingIncomeLoss": CONCEPT}) as net:
        first, _ = so.facts_for("AAPL")
        path = so._cache_path("CIK0000320193.json")
        old = time.time() - 2 * so.FACTS_TTL_S
        os.utime(path, (old, old))                           # the cache has expired
        net.answers["OperatingIncomeLoss"] = ConnectionError("down")
        again, status = so.facts_for("AAPL")
        assert again == first and status["status"] == "applied"


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
    print("all statement overlay tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
