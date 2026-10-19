"""Trade-level backtest of Martin Luk's pullback playbook (USIC 2025 winner).

The screens elsewhere hold a list for a month. This method's claimed edge is
not the list, it is the trade: buy a leader's turn at support with a stop
2.5-3% away, risk 0.5% of the account, sell a third at 3R and at 5R, trail
the rest on the 9-day EMA. So this simulates trades, day by day, with the
shipped signals (screener_data.pullback_signals via price_signals).

    python pullback_backtest.py [prices.pkl universe: "sp500" (default) | "broad"]

Rules, per session t (all known at t's close unless stated):
  leader   in the universe on t; top 25% by the combined 1/3/6-month return
           rank; up 30%+ over 1, 3 or 6 months; 50-day dollar volume >= $20M
  setup    leader & pb_rest (higher lows, contracting range, above the 9/21 EMA,
           21 > 50 EMA, a low in the last 3 sessions at the 21 EMA)
  entry    buy stop at t's high, filled on t+1 if t+1 trades through it (at the
           open if it gaps over); skipped if the gap leaves risk above 5%
  stop     just under the 3-session pullback low, at least 1% under the entry;
           a setup needing more than 3% is skipped (not tightened). Controls
           (random, leaders-only) get the SAME 3% cap, so "same exits" holds
  size     shares = RISK * equity / (fill - stop); no leverage (cash-capped)
  exits    stop (at the stop, or the open if it gaps through); 1/3 at 3R, 1/3
           at 5R; after the first partial the stop moves to breakeven and the
           rest exits on a close below the 9 EMA
  costs    10 bp per side

Daily bars cannot say whether the entry day's low came before or after the
trigger. "base" stops the entry day out only if it CLOSES below the stop;
"pessimistic" stops it whenever the day's low reaches the stop. The truth is
between; both are reported.
"""
import os
import sys

import numpy as np
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import backtest as B  # noqa: E402  (the PIT S&P 500 panel and membership)

COST = 0.0010
START = "2010-01-01"


def membership_mask(close):
    """Boolean panel: in the S&P 500 on that date (PIT)."""
    m = pd.DataFrame(False, index=close.index, columns=close.columns)
    mem = B.members
    dates = list(mem.index)
    for i, d in enumerate(dates):
        end = dates[i + 1] if i + 1 < len(dates) else close.index[-1] + pd.Timedelta(days=1)
        rows = (close.index >= d) & (close.index < end)
        if not rows.any():
            continue
        cols = [s.replace(".", "-") for s in mem.iloc[i]["tickers"]]
        cols = [c for c in cols if c in m.columns]
        m.loc[rows, cols] = True
    return m


def leaders(S, universe):
    """Step 1 — the SHIPPED definition (screener_data.pullback_leaders)."""
    return B.sd.pullback_leaders(S, universe)


