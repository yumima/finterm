#!/usr/bin/env python3
"""End-to-end tests for scripts/equity_talipp.py — the CLI shim the TALIpp
tab's compute path runs.

The shim delegates to scripts/Analytics/talipp_wrapper (the talipp-backed
implementations), so these tests drive the real subprocess contract the C++
side depends on: the tab's own indicator ids, OHLCV-on-argv in, a single
aligned {values, timestamps} series out, and the error-to-stderr rule that
lets a failure reach the UI instead of dying in stdout.

Semantic pins carried over from the previous hand-rolled implementation's
test (the inversions that once made the tab report the opposite of the
market):

  - RSI over a monotonic uptrend reads 100 (was 0 — maximally oversold).
  - MFI over pure accumulation reads 100 (same inversion).
  - A genuine 0.0 stochastic %K (close exactly at the window low) survives
    into the output — zero is a value, not an absence.

Skips (exit 77) when the talipp package is not importable — the runtime venv
has it (requirements-numpy2.txt), the CI system python may not.

Self-contained: no pytest, no network. Run directly or via ctest.
"""

import importlib.util
import json
import os
import re
import subprocess
import sys
import traceback

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SCRIPT = os.path.join(ROOT, "scripts", "equity_talipp.py")
HAVE_TALIPP = importlib.util.find_spec("talipp") is not None

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def approx(a, b, tol=1e-6):
    return a is not None and abs(a - b) < tol


def bars_from_closes(closes, spread=0.0):
    out = []
    for i, c in enumerate(closes):
        out.append({
            "timestamp": 1_700_000_000 + i * 86_400,
            "open": c,
            "high": c + spread,
            "low": c - spread,
            "close": c,
            "volume": 1_000_000,
        })
    return out


def run(indicator, bars, params=None):
    r = subprocess.run(
        [sys.executable, SCRIPT, indicator, json.dumps(bars), json.dumps(params or {})],
        capture_output=True, text=True)
    try:
        out = json.loads(r.stdout)
    except json.JSONDecodeError:
        out = {"success": False, "error": f"non-JSON stdout: {r.stdout[:120]!r}"}
    return r.returncode, out, r.stderr


# ── Semantic pins ────────────────────────────────────────────────────────────

def test_rsi_all_gains_is_overbought():
    rc, out, _ = run("rsi", bars_from_closes([100.0 + i for i in range(40)]), {"period": 14})
    check("rsi: monotonic uptrend reads 100 (was 0 — the inverse)",
          rc == 0 and out.get("success") and approx(out["values"][-1], 100.0),
          f"rc={rc} got {out.get('values', ['?'])[-1:]}")


def test_rsi_all_losses_is_oversold():
    rc, out, _ = run("rsi", bars_from_closes([100.0 - i * 0.5 for i in range(40)]), {"period": 14})
    check("rsi: monotonic downtrend reads 0",
          rc == 0 and out.get("success") and approx(out["values"][-1], 0.0),
          f"rc={rc} got {out.get('values', ['?'])[-1:]}")


def test_mfi_pure_accumulation_is_100():
    # Typical price rises every bar with volume — all money flow is positive.
    rc, out, _ = run("mfi", bars_from_closes([100.0 + i for i in range(40)], spread=0.5),
                     {"period": 14})
    check("mfi: pure accumulation reads 100 (was 0 — the inverse)",
          rc == 0 and out.get("success") and approx(out["values"][-1], 100.0),
          f"rc={rc} got {out.get('values', ['?'])[-1:]}")


def test_macd_flat_series_is_zero():
    rc, out, _ = run("macd", bars_from_closes([100.0] * 60), {"fast": 12, "slow": 26, "signal": 9})
    check("macd: a flat series has zero momentum",
          rc == 0 and out.get("success") and approx(out["values"][-1], 0.0),
          f"rc={rc} got {out.get('values', ['?'])[-1:]}")


def test_stoch_zero_k_is_a_value_not_an_absence():
    # Close pinned to the window low on the final bars → %K is genuinely 0.0.
    closes = [100.0 + i for i in range(30)] + [90.0] * 5
    bars = []
    for i, c in enumerate(closes):
        bars.append({"timestamp": 1_700_000_000 + i * 86_400, "open": c,
                     "high": c + 1.0, "low": min(c, 90.0), "close": c, "volume": 1_000_000})
    rc, out, _ = run("stoch", bars, {"k_period": 14, "d_period": 3})
    check("stoch: a genuine 0.0 %K survives into the output (not dropped, not 50)",
          rc == 0 and out.get("success") and any(approx(v, 0.0) for v in out["values"][-5:]),
          f"rc={rc} tail {out.get('values', [])[-5:]}")


