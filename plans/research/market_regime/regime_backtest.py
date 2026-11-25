"""The weekly brief's market call: what each state has been followed by.

    python regime_backtest.py      writes app/scripts/regime_evidence.json

State at each week's last close since 1990 (VIX starts then), the shipped
definition (weekly_brief_core.regime_state): the S&P 500 against its 200-day
average, and the VIX against 20 and 30 — conventional levels, chosen before
the test and not tuned. Forward numbers are the S&P 500 return with dividends
over the next 63 sessions (a quarter), whether the index ever closed 10% under
the starting close within them, and the size of the next week's move.

The stance is tested as a rule: that share of the money in the S&P 500 for the
next week, the rest in 13-week T-bills, 5 bp per unit traded.

Weekly 63-session windows overlap, so no t-stat: the interval is a moving-block
bootstrap (26-week blocks) of the state's mean minus the mean of all weeks.
"""
import json
import os
import sys

import numpy as np
import pandas as pd
import yfinance as yf

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "..", "..", "app", "scripts")
sys.path.insert(0, SCRIPTS)
from weekly_brief_core import REGIMES, STANCE, regime_state  # noqa: E402

H = 63


def closes(sym):
    s = yf.Ticker(sym).history(period="max", auto_adjust=False)["Close"].dropna()
    s.index = pd.to_datetime(s.index).tz_localize(None).normalize()
    return s


px, tr = closes("^GSPC"), closes("^SP500TR")
vix = closes("^VIX").reindex(px.index)
bill = closes("^IRX").reindex(px.index).ffill() / 100  # 13-week T-bill, annual
tr = tr.reindex(px.index)
sma = px.rolling(200).mean()
weeks = px.index.to_series().groupby(px.index.to_period("W-FRI")).max()
weeks = weeks[(weeks >= "1990-03-01")]

rows = []
for d in weeks:
    i = px.index.get_loc(d)
    if not np.isfinite(vix.iloc[i]):
        continue
    ok = i + H < len(px)
    path = px.iloc[i + 1:i + H + 1] if ok else None
    rows.append({"date": d, "state": regime_state(px.iloc[i], sma.iloc[i], vix.iloc[i]),
                 "f": tr.iloc[i + H] / tr.iloc[i] - 1 if ok else np.nan,
                 "dd10": float(path.min() / px.iloc[i] - 1 <= -0.10) if ok else np.nan,
                 "move": abs(px.iloc[i + 5] / px.iloc[i] - 1) if i + 5 < len(px) else np.nan})
t = pd.DataFrame(rows).set_index("date")
full = t.dropna(subset=["f"])


def stats(g):
    return {"weeks": int(len(g)), "mean": round(float(g.f.mean()), 4), "up": round(float((g.f > 0).mean()), 3),
            "dd10": round(float(g.dd10.mean()), 3), "week_move": round(float(g.move.mean()), 4)}


rng = np.random.default_rng(7)
L, n = 26, len(full)
draws = [np.concatenate([np.arange(i, i + L) % n for i in rng.integers(0, n, n // L + 1)])[:n] for _ in range(4000)]


def interval(state, col):
    s, f = full.state.to_numpy(), full[col].to_numpy()
    d = [f[k][s[k] == state].mean() - f[k].mean() for k in draws if (s[k] == state).sum() > 5]
    return [round(float(x), 4) for x in np.percentile(d, [2.5, 97.5])]


out = {"basis": f"S&P 500, {full.index[0]:%Y-%m} to {full.index[-1]:%Y-%m}, weekly, next 63 sessions",
       "from": f"{full.index[0]:%Y}", "to": f"{full.index[-1]:%Y}", "all": stats(full), "states": {}}
half = full.index[0] + (full.index[-1] - full.index[0]) / 2
for k in REGIMES:
    g = full[full.state == k]
    out["states"][k] = {**stats(g), "mean_diff_95": interval(k, "f"), "dd10_diff_95": interval(k, "dd10"),
                        "halves": [stats(g.loc[:half]), stats(g.loc[half:])]}

# The rule, and what it is compared with: next week's return, like for like.
wr = tr.reindex(weeks.values).pct_change().shift(-1)
cash = (1 + bill.reindex(weeks.values)) ** (1 / 52) - 1
st = t.state.reindex(weeks.values)


def run(exposure):
    e = st.map(exposure).astype(float)
    r = (e * wr + (1 - e) * cash - e.diff().abs().fillna(0) * 0.0005).dropna()
    eq = (1 + r).cumprod()
    half = len(r) // 2
    one = lambda x: round(float((1 + x).prod() ** (52 / len(x)) - 1), 4)
    return {"cagr": one(r), "vol": round(float(r.std() * 52 ** 0.5), 4),
            "worst": round(float((eq / eq.cummax() - 1).min()), 3),
            "halves": [one(r.iloc[:half]), one(r.iloc[half:])], "changes_a_year": round(float((e.diff().abs() > 0).sum() / len(r) * 52), 1)}


below = ("bearish", "fear")
out["rule"] = run(STANCE)
out["always"] = run({k: 1.0 for k in REGIMES})
out["compared"] = {
    "half when bearish only": run({k: 0.5 if k == "bearish" else 1.0 for k in REGIMES}),
    "none when bearish only": run({k: 0.0 if k == "bearish" else 1.0 for k in REGIMES}),
    "none below the average": run({k: 0.0 if k in below else 1.0 for k in REGIMES}),
}
with open(os.path.join(SCRIPTS, "regime_evidence.json"), "w") as fh:
    json.dump(out, fh, indent=1, sort_keys=True)
print(json.dumps(out, indent=1))
