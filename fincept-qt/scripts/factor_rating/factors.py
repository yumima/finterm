"""
The factors behind the cross-sectional trend rating.

Why this exists alongside the indicator rating
----------------------------------------------
The ER Technicals rating is a voting ensemble of ~20 indicators. Its structural
flaw is that the votes are not independent: twelve moving averages of one close
series are not twelve pieces of evidence, they are one fact — where price sits
in its own recent range — measured twelve times. Averaging them produces a
number that reads like confidence and is not, and adding more indicators makes
it look more confident without adding information.

So this module does the opposite. Six factors, each aimed at a *different*
question, each reduced to a single number, each standardised against the
universe on the same date:

  trend_position   where price sits relative to its long average, in units of
                   its own volatility. Absolute thresholds ("1.5% above") mean
                   different things for a utility and a small-cap miner; ATR
                   units make one number comparable across instruments.
  trend_quality    how *cleanly* it got there — the R² of log price on time.
                   A stock that ground steadily higher and one that round-tripped
                   twice can share an endpoint; only the first is a trend.
  momentum_12_1    the 12-month return excluding the most recent month. The
                   best-documented cross-sectional equity effect there is, and
                   the one signal in this file with decades of published
                   out-of-sample evidence behind it.
  reversal_1m      the last month's return, negated. Short-horizon returns
                   reverse; this is the honest version of "oversold", stated as
                   a cross-sectional effect rather than an oscillator threshold.
  volume_trend     whether volume is confirming: the sign of the price move
                   times the change in average volume. NOT orthogonalised
                   against returns — an earlier draft described a residualised
                   version that was never built, and the docstring claiming it
                   outlived the plan. As shipped this factor partially restates
                   price direction, which is one plausible reason it measured
                   insignificant.
  low_volatility   inverse realised volatility. Included for the published
                   low-volatility anomaly — and it is the one factor here that
                   measured as significant, with the sign reversed: over this
                   universe and period high-volatility names beat low-volatility
                   ones monotonically across every decile. Kept as written
                   rather than flipped, because flipping a factor to match the
                   sample it was measured on is how you manufacture a backtest.
                   See __init__.py.

Everything is computed on a panel and returns a DataFrame shaped like the
close panel, so the whole history can be scored at once for validation rather
than one date at a time.
"""

import numpy as np
import pandas as pd


def _log(df):
    return np.log(df.where(df > 0))


def atr(high, low, close, window=14):
    """Wilder-style ATR, computed column-wise across the panel."""
    tr = pd.DataFrame(
        np.maximum.reduce([
            (high - low).to_numpy(),
            (high - close.shift(1)).abs().to_numpy(),
            (low - close.shift(1)).abs().to_numpy(),
        ]),
        index=close.index, columns=close.columns,
    )
    return tr.ewm(alpha=1.0 / window, min_periods=window, adjust=False).mean()


def trend_position(close, high, low, window=200):
    """(price - long average) measured in ATRs."""
    ma = close.rolling(window, min_periods=window).mean()
    a = atr(high, low, close)
    return (close - ma) / a.where(a > 0)


def trend_quality(close, window=120):
    """Signed R² of log price on time — direction multiplied by straightness.

    A stock that ground steadily higher and one that round-tripped twice on the
    way can end at the same price with the same 200-day average; only the first
    is a trend, and R² is what separates them. Sign carries the direction, so
    the factor runs -1 (clean downtrend) to +1 (clean uptrend) with choppy
    names near zero regardless of where they finished.

    Computed closed-form from rolling sums rather than a rolling regression.
    With ~180 symbols over ~3000 dates a `.rolling().apply()` per column is
    half a million least-squares fits; the identity below is three rolling
    sums. Since x is always 0..n-1 within the window, its moments are
    constants, and sum(x·y) over a window ending at position t expands to
    rolling_sum(g·y) - (t-n+1)·rolling_sum(y) where g is the global index.
    """
    y = _log(close)
    n = window
    g = pd.Series(np.arange(len(y), dtype=float), index=y.index)

    sum_y = y.rolling(n, min_periods=n).sum()
    sum_gy = y.mul(g, axis=0).rolling(n, min_periods=n).sum()
    sum_yy = (y ** 2).rolling(n, min_periods=n).sum()

    start = g - (n - 1)                       # window's first global index
    sum_xy = sum_gy.sub(sum_y.mul(start, axis=0))
    x_mean = (n - 1) / 2.0
    y_mean = sum_y / n

    sxy = sum_xy - n * x_mean * y_mean
    syy = sum_yy - n * (y_mean ** 2)
    sxx = n * (n * n - 1) / 12.0              # variance of 0..n-1, times n

    r2 = (sxy ** 2) / (sxx * syy.where(syy > 0))
    return np.sign(sxy) * r2


