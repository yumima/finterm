"""Backtest the price-based screener strategies on point-in-time S&P 500 membership.

Run collect.py first (data/prices.pkl, data/members.pkl).

Method
------
- Rebalance at each month-end close, 2010-01 .. last complete month.
- Eligible on date t: in the S&P 500 on t (fja05680 membership), with >= 260
  bars of history ending at t. Signals read data <= t only.
- Each strategy holds its picks equal-weight for one month. Forward return is
  close(t+1m) / close(t) - 1; a name whose data ends inside the month is
  marked at its last close (acquisitions end near the deal price; the names
  Yahoo dropped entirely are the survivorship hole, measured in coverage).
- Costs: 10 bp per unit of one-way turnover.
- Baselines: equal-weight eligible universe (EW) and SPY.
- Excess = strategy minus EW, monthly. t-stat on non-overlapping monthly
  excess. IC = Spearman(signal, forward 1m return) per month.
"""
import os, pickle, sys
import numpy as np
import pandas as pd
from scipy import stats

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")
COST = 0.0010

with open(os.path.join(D, "prices.pkl"), "rb") as fh:
    frames = pickle.load(fh)
members = pd.read_pickle(os.path.join(D, "members.pkl")).set_index("date").sort_index()

def panel(col):
    return pd.DataFrame({s: f[col] for s, f in frames.items()}).sort_index()

close, high, low, vol = panel("Close"), panel("High"), panel("Low"), panel("Volume")
close.index = pd.to_datetime(close.index).tz_localize(None)
for p in (high, low, vol):
    p.index = close.index
spy = close.pop("SPY")
high = high.drop(columns="SPY"); low = low.drop(columns="SPY"); vol = vol.drop(columns="SPY")

# --- signals: the SHIPPED definitions (scripts/screener_data.py) -------------
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "fincept-qt", "scripts"))
import screener_data as sd

S = sd.price_signals(close, high, low)
r = {k[1:]: S[k] for k in ("r1m", "r3m", "r6m", "r9m", "r12m")}
mom12_1, rs_raw, vol252, adr20, tt_core = S["mom12_1"], S["rs_raw"], S["vol252"], S["adr20"], S["tt_core"]
pth = close / S["hi52"]

# --- month-end rebalance dates ---------------------------------------------
me = close.index.to_series().groupby(close.index.to_period("M")).max()
me = me[(me >= "2010-01-01")]
me = me.iloc[:-1] if me.iloc[-1].month == pd.Timestamp.today().month else me
dates = list(me.values)

def members_on(t):
    row = members.loc[:t].iloc[-1]
    return {s.replace(".", "-") for s in row["tickers"]}

def fwd_return(names, t0, t1):
    seg = close.loc[t0:t1, list(names)]
    start = seg.iloc[0]
    end = seg.ffill().iloc[-1]
    return (end / start - 1)

hist_len = close.notna().cumsum()

def pct_rank(s):
    return s.rank(pct=True)

STRATS = {}

def strat(name):
    def deco(fn):
        STRATS[name] = fn
        return fn
    return deco

def top_frac(s, frac):
    s = s.dropna()
    n = max(1, int(round(len(s) * frac)))
    return list(s.sort_values(ascending=False, kind="stable").index[:n])

@strat("Momentum 12-1 (top decile)")
def _(t, u): return top_frac(mom12_1.loc[t, u], 0.10)

@strat("52-week-high nearness (top decile)")
def _(t, u): return top_frac(pth.loc[t, u], 0.10)

@strat("Minervini Trend Template (all passers)")
def _(t, u):
    rs = pct_rank(rs_raw.loc[t, u])
    ok = tt_core.loc[t, u] & (rs >= 0.70)
    return list(ok[ok].index)

@strat("Trend Template, top 25 by RS")
def _(t, u):
    rs = pct_rank(rs_raw.loc[t, u])
    ok = tt_core.loc[t, u] & (rs >= 0.70)
    return list(rs[ok].sort_values(ascending=False, kind="stable").index[:25])

@strat("RS composite (top decile)")
def _(t, u): return top_frac(rs_raw.loc[t, u], 0.10)

@strat("Leaders: top 2% by 1/3/6m (Qullamaggie-style)")
def _(t, u):
    score = pct_rank(r["1m"].loc[t, u]) + pct_rank(r["3m"].loc[t, u]) + pct_rank(r["6m"].loc[t, u])
    return top_frac(score, 0.02)

@strat("Low volatility (lowest decile)")
def _(t, u): return top_frac(-vol252.loc[t, u], 0.10)

@strat("1-month reversal (biggest losers decile)")
def _(t, u): return top_frac(-r["1m"].loc[t, u], 0.10)

@strat("Momentum 12-1 within Trend Template, top 25")
def _(t, u):
    rs = pct_rank(rs_raw.loc[t, u])
    ok = tt_core.loc[t, u] & (rs >= 0.70)
    return list(mom12_1.loc[t, u][ok].dropna().sort_values(ascending=False, kind="stable").index[:25])

IC_SIGNALS = {
    "mom12_1": mom12_1, "pth (52w-high nearness)": pth, "rs composite": rs_raw,
    "-vol252": -vol252, "-ret1m (reversal)": -r["1m"], "trend template (0/1)": tt_core.astype(float),
}