def simulate(close, high, low, opn, S, setup, rank, risk=0.005, pessimistic=False, start=START):
    idx = close.index
    t0 = idx.searchsorted(pd.Timestamp(start))
    C, H, L, O = (x.to_numpy() for x in (close, high, low, opn))
    E9 = S["ema9"].to_numpy()
    ENT, STP = S["pb_entry"].to_numpy(), S["pb_stop"].to_numpy()
    SET, RNK = setup.to_numpy(), rank.to_numpy()
    cols = list(close.columns)
    cash, equity = 1.0, 1.0
    pos = {}  # col -> dict(sh, entry, stop, r, part)
    curve, trades = [], []
    for t in range(max(t0, 1), len(idx)):
        # 1) manage open positions on bar t
        for j in list(pos):
            p = pos[j]
            o, h, l, c = O[t, j], H[t, j], L[t, j], C[t, j]
            if not np.isfinite(c):
                # No bar. After 5 missing sessions in a row it is gone (delisted
                # or acquired): exit at its last close. Decided from the past
                # only — the first draft peeked 5 sessions ahead.
                p["missing"] = p.get("missing", 0) + 1
                if p["missing"] >= 5:
                    cash += p["sh"] * p["last"] * (1 - COST)
                    p["proceeds"] += p["sh"] * p["last"] * (1 - COST)
                    trades.append(p); del pos[j]
                continue
            p["missing"], p["last"] = 0, c
            exit_px = None
            if np.isfinite(o) and o <= p["stop"]:
                exit_px = o
            elif l <= p["stop"]:
                exit_px = p["stop"]
            if exit_px is None:
                for k, mult in ((0, 3), (1, 5)):
                    tgt = p["entry"] + mult * p["r"]
                    if p["part"] == k and h >= tgt:
                        sell = p["sh0"] / 3
                        cash += sell * tgt * (1 - COST)
                        p["proceeds"] += sell * tgt * (1 - COST)
                        p["sh"] -= sell
                        p["part"] += 1
                        p["stop"] = max(p["stop"], p["entry"])  # breakeven after the first partial
                if p["part"] >= 1 and np.isfinite(E9[t, j]) and c < E9[t, j]:
                    exit_px = c
            if exit_px is not None:
                cash += p["sh"] * exit_px * (1 - COST)
                p["proceeds"] += p["sh"] * exit_px * (1 - COST)
                p["sh"] = 0
                trades.append(p); del pos[j]
        # 2) fills for setups flagged at t-1's close
        cand = [j for j in np.flatnonzero(SET[t - 1]) if j not in pos]
        # highest combined rank first; names without a rank (random-control
        # picks without full history) last, deterministically — NaN keys sort
        # arbitrarily
        cand.sort(key=lambda j: (-np.nan_to_num(RNK[t - 1, j], nan=-1.0), j))
        for j in cand:
            e, st = ENT[t - 1, j], STP[t - 1, j]
            o, h, l, c = O[t, j], H[t, j], L[t, j], C[t, j]
            if not (np.isfinite(e) and np.isfinite(st) and np.isfinite(h) and h >= e):
                continue
            fill = max(o, e) if np.isfinite(o) else e
            r = fill - st
            if r <= 0 or r / fill > 0.05:
                continue
            sh = risk * equity / r
            cost = sh * fill * (1 + COST)
            if cost > cash:
                sh = cash / (fill * (1 + COST))
                cost = sh * fill * (1 + COST)
                if sh * r < 0.25 * risk * equity:  # too small to matter: no cash
                    continue
            cash -= cost
            p = {"col": cols[j], "date": idx[t], "entry": fill, "stop": st, "r": r, "sh": sh,
                 "sh0": sh, "part": 0, "cost": cost, "proceeds": 0.0,
                 "last": c if np.isfinite(c) else fill}
            stopped = (c < st) if not pessimistic else (l <= st)
            if stopped:
                px = min(st, c) if not pessimistic else st
                cash += sh * px * (1 - COST)
                p["proceeds"] = sh * px * (1 - COST)
                p["sh"] = 0
                trades.append(p)
            else:
                # partials count on the entry bar too: a breakout day that runs
                # through 3R banks its third like any later bar would
                for k, mult in ((0, 3), (1, 5)):
                    tgt = p["entry"] + mult * p["r"]
                    if p["part"] == k and h >= tgt:
                        sell = p["sh0"] / 3
                        cash += sell * tgt * (1 - COST)
                        p["proceeds"] += sell * tgt * (1 - COST)
                        p["sh"] -= sell
                        p["part"] += 1
                        p["stop"] = max(p["stop"], p["entry"])
                pos[j] = p
        # 3) mark to market
        # a missing bar is marked at the LAST close, not the entry price (which
        # made a one-bar hole read as a round trip to cost and back)
        mtm = sum(p["sh"] * p["last"] for p in pos.values())
        equity = cash + mtm
        curve.append((idx[t], equity, mtm / equity if equity > 0 else 0))
    for p in pos.values():  # still open at the end: closed at the last close, so they count
        p["proceeds"] += p["sh"] * p["last"] * (1 - COST)
        trades.append(p)
    eq = pd.DataFrame(curve, columns=["date", "equity", "exposure"]).set_index("date")
    tr = pd.DataFrame([{"date": p["date"], "col": p["col"],
                        "R": (p["proceeds"] - p["cost"]) / (p["sh0"] * p["r"]),
                        "ret": p["proceeds"] / p["cost"] - 1} for p in trades])
    return eq, tr