def momentum_12_1(close, long_window=252, skip=21):
    """12-month total return, excluding the most recent month."""
    return close.shift(skip) / close.shift(long_window) - 1.0


def reversal_1m(close, window=21):
    """Negated one-month return — short-horizon returns reverse."""
    return -(close / close.shift(window) - 1.0)


def volume_trend(close, volume, window=60):
    """Volume confirmation: sign of the window's return times relative volume.

    sign(60d return) x (60d avg volume / 180d avg volume - 1). Deliberately
    simple, and honestly labelled: this is NOT orthogonalised against the
    price move — a residualised design was described in an earlier draft and
    never implemented, so the factor partially restates price direction. It
    measured insignificant in validation (IC -0.005); if it is ever revisited,
    the residualisation is the first thing to actually build.
    """
    ret = close.pct_change(window)
    vol_chg = volume.rolling(window, min_periods=window).mean() / \
        volume.rolling(window * 3, min_periods=window * 3).mean() - 1.0
    signed = np.sign(ret) * vol_chg
    return signed


def low_volatility(close, window=120):
    """Inverse realised volatility (annualised)."""
    vol = close.pct_change().rolling(window, min_periods=window).std() * np.sqrt(252)
    return -vol


# ── cross-sectional machinery ────────────────────────────────────────────────

def zscore_rows(df, clip=3.0):
    """Standardise each date across the universe, robustly.

    Median and MAD rather than mean and standard deviation: a single symbol
    that doubled overnight otherwise drags the whole cross-section's mean and
    inflates its spread, which quietly rescores every other name on the date.
    """
    med = df.median(axis=1)
    mad = (df.sub(med, axis=0)).abs().median(axis=1)
    scale = mad * 1.4826
    # Dates where the universe barely moves give a ~0 scale; fall back to std.
    fallback = df.std(axis=1)
    scale = scale.where(scale > 1e-12, fallback)
    z = df.sub(med, axis=0).div(scale.where(scale > 1e-12), axis=0)
    return z.clip(-clip, clip)


FACTORS = ("trend_position", "trend_quality", "momentum_12_1",
           "reversal_1m", "volume_trend", "low_volatility")


def compute_factors(panel):
    """All six factors as raw (unstandardised) panels."""
    close, high = panel["close"], panel["high"]
    low, volume = panel["low"], panel["volume"]
    return {
        "trend_position": trend_position(close, high, low),
        "trend_quality": trend_quality(close),
        "momentum_12_1": momentum_12_1(close),
        "reversal_1m": reversal_1m(close),
        "volume_trend": volume_trend(close, volume),
        "low_volatility": low_volatility(close),
    }


def composite(factors, weights=None, min_factors=4):
    """Equal-weight mean of the standardised factors.

    Equal weight on purpose. Fitted weights need far more independent history
    than a decade of one universe provides, and a hand-picked set is taste
    wearing the costume of evidence — which is precisely the criticism of the
    rating this is meant to improve on. A name is scored only when at least
    `min_factors` of the six are available, so a symbol still inside its
    200-day warm-up is not rated off whatever happened to be ready.
    """
    z = {name: zscore_rows(df) for name, df in factors.items()}
    if weights is None:
        weights = {name: 1.0 for name in z}
    stack = None
    wsum = None
    for name, df in z.items():
        w = weights.get(name, 0.0)
        if w == 0.0:
            continue
        contrib = df * w
        present = df.notna() * w
        stack = contrib if stack is None else stack.add(contrib, fill_value=0.0)
        wsum = present if wsum is None else wsum.add(present, fill_value=0.0)
    avail = sum(df.notna() for df in z.values())
    out = stack / wsum.where(wsum > 0)
    return out.where(avail >= min_factors)


def to_percentile(score):
    """Cross-sectional percentile in [0, 1] — the rating's actual scale.

    Fixed cut-offs on a raw score are not self-calibrating: in a bull market
    every name clears them and the rating degenerates into a thermometer with
    no zero. A percentile always means the same thing — "against everything
    else today, this is where it stands".
    """
    return score.rank(axis=1, pct=True)


BANDS = ((0.90, "STRONG BUY"), (0.70, "BUY"), (0.30, "NEUTRAL"),
         (0.10, "SELL"), (0.00, "STRONG SELL"))


def to_rating(pct):
    def band(v):
        if pd.isna(v):
            return None
        for lo, name in BANDS:
            if v >= lo:
                return name
        return "STRONG SELL"
    return pct.map(band) if hasattr(pct, "map") else pct.applymap(band)
