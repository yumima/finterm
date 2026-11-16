#!/usr/bin/env python3
"""ER Report data shaping: filing excerpts (report_filings.py) and the
quarterly rows (yfinance_data._report_quarters).

Pinned: page furniture is dropped, a section is cut on a paragraph, a
by-reference stub is no section, a 10-K's own risk summary is preferred, and a
line a company does not report is None (never 0) in its quarter.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas as pd
except ImportError:
    sys.exit(77)

import report_filings as rf  # noqa: E402

FAILS = []


def check(name, got, want):
    if got != want:
        FAILS.append(f"{name}: got {got!r}, want {want!r}")


check("clean", rf.clean("Item 1. Business\n\n\n 12 \nTable of Contents\nWe  make\tthings.\n"),
      "Item 1. Business\n\nWe make things.")
check("clean none", rf.clean(None), "")

text = ("Para one. " * 20).strip() + "\n\n" + ("Para two. " * 20).strip() + "\n\n" + ("Para three. " * 20).strip()
body, cut = rf.cut(text, 420)
check("cut flag", cut, True)
check("cut ends on a paragraph", body, ("Para one. " * 20).strip() + "\n\n" + ("Para two. " * 20).strip())
check("no cut", rf.cut("short", 100), ("short", False))

check("stub is no section", rf.section("Incorporated by reference to pages 45-130.", "mdna"), None)

long_risk = ("Item 1A. Risk Factors\n\nRisk Factors Summary\n\n" + "• A summarised risk that matters.\n\n" * 60 +
             "Risk Factors\n\n" + "The long form of every risk. " * 400)
sec = rf.section(long_risk, "risk_factors")
check("risk summary preferred", sec["text"].startswith("Risk Factors Summary"), True)
check("risk long form left out", "The long form" in sec["text"], False)
check("risk marked truncated", sec["truncated"], True)

mdna = ("Forward-Looking Statements\n\n" + "This report contains forward-looking statements. " * 30 +
        "\n\nOverview\n\n" + "Revenue rose on data center demand. " * 80)
sec = rf.section(mdna, "mdna")
check("mdna starts at overview", sec["text"].startswith("Overview"), True)

# ── revenue breakdown from XBRL facts ────────────────────────────────────────
check("humanize", rf.humanize("nvda:ProfessionalVisualizationMember"), "Professional Visualization")
check("humanize acronym", rf.humanize("nvda:OEMAndOtherMember"), "OEM And Other")

# Data Center = Compute + Networking; charting all three would count it twice.
leaves = rf.drop_subtotals([("Data Center", 193.7), ("Compute", 162.4), ("Networking", 31.3), ("Gaming", 16.0)])
check("subtotal dropped", [l for l, _ in leaves], ["Compute", "Networking", "Gaming"])
check("equal rows are not a sum", len(rf.drop_subtotals([("A", 10.0), ("B", 10.0)])), 2)
# A breakdown that already sums to revenue has no subtotal in it, even though
# 60 = 35 + 25 here: with the total known, nothing is removed.
whole = [("A", 60.0), ("B", 35.0), ("C", 25.0)]
check("sums to revenue: all kept", len(rf.drop_subtotals(whole, total=120.0)), 3)
check("over revenue: the subtotal goes", [l for l, _ in rf.drop_subtotals(whole, total=60.0)], ["B", "C"])
check("non-positive rows dropped", rf.drop_subtotals([("A", 5.0), ("B", 0.0), ("C", None)]), [("A", 5.0)])


def fact(concept, value, start, end, **dims):
    row = {"concept": concept, "numeric_value": value, "period_start": start, "period_end": end,
           "dim_srt_ProductOrServiceAxis": None, "dim_srt_StatementGeographicalAxis": None,
           "dim_srt_MajorCustomersAxis": None, "dim_us-gaap_ConcentrationRiskByBenchmarkAxis": None,
           "dim_us-gaap_StatementBusinessSegmentsAxis": None}
    row.update({f"dim_{k}": v for k, v in dims.items()})
    return row


R = "us-gaap:Revenues"
facts = pd.DataFrame([
    fact(R, 200.0, "2025-01-27", "2026-01-25"),
    fact(R, 120.0, "2024-01-29", "2025-01-26"),
    fact(R, 60.0, "2025-10-27", "2026-01-25"),                                  # a quarter: not a year
    fact(R, 150.0, "2025-01-27", "2026-01-25", srt_ProductOrServiceAxis="x:DataCenterMember"),
    fact(R, 110.0, "2025-01-27", "2026-01-25", srt_ProductOrServiceAxis="x:ComputeMember"),
    fact(R, 40.0, "2025-01-27", "2026-01-25", srt_ProductOrServiceAxis="x:NetworkingMember"),
    fact(R, 50.0, "2025-01-27", "2026-01-25", srt_ProductOrServiceAxis="x:GamingMember"),
    fact(R, 70.0, "2024-01-29", "2025-01-26", srt_ProductOrServiceAxis="x:ComputeMember"),
    fact(R, 15.0, "2025-10-27", "2026-01-25", srt_ProductOrServiceAxis="x:GamingMember"),
    # cut two ways at once: belongs to neither chart
    fact(R, 30.0, "2025-01-27", "2026-01-25", srt_ProductOrServiceAxis="x:GamingMember",
         srt_StatementGeographicalAxis="country:US"),
    fact(R, 90.0, "2025-01-27", "2026-01-25", srt_StatementGeographicalAxis="country:US"),
    fact("us-gaap:ConcentrationRiskPercentage1", 0.22, "2025-01-27", "2026-01-25",
         srt_MajorCustomersAxis="x:CustomerOneMember",
         **{"us-gaap_ConcentrationRiskByBenchmarkAxis": "us-gaap:SalesRevenueNetMember"}),
    fact("us-gaap:ConcentrationRiskPercentage1", 0.25, "2025-01-27", "2026-01-25",
         srt_MajorCustomersAxis="x:CustomerOneMember",
         **{"us-gaap_ConcentrationRiskByBenchmarkAxis": "us-gaap:AccountsReceivableMember"}),
    # the fourth quarter's share, under the same period end: not the year's
    fact("us-gaap:ConcentrationRiskPercentage1", 0.31, "2025-10-27", "2026-01-25",
         srt_MajorCustomersAxis="x:CustomerTwoMember",
         **{"us-gaap_ConcentrationRiskByBenchmarkAxis": "us-gaap:SalesRevenueNetMember"}),
    fact("us-gaap:ConcentrationRiskPercentage1", 0.76, "2025-01-27", "2026-01-25",
         srt_MajorCustomersAxis="x:EndCustomersMember", srt_StatementGeographicalAxis="country:TW",
         **{"us-gaap_ConcentrationRiskByBenchmarkAxis": "us-gaap:SalesRevenueNetMember"}),
])
bd = rf.revenue_breakdown(facts, rf.humanize)
check("one axis charted (geography has a single row)", [b["axis"] for b in bd], ["product"])
check("breakdown rows", [(r["label"], r["value"], r["prior"]) for r in bd[0]["rows"]],
      [("Compute", 110.0, 70.0), ("Gaming", 50.0, None), ("Networking", 40.0, None)])
check("breakdown years", (bd[0]["period"], bd[0]["prior_period"]), ("2026-01-25", "2025-01-26"))
check("coverage", bd[0]["coverage_pct"], 100.0)
check("empty facts", rf.revenue_breakdown(pd.DataFrame(), rf.humanize), [])
# Segment revenue tagged "operating segments" on the consolidation axis is still
# the segment's revenue; the eliminations row on that axis is not.
seg = pd.DataFrame([
    fact(R, 100.0, "2025-01-01", "2025-12-31"),
    fact(R, 60.0, "2025-01-01", "2025-12-31", **{"us-gaap_StatementBusinessSegmentsAxis": "x:GovernmentMember",
                                               "srt_ConsolidationItemsAxis": "us-gaap:OperatingSegmentsMember"}),
    fact(R, 40.0, "2025-01-01", "2025-12-31", **{"us-gaap_StatementBusinessSegmentsAxis": "x:CommercialMember",
                                               "srt_ConsolidationItemsAxis": "us-gaap:OperatingSegmentsMember"}),
    fact(R, -7.0, "2025-01-01", "2025-12-31", **{"us-gaap_StatementBusinessSegmentsAxis": "x:CommercialMember",
                                               "srt_ConsolidationItemsAxis": "us-gaap:IntersegmentEliminationMember"}),
])
sb = rf.revenue_breakdown(seg, rf.humanize)
check("labelled segments are taken", [(b["axis"], [(r["label"], r["value"]) for r in b["rows"]]) for b in sb],
      [("segment", [("Government", 60.0), ("Commercial", 40.0)])])

# An axis that does not reconcile to revenue is carrying two cuts at once: not charted.
mixed = pd.concat([facts, pd.DataFrame([fact(R, 80.0, "2025-01-27", "2026-01-25",
                                             srt_ProductOrServiceAxis="x:UnrelatedCutMember")])])
check("unreconciled axis omitted", rf.revenue_breakdown(mixed, rf.humanize), [])
cc = rf.customer_concentration(facts, rf.humanize)
check("only revenue concentration, only uncut", [(c["label"], c["pct"]) for c in cc], [("Customer One", 22.0)])

try:
    import yfinance_data as y
except Exception:  # yfinance not importable outside the venv
    sys.exit(77)

cols = [pd.Timestamp("2026-09-30"), pd.Timestamp("2026-06-30")]
income = pd.DataFrame({cols[0]: {"Total Revenue": 100.0, "Net Income": 20.0, "Diluted EPS": 1.5},
                       cols[1]: {"Total Revenue": 90.0, "Net Income": float("nan"), "Diluted EPS": 1.2}})
cash = pd.DataFrame({cols[0]: {"Operating Cash Flow": 30.0, "Capital Expenditure": -5.0}})
rows = y._report_quarters(income, cash)
check("newest first", [r["period"] for r in rows], ["2026-09-30", "2026-06-30"])
check("value", rows[0]["revenue"], 100.0)
check("capex sign kept", rows[0]["capex"], -5.0)
check("absent line is None", rows[0]["gross_profit"], None)
check("nan is None", rows[1]["net_income"], None)
check("missing statement is None", rows[1]["ocf"], None)
check("no frames", y._report_quarters(None, None), [])
fallback = pd.DataFrame({cols[0]: {"Diluted EPS": float("nan"), "Basic EPS": 1.4}})
check("a NaN alias falls through to the next", y._report_quarters(fallback, None)[0]["eps"], 1.4)

from datetime import date as _date  # noqa: E402
T = _date(2026, 11, 16)
ft = y.latest_firm_targets([
    (_date(2026, 11, 2), "B. Riley", 75.0, 100.0, "Lowers", "Buy"),
    (_date(2026, 11, 6), "B. Riley", 70.0, 75.0, "Lowers", "Buy"),      # the same firm, later: this one counts
    (_date(2026, 5, 7), "JP Morgan", 50.0, 42.0, "Raises", "Neutral"),
    (_date(2024, 8, 12), "Benchmark", 12.0, 16.0, "Lowers", "Buy"),     # over a year old: no longer coverage
    (_date(2026, 9, 28), "B of A", 60.0, 0.0, "Announces", "Buy"),      # a first target has no prior
    (_date(2026, 10, 1), "No Target Co", float("nan"), None, "", "Buy"),
    (_date(2026, 12, 1), "Future Co", 10.0, None, "", ""),               # dated after today: not believed
], T)
check("latest per firm, newest first", [(r["firm"], r["date"], r["target"]) for r in ft],
      [("B. Riley", "2026-11-06", 70.0), ("B of A", "2026-09-28", 60.0), ("JP Morgan", "2026-05-07", 50.0)])
check("no prior on a first target", ft[1]["prior"], None)
# A note stamped 00:30 UTC was published the evening before in New York, after the close.
check("an evening note is dated in New York", y.analyst_action_when("2026-11-17 00:30:00"), (_date(2026, 11, 16), True))
check("a pre-market note", y.analyst_action_when("2026-11-06 12:27:41"), (_date(2026, 11, 6), False))
# A pre-market cut was published before that day's fall: it is set against the session before.
check("pre-market: the close before", ft[0]["set_against"], "2026-11-05")
ev = y.latest_firm_targets([(_date(2026, 11, 6), "Evening Co", 50.0, 60.0, "Lowers", "Buy", True)], T)
check("after the close: that day's close", ev[0]["set_against"], "2026-11-06")
check("no rows", y.latest_firm_targets([], T), [])
cl = {"2026-11-05": 30.0, "2026-11-06": 35.0, "2026-11-09": 33.0, "2026-11-10": float("nan")}
check("a weekend target is set against Friday's close", y.close_on_or_before(cl, "2026-11-07"), ("2026-11-06", 35.0))
check("no session that early", y.close_on_or_before(cl, "2026-11-01"), None)
check("an unusable close is not a close", y.close_on_or_before(cl, "2026-11-10"), ("2026-11-09", 33.0))

ranked = ["A", "B", "C", "D", "E", "F", "G"]
check("neighbours either side", y.nearest_in_ranking(ranked, "D", 4), ["C", "E", "B", "F"])
check("at the top", y.nearest_in_ranking(ranked, "A", 3), ["B", "C", "D"])
check("not ranked", y.nearest_in_ranking(ranked, "Z", 2), ["A", "B"])
check("alone", y.nearest_in_ranking(["A"], "A", 3), [])

if FAILS:
    print("\n".join(FAILS))
    sys.exit(1)
print("ok")