def report(name, eq, tr, bench):
    yrs = (eq.index[-1] - eq.index[0]).days / 365.25
    cagr = eq["equity"].iloc[-1] ** (1 / yrs) - 1
    dd = (eq["equity"] / eq["equity"].cummax() - 1).min()
    m = eq["equity"].resample("ME").last().pct_change().dropna()
    b = bench.reindex(m.index)
    ex = m - b
    t = ex.mean() / ex.std() * np.sqrt(len(ex))
    win = (tr["R"] > 0).mean()
    print(f"{name:34s} CAGR {cagr:6.1%}  maxDD {dd:6.1%}  exposure {eq['exposure'].mean():5.1%}  "
          f"trades {len(tr):5d}  win {win:5.1%}  avgR {tr['R'].mean():+.2f}  "
          f"avg win {tr.loc[tr.R > 0, 'R'].mean():+.2f}R  avg loss {tr.loc[tr.R <= 0, 'R'].mean():+.2f}R  "
          f"excess/yr vs EW {ex.mean() * 12:+.1%} (t {t:+.2f})")
    return {"cagr": cagr, "max_dd": dd, "trades": len(tr), "win": win, "avg_R": tr["R"].mean(),
            "excess": ex.mean() * 12, "t": t, "exposure": eq["exposure"].mean()}


def main():
    close, high, low = B.close, B.high, B.low
    opn = pd.DataFrame({s: f["Open"] for s, f in B.frames.items() if s != "SPY"}).sort_index()
    opn.index = close.index[: len(opn)] if len(opn) == len(close) else pd.to_datetime(opn.index).tz_localize(None)
    opn = opn.reindex(index=close.index, columns=close.columns)
    vol = B.vol.reindex(columns=close.columns)
    S = B.sd.price_signals(close, high, low, vol)
    uni = membership_mask(close)
    lead, combo = leaders(S, uni)
    setup = lead & S["pb_rest"]
    cap3 = S["pb_stop"] >= S["pb_entry"] * (1 - B.sd.PULLBACK_MAX_RISK)  # the setup's own stop cap
    lead = lead & cap3
    print(f"setups flagged: {int(setup.loc[START:].to_numpy().sum())} stock-days, "
          f"{setup.loc[START:].any(axis=1).mean():.0%} of sessions have one")
    # equal-weight monthly benchmark from backtest.py's run
    res, _, _ = B.run({}, {})
    # a row of backtest.run at month-end t holds the return from t to t+1:
    # index it by the month it was earned in
    bench = res["EW"].copy()
    bench.index = bench.index + pd.offsets.MonthEnd(0) + pd.offsets.MonthEnd(1)
    rate = setup.loc[START:].to_numpy().sum() / (uni & cap3).loc[START:].to_numpy().sum()
    rand = pd.DataFrame(np.random.default_rng(0).random(close.shape) < rate,
                        index=close.index, columns=close.columns) & uni & cap3
    out = {}
    for label, st, kw in [("base, 0.5% risk", setup, {}),
                          ("pessimistic fills, 0.5% risk", setup, {"pessimistic": True}),
                          ("base, 1.0% risk", setup, {"risk": 0.01}),
                          ("leaders only (no rest/zone)", lead, {}),
                          ("RANDOM entries (same rate)", rand, {})]:
        eq, tr = simulate(close, high, low, opn, S, st, combo, **kw)
        out[label] = (eq, tr, report(label, eq, tr, bench))
    eq, tr, _ = out["base, 0.5% risk"]
    tr["year"] = pd.to_datetime(tr["date"]).dt.year
    print("\nby year (base): trades, win rate, avg R")
    print(tr.groupby("year")["R"].agg(["count", lambda r: (r > 0).mean(), "mean"]).to_string(
        float_format=lambda x: f"{x:.2f}"))
    spy = B.spy.loc[START:]
    print(f"\nSPY over the same span: CAGR {(spy.iloc[-1] / spy.iloc[0]) ** (365.25 / (spy.index[-1] - spy.index[0]).days) - 1:.1%}")
    pd.to_pickle({k: (v[0], v[1], v[2]) for k, v in out.items()}, os.path.join(B.D, "pullback_results.pkl"))


