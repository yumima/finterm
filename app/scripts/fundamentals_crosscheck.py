#!/usr/bin/env python3
"""Cross-check Yahoo's financial statements against the company's own SEC filings.

Yahoo is a free, unofficial feed; the SEC's XBRL "company facts" are the
figures the company itself filed. For each annual period Yahoo reports, this
compares the headline lines — revenue, net income, operating income, diluted
EPS, total assets, liabilities, equity, cash, operating cash flow — with what
was filed for the same period, and says for each one:

    match      Yahoo equals a filed figure for that period (within 0.5%)
    mismatch   the company filed that line for that period and Yahoo differs
    no_sec     nothing comparable was filed (foreign filer, line not reported)

A filed line can live under several XBRL tags (revenue alone has five in
common use) and a period can be filed twice (as reported, then restated), so
Yahoo is compared with EVERY candidate; it matches if it equals any of them.
A mismatch is therefore a real disagreement with everything the company filed
under those tags — or a definition this script does not know, which is why
the batch mode prints the cases instead of only counting them.

Nothing here invents or repairs a number: it reads two sources and compares.

    fundamentals_crosscheck.py check AAPL           one company, JSON
    fundamentals_crosscheck.py batch AAPL MSFT …    mismatch rate per line
    fundamentals_crosscheck.py sample 300           the same over a spread of
                                                    the screener's universe
"""

from __future__ import annotations

import json
import math
import os
import sys
import time
from datetime import date, datetime
from typing import Any, Dict, List, Optional, Tuple

REL_TOL = 0.005          # 0.5%: rounding to thousands/millions, nothing more
EPS_ABS_TOL = 0.011      # EPS is quoted to the cent
PERIOD_SLACK_DAYS = 7    # 52/53-week fiscal years end on a weekday, not a month end

# line -> (Yahoo row aliases, statement, kind, XBRL tags in us-gaap, unit)
LINES: Dict[str, Tuple[Tuple[str, ...], str, str, Tuple[str, ...], str]] = {
    "revenue": (
        ("Total Revenue", "Operating Revenue"), "income", "flow",
        ("Revenues", "RevenueFromContractWithCustomerExcludingAssessedTax",
         "RevenueFromContractWithCustomerIncludingAssessedTax", "SalesRevenueNet",
         "RevenuesNetOfInterestExpense", "SalesRevenueGoodsNet"), "USD"),
    "net_income": (
        ("Net Income", "Net Income Common Stockholders"), "income", "flow",
        ("NetIncomeLoss", "NetIncomeLossAvailableToCommonStockholdersBasic", "ProfitLoss"), "USD"),
    "operating_income": (
        ("Operating Income",), "income", "flow", ("OperatingIncomeLoss",), "USD"),
    "eps_diluted": (
        ("Diluted EPS",), "income", "flow", ("EarningsPerShareDiluted",), "USD/shares"),
    "total_assets": (
        ("Total Assets",), "balance", "instant", ("Assets",), "USD"),
    "total_liabilities": (
        ("Total Liabilities Net Minority Interest",), "balance", "instant", ("Liabilities",), "USD"),
    "equity": (
        ("Stockholders Equity", "Common Stock Equity"), "balance", "instant",
        ("StockholdersEquity", "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest"), "USD"),
    "cash": (
        ("Cash And Cash Equivalents",), "balance", "instant",
        ("CashAndCashEquivalentsAtCarryingValue",
         "CashCashEquivalentsRestrictedCashAndRestrictedCashEquivalents"), "USD"),
    "operating_cash_flow": (
        ("Operating Cash Flow",), "cashflow", "flow",
        ("NetCashProvidedByUsedInOperatingActivities",
         "NetCashProvidedByUsedInOperatingActivitiesContinuingOperations"), "USD"),
}


def _d(s: str) -> Optional[date]:
    try:
        return datetime.strptime(str(s)[:10], "%Y-%m-%d").date()
    except ValueError:
        return None


def sec_candidates(facts: Dict[str, Any], tags: Tuple[str, ...], unit: str, kind: str,
                   period_end: date) -> List[Dict[str, Any]]:
    """Every figure the company filed for this line and this annual period."""
    out: List[Dict[str, Any]] = []
    gaap = (facts.get("facts") or {}).get("us-gaap") or {}
    for tag in tags:
        for row in ((gaap.get(tag) or {}).get("units") or {}).get(unit) or []:
            end = _d(row.get("end", ""))
            if end is None or abs((end - period_end).days) > PERIOD_SLACK_DAYS:
                continue
            if kind == "flow":
                start = _d(row.get("start", ""))
                # A fiscal YEAR: quarters and nine-month figures share the end date.
                if start is None or not (350 <= (end - start).days <= 380):
                    continue
            val = row.get("val")
            if not isinstance(val, (int, float)):
                continue
            out.append({"tag": tag, "value": float(val), "end": row.get("end"),
                        "form": row.get("form"), "filed": row.get("filed"), "accn": row.get("accn")})
    return out


