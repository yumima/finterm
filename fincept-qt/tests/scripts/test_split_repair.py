#!/usr/bin/env python3
r"""A split the vendor announced but never applied must not read as a crash.

On the session a split takes effect Yahoo flips the quote to the new basis
before it rewrites the historical bars. `Ticker.history()` then returns a
series where `.splits` reports the split and every pre-split bar is still on
the old basis, so the close drops by exactly the split ratio in one step and
nothing downstream can tell it from a collapse.

Observed live on 2026-09-04, the day after Amphenol's 2-for-1:

    close        82.93        (post-split)
    sma_10      150.24        (pre-split, unadjusted)
    sma_200     144.73
    cci        -539.26
    aroon_down  100.0
    rating      SELL, net -0.399

TradingView's independent read of the same tape that day was +0.467 overall
and +0.933 on moving averages. After the repair the same code returns +0.490.
The 52-week low, the chart's y-range and every moving-average distance were
wrong in the same direction, because all three read this one series.

Why detection keys on the RATIO and never on the size of the gap
----------------------------------------------------------------
A 2-for-1 that was applied leaves consecutive closes at a ratio near 1; one
that was not leaves them near 2. Those hypotheses are far apart, so the test
is "does the observed step match the announced ratio", not "is the step big".
That distinction is the whole safety argument: in the same 60-symbol window
ORCL (+36%), DELL (+33%), MRVL (+32%) and IONQ (four moves over 33%) all have
genuine single-bar gaps past 30% with no split announced on those dates, and a
magnitude rule would have "repaired" every one of them into fiction.

Nor is a blanket repair right: yfinance normally back-adjusts correctly --
LRCX, NFLX, PANW, KLAC, ANET and CRWD all have splits in the same window and
all come back clean. The exposure is the day or two after a split, which is
precisely when someone looks at the ticker.

Splits close to 1:1 (a 5:4, say) are deliberately left alone: there the two
hypotheses overlap and the observation cannot separate them.

No network and no yfinance import -- the repair is exercised against
hand-built frames, so this runs in CI.
"""

import os
import sys
import warnings

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))

failures = []


def fail(msg):
    failures.append(msg)
    print(f"  FAIL: {msg}")


def check(cond, msg):
    if cond:
        print(f"  ok: {msg}")
    else:
        fail(msg)


class FakeTicker:
    """Just the surface _repair_unadjusted_splits touches."""

    def __init__(self, splits, name="TEST"):
        self.splits = splits
        self.ticker = name


def build(closes, freq="B", volume_int=True):
    """Volume defaults to int64 -- the dtype yfinance actually returns. A float
    column here would hide the incompatible-dtype assignment that is a
    FutureWarning in pandas 2 and raises in pandas 3."""
    import pandas as pd
    idx = pd.date_range("2026-01-01", periods=len(closes), freq=freq)
    vol = [1_000_000] * len(closes) if volume_int else [1_000_000.0] * len(closes)
    return pd.DataFrame(
        {
            "Open": closes,
            "High": [c * 1.01 for c in closes],
            "Low": [c * 0.99 for c in closes],
            "Close": closes,
            "Volume": vol,
        },
        index=idx,
    )


