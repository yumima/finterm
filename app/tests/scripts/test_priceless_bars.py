#!/usr/bin/env python3
r"""A bar with no prices must never become a bar priced at zero.

yfinance emits an all-NaN OHLC row for a session it has no prices for yet —
typically today's bar before the feed fills it in, occasionally the first bar
of a long window. Volume is usually present on that row, so it looks like a
bar and is not one.

The path that makes this dangerous rather than merely absent:

    yfinance        NaN
    _sanitize_for_json  ->  JSON null      (it MUST: bare NaN is not valid
                                            JSON and QJsonDocument::fromJson
                                            rejects the whole document)
    QJsonValue::toDouble()  ->  0.0        (silently, no error, no warning)

So "no price" becomes "priced at zero". One such bar sets a chart's y-range
low to 0; the 6% margin then makes it negative; the log axis clamps to 1e-9
and spans 1e-9…hi, which flattens every real candle into the top ~1% of the
plot. The observed symptom was an MSFT 3M chart drawn as a flat line while
1M and 6M — separate yfinance calls, separate cache entries, only some of
them carrying the bad row — looked fine.

This was a REGRESSION, not a new vendor behaviour. Before _sanitize_for_json
existed the same NaN reached the wire as the bare token `NaN`, Qt rejected
the document, and the fetch failed loudly. The sanitiser fixed a real
serialisation bug and, in doing so, converted a loud parse error into a
silent zero. Nothing on the C++ side was ever taught that null means "no
value".

Two things are guarded here, because fixing instances is what failed before:

  1. BEHAVIOUR — _has_prices accepts real prints and rejects absent ones.
     Critically it must NOT be a positivity test: ^IRX closed at 0.000
     through much of 2020-2021 and CL=F settled at -37.63 on 2020-04-20,
     and both are on this app's own watchlists. A `> 0` rule would delete
     real history rather than repair it. The first fix for this bug had
     exactly that rule.

  2. STRUCTURE — every loop in the daemon that builds an OHLC row out of a
     DataFrame must consult _has_prices, and the C++ boundary that turns
     JSON into bars (services/markets/Bars.h, read by every history path
     through MarketDataService::fetch_bars and the market:history topic)
     must consult row_has_prices. A new unguarded builder
     fails here rather than shipping a chart that silently reads zero.

The structural half is a pure text scan: no build, no network, no pandas.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "..", "scripts")
SRC = os.path.join(HERE, "..", "..", "src")
DAEMON = os.path.join(SCRIPTS, "yfinance_data.py")
BARS = os.path.join(SRC, "services", "markets", "Bars.h")
MDS = os.path.join(SRC, "services", "markets", "MarketDataService.cpp")

failures = []


def check(cond, label, detail=""):
    if cond:
        print(f"PASS : {label}")
    else:
        print(f"FAIL : {label}" + (f"\n       {detail}" if detail else ""))
        failures.append(label)


# ── 1. Structure: no unguarded OHLC row builder ──────────────────────────────
#
# Matched on the row dict itself rather than on `iterrows`, because the defect
# is "a row was built from fields that may be NaN", not "a frame was iterated".

def check_daemon_builders():
    src = open(DAEMON, encoding="utf-8").read()

    if "def _has_prices(" not in src:
        check(False, "_has_prices exists in yfinance_data.py",
              "the daemon-side guard is gone; every consumer is exposed again")
        return

    # Every dict literal that pulls all four OHLC fields off a DataFrame row.
    builder = re.compile(
        r"""\{[^{}]*?["']open["']\s*:[^{}]*?row\[['"]Open['"]\][^{}]*?\}""",
        re.DOTALL,
    )
    found = list(builder.finditer(src))
    check(bool(found), "found the daemon's OHLC row builders",
          "the scan matched nothing — the pattern has drifted and this test "
          "is now blind; fix the pattern rather than deleting the test")

    for m in found:
        # The guard must appear in the enclosing loop, above the dict.
        line_no = src.count("\n", 0, m.start()) + 1
        window = src[max(0, m.start() - 900):m.start()]
        label = f"OHLC row builder at yfinance_data.py:{line_no} is guarded"
        check("_has_prices(" in window, label,
              "this loop appends an OHLC row without consulting _has_prices; "
              "an all-NaN vendor row will reach the wire as nulls and be read "
              "as 0.0 on the C++ side")


# ── 2. Structure: the C++ boundary still filters ─────────────────────────────
#
# The daemon fix stops new payloads, but candle arrays cached before it (24h
# SWR, plus the per-symbol disk cache) are re-parsed on every hit, and the
# technicals / TALIpp paths never build Candles at all — they forward the raw
# array back to Python. Each entry point needs the filter.

def check_cpp_boundary():
    bars = open(BARS, encoding="utf-8").read()
    mds = open(MDS, encoding="utf-8").read()

    check("inline bool row_has_prices(" in bars,
          "row_has_prices exists in Bars.h",
          "the C++ boundary guard is gone; cached null-bearing payloads will "
          "be read as zero-priced bars again")

    m = re.search(r"inline QVector<HistoryPoint> parse\(.*?\n\}", bars, re.DOTALL)
    check(m is not None, "found bars::parse",
          "the scan lost bars::parse; fix the pattern rather than deleting the test")
    if m:
        check("row_has_prices(" in m.group(0),
              "bars::parse filters price-less rows",
              "a null OHLC field will become a 0.0 price on every chart")

    # Every history reply is filtered BEFORE it is cached or handed out — the
    # rows reach the technicals / TALIpp compute in Python as-is.
    m = re.search(r"void MarketDataService::fetch_bars\(.*?\n\}", mds, re.DOTALL)
    check(m is not None, "found fetch_bars",
          "the scan lost fetch_bars; fix the pattern, do not delete it")
    if m:
        check("bars::priced_rows(" in m.group(0),
              "fetch_bars keeps priced rows only (cache and the rows handed to Python)",
              "pandas reads a null close as NaN and every rolling window "
              "covering that bar comes back blank")
    check("ph.rows = bars::priced_rows(" in mds,
          "the market:history topic keeps priced rows only",
          "the hub's history path would publish (and cache) null bars")

    # The rule that must NOT come back: a positivity test deletes real bars.
    m = re.search(r"inline bool row_has_prices\(.*?\n\}", bars, re.DOTALL)
    if m:
        check("<= 0" not in m.group(0) and "> 0.0" not in m.group(0),
              "row_has_prices is a finiteness test, not a positivity test",
              "^IRX closed at 0.000 and CL=F settled at -37.63; a `> 0` rule "
              "silently deletes those bars from the chart")


# ── 3. Behaviour: _has_prices itself ─────────────────────────────────────────

def check_behaviour():
    sys.path.insert(0, SCRIPTS)
    try:
        import pandas  # noqa: F401
    except ImportError:
        # 77 = "the app venv isn't present" — reported as skipped rather than
        # passed, so a missing dependency can never mask a regression.
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

    nan = float("nan")
    cases = [
        # (values,                        expected, why)
        ((100.0, 101.0, 99.0, 100.5),     True,  "an ordinary equity bar"),
        ((nan, nan, nan, nan),            False, "the all-NaN row (the bug)"),
        ((None, None, None, None),        False, "JSON nulls round-tripped back"),
        ((100.0, 101.0, nan, 100.5),      False, "one missing field is enough"),
        ((float("inf"),) * 4,             False, "infinities are not prices"),
        (("", "", "", ""),                False, "empty strings are not prices"),
        # The half that must never become a positivity test:
        ((0.0, 0.0, 0.0, 0.0),            True,  "^IRX at 0.000 is REAL data"),
        ((-37.63, -36.0, -40.3, -37.63),  True,  "CL=F 2020-04-20 is REAL data"),
    ]
    for values, expected, why in cases:
        got = yd._has_prices(*values)
        check(got is expected, f"_has_prices{values} is {expected} — {why}",
              f"got {got}")
    return True


def main():
    check_daemon_builders()
    check_cpp_boundary()
    ran_behaviour = check_behaviour()

    print()
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        return 1
    if ran_behaviour:
        print("All price-less-bar guards hold.")
        return 0
    # 77 = ctest SKIPPED. The structural half ran and fails loudly above if
    # violated, but _has_prices' own behaviour was not exercised — a green
    # "Passed" here would overstate what was checked.
    print("Structural guards hold; behaviour checks did NOT run — reporting as skipped.")
    return 77


if __name__ == "__main__":
    sys.exit(main())
