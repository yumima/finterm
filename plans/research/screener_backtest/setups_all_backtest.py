"""Setting-up lists for every testable screen: the names next in line.

Companion to setups_backtest.py (the Trend Template). Same method and data
(point-in-time S&P 500, monthly, equal weight, 10 bp per side, excess vs the
equal-weighted universe; fundamentals point-in-time from SEC filings), the
shipped definitions. For each screen, "next in line" is:

  ranked screens   the names just outside the cut, as many again
                   (Momentum / Low vol: the next decile; Magic Formula:
                   ranks 31-60; Quality+Momentum / Trending Value: ranks
                   26-50 inside their pools)
  Piotroski        F-score exactly 7 (one test short of 8)
  Breakout         a top-5% leader failing exactly one of its four filters
  Leader Pullback  a leader resting (higher lows, contracting, above the 9
                   and 21 EMA, 21 > 50) that has NOT yet touched its 21 EMA,
                   with its 3-session low within 5% above it

Two measurements each: held as a list (does it beat the index?), and
graduation (how often its names are on the screen itself next time — the
next month-end; for the pullback, any of the next 10 sessions — against the
base rate for every other name). CAN SLIM has no test (no point-in-time
quarterly EPS here). Run after collect.py, fund_collect.py.
"""
import os
import sys

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fund_backtest as F  # noqa: E402  (loads backtest.py's data too)

B = F.B
S, close = B.S, B.close
sd = B.sd
pct_rank = B.pct_rank


def ranked(s, lo, hi):
    """Names ranked lo+1..hi by s (descending, stable), as the screens rank."""
    s = s.dropna().sort_values(ascending=False, kind="stable")
    return list(s.index[lo:hi])


def frac_n(s, frac):
    return max(1, int(round(s.notna().sum() * frac)))


dvol50 = (close * B.vol).rolling(50, min_periods=40).mean()


def breakout(t, u):
    combo = pct_rank(B.r["1m"].loc[t, u]) + pct_rank(B.r["3m"].loc[t, u]) + pct_rank(B.r["6m"].loc[t, u])
    leader = combo >= combo.quantile(0.95)
    px = close.loc[t, u]
    filt = [S["adr20"].loc[t, u] >= 0.04, px / S["hi52"].loc[t, u] - 1 >= -0.15,
            (px > S["sma10"].loc[t, u]) & (px > S["sma20"].loc[t, u]), dvol50.loc[t, u] >= 20e6]
    fails = sum((~f.fillna(False)).astype(int) for f in filt)
    return leader, fails


def pb_leaders_on(rows, u):
    one = {n: S[n].loc[rows, u] for n in ("r1m", "r3m", "r6m")}
    one["dollar_vol50"] = dvol50.loc[rows, u]
    lead, _ = sd.pullback_leaders(one, pd.DataFrame(True, index=rows, columns=u))
    return lead


def pullback_list(rows, u):
    """The shipped pullback list (leader & resting) on each of `rows`."""
    return pb_leaders_on(rows, u) & S["pb_rest"].loc[rows, u].fillna(False).astype(bool)


def pullback_approaching(t, u):
    lead = pb_leaders_on([t], u).iloc[0]
    g = lambda n: S[n].loc[t, u].fillna(False).astype(bool)
    resting = g("pb_higher_lows") & g("pb_contracting") & g("pb_above") & g("pb_trend")
    near = S["pb_low"].loc[t, u] <= S["ema21"].loc[t, u] * 1.05
    sel = lead & resting & ~g("pb_touched") & near
    return sel