# --- run ----------------------------------------------------------------------
def run(strats, ic_signals):
    rows, ics, held_prev = [], {k: [] for k in ic_signals}, {k: set() for k in strats}
    coverage = []
    for i in range(len(dates) - 1):
        t0, t1 = pd.Timestamp(dates[i]), pd.Timestamp(dates[i + 1])
        mem = members_on(t0)
        # sorted + stable sorts: names AT their 52-week high tie at 1.0, and set
        # order (string hashing is randomised per process) would break the tie
        have = [s for s in sorted(mem) if s in close.columns and hist_len.at[t0, s] >= 260
                and pd.notna(close.at[t0, s])]
        coverage.append((t0, len(mem), len(have)))
        fr = fwd_return(have, t0, t1)
        row = {"date": t0, "EW": fr.mean(), "SPY": spy.loc[t1] / spy.loc[t0] - 1, "n_univ": len(have)}
        for name, fn in strats.items():
            picks = [p for p in fn(t0, have) if pd.notna(fr.get(p))]
            g = fr[picks].mean() if picks else np.nan
            prev = held_prev[name]
            cur = set(picks)
            turnover = 1.0 if not prev else len(cur - prev) / max(len(cur), 1)
            row[name] = g - 2 * COST * turnover if picks else 0.0
            row[name + "|n"] = len(picks)
            held_prev[name] = cur
        for k, sig in ic_signals.items():
            x = sig.loc[t0, have] if isinstance(sig, pd.DataFrame) else sig(t0, have)
            ok = x.notna() & fr.notna()
            if ok.sum() > 50:
                ics[k].append(stats.spearmanr(x[ok], fr[ok]).statistic)
        rows.append(row)
    res = pd.DataFrame(rows).set_index("date")
    cov = pd.DataFrame(coverage, columns=["date", "members", "with_data"]).set_index("date")
    return res, ics, cov


def summarize(s, bench):
    s = s.dropna()
    eq = (1 + s).cumprod()
    yrs = len(s) / 12
    cagr = eq.iloc[-1] ** (1 / yrs) - 1
    vol_ = s.std() * np.sqrt(12)
    dd = (eq / eq.cummax() - 1).min()
    ex = (s - bench.loc[s.index])
    t = ex.mean() / ex.std() * np.sqrt(len(ex)) if ex.std() > 0 else np.nan
    hit = (ex > 0).mean()
    return cagr, vol_, cagr / vol_ if vol_ else np.nan, dd, ex.mean() * 12, t, hit


def report(res, ics, names):
    out = []
    for c in ["EW", "SPY"] + list(names):
        cagr, v, sr, dd, ex, t, hit = summarize(res[c], res["EW"])
        n = res[c + "|n"].median() if c + "|n" in res else res["n_univ"].median()
        out.append((c, cagr, v, sr, dd, ex, t, hit, n))
    tab = pd.DataFrame(out, columns=["strategy", "CAGR", "vol", "CAGR/vol", "maxDD",
                                     "excess/yr vs EW", "t(excess)", "hit vs EW", "median n"])
    pd.set_option("display.width", 220); pd.set_option("display.max_columns", 20)
    fmt = {c: "{:.1%}".format for c in ["CAGR", "vol", "maxDD", "excess/yr vs EW", "hit vs EW"]}
    fmt.update({"CAGR/vol": "{:.2f}".format, "t(excess)": "{:.2f}".format, "median n": "{:.0f}".format})
    print(f"Period {res.index[0].date()} .. {res.index[-1].date()} ({len(res)} months), cost {COST*1e4:.0f}bp/side")
    print(tab.to_string(index=False, formatters=fmt))

    print("\nExcess/yr vs EW by sub-period")
    periods = [("2010-01", "2014-12"), ("2015-01", "2019-12"), ("2020-01", "2022-12"), ("2023-01", "2026-12")]
    sub = {}
    for c in list(names) + ["SPY"]:
        sub[c] = [((res.loc[a:b, c] - res.loc[a:b, "EW"]).mean() * 12) for a, b in periods]
    print(pd.DataFrame(sub, index=[f"{a[:4]}-{b[:4]}" for a, b in periods]).T.to_string(float_format=lambda x: f"{x:+.1%}"))

    print("\nMonthly IC (Spearman vs forward 1m return)")
    for k, v in ics.items():
        v = np.array(v)
        print(f"  {k:28s} mean {v.mean():+.3f}  t {v.mean()/v.std()*np.sqrt(len(v)):+.2f}  months {len(v)}")


if __name__ == "__main__":
    res, ics, cov = run(STRATS, IC_SIGNALS)
    # CAN SLIM "M" / Minervini market filter: in cash when SPY closed the month
    # below its 200-day average. Applied to EW too, so timing and selection
    # can be told apart.
    spy_up = (spy > spy.rolling(200).mean()).reindex(res.index)
    names = list(STRATS)
    for c in ["EW", "Momentum 12-1 (top decile)", "Minervini Trend Template (all passers)",
              "Trend Template, top 25 by RS", "RS composite (top decile)"]:
        res[c + " +M"] = res[c].where(spy_up, 0.0)
        names.append(c + " +M")
    report(res, ics, names)
    print("\nCoverage (index members with Yahoo data), by year")
    cy = cov.groupby(cov.index.year).mean()
    print((cy["with_data"] / cy["members"]).to_string(float_format=lambda x: f"{x:.1%}"))
    res.to_pickle(os.path.join(D, "results.pkl"))