# A total the company files in two parts. Yahoo reports operating cash flow
# for the whole company; a filer with a discontinued business tags the
# continuing part and the discontinued part separately and no total.
SUMS: Dict[str, Tuple[Tuple[str, ...], Tuple[str, ...]]] = {
    "operating_cash_flow": (
        ("NetCashProvidedByUsedInOperatingActivitiesContinuingOperations",),
        ("CashProvidedByUsedInOperatingActivitiesDiscontinuedOperations",)),
}


def summed_candidates(facts: Dict[str, Any], line: str, unit: str, kind: str,
                      period_end: date) -> List[Dict[str, Any]]:
    """Filed part + filed part, for the lines in SUMS. Both parts must be filed
    for the period; the sum is arithmetic on two filed figures, not an estimate."""
    if line not in SUMS:
        return []
    first, second = SUMS[line]
    out = []
    for a in sec_candidates(facts, first, unit, kind, period_end):
        for b in sec_candidates(facts, second, unit, kind, period_end):
            out.append({"tag": a["tag"] + " + " + b["tag"], "value": a["value"] + b["value"],
                        "end": a["end"], "form": a["form"], "filed": a["filed"], "accn": a["accn"]})
    return out


def compare(line: str, yahoo: float, cands: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Yahoo against every filed candidate; the closest one is reported."""
    if not cands:
        return {"status": "no_sec"}
    best = min(cands, key=lambda c: abs(c["value"] - yahoo))
    diff = yahoo - best["value"]
    if line == "eps_diluted":
        ok = abs(diff) <= EPS_ABS_TOL
    else:
        ok = abs(diff) <= REL_TOL * max(abs(best["value"]), abs(yahoo), 1.0)
    # None when the filed figure is 0: a percentage of zero is undefined, and
    # Infinity is not JSON.
    rel = round(diff / best["value"] * 100.0, 3) if best["value"] else None
    return {"status": "match" if ok else "mismatch", "sec": best["value"], "sec_tag": best["tag"],
            "sec_form": best["form"], "sec_filed": best["filed"], "diff_pct": rel,
            "candidates": len(cands)}


def check_frames(symbol: str, frames: Dict[str, Dict[str, Dict[str, float]]],
                 facts: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Pure comparison: Yahoo's annual frames ({stmt: {period: {row: value}}})
    against a company-facts document. No network — this is what the tests drive."""
    rows: List[Dict[str, Any]] = []
    for line, (aliases, stmt, kind, tags, unit) in LINES.items():
        for period, values in sorted((frames.get(stmt) or {}).items(), reverse=True):
            end = _d(period)
            if end is None:
                continue
            yahoo = next((values[a] for a in aliases
                          if isinstance(values.get(a), (int, float)) and math.isfinite(values[a])), None)
            if yahoo is None:
                continue  # Yahoo does not report the line: nothing to check
            cands = sec_candidates(facts, tags, unit, kind, end) + summed_candidates(facts, line, unit, kind, end)
            r = compare(line, float(yahoo), cands)
            rows.append({"symbol": symbol, "line": line, "period": str(period)[:10], "yahoo": float(yahoo), **r})
    return rows


# ── Network side ─────────────────────────────────────────────────────────────

class SourceUnavailable(RuntimeError):
    """A source refused or failed. Never the same thing as "nothing there"."""


def cik_for(symbol: str) -> Optional[str]:
    """CIK for a ticker from sec_directory (the app's one copy of SEC's
    directory and its "." → "-" rule). A refused directory raises — no ticker
    is ever reported as "not an SEC registrant" because the directory could
    not be read."""
    import sec_directory
    try:
        return sec_directory.cik(symbol)
    except Exception as e:  # noqa: BLE001
        raise SourceUnavailable(f"SEC ticker directory unavailable: {type(e).__name__}: {str(e)[:100]}") from e


def yahoo_frames(symbol: str) -> Dict[str, Dict[str, Dict[str, float]]]:
    import pandas as pd
    import yfinance as yf
    t = yf.Ticker(symbol)

    def conv(df) -> Dict[str, Dict[str, float]]:
        out: Dict[str, Dict[str, float]] = {}
        if df is None or df.empty:
            return out
        df = df[~df.index.duplicated(keep="first")]  # a repeated row label would make a cell a Series
        for col in df.columns:
            vals = pd.to_numeric(df[col], errors="coerce")  # numpy ints and object columns included
            out[str(col)[:10]] = {str(i): float(v) for i, v in vals.items() if pd.notna(v)}
        return out

    return {"income": conv(t.financials), "balance": conv(t.balance_sheet), "cashflow": conv(t.cashflow)}


def check(symbol: str) -> Dict[str, Any]:
    import sec_xbrl_data as sx
    try:
        cik = cik_for(symbol)
    except SourceUnavailable as e:
        return {"symbol": symbol, "error": str(e), "rows": []}
    if not cik:
        return {"symbol": symbol, "error": "no SEC registrant for this ticker", "rows": []}
    facts = sx.get_company_facts(cik)
    if not isinstance(facts, dict) or "error" in facts or "facts" not in facts:
        # A refusal is not an answer: say so rather than report "nothing filed".
        return {"symbol": symbol, "error": f"SEC company facts unavailable: {str(facts)[:120]}", "rows": []}
    try:
        frames = yahoo_frames(symbol)
    except Exception as e:  # noqa: BLE001
        return {"symbol": symbol, "error": f"Yahoo statements unavailable: {e}", "rows": []}
    if not any(frames.values()):
        # Yahoo's usual refusal is an empty frame, not an exception. Reported,
        # so the company is not counted as "checked and clean".
        return {"symbol": symbol, "cik": cik, "error": "Yahoo returned no statements", "rows": []}
    return {"symbol": symbol, "cik": cik, "rows": check_frames(symbol, frames, facts)}


def summarize(results: List[Dict[str, Any]]) -> Dict[str, Any]:
    per: Dict[str, Dict[str, int]] = {}
    mism: List[Dict[str, Any]] = []
    for res in results:
        for r in res.get("rows", []):
            c = per.setdefault(r["line"], {"match": 0, "mismatch": 0, "no_sec": 0})
            c[r["status"]] += 1
            if r["status"] == "mismatch":
                mism.append(r)
    lines = {}
    for line, c in per.items():
        compared = c["match"] + c["mismatch"]
        lines[line] = {**c, "compared": compared,
                       "mismatch_rate": round(c["mismatch"] / compared, 4) if compared else None}
    tot_c = sum(v["compared"] for v in lines.values())
    tot_m = sum(v["mismatch"] for v in lines.values())
    return {"companies": len(results), "errors": [r for r in results if r.get("error")],
            "lines": lines, "compared": tot_c, "mismatches": tot_m,
            "mismatch_rate": round(tot_m / tot_c, 4) if tot_c else None, "cases": mism}


def sample_universe(n: int) -> List[str]:
    """`n` tickers spread evenly across the screener's universe by market cap,
    so the measurement is not only the mega-caps everyone checks."""
    import finterm_paths
    base = str(finterm_paths.cache_dir("screener"))
    scans = sorted((f for f in os.listdir(base) if f.startswith("scan_v") and f.endswith(".json")),
                   key=lambda f: os.path.getmtime(os.path.join(base, f)))
    if not scans:
        raise SystemExit(f"no screener scan in {base}: open the Screener once, or pass tickers to `batch`")
    with open(os.path.join(base, scans[-1]), encoding="utf-8") as f:
        rows = json.load(f).get("rows") or {}
    ranked = sorted(rows, key=lambda s: -(rows[s].get("mcap") or rows[s].get("market_cap") or 0))
    if n >= len(ranked):
        return ranked
    step = len(ranked) / n
    return [ranked[int(i * step)] for i in range(n)]


def main(argv: List[str]) -> int:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    if len(argv) < 2:
        print(__doc__)
        return 2
    # SEC refuses a client that declares no contact. The app passes the user's
    # configured one in the environment; run by hand, read it from the keyring
    # as the weekly brief does. It goes to SEC only, and must be set BEFORE
    # sec_xbrl_data is imported (its session reads it at import).
    if not os.environ.get("FINTERM_CONTACT_EMAIL"):
        try:
            from weekly_brief_core import CONTACT_KEY, contact_from_keyring
            c = contact_from_keyring()
            if c:
                os.environ[CONTACT_KEY] = c
        except Exception:  # noqa: BLE001
            pass
        if not os.environ.get("FINTERM_CONTACT_EMAIL"):
            print("note: no SEC contact email configured (Settings › Credentials › Contact email); "
                  "SEC will refuse the requests.", file=sys.stderr)
    cmd = argv[0]
    if cmd not in ("check", "batch", "sample") or (cmd == "sample" and not argv[1].isdigit()):
        print(__doc__)
        return 2
    if cmd == "check":
        print(json.dumps(check(argv[1].upper()), indent=1))
        return 0
    syms = sample_universe(int(argv[1])) if cmd == "sample" else [s.upper() for s in argv[1:]]
    results = []
    for i, s in enumerate(syms):
        results.append(check(s))
        if (i + 1) % 25 == 0:
            print(f"… {i + 1}/{len(syms)}", file=sys.stderr, flush=True)
        time.sleep(0.15)  # SEC asks for at most 10 requests a second
    print(json.dumps(summarize(results), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
