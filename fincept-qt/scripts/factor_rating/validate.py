"""
Walk-forward validation for the factor rating.

This is the part the indicator rating never had, and the reason to trust or
discard anything in factors.py. A rating that has not been scored against
forward returns is an opinion with a number attached.

What is measured
----------------
information coefficient   Spearman rank correlation, on each date, between the
                          score and the forward excess return. The standard
                          cross-sectional measure: it asks "did the ranking
                          hold", not "did the market go up".

                          Sampled on non-overlapping steps. With a 20-day
                          forward window and daily sampling, consecutive
                          observations share 19/20 of their return path, and a
                          t-stat computed over them overstates significance by
                          roughly sqrt(20). Stepping one holding period at a
                          time is the difference between an honest t-stat and a
                          flattering one.

decile spread             mean forward excess return of the top decile minus
                          the bottom. Monotonicity across all ten matters more
                          than the spread itself — a signal that works only at
                          the extremes is usually fitting noise.

hit rate                  fraction of periods the top decile beat the universe
                          median. Compared against 50%, not against zero: in a
                          rising market "everything went up" is not skill.

Baselines are run alongside, because a factor model that cannot beat plain
12-1 momentum or a moving-average vote has not earned its complexity.
"""

import numpy as np
import pandas as pd
from scipy import stats

from . import factors as F

HORIZON = 21  # ~one month of trading days


def forward_excess_return(close, benchmark="SPY", horizon=HORIZON):
    """Forward return over `horizon`, net of the benchmark over the same span.

    Excess rather than raw: a rating is a statement about *this* stock, and
    leaving the market in the number lets a signal that merely tracks beta look
    predictive.
    """
    fwd = close.shift(-horizon) / close - 1.0
    if benchmark in close.columns:
        bench = fwd[benchmark]
        fwd = fwd.sub(bench, axis=0)
    return fwd


def ic_series(score, fwd, step=HORIZON, min_names=20):
    """Spearman IC per non-overlapping date."""
    dates = score.index[::step]
    out = {}
    for d in dates:
        if d not in fwd.index:
            continue
        s, f = score.loc[d], fwd.loc[d]
        both = pd.concat([s, f], axis=1).dropna()
        if len(both) < min_names:
            continue
        rho, _ = stats.spearmanr(both.iloc[:, 0], both.iloc[:, 1])
        if not np.isnan(rho):
            out[d] = rho
    return pd.Series(out)


def decile_table(score, fwd, step=HORIZON, n_bins=10, min_names=30):
    """Mean forward excess return by score decile, pooled over sampled dates."""
    rows = []
    for d in score.index[::step]:
        if d not in fwd.index:
            continue
        both = pd.concat([score.loc[d], fwd.loc[d]], axis=1).dropna()
        both.columns = ["score", "fwd"]
        if len(both) < min_names:
            continue
        both["bin"] = pd.qcut(both["score"].rank(method="first"), n_bins,
                              labels=False, duplicates="drop")
        rows.append(both.groupby("bin")["fwd"].mean())
    if not rows:
        return pd.Series(dtype=float)
    return pd.concat(rows, axis=1).mean(axis=1)


def top_decile_hit_rate(score, fwd, step=HORIZON, min_names=30):
    """How often the top decile beat the universe median that period."""
    wins = total = 0
    for d in score.index[::step]:
        if d not in fwd.index:
            continue
        both = pd.concat([score.loc[d], fwd.loc[d]], axis=1).dropna()
        both.columns = ["score", "fwd"]
        if len(both) < min_names:
            continue
        cut = both["score"].quantile(0.9)
        top = both.loc[both["score"] >= cut, "fwd"].mean()
        total += 1
        wins += int(top > both["fwd"].median())
    return (wins / total if total else float("nan")), total


def summarise(name, score, fwd, step=HORIZON):
    ic = ic_series(score, fwd, step)
    if ic.empty:
        return {"name": name, "n": 0}
    # Non-overlapping, so the plain t-stat is legitimate here.
    t = ic.mean() / ic.std(ddof=1) * np.sqrt(len(ic)) if ic.std(ddof=1) > 0 else np.nan
    dec = decile_table(score, fwd, step)
    hit, periods = top_decile_hit_rate(score, fwd, step)
    spread = (dec.iloc[-1] - dec.iloc[0]) if len(dec) >= 2 else np.nan
    # Monotonicity: rank correlation of decile index against decile mean return.
    mono = np.nan
    if len(dec) >= 3:
        mono, _ = stats.spearmanr(np.arange(len(dec)), dec.values)
    return {"name": name, "n": len(ic), "ic": ic.mean(), "ic_t": t,
            "spread": spread, "mono": mono, "hit": hit, "periods": periods}