# ── Contract pins (the bugs that made the tab show 'Calculation failed —') ──

def test_tab_ids_resolve():
    # 'bb' / 'williams' / 'supertrend' are the tab's ids; the previous script
    # keyed them 'BBANDS' / 'WILLR' / (absent), so over half the catalogue
    # exited "unknown indicator".
    bars = bars_from_closes([100.0 + (i % 7) for i in range(80)], spread=1.0)
    for ind, params in [("bb", {"period": 20, "stddev": 2.0}),
                        ("williams", {"period": 14}),
                        ("supertrend", {"period": 7, "multiplier": 3.0})]:
        rc, out, _ = run(ind, bars, params)
        check(f"tab id resolves: {ind}",
              rc == 0 and out.get("success") and len(out["values"]) == len(out["timestamps"]) > 0,
              f"rc={rc} err={out.get('error', '')[:80]}")


def test_errors_reach_stderr():
    rc, out, err = run("definitely_not_an_indicator", bars_from_closes([100.0] * 30))
    check("errors land on stderr (the C++ failure path only reads stderr)",
          rc == 1 and not out.get("success") and err.strip() != "",
          f"rc={rc} stderr={err[:60]!r}")


def test_zigzag_pairs_with_its_own_timestamps():
    # ZigZag emits per PIVOT, not per bar — the shim must pair against the
    # pivot timestamps it returns, and every pair must be a real instant.
    closes, p, up = [], 100.0, True
    for _ in range(10):
        for _ in range(12):
            p = p * (1.02 if up else 0.98)
            closes.append(p)
        up = not up
    rc, out, _ = run("zigzag", bars_from_closes(closes, spread=0.2), {"deviation": 5.0})
    ok = (rc == 0 and out.get("success")
          and 0 < len(out["values"]) == len(out["timestamps"]) < len(closes)
          and all(t > 1_600_000_000 for t in out["timestamps"]))
    check("zigzag: pivots pair with their own timestamps", ok,
          f"rc={rc} n={len(out.get('values', []))} err={out.get('error', '')[:80]}")


# ── Static drift guard (no talipp needed) ───────────────────────────────────

def static_test_param_names_stay_consumable():
    """Every spinbox name the tab offers must land, via the shim's PARAM_MAP,
    on a kwarg talipp_service.dispatch actually consumes — otherwise a renamed
    or new knob silently computes with library defaults while the UI displays
    the user's value (the same silent-drift class as the old id mismatch)."""
    cpp = open(os.path.join(ROOT, "src", "screens", "equity_research",
                            "EquityTalippTab.cpp"), encoding="utf-8").read()
    m = re.search(r"param_defs\(\) \{\s*static const QMap.*?= \{(.*?)\n    \};", cpp, re.S)
    check("param drift: found the tab's param_defs table", bool(m))
    if not m:
        return
    tab = {}
    for im in re.finditer(r'\{"(\w+)",\s*\{(.*?)\}\},', m.group(1), re.S):
        tab[im.group(1)] = re.findall(r'\{"(\w+)",', im.group(2))

    svc = open(os.path.join(ROOT, "scripts", "Analytics", "talipp_wrapper",
                            "talipp_service.py"), encoding="utf-8").read()
    consumed = {}
    for chunk in re.split(r'operation == "', svc)[1:]:
        op = chunk.split('"')[0]
        consumed[op] = set(re.findall(r'data\.get\("(\w+)"', chunk))

    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import equity_talipp as shim

    bad = []
    for ind, names in tab.items():
        for n in names:
            key = shim.PARAM_MAP.get(ind, {}).get(n, n)
            if key not in consumed.get(ind, set()):
                bad.append(f"{ind}.{n} -> {key}")
    check("param drift: every tab knob maps to a kwarg dispatch consumes",
          not bad, f"unconsumed: {bad}")


def main():
    # Static checks run on any interpreter; the end-to-end tests need talipp.
    statics = [v for k, v in sorted(globals().items()) if k.startswith("static_test_")]
    for t in statics:
        try:
            t()
        except Exception:
            traceback.print_exc()
            FAILURES.append(t.__name__)

    if not HAVE_TALIPP:
        if FAILURES:
            print(f"\n{len(FAILURES)} failure(s): {', '.join(sorted(set(FAILURES)))}")
            sys.exit(1)
        print("SKIP : talipp not importable — static checks passed, "
              "end-to-end tests skipped")
        sys.exit(77)

    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            traceback.print_exc()
            FAILURES.append(t.__name__)
    if FAILURES:
        print(f"\n{len(FAILURES)} failure(s): {', '.join(sorted(set(FAILURES)))}")
        sys.exit(1)
    print("\nAll equity_talipp shim tests passed.")


if __name__ == "__main__":
    main()
