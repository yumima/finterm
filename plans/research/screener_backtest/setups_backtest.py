"""Do "setting up" lists — names one rule away from a screen — beat the index?

The question behind it: the screens list what already happened (MRNA appears
after +280%); a reader wants what could come next. The candidate answer is a
list of names that fail exactly ONE rule today, plus the fresh passers. Same
method and data as backtest.py (point-in-time S&P 500, monthly, equal weight,
10 bp per side, excess vs the equal-weighted universe), same shipped signal
definitions (screener_data.price_signals). Run after collect.py.

Also measured: how often a one-away name is ON the screen a month later
("graduation"), against the base rate for any non-passer.
"""
import os
import sys

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import backtest as bt  # loads the data and the shipped signals

S, close, r = bt.S, bt.close, bt.r
pct_rank, top_frac = bt.pct_rank, bt.top_frac
tt_count, tt_core, rs_raw, mom12_1 = S["tt_count"], S["tt_core"], S["rs_raw"], S["mom12_1"]
adr20, hi52, sma10, sma20 = S["adr20"], S["hi52"], S["sma10"], S["sma20"]
dvol50 = (close * bt.vol).rolling(50, min_periods=40).mean()


def tt_rules(t, u):
    """(passes, failed-count) on the 8 shipped rules: 7 price criteria + RS >= 70."""
    rs = pct_rank(rs_raw.loc[t, u])
    price_ok = tt_count.loc[t, u]  # 0..7
    fails = (7 - price_ok) + (rs < 0.70).astype(int)
    return fails == 0, fails


def tt_passers(t, u):
    ok, _ = tt_rules(t, u)
    return set(ok[ok].index)


def breakout_rules(t, u):
    combo = pct_rank(r["1m"].loc[t, u]) + pct_rank(r["3m"].loc[t, u]) + pct_rank(r["6m"].loc[t, u])
    leader = combo >= combo.quantile(0.95)
    px = close.loc[t, u]
    filt = [adr20.loc[t, u] >= 0.04, px / hi52.loc[t, u] - 1 >= -0.15,
            (px > sma10.loc[t, u]) & (px > sma20.loc[t, u]), dvol50.loc[t, u] >= 20e6]
    fails = sum((~f.fillna(False)).astype(int) for f in filt)
    return leader, fails


STRATS = {}


def strat(name):
    def deco(fn):
        STRATS[name] = fn
        return fn
    return deco


@strat("Trend Template (all passers) [reference]")
def _(t, u):
    return sorted(tt_passers(t, u))


@strat("TT one rule away")
def _(t, u):
    _, fails = tt_rules(t, u)
    return sorted(fails[fails == 1].index)


@strat("TT one rule away, the rule = RS (price trend complete)")
def _(t, u):
    rs = pct_rank(rs_raw.loc[t, u])
    ok = (tt_count.loc[t, u] == 7) & (rs < 0.70)
    return sorted(ok[ok].index)


@strat("TT one rule away, the rule = a price criterion (RS >= 70)")
def _(t, u):
    rs = pct_rank(rs_raw.loc[t, u])
    ok = (tt_count.loc[t, u] == 6) & (rs >= 0.70)
    return sorted(ok[ok].index)


_prev_tt = {}


@strat("TT fresh passers (new this month)")
def _(t, u):
    i = bt.dates.index(np.datetime64(t)) if np.datetime64(t) in bt.dates else None
    cur = tt_passers(t, u)
    prev_t = pd.Timestamp(bt.dates[i - 1]) if i else None
    prev = tt_passers(prev_t, [s for s in u if s in close.columns]) if prev_t is not None else set()
    return sorted(cur - prev)


@strat("TT passers within 5% of the 52-week high (near a pivot)")
def _(t, u):
    ok, _ = tt_rules(t, u)
    near = close.loc[t, u] / hi52.loc[t, u] - 1 >= -0.05
    sel = ok & near
    return sorted(sel[sel].index)


@strat("Breakout Leaders (shipped filters) [reference]")
def _(t, u):
    leader, fails = breakout_rules(t, u)
    sel = leader & (fails == 0)
    return sorted(sel[sel].index)


@strat("Breakout Leaders one filter away")
def _(t, u):
    leader, fails = breakout_rules(t, u)
    sel = leader & (fails == 1)
    return sorted(sel[sel].index)


@strat("Momentum 12-1, the next decile (ranks 10-20%)")
def _(t, u):
    top10 = set(top_frac(mom12_1.loc[t, u], 0.10))
    top20 = top_frac(mom12_1.loc[t, u], 0.20)
    return [s for s in top20 if s not in top10]


def graduation():
    """P(on the screen next month | one rule away now) vs | any non-passer:
    the Trend Template (all one-away, and the RS >= 70 subset that misses one
    price criterion) and the Breakout Leaders."""
    rows = []
    for i in range(len(bt.dates) - 1):
        t0, t1 = pd.Timestamp(bt.dates[i]), pd.Timestamp(bt.dates[i + 1])
        mem = bt.members_on(t0)
        u0 = [s for s in sorted(mem) if s in close.columns and bt.hist_len.at[t0, s] >= 260
              and pd.notna(close.at[t0, s])]
        u1 = [s for s in sorted(bt.members_on(t1)) if s in close.columns and bt.hist_len.at[t1, s] >= 260
              and pd.notna(close.at[t1, s])]
        _, f0 = tt_rules(t0, u0)
        nxt = tt_passers(t1, u1)
        one = set(f0[f0 == 1].index)
        non = set(f0[f0 >= 1].index)
        rs0 = pct_rank(rs_raw.loc[t0, u0])
        price1 = set(((tt_count.loc[t0, u0] == 6) & (rs0 >= 0.70)).pipe(lambda x: x[x].index))
        lead0, bf0 = breakout_rules(t0, u0)
        lead1, bf1 = breakout_rules(t1, u1)
        b_one = set((lead0 & (bf0 == 1)).pipe(lambda x: x[x].index))
        b_non = set((bf0.index[~(lead0 & (bf0 == 0))]))
        b_nxt = set((lead1 & (bf1 == 0)).pipe(lambda x: x[x].index))
        frac = lambda a_, b_: len(a_ & b_) / len(a_) if a_ else np.nan
        rows.append((frac(one, nxt), frac(non, nxt), frac(price1, nxt), frac(b_one, b_nxt), frac(b_non, b_nxt)))
    a = np.nanmean(np.array(rows, dtype=float), axis=0)
    return a


if __name__ == "__main__":
    res, ics, cov = bt.run(STRATS, {})
    bt.report(res, {}, list(STRATS))
    g = graduation()
    print(f"\nOn the screen a month later (mean of monthly rates):")
    print(f"  Trend Template: one rule away {g[0]:.1%}, its RS >= 70 subset {g[2]:.1%}, any non-passer {g[1]:.1%}")
    print(f"  Breakout Leaders: one filter away {g[3]:.1%}, any non-passer {g[4]:.1%}")
    res.to_pickle(os.path.join(bt.D, "setups_results.pkl"))
    pd.Series({"tt_one_away": g[0], "tt_non_passer": g[1], "tt_price_one_away": g[2],
               "brk_one_away": g[3], "brk_non_passer": g[4]}).to_pickle(os.path.join(bt.D, "setups_graduation.pkl"))
