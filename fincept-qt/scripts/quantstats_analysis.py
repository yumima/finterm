"""
QuantStats Analysis — Comprehensive quantitative statistics for a portfolio.
Input: JSON as the first argv argument (PythonRunner passes it there; an
"@/path" argument names a spill file holding the JSON), stdin as fallback:
  {"symbols": ["AAPL","MSFT"],
   "weights_by_symbol": {"AAPL": 0.5, "MSFT": 0.5},   # preferred
   "weights": [0.5, 0.5],                             # legacy positional
   "risk_free": 0.042}                                # annual decimal; null = unknown
Output: JSON to stdout with performance, risk, and ratio metrics.

Weights are keyed by symbol because yf.download orders columns its own way
and a single failed symbol used to shift every weight onto the wrong ticker
(or silently fall back to equal weight). Any symbol that returns no data is
reported in "dropped_symbols" — the caller decides whether a partial answer
is still an answer. Note the model: constant weights, i.e. a daily-rebalanced
portfolio, not buy-and-hold; stated here so nobody mistakes it.

A statistic that cannot be computed (no variance, no losing days, unknown
risk-free rate, NaN/inf) is emitted as null — never 0.0, which would read as
a real measurement. The caller renders null as a dash.
"""
import sys

from script_args import read_input
import json
import numpy as np


def convert_numpy(obj):
    if isinstance(obj, dict):
        return {k: convert_numpy(v) for k, v in obj.items()}
    elif isinstance(obj, (list, tuple)):
        return [convert_numpy(v) for v in obj]
    elif isinstance(obj, (np.integer,)):
        return int(obj)
    elif isinstance(obj, (np.floating,)):
        v = float(obj)
        if np.isnan(v) or np.isinf(v):
            return None  # unknown, not zero
        return v
    elif isinstance(obj, np.ndarray):
        return [convert_numpy(x) for x in obj]
    elif isinstance(obj, float):
        if np.isnan(obj) or np.isinf(obj):
            return None  # unknown, not zero
    return obj