# (screen's list, its setting-up list) per shipped strategy id
SCREENS = {
    "momentum": (lambda t, u: ranked(B.mom12_1.loc[t, u], 0, frac_n(B.mom12_1.loc[t, u], 0.10)),
                 lambda t, u: ranked(B.mom12_1.loc[t, u], frac_n(B.mom12_1.loc[t, u], 0.10),
                                     2 * frac_n(B.mom12_1.loc[t, u], 0.10))),
    "low_vol": (lambda t, u: ranked(-B.vol252.loc[t, u], 0, frac_n(B.vol252.loc[t, u], 0.10)),
                lambda t, u: ranked(-B.vol252.loc[t, u], frac_n(B.vol252.loc[t, u], 0.10),
                                    2 * frac_n(B.vol252.loc[t, u], 0.10))),
    "breakout": (lambda t, u: (lambda lf: list(lf[0].index[lf[0] & (lf[1] == 0)]))(breakout(t, u)),
                 lambda t, u: (lambda lf: list(lf[0].index[lf[0] & (lf[1] == 1)]))(breakout(t, u))),
    "magic_formula": (lambda t, u: ranked(F.metrics(t, u)["magic"], 0, 30),
                      lambda t, u: ranked(F.metrics(t, u)["magic"], 30, 60)),
    "piotroski": (lambda t, u: (lambda m: list(m.index[m["f"] >= 8]))(F.metrics(t, u)),
                  lambda t, u: (lambda m: list(m.index[m["f"] == 7]))(F.metrics(t, u))),
    "quality_momentum": (
        lambda t, u: ranked(B.mom12_1.loc[t, F.top(F.metrics(t, u)["gpa"], frac=0.30)], 0, 25),
        lambda t, u: ranked(B.mom12_1.loc[t, F.top(F.metrics(t, u)["gpa"], frac=0.30)], 25, 50)),
    "trending_value": (
        lambda t, u: ranked(B.r["6m"].loc[t, F.top(F.metrics(t, u)["value"], frac=0.10)], 0, 25),
        lambda t, u: ranked(B.r["6m"].loc[t, F.top(F.metrics(t, u)["value"], frac=0.10)], 25, 50)),
    "luk_pullback": (lambda t, u: list(pullback_list([t], u).iloc[0].pipe(lambda x: x[x].index)),
                     lambda t, u: list(pullback_approaching(t, u).pipe(lambda x: x[x].index))),
}
SCREENS = {k: v for k, v in SCREENS.items() if len(sys.argv) < 2 or k in sys.argv[1:]}


def universe(t):
    return [s for s in sorted(B.members_on(t)) if s in close.columns and B.hist_len.at[t, s] >= 260
            and pd.notna(close.at[t, s])]


def graduation(sid):
    screen, setup = SCREENS[sid]
    rows = []
    idx = close.index
    for i in range(len(B.dates) - 1):
        t0, t1 = pd.Timestamp(B.dates[i]), pd.Timestamp(B.dates[i + 1])
        u0 = universe(t0)
        nxt_set = set(setup(t0, u0))
        on0 = set(screen(t0, u0))
        others = set(u0) - on0 - nxt_set
        if sid == "luk_pullback":
            # the pullback list is a daily state: on it on ANY of the next 10 sessions
            j = idx.get_loc(t0)
            fut = idx[j + 1:j + 11]
            hit = set(pullback_list(fut, u0).any(axis=0).pipe(lambda x: x[x].index)) if len(fut) else set()
        else:
            hit = set(screen(t1, universe(t1)))
        rate = lambda a: len(a & hit) / len(a) if a else np.nan
        rows.append((rate(nxt_set), rate(others)))
    a = np.array(rows, dtype=float)
    return float(np.nanmean(a[:, 0])), float(np.nanmean(a[:, 1]))


if __name__ == "__main__":
    strats = {f"{sid} | setting up": setup for sid, (_, setup) in SCREENS.items()}
    res, _, _ = B.run(strats, {})
    B.report(res, {}, list(strats))
    path = os.path.join(B.D, "setups_all_results.pkl")
    if len(sys.argv) > 1 and os.path.exists(path):  # a partial run adds to the full one
        old = pd.read_pickle(path)
        res = pd.concat([old.drop(columns=[c for c in res.columns if c in old.columns and c not in
                                           ("EW", "SPY", "n_univ")]), res[[c for c in res.columns if
                                                                          c not in ("EW", "SPY", "n_univ")]]],
                        axis=1)
    res.to_pickle(path)
    gpath = os.path.join(B.D, "setups_all_graduation.pkl")
    grad = pd.read_pickle(gpath).to_dict() if len(sys.argv) > 1 and os.path.exists(gpath) else {}
    print("\nOn the screen next time (mean of monthly rates): setting up vs every other name")
    for sid in SCREENS:
        g1, g0 = graduation(sid)
        grad[sid] = (g1, g0)
        print(f"  {sid:18s} {g1:6.1%}  vs  {g0:6.1%}")
    pd.Series(grad).to_pickle(gpath)