def selftest(close, seed=0, verbose=True):
    """Positive control: can this harness see a signal that is there by construction?

    A null result is only worth reporting if the instrument that produced it can
    detect a real effect. A misaligned forward return, a sign error or a broken
    rank would all return "nothing works" just as convincingly as an honest
    zero, and would be indistinguishable from it without this.

    Feeds the scorer the forward return itself (must give IC 1), the same with
    increasing noise (must decay smoothly), pure noise (must give 0) and the
    negated forward return (must give -1). Returns the table so a caller can
    assert on it.
    """
    fwd = forward_excess_return(close)
    rng = np.random.default_rng(seed)
    scale = float(np.nanstd(fwd.values))
    cases = [("perfect foresight", fwd), ("inverted foresight", -fwd)]
    for k in (0.5, 1.0, 2.0, 5.0):
        cases.append((f"foresight + {k}x noise",
                      fwd + rng.normal(0, k * scale, fwd.shape)))
    noise = fwd.copy()
    noise[:] = rng.normal(0, 1, fwd.shape)
    cases.append(("pure noise", noise))

    rows = [summarise(name, sig, fwd) for name, sig in cases]
    if not rows[0].get("n"):
        raise ValueError(
            f"universe too small to rank: {close.shape[1]} symbols yields no date with "
            f"the {ic_series.__defaults__[1]} names a cross-sectional IC needs. "
            "The control is only meaningful on the full universe.")
    if verbose:
        print(f"{'control':<30}{'IC':>9}{'mono':>7}{'n':>5}")
        print("-" * 51)
        for r in rows:
            print(f"{r['name']:<30}{r['ic']:>9.4f}{r['mono']:>7.2f}{r['n']:>5}")
    ok = {r["name"]: r["ic"] for r in rows}
    assert ok["perfect foresight"] > 0.99, ok
    assert ok["inverted foresight"] < -0.99, ok
    assert abs(ok["pure noise"]) < 0.05, ok
    return rows


def baselines(panel):
    """Reference signals the factor model has to beat to justify itself."""
    close = panel["close"]
    out = {}
    # Plain 12-1 momentum — the single best-documented cross-sectional effect.
    out["baseline: 12-1 momentum"] = F.momentum_12_1(close)
    # Stand-in for the shipped indicator rating's dominant component: the
    # fraction of moving averages price sits above. The shipped rating is
    # ~50% this by weight, so if the factor model cannot beat it there is no
    # case for replacing anything.
    windows = (10, 20, 30, 50, 100, 200)
    votes = [np.sign(close - close.rolling(w, min_periods=w).mean()) for w in windows]
    out["baseline: MA vote (proxy for shipped rating)"] = sum(votes) / len(windows)
    return out


def run(panel, verbose=True):
    close = panel["close"]
    fwd = forward_excess_return(close)

    raw = F.compute_factors(panel)
    z = {k: F.zscore_rows(v) for k, v in raw.items()}
    comp = F.composite(raw)

    results = []
    for name, df in z.items():
        results.append(summarise(name, df, fwd))
    for name, df in baselines(panel).items():
        results.append(summarise(name, df, fwd))
    results.append(summarise("COMPOSITE (equal weight)", comp, fwd))

    if verbose:
        print(f"{'signal':<44}{'IC':>8}{'t':>7}{'spread':>9}{'mono':>7}{'hit':>7}{'n':>5}")
        print("-" * 87)
        for r in results:
            if not r.get("n"):
                print(f"{r['name']:<44}  (no observations)")
                continue
            print(f"{r['name']:<44}{r['ic']:>8.4f}{r['ic_t']:>7.2f}"
                  f"{r['spread']*100:>8.2f}%{r['mono']:>7.2f}{r['hit']*100:>6.0f}%{r['n']:>5}")
        print("-" * 87)
        print(f"horizon {HORIZON}d, non-overlapping; spread = top-minus-bottom decile "
              f"mean forward excess return; mono = decile monotonicity; hit = top decile "
              f"beat universe median")
    return results, comp
