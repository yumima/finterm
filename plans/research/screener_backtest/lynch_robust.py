"""How far to trust the Lynch Value record (plans/research/lynch_screen_2026-11.md).

    python fund_backtest.py && python lynch_robust.py

The shipped list held one or two names a month, so: drop its biggest names one
at a time; draw as many names at random from the same month's passers of every
test before the price (100 draws); the year-by-year excess; and the record when
the index is held in the months the list is empty.
"""
import os
import sys

import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fund_backtest as F  # noqa: E402
B = F.B
def stat(res, col):
    s = res[col]; s = s[res[col + "|n"] > 0]
    ex = s - res.loc[s.index, "EW"]
    return ex.mean() * 12, ex.mean() / ex.std() * np.sqrt(len(ex)), len(s)
strats = {"ship": F.lynch_list}
for sym in ["PHM", "AAPL", "META", "REGN", "LEN", "RHI"]:
    strats[f"-{sym}"] = (lambda sym: lambda t, u: [p for p in F.lynch_list(t, u) if p != sym])(sym)
top3 = {"PHM", "AAPL", "META"}
strats["-PHM,AAPL,META"] = lambda t, u: [p for p in F.lynch_list(t, u) if p not in top3]
homes = {"PHM", "LEN", "DHI", "NVR", "TOL", "KBH"}
strats["-homebuilders"] = lambda t, u: [p for p in F.lynch_list(t, u) if p not in homes]
# control: as many names, at random, from the same month's passers of every test before the price
K = 100
def ctrl(k):
    r = np.random.default_rng(1000 + k)
    def f(t, u):
        n = len(F.lynch_list(t, u))
        m = F.metrics(t, u); pool = sorted(m.index[m["lyn_good"].astype(bool)])
        return list(r.choice(pool, size=min(n, len(pool)), replace=False)) if n and pool else []
    return f
for k in range(K):
    strats[f"ctrl{k}"] = ctrl(k)
res, _, _ = B.run(strats, {})
for c in strats:
    if not c.startswith("ctrl"):
        e, t, n = stat(res, c); print(f"{c:18s} excess {e:+.1%} t {t:.2f} months {n}")
ce = np.array([stat(res, f"ctrl{k}")[0] for k in range(K)]); ct = np.array([stat(res, f"ctrl{k}")[1] for k in range(K)])
e0 = stat(res, "ship")[0]
print(f"random same-count picks from the passers: excess median {np.median(ce):+.1%}, 95th pct {np.percentile(ce,95):+.1%}; t median {np.median(ct):.2f}; share >= shipped {(ce >= e0).mean():.2f}")
s = res["ship"]; s = s[res["ship|n"] > 0]; ex = s - res.loc[s.index, "EW"]
print("by year:"); print((ex.groupby(ex.index.year).agg(["mean", "count"]).assign(mean=lambda d: d["mean"] * 12)).round(3).to_string())
# the index in the months the list is empty: those months' excess is zero
ex_all = (res["ship"] - res["EW"]).where(res["ship|n"] > 0, 0.0)
print(f"index when empty, all {len(ex_all)} months: excess {ex_all.mean() * 12:+.1%} "
      f"t {ex_all.mean() / ex_all.std() * np.sqrt(len(ex_all)):.2f}")
names = pd.Series([p for t in B.dates[:-1] for p in F.lynch_list(pd.Timestamp(t), [s for s in sorted(B.members_on(pd.Timestamp(t))) if s in B.close.columns])])
print(f"companies ever listed: {names.nunique()}; most months: {names.value_counts().head(6).to_dict()}")