def main_broad():
    """Second read on today's >= $300M universe (survivorship-biased: only names
    alive today, which flatters a long strategy)."""
    import pickle
    frames = pickle.load(open(os.path.join(B.D, "broad_prices.pkl"), "rb"))
    def panel(col):
        p = pd.DataFrame({k: f[col] for k, f in frames.items()}).sort_index()
        p.index = pd.to_datetime(p.index)
        if p.index.tz is not None:
            p.index = p.index.tz_localize(None)
        return p
    close, high, low, opn, vol = (panel(c) for c in ("Close", "High", "Low", "Open", "Volume"))
    spy = close.pop("SPY")
    for p in (high, low, opn, vol):
        p.drop(columns="SPY", inplace=True)
    # vendor zeros (2 frames) are absent prices, not prices of zero
    ok = (close > 0) & (low > 0)
    close, high, low, opn = close.where(ok), high.where(ok), low.where(ok), opn.where(ok)
    S = B.sd.price_signals(close, high, low, vol)
    uni = close.notna() & (close.notna().cumsum() >= 260)
    lead, combo = leaders(S, uni)
    setup = lead & S["pb_rest"]
    cap3 = S["pb_stop"] >= S["pb_entry"] * (1 - B.sd.PULLBACK_MAX_RISK)
    lead = lead & cap3
    start = close.index[260].strftime("%Y-%m-%d")
    print(f"broad universe {close.shape[1]} names, {start}..{close.index[-1]:%Y-%m-%d}; "
          f"setups {int(setup.loc[start:].to_numpy().sum())} stock-days")
    # equal-weight monthly benchmark of the same universe
    me = close.loc[start:].resample("ME").last()
    bench = me.pct_change(fill_method=None).where(uni.resample("ME").last()).mean(axis=1).dropna()
    rng = np.random.default_rng(0)
    # the setup's rate PER IN-UNIVERSE stock-day (over all cells it was diluted
    # by names not yet in the universe, so the control fired less often)
    rate = setup.loc[start:].to_numpy().sum() / (uni & cap3).loc[start:].to_numpy().sum()
    rand = pd.DataFrame(rng.random(close.shape) < rate, index=close.index, columns=close.columns) & uni & cap3
    out = {}
    for label, st, kw in [("broad: base, 0.5% risk", setup, {}),
                          ("broad: pessimistic fills", setup, {"pessimistic": True}),
                          ("broad: leaders only", lead, {}),
                          ("broad: RANDOM control", rand, {})]:
        eq, tr = simulate(close, high, low, opn, S, st, combo, start=start, **kw)
        out[label] = report(label, eq, tr, bench)
        q = tr["R"].quantile([0.05, 0.25, 0.5, 0.75, 0.95])
        print(f"{'':34s} R quantiles 5/25/50/75/95%: " + " ".join(f"{v:+.2f}" for v in q)
              + f"   losses beyond -1.5R: {(tr.R < -1.5).mean():.1%}")
    s0 = spy.loc[start:]
    print(f"SPY {(s0.iloc[-1] / s0.iloc[0]) ** (365.25 / (s0.index[-1] - s0.index[0]).days) - 1:.1%}/yr; "
          f"EW universe {((1 + bench).prod()) ** (12 / len(bench)) - 1:.1%}/yr over the same span")
    pd.to_pickle(out, os.path.join(B.D, "pullback_broad.pkl"))


if __name__ == "__main__":
    main_broad() if len(sys.argv) > 1 and sys.argv[1] == "broad" else main()
