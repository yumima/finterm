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
    s = res[col].dropna()
    ex = s - res.loc[s.index, "EW"]
    eq = (1 + s).cumprod()
    return {
        "excess": num(ex.mean() * 12, 4),
        "t": num(ex.mean() / ex.std() * np.sqrt(len(ex)) if ex.std() > 0 else np.nan, 2),
        "max_dd": num((eq / eq.cummax() - 1).min(), 4),
        "hit": num((ex > 0).mean(), 3),
        "subperiods": [num((res.loc[a:b, col] - res.loc[a:b, "EW"]).mean() * 12, 4)
                       for a, b in PERIODS],
        "median_names": int(res[col + "|n"].median()) if col + "|n" in res else None,
    }


cols = [c for c in res.columns if not c.endswith("|n") and c not in ("EW", "SPY", "n_univ")]
out = {
    "basis": (f"S&P 500 point-in-time, {res.index[0]:%Y-%m} to {res.index[-1]:%Y-%m}, monthly "
              "rebalance, 10 bp/side; excess = per year vs the equal-weighted index"),
    "subperiods": ["2010-14", "2015-19", "2020-22", "2023-26"],
    "months": len(res),
    "backtests": {c: stats(c) for c in cols},
}
with open(OUT, "w") as fh:
    json.dump(out, fh, indent=1, sort_keys=True, allow_nan=False)
print(f"wrote {len(cols)} backtests to {os.path.normpath(OUT)}")
