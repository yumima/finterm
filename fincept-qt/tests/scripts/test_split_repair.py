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


def build(closes, freq="B", volume_int=True, splits=None):
    """Volume defaults to int64 -- the dtype yfinance actually returns. A float
    column here would hide the incompatible-dtype assignment that is a
    FutureWarning in pandas 2 and raises in pandas 3.

    `splits` is {position: ratio}. It becomes a "Stock Splits" column, which is
    where the repair reads them from and how yfinance actually delivers them --
    NaN-and-zero padded on every other bar, as the bulk download returns it."""
    import pandas as pd
    import numpy as np
    idx = pd.date_range("2026-01-01", periods=len(closes), freq=freq)
    vol = [1_000_000] * len(closes) if volume_int else [1_000_000.0] * len(closes)
    # NaN padding FIRST: writing it afterwards clobbered a split announced at
    # position 0, so the "no bars behind it" case asserted against a frame with
    # no split in it at all and passed without exercising the guard.
    action = [0.0] * len(closes)
    if action:
        action[min(1, len(action) - 1)] = np.nan  # the bulk frame carries NaNs here
    for pos, ratio in (splits or {}).items():
        action[pos] = ratio
    return pd.DataFrame(
        {
            "Open": closes,
            "High": [c * 1.01 for c in closes],
            "Low": [c * 0.99 for c in closes],
            "Close": closes,
            "Volume": vol,
            "Stock Splits": action,
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
    df = build([160.0] * 20 + [80.0] * 3, splits={20: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "APH")
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
    df = build([160.0] * 20 + [float("nan")] + [80.0] * 3, splits={20: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "APH")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"repaired across the price-less split bar (got {out['Close'].iloc[19]})")

    print("\nAlready-adjusted split is left alone:")
    df = build([80.0] * 23, splits={20: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "NFLX")
    check(abs(out["Close"].iloc[0] - 80.0) < 1e-6,
          f"correctly-adjusted history untouched (got {out['Close'].iloc[0]})")

    print("\nA genuine crash on a split date is NOT repaired:")
    # -30% is a real move, nothing like the announced 2:1. Repairing it would
    # invent a 40% rally out of a selloff.
    df = build([160.0] * 20 + [112.0] * 3, splits={20: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "CRASH")
    check(abs(out["Close"].iloc[0] - 160.0) < 1e-6,
          f"step that does not match the ratio is left alone (got {out['Close'].iloc[0]})")

    print("\nA genuine gap with no split announced is NOT repaired:")
    # ORCL/DELL/MRVL/IONQ all look like this: a >30% earnings move, no split.
    df = build([100.0] * 20 + [136.0] * 3)
    out = _repair_unadjusted_splits(df.copy(), "ORCL")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"earnings gap with no split left alone (got {out['Close'].iloc[0]})")

    print("\nReverse split is repaired in the other direction:")
    df = build([8.0] * 20 + [80.0] * 3, splits={20: 0.1})
    out = _repair_unadjusted_splits(df.copy(), "RVRS")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"1-for-10 back-adjusted upward (got {out['Close'].iloc[19]})")

    print("\nNear-1:1 split is left alone (undecidable):")
    df = build([100.0] * 20 + [80.0] * 3, splits={20: 1.25})
    out = _repair_unadjusted_splits(df.copy(), "SMALL")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"ratio too close to 1 to decide -- left alone (got {out['Close'].iloc[0]})")

    print("\nSplit outside the window changes nothing:")
    # A split before the first bar never appears in the frame's actions column,
    # and a split ON the first bar has nothing behind it to adjust.
    df = build([80.0] * 23, splits={0: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "OLD")
    check(abs(out["Close"].iloc[0] - 80.0) < 1e-6,
          f"split with no bars behind it left alone (got {out['Close'].iloc[0]})")

    print("\nDegenerate inputs are no-ops, not exceptions:")
    df = build([80.0] * 5)
    check(_repair_unadjusted_splits(df.copy(), "X") is not None,
          "all-zero actions column tolerated")
    check(_repair_unadjusted_splits(pd.DataFrame(), "X").empty,
          "empty frame tolerated")
    no_actions = df.drop(columns=["Stock Splits"])
    check(_repair_unadjusted_splits(no_actions.copy(), "X") is not None,
          "frame without an actions column tolerated (bulk download default)")
    df0 = build([80.0] * 5, splits={2: 0.0})
    check(abs(_repair_unadjusted_splits(df0.copy(), "ZERO")["Close"].iloc[0] - 80.0) < 1e-6,
          "zero ratio cannot divide anything")

    print("\nWeekly bars: the straddling bar is already on the new basis:")
    # A weekly bar is stamped at the START of its week, so the bar containing a
    # mid-week split sorts BEFORE the split instant. Comparing timestamps to the
    # split naively put ALL 105 of APH's weekly bars on the "before" side, left
    # nothing to compare against, and skipped the repair while the 47% cliff
    # stayed in the weekly rating. The boundary must be found positionally.
    df = build([160.0] * 20 + [80.0] * 1, freq="W-MON", splits={20: 2.0})
    out = _repair_unadjusted_splits(df.copy(), "APH")
    check(abs(out["Close"].iloc[19] - 80.0) < 1e-6,
          f"weekly pre-split bars repaired (got {out['Close'].iloc[19]})")
    check(abs(out["Close"].iloc[20] - 80.0) < 1e-6,
          f"weekly straddling bar untouched (got {out['Close'].iloc[20]})")

    print("\nAn undecidable 3:2 is left alone even when the step looks right:")
    # The band around k=1.5 at a 25% tolerance reached down to 1.125, and an
    # ALREADY-ADJUSTED 3-for-2 on a day the stock fell 11% produces exactly
    # that step. Repairing it would invent a 50% up-gap -- the bug's own harm,
    # inverted. The cutoff exists so this case is never even considered.
    df = build([100.0] * 20 + [88.0] * 3, splits={20: 1.5})
    out = _repair_unadjusted_splits(df.copy(), "THREETWO")
    check(abs(out["Close"].iloc[0] - 100.0) < 1e-6,
          f"correctly-adjusted 3:2 after an 11% fall left alone (got {out['Close'].iloc[0]})")

    print("\nint64 Volume survives the repair (pandas 2 warns, pandas 3 raises):")
    df = build([160.0] * 20 + [80.0] * 3, splits={20: 2.0})
    check(str(df["Volume"].dtype) == "int64", "fixture really is int64, as yfinance returns")
    with warnings.catch_warnings():
        warnings.simplefilter("error", FutureWarning)
        out = _repair_unadjusted_splits(df.copy(), "APH")
    check(abs(out["Volume"].iloc[19] - 2_000_000.0) < 1e-6,
          f"int64 volume scaled without a dtype warning (got {out['Volume'].iloc[19]})")
    check(int(out["Volume"].iloc[19]) == 2_000_000,
          "scaled volume still converts to int for the JSON row builder")

    print("\nA repair that cannot run returns the series, never raises:")
    # get_historical_period wraps everything in `except Exception` and turns a
    # raise into {"error": ...} -- which blanks the chart, the technicals AND
    # the 52-week band. Failing soft is the difference between "unrepaired" and
    # "gone".
    class Exploding(pd.DataFrame):
        @property
        def _constructor(self):
            return Exploding
        def get(self, *a, **k):
            raise RuntimeError("vendor blew up")
    boom = Exploding(build([80.0] * 5))
    out = _repair_unadjusted_splits(boom, "BOOM")
    check(out is not None and len(out) == 5, "a raising frame degrades to the untouched series")

    print()

    print("\nA duplicated timestamp does not drag a post-split bar into the repair:")
    # .loc with LABELS matches every occurrence of a repeated stamp, so a bar
    # after the boundary sharing a label with one before it got divided by k as
    # well -- a correctly-basised bar silently halved. yfinance emits duplicate
    # stamps from synthetic action rows and DST/exchange-date collisions.
    df = build([160.0] * 20 + [80.0] * 3, splits={20: 2.0})
    idx = list(df.index)
    idx[21] = idx[5]
    df.index = pd.DatetimeIndex(idx)
    out = _repair_unadjusted_splits(df.copy(), "DUP")
    post = [round(x, 1) for x in out["Close"].iloc[20:].tolist()]
    check(post == [80.0, 80.0, 80.0],
          f"post-split bars all untouched despite a duplicate stamp (got {post})")

    print("\nPer-share cash amounts move with the prices:")
    df = build([160.0] * 20 + [80.0] * 3, splits={20: 2.0})
    df["Dividends"] = [1.0] * 23
    out = _repair_unadjusted_splits(df.copy(), "DIV")
    check(abs(out["Dividends"].iloc[19] - 0.5) < 1e-9,
          f"pre-split dividend rebased (got {out['Dividends'].iloc[19]})")
    check(abs(out["Dividends"].iloc[20] - 1.0) < 1e-9,
          f"post-split dividend untouched (got {out['Dividends'].iloc[20]})")

    print("The repair is wired into the chokepoint charts/technicals/52W share:")
    import inspect
    from yfinance_data import get_historical_period
    check("_repair_unadjusted_splits" in inspect.getsource(get_historical_period),
          "get_historical_period calls _repair_unadjusted_splits")
    from yfinance_data import _resolve_for_history
    bulk = inspect.getsource(_resolve_for_history)
    check("_repair_unadjusted_splits" in bulk,
          "_resolve_for_history repairs too -- CapChg is not auto_adjust's to decline")
    check("actions=True" in bulk,
          "the bulk download requests the Stock Splits column the repair reads")
    # group_by="ticker" yields MultiIndex columns even for ONE ticker, so a
    # `data["Close"]` shortcut for the single-candidate case is a KeyError that
    # closes_for swallows into "missing" -- BTC-USD, RY.TO and ^GSPC generate no
    # extra suffix candidates, so a portfolio holding only one of them silently
    # got no NAV backfill.
    check("MultiIndex" in bulk,
          "the single-ticker column shape is handled, not assumed flat")

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
