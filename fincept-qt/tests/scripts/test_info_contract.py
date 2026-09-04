#!/usr/bin/env python3
r"""The `info` payload's two contracts: absent is not zero, and yields are fractions.

yfinance's `info` dict is routinely PARTIAL. Measured across 231 cached
payloads in this app's own cache:

    beta                        absent in 148 / 231
    revenue_growth              absent in 114 / 231
    float_shares                absent in  82 / 231
    market_cap                  absent in  76 / 231
    held_percent_institutions   absent in  76 / 231
    gross_margins               absent in  72 / 231
    profit_margins              absent in  72 / 231

Every absent field is sent as JSON null by _sanitize_for_json, and a bare
QJsonValue::toDouble() reads null back as 0.0 without complaint. So roughly a
third of the time the research panels stated, with the same confidence as a
real figure, that a company had a 0.00% gross margin, 0 float and 0% growth.

TWO invariants are guarded here.

  1. ABSENT IS NOT ZERO.  StockInfo's numeric fields default to kUnknown
     (NaN) and parse_info reads them through num(), which yields NaN for a
     null. NaN fails every `> 0` test the panels already do, formats as the
     em-dash placeholder, and Qt writes it back out as JSON null — so the MCP
     tools tell an agent "unknown" rather than inventing a zero.

     The rule is not new; NumberFormat.h has stated it all along: "Missing /
     unavailable: ONE sentinel, the em-dash. A real 0.0 is NOT missing —
     callers must gate on NaN / optional, never on == 0.0." parse_info simply
     never gave callers anything to gate on.

     Note the direction that gating on `!= 0` gets wrong once NaN arrives:
     NaN != 0 is TRUE, so the old AI-prompt guards would have printed a
     literal "nan" into the model's prompt.

  2. DIVIDEND YIELD IS A FRACTION.  Yahoo changed dividendYield to a
     PERCENTAGE in early 2025 (0.73 means 0.73%) and yfinance passes
     quoteSummary through untouched — confirmed on both pinned versions,
     0.2.66 and 1.3.0, neither of which mentions the field. Every C++
     consumer multiplies by 100 to display it, so MSFT rendered a 73.00%
     dividend yield and the portfolio heatmap's weighted yield was 100x.

     Checked field by field against a known ticker: margins, ROE, ROA,
     revenue/earnings growth, insider/institutional holdings and short
     percent of float are ALL fractions. dividendYield alone is a percentage.
     That is why the conversion is one field and not a sweep — and why this
     test pins it, so a future "consistency" cleanup cannot apply it wider.

Structural checks are a pure text scan: no build, no network, no pandas.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "..", "scripts")
SRC = os.path.join(HERE, "..", "..", "src")
DAEMON = os.path.join(SCRIPTS, "yfinance_data.py")
RELMAP = os.path.join(SCRIPTS, "relationship_map.py")
SERVICE = os.path.join(SRC, "services", "equity", "EquityResearchService.cpp")
MODELS = os.path.join(SRC, "services", "equity", "EquityResearchModels.h")
OVERVIEW = os.path.join(SRC, "screens", "equity_research", "EquityOverviewTab.cpp")
ANALYSIS = os.path.join(SRC, "screens", "equity_research", "EquityAnalysisTab.cpp")
AITAB = os.path.join(SRC, "screens", "equity_research", "EquityAiTab.cpp")

failures = []


def check(cond, label, detail=""):
    if cond:
        print(f"PASS : {label}")
    else:
        print(f"FAIL : {label}" + (f"\n       {detail}" if detail else ""))
        failures.append(label)


def read(p):
    return open(p, encoding="utf-8").read()


# ── 1. Dividend yield is converted at every producer ─────────────────────────

def check_dividend_producers():
    daemon = read(DAEMON)
    check("def _dividend_yield_fraction(" in daemon,
          "_dividend_yield_fraction exists",
          "the percentage->fraction conversion is gone; every display site "
          "multiplies by 100 and will overstate every yield 100x")

    # Every place the raw Yahoo key is read must go through the conversion.
    for m in re.finditer(r'^.*info\.get\(\s*[\'"]dividendYield[\'"].*$', daemon, re.M):
        line = m.group(0)
        line_no = daemon.count("\n", 0, m.start()) + 1
        check("_dividend_yield_fraction(" in line,
              f"yfinance_data.py:{line_no} converts dividendYield",
              f"raw passthrough: {line.strip()}")

    relmap = read(RELMAP)
    for m in re.finditer(r'^.*info\.get\(\s*[\'"]dividendYield[\'"].*$', relmap, re.M):
        line_no = relmap.count("\n", 0, m.start()) + 1
        check("/ 100" in m.group(0),
              f"relationship_map.py:{line_no} converts dividendYield",
              f"raw passthrough: {m.group(0).strip()}")

    # The conversion must stay scoped to this ONE field. Every other
    # percentage-like field from Yahoo is already a fraction; applying the
    # same /100 to them would silently divide real data by 100.
    for field in ("grossMargins", "profitMargins", "returnOnEquity",
                  "revenueGrowth", "heldPercentInstitutions",
                  "shortPercentOfFloat"):
        hits = [l for l in daemon.splitlines()
                if field in l and "_dividend_yield_fraction" in l]
        check(not hits,
              f"{field} is NOT run through the dividend conversion",
              f"it is already a fraction; /100 would understate it 100x: {hits}")


# ── 2. Absent is not zero ────────────────────────────────────────────────────

def check_absent_is_not_zero():
    svc = read(SERVICE)
    check("double num(const QJsonObject& o, const char* key)" in svc,
          "num() helper exists in EquityResearchService.cpp",
          "the NaN-for-absent reader is gone; nulls become 0.0 again")

    m = re.search(r"StockInfo EquityResearchService::parse_info.*?\n\}", svc, re.DOTALL)
    check(m is not None, "found parse_info",
          "the scan lost parse_info; fix the pattern rather than deleting this")
    if m:
        raw = re.findall(r'o\["[a-z0-9_]+"\]\.toDouble\(\)', m.group(0))
        check(not raw,
              "parse_info reads no numeric field with a bare toDouble()",
              f"these would turn a vendor null into a confident 0.0: {raw}")

    models = read(MODELS)
    m = re.search(r"struct StockInfo \{.*?\n\};", models, re.DOTALL)
    check(m is not None, "found the StockInfo struct")
    if m:
        zero_defaults = re.findall(r"double\s+(\w+)\s*=\s*0\.0\s*;", m.group(0))
        check(not zero_defaults,
              "StockInfo numeric fields default to kUnknown, not 0.0",
              "a default-constructed StockInfo is what the panels render "
              f"before the fetch resolves: {zero_defaults}")

    # The formatters must decide on absence, not on zero.
    for path, names in ((OVERVIEW, ("fmt_pct", "fmt_large", "fmt_price")),
                        (ANALYSIS, ("fmt", "fmt_large", "fmt_pct"))):
        src = read(path)
        base = os.path.basename(path)
        cls = base.replace(".cpp", "")
        for fn in names:
            m = re.search(rf"QString {cls}::{fn}\(double v[^)]*\)[^{{]*\{{(.*?)\n\}}",
                          src, re.DOTALL)
            check(m is not None, f"found {base}::{fn}",
                  "the scan lost this formatter; fix the pattern")
            if m:
                body = m.group(1)
                check("isfinite" in body,
                      f"{base}::{fn} gates on NaN",
                      "gating on == 0.0 cannot tell an absent value from a "
                      "real zero, and shows one as the other")

    # QuoteData shares fmt_price with StockInfo, so it must share the
    # sentinel. While it still used 0.0-as-missing, moving fmt_price onto a
    # NaN gate turned "—" into a fabricated "$0.00" for any symbol whose
    # open/high/low the vendor omits — indices, funds, FX, anything pre-open.
    m = re.search(r"struct QuoteData \{.*?\n\};", models, re.DOTALL)
    check(m is not None, "found the QuoteData struct")
    if m:
        zero_defaults = re.findall(r"double\s+(\w+)\s*=\s*0\.0\s*;", m.group(0))
        check(not zero_defaults,
              "QuoteData numeric fields default to kUnknown, not 0.0",
              "it shares fmt_price with StockInfo; two sentinels in one "
              f"formatter means one of them is rendered wrong: {zero_defaults}")

    m = re.search(r"QuoteData EquityResearchService::parse_quote.*?\n\}", svc, re.DOTALL)
    check(m is not None, "found parse_quote")
    if m:
        # "timestamp" is exempt: it is a qint64 epoch second, not a measured
        # quantity, and casting a NaN to an integer is undefined. Its absence
        # is already carried by QuoteData::valid.
        raw = [f for f in re.findall(r'o\["([a-z_]+)"\]\.toDouble\(\)', m.group(0))
               if f != "timestamp"]
        check(not raw,
              "parse_quote reads no measured field with a bare toDouble()",
              f"both quote producers emit null for these: {raw}")

    # The daemon fills missing strings with the literal "N/A"; unscrubbed,
    # every isEmpty() check reads it as a real value and the AI prompt states
    # "Company: N/A" as a fact.
    check("QString str(const QJsonObject& o, const char* key)" in svc,
          "str() helper scrubs the daemon's \"N/A\" filler")
    m = re.search(r"StockInfo EquityResearchService::parse_info.*?\n\}", svc, re.DOTALL)
    if m:
        raw = re.findall(r'o\["[a-z_]+"\]\.toString\(\)', m.group(0))
        check(not raw,
              "parse_info reads no string field with a bare toString()",
              f"these would surface the literal \"N/A\" as a value: {raw}")

    # NaN != 0 is TRUE — an `!= 0` gate on a StockInfo double now lets a NaN
    # through to be printed. In the AI tab that text goes into a model prompt.
    ai = read(AITAB)
    bad = re.findall(r"info_\.(\w+)\s*!=\s*0\b", ai)
    check(not bad,
          "EquityAiTab gates on isfinite, not `!= 0`",
          f"NaN != 0 is true, so these would print a literal 'nan' into the "
          f"model's prompt: {bad}")


# ── 3. Behaviour of the conversion itself ────────────────────────────────────

def check_behaviour():
    sys.path.insert(0, SCRIPTS)
    try:
        import pandas  # noqa: F401
    except ImportError:
        print("SKIP : behaviour checks (pandas not available)")
        return False
    import types
    for name in ("yfinance", "numpy", "requests", "curl_cffi"):
        if name not in sys.modules:
            try:
                __import__(name)
            except ImportError:
                sys.modules[name] = types.ModuleType(name)
    try:
        import yfinance_data as yd
    except Exception as exc:  # noqa: BLE001
        print(f"SKIP : behaviour checks (yfinance_data not importable: {exc})")
        return False

    f = yd._dividend_yield_fraction
    # Real observed values, with the percentage each actually represents.
    cases = [(0.73, 0.0073, "MSFT"), (0.33, 0.0033, "AAPL"), (2.75, 0.0275, "HD"),
             (2.53, 0.0253, "LMT"), (0.08, 0.0008, "TXT")]
    for raw, want, sym in cases:
        got = f(raw)
        check(got is not None and abs(got - want) < 1e-12,
              f"_dividend_yield_fraction({raw}) -> {want} ({sym})", f"got {got}")
    for absent in (None, float("nan"), float("inf"), "", "abc"):
        check(f(absent) is None,
              f"_dividend_yield_fraction({absent!r}) is None (absent stays absent)",
              f"got {f(absent)!r}")
    return True


def main():
    check_dividend_producers()
    check_absent_is_not_zero()
    ran = check_behaviour()
    print()
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for x in failures:
            print(f"  - {x}")
        return 1
    if ran:
        print("info contract holds.")
        return 0
    # 77 = ctest SKIPPED. The structural half ran and would have FAILED above
    # if violated, but the half that pins the actual arithmetic (0.73 ->
    # 0.0073) did not — reporting that as a pass would let an inverted
    # conversion ship green on any box without pandas, which is this one.
    print("Structural checks hold; behaviour checks did NOT run — reporting as skipped.")
    return 77


if __name__ == "__main__":
    sys.exit(main())
