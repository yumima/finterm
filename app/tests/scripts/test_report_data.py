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

if FAILS:
    print("\n".join(FAILS))
    sys.exit(1)
print("ok")