def main():
    try:
        import pandas as pd
    except ImportError:
        print("pandas unavailable -- cannot exercise the repair.")
        return 77

    try:
        from yfinance_data import _repair_unadjusted_splits
    except ImportError as e:
        print(f"cannot import the daemon module: {e!r}")
        return 77

    print("Unapplied split is repaired:")
    # 20 bars near 160, then the split, then bars near 80. Only the tail is on
    # the new basis -- exactly what Yahoo returned for APH.
    df = build([160.0] * 20 + [80.0] * 3)
    t = FakeTicker(pd.Series([2.0], index=[df.index[20]]), "APH")
    out = _repair_unadjusted_splits(df.copy(), t, "APH")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"pre-split bars divided by the ratio (got {out['Close'].iloc[19]})")
    check(abs(out["Close"].iloc[20] - 80.0) < 1e-6,
          f"post-split bars untouched (got {out['Close'].iloc[20]})")
    check(abs(out["Volume"].iloc[19] - 2_000_000.0) < 1e-6,
          f"pre-split volume multiplied by the ratio (got {out['Volume'].iloc[19]})")
    check(abs(out["High"].iloc[19] - 160.0 * 1.01 / 2) < 1e-6, "High adjusted with Close")
    check(abs(out["Low"].iloc[19] - 160.0 * 0.99 / 2) < 1e-6, "Low adjusted with Close")
    check(abs(out["Volume"].iloc[20] - 1_000_000.0) < 1e-6, "post-split volume untouched")

    print("\nA NaN bar on the split date does not defeat detection:")
    # Yahoo left APH's split-date bar all-NaN, so the observed step spans two
    # sessions and carries a real move on top of the ratio (1.913 against 2).
    df = build([160.0] * 20 + [float("nan")] + [80.0] * 3)
    t = FakeTicker(pd.Series([2.0], index=[df.index[20]]), "APH")
    out = _repair_unadjusted_splits(df.copy(), t, "APH")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"repaired across the price-less split bar (got {out['Close'].iloc[19]})")

    print("\nAlready-adjusted split is left alone:")
    df = build([80.0] * 23)
    t = FakeTicker(pd.Series([2.0], index=[df.index[20]]), "NFLX")
    out = _repair_unadjusted_splits(df.copy(), t, "NFLX")
    check(abs(out["Close"].iloc[0] - 80.0) < 1e-6,
          f"correctly-adjusted history untouched (got {out['Close'].iloc[0]})")

    print("\nA genuine crash on a split date is NOT repaired:")
    # -30% is a real move, nothing like the announced 2:1. Repairing it would
    # invent a 40% rally out of a selloff.
    df = build([160.0] * 20 + [112.0] * 3)
    t = FakeTicker(pd.Series([2.0], index=[df.index[20]]), "CRASH")
    out = _repair_unadjusted_splits(df.copy(), t, "CRASH")
    check(abs(out["Close"].iloc[0] - 160.0) < 1e-6,
          f"step that does not match the ratio is left alone (got {out['Close'].iloc[0]})")

    print("\nA genuine gap with no split announced is NOT repaired:")
    # ORCL/DELL/MRVL/IONQ all look like this: a >30% earnings move, no split.
    df = build([100.0] * 20 + [136.0] * 3)
    t = FakeTicker(pd.Series(dtype=float), "ORCL")
    out = _repair_unadjusted_splits(df.copy(), t, "ORCL")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"earnings gap with no split left alone (got {out['Close'].iloc[0]})")

    print("\nReverse split is repaired in the other direction:")
    df = build([8.0] * 20 + [80.0] * 3)
    t = FakeTicker(pd.Series([0.1], index=[df.index[20]]), "RVRS")
    out = _repair_unadjusted_splits(df.copy(), t, "RVRS")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"1-for-10 back-adjusted upward (got {out['Close'].iloc[19]})")

    print("\nNear-1:1 split is left alone (undecidable):")
    df = build([100.0] * 20 + [80.0] * 3)
    t = FakeTicker(pd.Series([1.25], index=[df.index[20]]), "SMALL")
    out = _repair_unadjusted_splits(df.copy(), t, "SMALL")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"ratio too close to 1 to decide -- left alone (got {out['Close'].iloc[0]})")

    print("\nSplit outside the window changes nothing:")
    df = build([80.0] * 23)
    t = FakeTicker(pd.Series([2.0], index=[df.index[0] - pd.Timedelta(days=400)]), "OLD")
    out = _repair_unadjusted_splits(df.copy(), t, "OLD")
    check(abs(out["Close"].iloc[0] - 80.0) < 1e-6,
          f"split predating the window left alone (got {out['Close'].iloc[0]})")

    print("\nDegenerate inputs are no-ops, not exceptions:")
    df = build([80.0] * 5)
    check(_repair_unadjusted_splits(df.copy(), FakeTicker(None), "X") is not None,
          "None splits tolerated")
    check(_repair_unadjusted_splits(pd.DataFrame(), FakeTicker(pd.Series([2.0])), "X").empty,
          "empty frame tolerated")
    check(_repair_unadjusted_splits(df.copy(), object(), "X") is not None,
          "ticker without .splits tolerated")
    t = FakeTicker(pd.Series([0.0], index=[df.index[2]]), "ZERO")
    check(abs(_repair_unadjusted_splits(df.copy(), t, "ZERO")["Close"].iloc[0] - 80.0) < 1e-6,
          "zero ratio cannot divide anything")

    print("\nWeekly bars: the straddling bar is already on the new basis:")
    # A weekly bar is stamped at the START of its week, so the bar containing a
    # mid-week split sorts BEFORE the split instant. Comparing timestamps to the
    # split naively put ALL 105 of APH's weekly bars on the "before" side, left
    # nothing to compare against, and skipped the repair while the 47% cliff
    # stayed in the weekly rating. The boundary must be found positionally.
    df = build([160.0] * 20 + [80.0] * 1, freq="W-MON")
    split_ts = df.index[20] + pd.Timedelta(days=2)   # mid-week, after the stamp
    t = FakeTicker(pd.Series([2.0], index=[split_ts]), "APH")
    out = _repair_unadjusted_splits(df.copy(), t, "APH")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"weekly pre-split bars repaired (got {out['Close'].iloc[19]})")
    check(abs(out["Close"].iloc[20] - 80.0) < 1e-6,
          f"weekly straddling bar untouched (got {out['Close'].iloc[20]})")

    print("\nAn undecidable 3:2 is left alone even when the step looks right:")
    # The band around k=1.5 at a 25% tolerance reached down to 1.125, and an
    # ALREADY-ADJUSTED 3-for-2 on a day the stock fell 11% produces exactly
    # that step. Repairing it would invent a 50% up-gap -- the bug's own harm,
    # inverted. The cutoff exists so this case is never even considered.
    df = build([100.0] * 20 + [88.0] * 3)
    t = FakeTicker(pd.Series([1.5], index=[df.index[20]]), "THREETWO")
    out = _repair_unadjusted_splits(df.copy(), t, "THREETWO")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"correctly-adjusted 3:2 after an 11% fall left alone (got {out['Close'].iloc[0]})")

    print("\nint64 Volume survives the repair (pandas 2 warns, pandas 3 raises):")
    df = build([160.0] * 20 + [80.0] * 3)
    check(str(df["Volume"].dtype) == "int64", "fixture really is int64, as yfinance returns")
    t = FakeTicker(pd.Series([2.0], index=[df.index[20]]), "APH")
    with warnings.catch_warnings():
        warnings.simplefilter("error", FutureWarning)
        out = _repair_unadjusted_splits(df.copy(), t, "APH")
    check(abs(out["Volume"].iloc[19] - 2_000_000.0) < 1e-6,
          f"int64 volume scaled without a dtype warning (got {out['Volume'].iloc[19]})")
    check(int(out["Volume"].iloc[19]) == 2_000_000,
          "scaled volume still converts to int for the JSON row builder")

    print("\nA repair that cannot run returns the series, never raises:")
    # get_historical_period wraps everything in `except Exception` and turns a
    # raise into {"error": ...} -- which blanks the chart, the technicals AND
    # the 52-week band. Failing soft is the difference between "unrepaired" and
    # "gone".
    class Exploding:
        @property
        def splits(self):
            raise RuntimeError("vendor blew up")
    df = build([80.0] * 5)
    out = _repair_unadjusted_splits(df.copy(), Exploding(), "BOOM")
    check(out is not None and len(out) == 5, "raising ticker degrades to the untouched series")

    print()
    print("The repair is wired into the chokepoint charts/technicals/52W share:")
    import inspect
    from yfinance_data import get_historical_period
    check("_repair_unadjusted_splits" in inspect.getsource(get_historical_period),
          "get_historical_period calls _repair_unadjusted_splits")

    print()
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("All split-repair guards hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
