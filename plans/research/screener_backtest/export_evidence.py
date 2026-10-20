"""Write the SCREENER tab's evidence card numbers from the backtest results.

    python backtest.py && python fund_backtest.py && python export_evidence.py

Writes fincept-qt/scripts/screener_evidence.json, which screener_data.py
reads. The numbers on the card are therefore never hand-copied — a first
draft carried a mistyped sub-period, and the Magic Formula's row measured an
inverted rank; both would have been caught had the card been generated.
"""
import json
import os
import sys

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")
OUT = os.path.join(HERE, "..", "..", "..", "fincept-qt", "scripts", "screener_evidence.json")

PERIODS = [("2010-01", "2014-12"), ("2015-01", "2019-12"), ("2020-01", "2022-12"), ("2023-01", "2026-12")]

price = pd.read_pickle(os.path.join(D, "results.pkl"))
fund = pd.read_pickle(os.path.join(D, "fund_results.pkl"))
assert price.index.equals(fund.index), "the two backtests must cover the same months"
res = pd.concat([price, fund.drop(columns=[c for c in fund.columns if c in price.columns])], axis=1)


def num(x, nd):
    """A finite rounded float, or None — the file must never hold NaN."""
    x = float(x)
    return round(x, nd) if np.isfinite(x) else None


def stats(col):
    # months that held names only (an empty month is booked as 0.0 cash by the
    # runners, and its "excess" would be minus the index): see setup_stats
    s = res[col].dropna()
    if col + "|n" in res:
        s = s[res.loc[s.index, col + "|n"] > 0]
    ex = s - res.loc[s.index, "EW"]
    eq = (1 + s).cumprod()
    return {
        "excess": num(ex.mean() * 12, 4),
        "t": num(ex.mean() / ex.std() * np.sqrt(len(ex)) if ex.std() > 0 else np.nan, 2),
        "max_dd": num((eq / eq.cummax() - 1).min(), 4),
        "hit": num((ex > 0).mean(), 3),
        "subperiods": [num((ex.loc[a:b]).mean() * 12, 4) for a, b in PERIODS],
        "median_names": int(res[col + "|n"].median()) if col + "|n" in res else None,
        **({"trades": int(len(TRADES[col])), "win_rate": num((TRADES[col]["R"] > 0).mean(), 3),
            "avg_R": num(TRADES[col]["R"].mean(), 2)} if col in TRADES else {}),
    }


# Luk pullback: a TRADE simulation (pullback_backtest.py), not a monthly list.
# Its daily equity becomes monthly returns on the same calendar, so the card
# compares it with the same index; trade statistics ride along.
pb = pd.read_pickle(os.path.join(D, "pullback_results.pkl"))
eq, tr, _ = pb["base, 0.5% risk"]
pb_m = eq["equity"].resample("ME").last().pct_change()
months = res.index + pd.offsets.MonthEnd(0) + pd.offsets.MonthEnd(1)   # row t = return over t..t+1
res["Luk pullback (trades, 0.5% risk)"] = pb_m.reindex(months).to_numpy()
TRADES = {"Luk pullback (trades, 0.5% risk)": tr}
broad = pd.read_pickle(os.path.join(D, "pullback_broad.pkl"))

cols = [c for c in res.columns if not c.endswith("|n") and c not in ("EW", "SPY", "n_univ")]
out = {
    "basis": (f"S&P 500 point-in-time, {res.index[0]:%Y-%m} to {res.index[-1]:%Y-%m}, monthly "
              "rebalance, 10 bp/side; excess = per year vs the equal-weighted index"),
    "subperiods": ["2010-14", "2015-19", "2020-22", "2023-26"],
    "months": len(res),
    "backtests": {c: stats(c) for c in cols},
    # the same playbook on today's >= $300M universe, 5y: a second read with
    # survivorship bias in its favour (only names alive today)
    "pullback_broad": {k: {kk: num(vv, 4) if isinstance(vv, float) else vv for kk, vv in v.items()}
                       for k, v in broad.items()},
}
# Setting-up lists (setups_backtest.py): names one rule away from a screen.
# Same months and index as the screens; the qualify-next-month rates are
# that script's graduation measurement.
setups = pd.read_pickle(os.path.join(D, "setups_results.pkl"))
grad = pd.read_pickle(os.path.join(D, "setups_graduation.pkl"))
assert setups.index.equals(res.index), "the setups backtest must cover the same months"


def setup_stats(col):
    # Months that HELD names only: bt.run books an empty month as 0.0 (cash),
    # and a cash month's excess is just minus the index — the RS-trap list was
    # empty in 38 of 200 months, which inflated its record from -5.6% (t -2.4)
    # to -9.6% (t -3.0) in a first export.
    held = setups[col + "|n"] > 0
    s = setups.loc[held, col].dropna()
    ex = s - setups.loc[s.index, "EW"]
    eq = (1 + s).cumprod()
    return {"excess": num(ex.mean() * 12, 4),
            "t": num(ex.mean() / ex.std() * np.sqrt(len(ex)) if ex.std() > 0 else np.nan, 2),
            "max_dd": num((eq / eq.cummax() - 1).min(), 4),
            "subperiods": [num(((s - setups.loc[s.index, "EW"]).loc[a:b]).mean() * 12, 4) for a, b in PERIODS],
            "median_names": int(setups[col + "|n"].median()),
            "months_held": int(held.sum()), "months": int(len(setups))}


out["setups"] = {
    # what the tab lists: RS >= 70 and exactly one of the 7 price criteria missed
    "trend_template": {**setup_stats("TT one rule away, the rule = a price criterion (RS >= 70)"),
                       "qualify_next": num(grad["tt_price_one_away"], 3),
                       "qualify_base": num(grad["tt_non_passer"], 3)},
    # what it leaves out, and why: the full price trend without the RS
    "rs_trap": setup_stats("TT one rule away, the rule = RS (price trend complete)"),
}
# every other screen's next-in-line list (setups_all_backtest.py)
setups_all = pd.read_pickle(os.path.join(D, "setups_all_results.pkl"))
grad_all = pd.read_pickle(os.path.join(D, "setups_all_graduation.pkl"))
assert setups_all.index.equals(res.index), "the setups backtest must cover the same months"
setups = setups_all  # setup_stats reads the module-level frame
for sid, (g1, g0) in grad_all.items():
    out["setups"][sid] = {**setup_stats(f"{sid} | setting up"),
                          "qualify_next": num(g1, 3), "qualify_base": num(g0, 3)}
with open(OUT, "w") as fh:
    json.dump(out, fh, indent=1, sort_keys=True, allow_nan=False)
print(f"wrote {len(cols)} backtests to {os.path.normpath(OUT)}")