def compute_stats(symbols, weights_by_symbol, risk_free=None, period="1y"):
    import yfinance as yf

    # auto_adjust=True deliberately: statistics over TOTAL returns, so a
    # dividend is performance rather than a phantom down-day.
    data = yf.download(symbols, period=period, interval="1d", progress=False,
                       auto_adjust=True)
    if data is None or data.empty:
        return {"error": "Could not fetch price data"}

    close = data["Close"]
    import pandas as pd
    if not isinstance(close, pd.DataFrame):
        close = pd.DataFrame({symbols[0]: close})

    close = close.dropna(axis=1, how="all")
    dropped = [s for s in symbols if s not in close.columns]
    if close.shape[1] == 0:
        return {"error": "No symbol returned price data", "dropped_symbols": dropped}

    returns = close.pct_change().dropna()
    # Weights aligned BY SYMBOL to the columns that actually arrived, then
    # renormalised over the survivors.
    if returns.empty:
        return {"error": "Not enough overlapping price history to compute returns",
                "dropped_symbols": dropped}
    w = np.array([float(weights_by_symbol.get(c, 0.0)) for c in returns.columns])
    if w.sum() <= 0:
        # No real weight survived — inventing equal weights would analyse a
        # portfolio the user does not hold.
        return {"error": "No weighted symbol returned price data", "dropped_symbols": dropped}
    w = w / w.sum()

    port_returns = (returns * w).sum(axis=1)
    cumulative = (1 + port_returns).cumprod()

    # Unknown risk-free rate (the app never fetched one): the rf-dependent
    # figures are null rather than computed against an invented hurdle.
    rf = float(risk_free) if risk_free is not None else None
    rf_daily = rf / 252 if rf is not None else None
    trading_days = len(port_returns)
    ann_factor = 252

    # cumulative already includes the first day's return, so the total is
    # cumulative[-1] - 1; dividing by cumulative[0] silently dropped day 1.
    total_return = float(cumulative.iloc[-1] - 1)
    ann_return = float((1 + total_return) ** (ann_factor / max(trading_days, 1)) - 1)
    ann_vol = float(port_returns.std() * np.sqrt(ann_factor))
    sharpe = float((ann_return - rf) / ann_vol) if (rf is not None and ann_vol > 0) else None
    # Downside deviation over the FULL sample vs the risk-free MAR — dividing
    # only by the count of down days (the old .std() over the negative subset)
    # understates Sortino, increasingly so the fewer down days there are.
    if rf is not None:
        downside = np.minimum(port_returns - rf_daily, 0.0)
        sortino_vol = float(np.sqrt((downside ** 2).mean()) * np.sqrt(ann_factor))
        sortino = float((ann_return - rf) / sortino_vol) if sortino_vol > 0 else None
    else:
        sortino_vol = None
        sortino = None

    # Drawdown from the 1.0 starting value, so a first-day loss counts.
    peak = cumulative.expanding().max().clip(lower=1.0)
    drawdown = (cumulative - peak) / peak
    max_dd = float(drawdown.min())
    calmar = float(ann_return / abs(max_dd)) if max_dd != 0 else None

    var_95 = float(np.percentile(port_returns, 5))
    cvar_95 = float(port_returns[port_returns <= var_95].mean()) if len(port_returns[port_returns <= var_95]) > 0 else var_95

    wins = int((port_returns > 0).sum())
    losses = int((port_returns < 0).sum())
    win_rate = float(wins / (wins + losses)) if (wins + losses) > 0 else None

    best_day = float(port_returns.max())
    worst_day = float(port_returns.min())
    avg_win = float(port_returns[port_returns > 0].mean()) if wins > 0 else None
    avg_loss = float(port_returns[port_returns < 0].mean()) if losses > 0 else None
    profit_factor = (float(abs(avg_win * wins) / abs(avg_loss * losses))
                     if (avg_win is not None and avg_loss is not None and avg_loss != 0) else None)

    skew = float(port_returns.skew())
    kurt = float(port_returns.kurtosis())

    return {
        "risk_free_used": rf,
        "dropped_symbols": dropped,
        "performance": {
            "total_return": total_return,
            "annualized_return": ann_return,
            "trading_days": trading_days,
            "best_day": best_day,
            "worst_day": worst_day,
            "avg_daily_return": float(port_returns.mean()),
        },
        "risk": {
            "annualized_volatility": ann_vol,
            "max_drawdown": max_dd,
            "var_95_daily": var_95,
            "cvar_95_daily": cvar_95,
            "downside_deviation": sortino_vol,
        },
        "ratios": {
            "sharpe_ratio": sharpe,
            "sortino_ratio": sortino,
            "calmar_ratio": calmar,
            "profit_factor": profit_factor,
        },
        "distribution": {
            "skewness": skew,
            "kurtosis": kurt,
            "win_rate": win_rate,
            "win_days": wins,
            "loss_days": losses,
            "avg_win": avg_win,
            "avg_loss": avg_loss,
        }
    }


def main():
    raw = read_input()
    if not raw.strip():
        print(json.dumps({"error": "No input data"}))
        return

    try:
        params = json.loads(raw)
    except json.JSONDecodeError as e:
        print(json.dumps({"error": f"Invalid JSON input: {e}"}))
        return
    symbols = params.get("symbols", [])
    if not symbols:
        print(json.dumps({"error": "No symbols provided"}))
        return

    weights_by_symbol = params.get("weights_by_symbol") or {}
    if not weights_by_symbol:
        # Legacy positional list: meaningful only while every symbol downloads,
        # kept for old callers; keyed weights are what the app sends.
        legacy = params.get("weights", [])
        if legacy and len(legacy) == len(symbols):
            weights_by_symbol = dict(zip(symbols, legacy))
        else:
            print(json.dumps({"error": "No weights provided"}))
            return

    try:
        result = compute_stats(symbols, weights_by_symbol, params.get("risk_free"))
    except Exception as e:
        result = {"error": f"QuantStats analysis failed: {e}"}
    # allow_nan=False: any non-finite value that slipped past convert_numpy
    # fails loudly instead of emitting invalid JSON (NaN) the app cannot parse.
    print(json.dumps(convert_numpy(result), allow_nan=False))


if __name__ == "__main__":
    main()
