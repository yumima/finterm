#!/usr/bin/env python3
"""Indicator correctness tests for scripts/equity_talipp.py.

These pin the inversions that made the TALIpp tab report the opposite of
what the market did:

  - RSI returned 0 — maximally OVERSOLD — for a window with no down days,
    the strongest possible uptrend. The seed value handled the zero-loss
    case correctly; the recursion one line below did not, so the bug only
    appeared after the first `period + 1` bars.
  - MFI carried the identical inversion on pure accumulation.
  - Stochastic %D coerced a genuine 0.0 %K (a close exactly at the window
    low — the strongest oversold reading) to a neutral 50 before the %D
    average ever saw it, because `v or 50.0` cannot tell None from zero.

Self-contained: no pytest, no network. Run directly or via ctest.
"""

import os
import sys
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

import equity_talipp as ta  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def approx(a, b, tol=1e-9):
    return a is not None and abs(a - b) < tol


# ── RSI ──────────────────────────────────────────────────────────────────────

def test_rsi_all_gains_is_overbought():
    # 40 consecutive up days: there is no downside at all.
    closes = [100.0 + i for i in range(40)]
    r = ta.rsi(closes, period=14)
    check("rsi: monotonic uptrend reads 100 (was 0 — the inverse)",
          approx(r[-1], 100.0), f"got {r[-1]}")
    # Every value past the warm-up must be 100, not just the seed.
    tail_ok = all(approx(v, 100.0) for v in r[15:])
    check("rsi: the recursion agrees with the seed", tail_ok, f"got {r[15:20]}")


def test_rsi_all_losses_is_oversold():
    closes = [100.0 - i for i in range(40)]
    r = ta.rsi(closes, period=14)
    check("rsi: monotonic downtrend reads 0", approx(r[-1], 0.0), f"got {r[-1]}")


def test_rsi_midrange_is_sane():
    # Alternating equal up/down moves sit near the midpoint.
    closes = [100.0 + (1.0 if i % 2 else 0.0) for i in range(60)]
    r = ta.rsi(closes, period=14)
    check("rsi: balanced series lands between 20 and 80",
          r[-1] is not None and 20.0 < r[-1] < 80.0, f"got {r[-1]}")


# ── MFI ──────────────────────────────────────────────────────────────────────

def test_mfi_pure_accumulation_is_100():
    n = 40
    highs = [101.0 + i for i in range(n)]
    lows = [99.0 + i for i in range(n)]
    closes = [100.0 + i for i in range(n)]
    volumes = [1000.0] * n
    m = ta.mfi(highs, lows, closes, volumes, period=14)
    check("mfi: pure accumulation reads 100 (was 0 — the inverse)",
          approx(m[-1], 100.0), f"got {m[-1]}")


# ── Stochastic ───────────────────────────────────────────────────────────────

def test_stoch_zero_k_survives_into_d():
    # Build a window whose final close sits exactly at the window low, so
    # %K is a genuine 0.0. %D must average that real zero, not a 50 the
    # coercion invented.
    n = 20
    highs = [110.0] * n
    lows = [100.0] * n
    closes = [105.0] * (n - 3) + [100.0, 100.0, 100.0]
    k, d = ta.stoch(highs, lows, closes, k_period=14, d_period=3)
    check("stoch: a close at the window low gives %K = 0",
          approx(k[-1], 0.0), f"got {k[-1]}")
    check("stoch: %D averages the real zero, not an invented 50",
          approx(d[-1], 0.0), f"got {d[-1]}")


def test_stoch_warmup_still_none():
    n = 20
    highs = [110.0] * n
    lows = [100.0] * n
    closes = [105.0] * n
    k, _ = ta.stoch(highs, lows, closes, k_period=14, d_period=3)
    check("stoch: warm-up bars stay None", k[0] is None, f"got {k[0]}")


# ── MACD (the same coercion shape) ───────────────────────────────────────────

def test_macd_flat_series_is_zero():
    closes = [100.0] * 80
    macd_line, sig, hist = ta.macd_calc(closes)
    check("macd: a flat series has a zero line", approx(macd_line[-1], 0.0), f"got {macd_line[-1]}")
    check("macd: a flat series has a zero histogram", approx(hist[-1], 0.0), f"got {hist[-1]}")


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all indicator tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
