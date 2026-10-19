#!/usr/bin/env python3
"""The SCREENER tab's backend: universe parse, price signals and the screens.

No network. Synthetic price paths and fundamentals pin the rules that decide
what appears on each list — above all the vendor-null rule: a value the vendor
did not report is ABSENT, and an absent value must neither rank (a missing
EBIT is not an earnings yield of zero) nor fail a test it was never measured
on (a missing ROE is not a CAN SLIM rejection on ROE — it is no data).

Exit 77 (ctest SKIPPED) without pandas.
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))

try:
    import numpy as np
    import pandas as pd
except ImportError:
    print("pandas/numpy not available — skipping")
    sys.exit(77)

import screener_data as sd

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
        print("FAIL:", msg)


# ── parse_universe ───────────────────────────────────────────────────────────

cols = list(sd.TV_COLUMNS)


def tv_row(**kv):
    return {"d": [kv.get(c) for c in cols]}


rows = [
    tv_row(name="AAA", description="Alpha Corp", sector="Technology Services",
           market_cap_basic=5e9, return_on_equity_fq=25.0, dividends_yield_current=None,
           average_volume_30d_calc=1e6),
    # same company, two classes: the more traded one wins
    tv_row(name="BRK.A", description="Berkshire Hathaway Inc. Class A", sector="Finance",
           market_cap_basic=1e12, average_volume_30d_calc=200),
    tv_row(name="BRK.B", description="Berkshire Hathaway Inc. Class B", sector="Finance",
           market_cap_basic=1e12, average_volume_30d_calc=4e6),
]
u = sd.parse_universe(rows)
check(set(u) == {"AAA", "BRK-B"}, f"share classes collapse to the traded one, TV dot -> Yahoo dash: {sorted(u)}")
check(abs(u["AAA"]["roe"] - 0.25) < 1e-12, "TradingView percent converted to a fraction")
check(u["AAA"]["div_yield"] is None, "absent dividend yield stays None, not 0")
check(u["AAA"]["piotroski"] is None, "absent Piotroski stays None")
check(sd._num(float("nan")) is None and sd._num(True) is None and sd._num("x") is None,
      "_num: NaN / bool / text are absent")

# ── price_signals ────────────────────────────────────────────────────────────

n = 320
idx = pd.bdate_range("2025-01-01", periods=n)
t = np.arange(n)
rng = np.random.default_rng(7)
paths = {
    # steady uptrend: passes every Trend Template price criterion
    "UP": 50 * np.exp(0.003 * t) * (1 + 0.004 * rng.standard_normal(n)),
    # steady downtrend: fails
    "DOWN": 100 * np.exp(-0.002 * t) * (1 + 0.004 * rng.standard_normal(n)),
    # calm and flat: lowest volatility
    "CALM": 40 * (1 + 0.001 * rng.standard_normal(n)),
    # violent and flat
    "WILD": 40 * (1 + 0.05 * rng.standard_normal(n)),
}
close = pd.DataFrame(paths, index=idx)
high, low = close * 1.01, close * 0.99
sig = sd.price_signals(close, high, low, close * 0 + 1e6)
last = {k: v.iloc[-1] for k, v in sig.items()}
check(bool(last["tt_core"]["UP"]), "uptrend passes the Trend Template price criteria")
check(not bool(last["tt_core"]["DOWN"]), "downtrend fails it")
check(last["tt_count"]["UP"] == 7, f"all 7 price criteria counted once each: {last['tt_count']['UP']}")
short = sd.price_signals(close.iloc[:150], high.iloc[:150], low.iloc[:150])
check(not short["tt_core"].iloc[-1].any(),
      "too little history for a 200-day average never passes (NaN comparisons are False)")
check(abs(last["mom12_1"]["UP"] - (close["UP"].iloc[-22] / close["UP"].iloc[-253] - 1)) < 1e-12,
      "12-1 momentum skips the latest month")
check(last["vol252"]["CALM"] < last["vol252"]["WILD"], "volatility orders calm below wild")

# ── compute: the screens ─────────────────────────────────────────────────────

frames = {s: pd.DataFrame({"Open": close[s], "High": high[s], "Low": low[s], "Close": close[s],
                           "Volume": 1e6}) for s in close.columns}
spy = 400 * np.exp(0.001 * t)
frames["SPY"] = pd.DataFrame({"Open": spy, "High": spy, "Low": spy, "Close": spy, "Volume": 1e8}, index=idx)


def fund_rec(sym, **kv):
    base = {k: None for k, _ in sd.TV_COLUMNS.values()}
    base.update({"symbol": sym, "name": sym, "sector": "Technology Services"})
    base.update(kv)
    return base


fund = {
    "UP": fund_rec("UP", ebit=5e8, ev=5e9, roic=0.30, gross_profit=4e9, total_assets=1e10,
                   piotroski=9, pb=2.0, eps_growth_q=0.4, eps_growth_fy=0.5, roe=0.3),
    # cheapest on EBIT/EV but a bank: excluded from the Magic Formula
    "DOWN": fund_rec("DOWN", sector="Finance", ebit=9e8, ev=1e9, roic=0.5, piotroski=8, pb=0.5),
    # EBIT absent: must not rank as an earnings yield of zero
    "CALM": fund_rec("CALM", ebit=None, ev=3e9, roic=0.2, piotroski=None),
    # CAN SLIM growth but ROE unreported: no data, not a pass
    "WILD": fund_rec("WILD", ebit=1e8, ev=2e9, roic=0.1, eps_growth_q=0.6, eps_growth_fy=0.6, roe=None),
}
df, lists, market, asof = sd.compute(fund, frames)

# worked examples: the card's rule-by-rule check of the first pick must pass
# every rule, and the first name that missed must fail at least one — else the
# example's comparisons have drifted from the screen's
ex_extras = {}
sd.compute(fund, frames, ex_extras)
for sid_, ex_ in ex_extras["examples"].items():
    if lists.get(sid_):
        pick_ = ex_.get("pick") or {}
        check(pick_.get("symbol") == lists[sid_][0] and pick_["checks"] and all(c["ok"] for c in pick_["checks"]),
              f"{sid_}: the first pick passes every checked rule: {pick_}")
    if "miss" in ex_:
        check(ex_["miss"]["symbol"] not in lists.get(sid_, [])
              and not all(c["ok"] for c in ex_["miss"]["checks"]),
              f"{sid_}: the first miss fails a checked rule: {ex_['miss']}")
check(set(ex_extras["examples"]) == {st["id"] for st in sd.STRATEGIES},
      f"every strategy has a worked example (none failed silently): {sorted(ex_extras['examples'])}")
json.dumps(ex_extras["examples"], allow_nan=False)

# The same on a 60-name market wide enough that the top-25/top-30 cuts, the
# 95% quantile and the pools all bind, and most strategies have BOTH a pick
# and a miss: a check that drifts from its screen shows up here.
rng60 = np.random.default_rng(11)
n60 = 330
idx60 = pd.bdate_range("2025-01-01", periods=n60)
t60 = np.arange(n60)
fr60, fund60 = {}, {}
for i in range(60):
    drift = rng60.uniform(-0.002, 0.006)
    vol = rng60.uniform(0.005, 0.04)
    c = 30 * np.exp(drift * t60 + np.cumsum(vol * rng60.standard_normal(n60)))
    if i % 7 == 0:  # some resting leaders: a strong run, then a shallow, tightening pullback
        c = 30 * np.exp(0.006 * np.minimum(t60, n60 - 12)) * (1 + 0.004 * rng60.standard_normal(n60))
    if i % 7 == 1:  # explosive movers: a big, volatile run to new highs (breakout candidates)
        c = 20 * np.exp(0.009 * t60) * (1 + 0.01 * rng60.standard_normal(n60))
    rngs = 1 + rng60.uniform(0.005, 0.05, n60)
    if i % 7 == 1:
        rngs = 1 + rng60.uniform(0.05, 0.08, n60)  # ADR well above 4%
    hi, lo = c * np.sqrt(rngs), c / np.sqrt(rngs)
    sym = f"S{i:02d}"
    fr60[sym] = pd.DataFrame({"Open": c, "High": hi, "Low": lo, "Close": c,
                              "Volume": rng60.uniform(2e5, 5e6)}, index=idx60)
    mc = float(rng60.uniform(2e9, 2e11))
    fund60[sym] = fund_rec(sym, sector=["Technology Services", "Finance", "Health Technology"][i % 3],
                           industry=f"I{i % 6}", mcap=mc, ev=mc * rng60.uniform(0.8, 1.3),
                           ebit=mc * rng60.uniform(-0.02, 0.15), roic=rng60.uniform(-0.1, 0.4),
                           gross_profit=mc * rng60.uniform(0.05, 0.5), total_assets=mc * rng60.uniform(0.5, 2),
                           piotroski=int(rng60.integers(3, 10)), pb=rng60.uniform(-1, 8),
                           eps_growth_q=rng60.uniform(-0.5, 1.0), eps_growth_fy=rng60.uniform(-0.5, 1.0),
                           roe=rng60.uniform(-0.1, 0.5), net_income=mc * rng60.uniform(-0.02, 0.1),
                           revenue=mc * rng60.uniform(0.1, 2), ocf=mc * rng60.uniform(-0.02, 0.12),
                           div_yield=rng60.uniform(0, 0.04), buyback_yield=rng60.uniform(0, 0.03))
    if i % 7 in (0, 1):  # growth leaders: CAN SLIM's earnings and returns
        fund60[sym].update(eps_growth_q=0.6, eps_growth_fy=0.5, roe=0.3)
fr60["SPY"] = frames["SPY"].reindex(idx60).ffill().bfill()
ex60 = {}
_, lists60, _, _ = sd.compute(fund60, fr60, ex60)
both = 0
for st in sd.STRATEGIES:
    sid_ = st["id"]
    ex_ = ex60["examples"].get(sid_)
    check(ex_ is not None, f"60 names: {sid_} has a worked example")
    if not ex_:
        continue
    if lists60.get(sid_):
        check(ex_["pick"]["symbol"] == lists60[sid_][0] and all(c["ok"] for c in ex_["pick"]["checks"]),
              f"60 names: {sid_} pick passes every check: {ex_['pick']}")
    if "miss" in ex_:
        check(ex_["miss"]["symbol"] not in lists60.get(sid_, [])
              and not all(c["ok"] for c in ex_["miss"]["checks"]),
              f"60 names: {sid_} miss fails a check: {ex_['miss']}")
    both += bool(ex_.get("pick")) and bool(ex_.get("miss"))
# all but the Leader Pullback, whose resting-at-support setup a random market
# does not produce (its pick path was checked on the real 2026-10-19 scan)
check(both >= 10, f"60 names: strategies with both a pick and a miss: {both} of {len(sd.STRATEGIES)}")
check("nan" not in json.dumps(ex60["examples"]).lower().replace("finance", ""),
      "no example value prints a literal nan")
check("SPY" not in df.index, "the benchmark is not a screen candidate")
check(lists["magic_formula"][:1] == ["UP"] and "DOWN" not in lists["magic_formula"],
      f"Magic Formula ranks operating companies only: {lists['magic_formula']}")
check("CALM" not in lists["magic_formula"], "absent EBIT does not rank")
check(np.isnan(df.at["CALM", "earnings_yield"]), "absent EBIT -> earnings yield NaN, not 0")
check(set(lists["piotroski"]) == {"UP", "DOWN"} and lists["piotroski"][0] == "DOWN",
      f"F >= 8, cheapest book first; absent score excluded: {lists['piotroski']}")
check("WILD" not in lists["canslim"], "unreported ROE is not a CAN SLIM pass")
check(lists["low_vol"] == ["CALM"], f"low volatility = calmest decile: {lists['low_vol']}")
check("UP" in lists["trend_template"] and "DOWN" not in lists["trend_template"],
      f"trend template list: {lists['trend_template']}")
check(np.isnan(df.at["CALM", "shareholder_yield"]),
      "no dividend and no buyback reported -> shareholder yield absent, not 0")
check(np.isnan(df.at["CALM", "value_score"]), "value composite needs >= 4 of its 6 ratios")
check(np.isnan(df.at["DOWN", "gpa"]), "gross profitability is not computed for Finance")
check(market.get("above200") is True, f"market regime read from SPY: {market}")
check(asof == idx[-1].strftime("%Y-%m-%d"), "as-of is the last bar's exchange date")
check(1 <= df["rs"].min() and df["rs"].max() <= 99, "RS rating is a 1..99 percentile")

# ── output contract ──────────────────────────────────────────────────────────

check(sd._clean(float("nan")) is None and sd._clean(np.float64("inf")) is None,
      "NaN/inf serialise as null")
check(sd._clean(np.bool_(True)) is True, "numpy bool serialises as a JSON bool")
row = {k: sd._clean(df.at["CALM", k]) for k in sd.OUT_FIELDS if k in df.columns}
json.dumps(row, allow_nan=False)  # raises if a NaN slipped through
ids = [s["id"] for s in sd.STRATEGIES]
check(len(ids) == len(set(ids)) and set(ids) == set(lists),
      "every strategy has a list and every list a strategy")
for s in sd.STRATEGIES:
    check(all(c in sd.OUT_FIELDS for c in s["columns"]), f"{s['id']}: columns are emitted fields")
    check("backtest" in s and s.get("note"), f"{s['id']}: names its backtest (or None) and carries a note")

# every column a strategy emits must have a C++ definition, or the screen
# silently drops it (ScreenerScreen skips unknown keys)
import re
hdr = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src",
                        "screens", "screener", "ScreenerModel.h"), encoding="utf-8").read()
cpp_keys = set(re.findall(r'\{"([a-z0-9_]+)", "[^"]+", Kind::', hdr))
check(len(cpp_keys) > 20, f"parsed the C++ column table: {len(cpp_keys)} keys")
for s_ in sd.STRATEGIES:
    missing = [c for c in s_["columns"] if c not in cpp_keys]
    check(not missing, f"{s_['id']}: columns without a C++ ColumnDef: {missing}")

# ── review fixes ─────────────────────────────────────────────────────────────

# one missing interior bar must not blank a stock's 52-week/SMA signals
gap = close.copy()
gap.iloc[-30, gap.columns.get_loc("UP")] = np.nan
gs = sd.price_signals(gap, gap * 1.01, gap * 0.99)
check(bool(gs["tt_core"]["UP"].iloc[-1]), "a one-bar hole does not drop the uptrend from the Trend Template")
check(np.isfinite(gs["hi52"]["UP"].iloc[-1]), "52-week high survives a one-bar hole")
# a missing LAST bar is carried (<= 5 sessions, no look at later bars); a
# series that ended longer ago goes NaN
ended = close.copy()
ended.iloc[-3:, ended.columns.get_loc("DOWN")] = np.nan
es = sd.price_signals(ended, ended * 1.01, ended * 0.99)
check(np.isfinite(es["sma50"]["DOWN"].iloc[-1]), "a 3-bar trailing hole is carried")
ended.iloc[-8:, ended.columns.get_loc("DOWN")] = np.nan
es = sd.price_signals(ended, ended * 1.01, ended * 0.99)
check(np.isnan(es["sma50"]["DOWN"].iloc[-1]), "a series that ended 8 sessions ago is not carried")
# volatility is measured on real bars: carried bars are not 0% days
holes = close.copy()
holes.iloc[-100::10, holes.columns.get_loc("WILD")] = np.nan
hs = sd.price_signals(holes, holes * 1.01, holes * 0.99)
want = holes["WILD"].pct_change(fill_method=None).rolling(252, min_periods=200).std().iloc[-1] * np.sqrt(252)
check(abs(hs["vol252"]["WILD"].iloc[-1] - want) < 1e-12,
      "volatility is measured on real bars only — carried bars are not 0% days")

# no bar on the last session -> no 1-day change (not +0.0%)
fr2 = {k: v.copy() for k, v in frames.items()}
fr2["UP"] = fr2["UP"].iloc[:-1]
df4, _, _, _ = sd.compute(fund, fr2)
check(np.isnan(df4.at["UP", "chg_1d"]), "missing last bar -> 1D change absent")
check(np.isfinite(df4.at["CALM", "chg_1d"]), "others keep their 1D change")

# one frame with an extra date (a pre-market stub, a holiday bar) must not
# become "the last session" for everyone
fr3 = {k: v.copy() for k, v in frames.items()}
stub = fr3["CALM"].iloc[-1:].copy()
stub.index = stub.index + pd.Timedelta(days=1)
stub[["Open", "High", "Low", "Close"]] = np.nan
fr3["CALM"] = pd.concat([fr3["CALM"], stub])
df5, _, _, asof5 = sd.compute(fund, fr3)
check(asof5 == idx[-1].strftime("%Y-%m-%d"), f"a one-frame stray row does not date the scan: {asof5}")
check(np.isfinite(df5.at["UP", "chg_1d"]), "a one-frame stray row does not blank everyone's 1D change")

# ADR is measured on real bars: a carried bar does not repeat yesterday's range
ah = close.copy()
ah.iloc[-5:-2, ah.columns.get_loc("WILD")] = np.nan
hi_, lo_ = ah * 1.03, ah * 0.99
hi_.iloc[-6, hi_.columns.get_loc("WILD")] = ah["WILD"].iloc[-6] * 1.5  # one huge range before the gap
asg = sd.price_signals(ah, hi_, lo_)
real_bars = (hi_["WILD"].iloc[-20:] / lo_["WILD"].iloc[-20:] - 1).dropna()
check(len(real_bars) == 17, "fixture: 3 of the last 20 bars missing")
check(abs(asg["adr20"]["WILD"].iloc[-1] - float(np.mean(real_bars.to_numpy()))) < 1e-12,
      "ADR = mean range of the REAL bars among the last 20")
carried = (hi_["WILD"].ffill(limit=5) / lo_["WILD"].ffill(limit=5) - 1).iloc[-20:].mean()
check(abs(asg["adr20"]["WILD"].iloc[-1] - carried) > 1e-6, "…which differs from counting carried bars")

# the price cache holds every universe: a write must merge, even from a
# forced run that never read the file (it once replaced it wholesale)
import tempfile, pickle as _pk, time as _t
with tempfile.TemporaryDirectory() as td:
    cp = os.path.join(td, "prices.pkl")
    with open(cp, "wb") as fh:
        _pk.dump({"frames": {"OTHER": "o"}, "fetched": {"OTHER": 1.0}}, fh)
    mine = {"frames": {"MINE": "m"}, "fetched": {"MINE": 2.0}}
    sd._persist(mine, cp, {"mtime": None, "saved": _t.time()})   # force-style state
    with open(cp, "rb") as fh:
        got = _pk.load(fh)
    check(set(got["frames"]) == {"OTHER", "MINE"}, f"forced write merges with the disk: {sorted(got['frames'])}")
    # and a newer frame on disk beats ours
    with open(cp, "wb") as fh:
        _pk.dump({"frames": {"MINE": "newer"}, "fetched": {"MINE": 9.0}}, fh)
    sd._persist({"frames": {"MINE": "m"}, "fetched": {"MINE": 2.0}}, cp, {"mtime": None, "saved": 0})
    with open(cp, "rb") as fh:
        check(_pk.load(fh)["frames"]["MINE"] == "newer", "newest fetch wins the merge")

# Leader pullback: a constructed setup — a 60% run, then a quiet, shallow
# pullback to the 21 EMA with higher lows and shrinking ranges, turning up
n2 = 320
t2 = np.arange(n2)
base = np.where(t2 < 250, 40 * np.exp(0.0035 * t2), np.nan)
up = 40 * np.exp(0.0035 * 249)
tail = np.concatenate([up * (1 + 0.012 * np.sin(np.arange(70) / 3.0) * np.exp(-np.arange(70) / 40)
                             + 0.0006 * np.arange(70))])
path = np.where(t2 < 250, base, tail[np.clip(t2 - 250, 0, 69)])
cp = pd.DataFrame({"PB": path}, index=pd.bdate_range("2025-01-01", periods=n2))
rng_w = np.linspace(0.03, 0.008, n2)
hp, lp = cp * (1 + rng_w[:, None] / 2), cp * (1 - rng_w[:, None] / 2)
ps = sd.price_signals(cp, hp, lp, cp * 0 + 1e6)
e, st = ps["pb_entry"]["PB"].iloc[-1], ps["pb_stop"]["PB"].iloc[-1]
check(1 - st / e >= 0.01 - 1e-9, f"stop at least 1% under the entry: {1 - st / e:.4f}")
check(e == hp["PB"].iloc[-1], "entry = a buy stop at today's high")
# the stop sits UNDER the pullback low, never inside the pullback; a low more
# than 3% away is skipped, not tightened
deep = sd.pullback_signals(cp, hp, lp.mul(0.5))
d_low = lp.mul(0.5)["PB"].iloc[-3:].min()
check(deep["pb_stop"]["PB"].iloc[-1] < d_low, "the stop is under the pullback low")
check(not deep["pb_rest"]["PB"].iloc[-1], "a pullback needing more than a 3% stop is not a setup")
tight = sd.pullback_signals(cp, hp, hp * 0.9999)
check(abs(1 - tight["pb_stop"]["PB"].iloc[-1] / tight["pb_entry"]["PB"].iloc[-1] - 0.01) < 1e-9, "tight low -> 1% stop")
# the leader cut is the top quarter of the COMBINED rank, not >= 0.75 on an average of ranks
grid = {k: pd.DataFrame([np.linspace(0.31, 1.0, 100) if k != "r6m" else np.linspace(1.0, 0.31, 100)],
                        columns=[f"S{i}" for i in range(100)]) for k in ("r1m", "r3m", "r6m")}
grid["dollar_vol50"] = pd.DataFrame([[5e7] * 100], columns=grid["r1m"].columns)
ld2, _ = sd.pullback_leaders(grid, pd.DataFrame(True, index=[0], columns=grid["r1m"].columns))
check(20 <= int(ld2.iloc[0].sum()) <= 30, f"about a quarter of an all-up universe leads: {int(ld2.iloc[0].sum())}")
# the zone is judged against EACH day's EMA: a fast riser whose average climbs
# up to an old low has not touched support
# KOD's shape: a long base, a jump, then a dip to a level the RISING average
# only reaches days later
kod = np.r_[np.full(200, 20.0), np.linspace(21, 40, 12), [30.0, 37.0, 38.0]]
riser = pd.DataFrame({"R": kod}, index=pd.bdate_range("2025-01-01", periods=len(kod)))
rs_ = sd.pullback_signals(riser, riser * 1.005, riser * 0.995)
lows, e21 = riser["R"] * 0.995, rs_["ema21"]["R"]
check(lows.iloc[-3:].min() <= e21.iloc[-1] * 1.02, "fixture: the old low is within 2% of TODAY's EMA")
check(not (lows.iloc[-3:] <= e21.iloc[-3:] * 1.02).any(), "fixture: no day's low reached THAT day's EMA")
check(not rs_["pb_rest"]["R"].iloc[-1], "support is judged per day: an average climbing to an old low is not a touch")
# falling knife: below the averages, lower lows -> never a setup
knife = sd.pullback_signals(close[["DOWN"]], high[["DOWN"]], low[["DOWN"]])
check(not knife["pb_rest"]["DOWN"].iloc[-60:].any(), "a downtrend is never 'resting'")
# leaders are cross-sectional within the universe given
lead_df = {k: pd.DataFrame({"A": [0.5], "B": [0.01], "C": [0.02], "D": [0.03]}) for k in ("r1m", "r3m", "r6m")}
lead_df["dollar_vol50"] = pd.DataFrame({"A": [5e7], "B": [5e7], "C": [5e7], "D": [5e7]})
ld, _ = sd.pullback_leaders(lead_df, pd.DataFrame(True, index=[0], columns=list("ABCD")))
check(ld.iloc[0].tolist() == [True, False, False, False], f"only the top quarter, up 30%+, leads: {ld.iloc[0].tolist()}")
lead_df["dollar_vol50"]["A"] = 1e6
ld, _ = sd.pullback_leaders(lead_df, pd.DataFrame(True, index=[0], columns=list("ABCD")))
check(not ld.iloc[0, 0], "a leader that barely trades is removed")

# meta: strategies + evidence, no lists, no network
m = sd.meta()
check(len(m["strategies"]) == len(sd.STRATEGIES) and "rows" not in m, "meta has every strategy and no rows")
check(all(s_["symbols"] == [] for s_ in m["strategies"]), "meta lists are empty")
check(all(s_["rules"] for s_ in m["strategies"]), "meta carries every strategy's rules")
for s_ in m["strategies"]:
    check(all(s_.get(k) for k in ("idea", "use", "tested", "practice")) and s_.get("reading"),
          f"{s_['id']}: the write-up has its idea, use, test, practice and reading")
by_id = {s_["id"]: s_ for s_ in m["strategies"]}
check(by_id["rs_leaders"]["rules"][:-1] == by_id["trend_template"]["rules"]
      and "25" in by_id["rs_leaders"]["rules"][-1], "RS Leaders lists the Trend Template's rules, then the top 25")
json.dumps(m, allow_nan=False, default=sd._clean)

from datetime import datetime, timezone
# the live bar (TradingView, delayed) during the session
sess = datetime(2026, 7, 15, 17, 0, tzinfo=timezone.utc)  # 13:00 EDT, a Wednesday
didx = pd.bdate_range("2026-06-01", "2026-07-14")
fr_live = {"AAA": pd.DataFrame({"Open": 50.0, "High": 51.0, "Low": 49.0, "Close": 50.0, "Volume": 1e6}, index=didx)}
bar_t = datetime(2026, 7, 15, 13, 30, tzinfo=timezone.utc).timestamp()
q_ok = {"AAA": {"q_open": 50.5, "q_high": 52.0, "q_low": 50.1, "q_close": 51.7, "q_volume": 5e5, "q_change": 0.034,
                "q_time": bar_t, "q_update": sess.timestamp() - 900, "q_mode": "delayed_streaming_900"}}
out, info = sd.apply_live_bars(fr_live, q_ok, now=sess)
check(info["applied"] == 1 and out["AAA"].index[-1] == pd.Timestamp("2026-07-15")
      and out["AAA"]["Close"].iloc[-1] == 51.7, f"today's bar appended from the quote: {info}")
# update_time is when the delayed quote was SENT: the prices are 15 min older
check(info["delayed_min"] == 15 and info["price_time"] == "12:30 ET", f"delay + price time, as reported: {info}")
for label, q, why in [
    # 2:1 split Yahoo has not absorbed: ours -49%, TradingView's own +1%
    ("2:1 split day", dict(q_ok["AAA"], q_open=25.0, q_high=25.6, q_low=24.9, q_close=25.3, q_change=0.012), "split"),
    ("no TradingView change", dict(q_ok["AAA"], q_change=None), "unverifiable"),
    ("inconsistent", dict(q_ok["AAA"], q_low=52.5), "ohlc"),
    ("yesterday's bar", dict(q_ok["AAA"], q_time=bar_t - 86400), "date"),
]:
    _, inf = sd.apply_live_bars(fr_live, {"AAA": q}, now=sess)
    check(inf["applied"] == 0 and inf["on_last_close"] == 1, f"{label}: not used, the name stays on its last close")
# a REAL +40% earnings gap is consistent with TradingView's own change: kept
gap = {"AAA": dict(q_ok["AAA"], q_open=66.0, q_high=72.0, q_low=65.0, q_close=70.0, q_change=0.40)}
og, ig = sd.apply_live_bars(fr_live, gap, now=sess)
check(ig["applied"] == 1 and og["AAA"]["Close"].iloc[-1] == 70.0, "a genuine big move is not mistaken for a split")
# a history missing yesterday (its download failed) gets no live bar across the hole
spy_l = pd.DataFrame({"Open": 1.0, "High": 1.0, "Low": 1.0, "Close": 1.0, "Volume": 1.0}, index=didx)
holey = {"SPY": spy_l, "BBB": fr_live["AAA"], "CCC": fr_live["AAA"],
         "AAA": fr_live["AAA"].iloc[:-1]}  # AAA ends the session before yesterday
oh, ih = sd.apply_live_bars(holey, q_ok, now=sess)
check(ih["applied"] == 0 and oh["AAA"].index[-1] == pd.Timestamp("2026-07-13"), "no splice across a missing day")
ok_l = {"SPY": spy_l, "AAA": fr_live["AAA"]}
_, ik = sd.apply_live_bars(ok_l, q_ok, now=sess)
check(ik["applied"] == 1, "history ending on the previous session takes the live bar")
# SPY itself stale must not make every current name look stale
stale_spy = {"SPY": spy_l.iloc[:-1], "BBB": fr_live["AAA"], "CCC": fr_live["AAA"], "AAA": fr_live["AAA"]}
_, iss = sd.apply_live_bars(stale_spy, q_ok, now=sess)
check(iss["applied"] == 1, "a stale SPY does not drag the previous session back a day")
# an 11:10 split Yahoo has not applied: ours -9%, TradingView's ~0 -> held back
s1110 = {"AAA": dict(q_ok["AAA"], q_open=45.4, q_high=45.6, q_low=45.2, q_close=45.5, q_change=0.0)}
_, i1110 = sd.apply_live_bars(fr_live, s1110, now=sess)
check(i1110["applied"] == 0, "an 11:10 split is not spliced in as a -9% day")
# a name WITHOUT a usable quote loses today's partial Yahoo bar — never a frozen price
partial = {"AAA": pd.concat([fr_live["AAA"], pd.DataFrame({"Open": 50.0, "High": 50.2, "Low": 49.9, "Close": 50.1,
                                                            "Volume": 1e5}, index=[pd.Timestamp("2026-07-15")])])}
o2, inf2 = sd.apply_live_bars(partial, {}, now=sess)
check(o2["AAA"].index[-1] == pd.Timestamp("2026-07-14"), "a frozen partial bar is dropped: last real close stands")
_, inf3 = sd.apply_live_bars(fr_live, {"AAA": dict(q_ok["AAA"], q_mode="delayed_streaming")}, now=sess)
check(inf3["delayed_min"] is None, "an unrecognised delay mode is 'unknown', not 0 and not a crash")
check(inf3["price_time"] is None, "unknown delay -> unknown price time (the send time is not the price time)")
check(sd._delay_minutes("streaming") == 0 and sd._delay_minutes("delayed_streaming_900") == 15,
      "delay parse")
_, info5 = sd.apply_live_bars(fr_live, q_ok, now=datetime(2026, 7, 15, 22, 0, tzinfo=timezone.utc))
check(info5["applied"] == 0, "after the session the Yahoo close is used, not the quote")

# a NaN in the evidence file reads as absent and still serialises
bad = {"backtests": {"X": {"excess": float("nan"), "t": 1.0, "max_dd": -0.1, "hit": 0.5,
                           "subperiods": [0.1, float("nan"), 0.0, 0.2]}}}
e = sd.evidence({"backtest": "X", "note": "n"}, bad)
check(e["excess"] is None and e["subperiods"][1] is None, "NaN evidence -> None")
json.dumps(e, allow_nan=False)

# shareholder yield: one part unknown -> unknown (TradingView reports 0 for none)
fund2 = dict(fund)
fund2["UP"] = dict(fund["UP"], div_yield=0.02, buyback_yield=None)
fund2["WILD"] = dict(fund["WILD"], div_yield=0.0, buyback_yield=0.01)
df2, _, _, _ = sd.compute(fund2, frames)
check(np.isnan(df2.at["UP", "shareholder_yield"]), "dividend known, buyback unknown -> shareholder yield absent")
check(abs(df2.at["WILD", "shareholder_yield"] - 0.01) < 1e-12, "explicit zeros add up")

# a loss ranks as EXPENSIVE in the value composite, it does not drop out
v = {s: fund_rec(s, mcap=1e9, ev=1e9, ebit=e, net_income=ni, revenue=1e9, ocf=1e8, pb=1.0,
                 div_yield=0.0, buyback_yield=0.0)
     # DOWN matches UP on every ratio but E/P, so only the loss can rank it last
     for s, e, ni in (("UP", 1e8, 1e8), ("DOWN", 1e8, -5e8), ("CALM", 1e8, 1e8), ("WILD", 1e8, 1e8))}
df3, _, _, _ = sd.compute(v, frames)
check(df3.at["DOWN", "value_n"] == 6, "a loss keeps its E/P term (negative), it is not dropped")
check(df3["value_score"]["DOWN"] < df3["value_score"].drop("DOWN").min(), f"the loss-maker is the most expensive: {df3['value_score'].to_dict()}")

# cache clock: DST-correct close, and intraday fetches expire
from datetime import datetime, timezone
jan_1530_est = datetime(2027, 1, 13, 20, 30, tzinfo=timezone.utc)  # 15:30 EST, Wednesday
check(sd._last_close_utc(jan_1530_est).day == 12, "15:30 EST is before the close (EST, not a fixed UTC-4)")
check(sd.in_session(jan_1530_est), "15:30 EST is in session")
jul_1000 = datetime(2026, 7, 15, 14, 0, tzinfo=timezone.utc)  # 10:00 EDT
check(sd.is_fresh(jul_1000.timestamp() - 60, jul_1000), "a fetch a minute ago is fresh mid-session")
later = datetime(2026, 7, 15, 19, 30, tzinfo=timezone.utc)  # 15:30 EDT
check(sd.is_fresh(jul_1000.timestamp(), later),
      "history fetched this morning is current all session (today's bar comes from the quote)")
check(not sd.is_fresh(datetime(2026, 7, 14, 19, 0, tzinfo=timezone.utc).timestamp(), later),
      "history fetched before yesterday's close is stale")
evening = datetime(2026, 7, 15, 22, 0, tzinfo=timezone.utc)  # 18:00 EDT
check(sd.is_fresh(datetime(2026, 7, 15, 20, 30, tzinfo=timezone.utc).timestamp(), evening),
      "a post-close fetch is fresh for the evening")

# evidence: every backtested strategy's column exists in the exported file
ev_doc = sd.load_evidence()
check(bool(ev_doc.get("backtests")), "screener_evidence.json present and parsed")
for st in sd.STRATEGIES:
    e = sd.evidence(st, ev_doc)
    if st.get("backtest"):
        check(e["tested"] and e["excess"] is not None and len(e["subperiods"]) == 4,
              f"{st['id']}: evidence found for backtest {st['backtest']!r}")
    else:
        check(not e["tested"] and e["note"], f"{st['id']}: untested strategies say so")
check(not sd.evidence({"backtest": "nope", "note": "n"}, ev_doc)["tested"], "unknown column -> untested")
check(not sd.evidence({"backtest": "x", "note": "n"}, {})["tested"], "missing file -> untested, not zeros")

# the clock the cache trusts: a Saturday maps back to Friday's close
from datetime import datetime, timezone
sat = datetime(2026, 10, 17, 18, 0, tzinfo=timezone.utc)
lc = sd._last_close_utc(sat)
check(lc.weekday() == 4 and lc < sat, f"last close before a Saturday is Friday's: {lc}")
mon_morning = datetime(2026, 10, 19, 13, 0, tzinfo=timezone.utc)  # 09:00 ET
check(sd._last_close_utc(mon_morning).weekday() == 4, "Monday pre-open still trusts Friday's close")

# load_prices through a download, with the scan's clock passed in: the
# download loop once rebound `now` to a float timestamp, so the stale count
# after it crashed every scan that fetched anything ('float' object has no
# attribute 'astimezone') — the first scan after every close.
import tempfile
import yfinance as yf
import yfinance_data  # noqa: F401 — it wraps yf.download in its gate on import; patch after
_real_download = yf.download
def _fake_download(tickers, **kw):
    idx = pd.date_range("2026-05-01", periods=40, freq="B")  # _ticker_frame wants >= 30 closes
    one = pd.DataFrame({"Open": 10.0, "High": 11.0, "Low": 9.0, "Close": 10.5, "Volume": 1e6,
                        "Dividends": 0.0, "Stock Splits": 0.0}, index=idx)
    return pd.concat({t: one for t in tickers}, axis=1)
_prior_cache_dir = os.environ.get("FINCEPT_CACHE_DIR")
with tempfile.TemporaryDirectory() as tmp:
    os.environ["FINCEPT_CACHE_DIR"] = tmp
    yf.download = _fake_download
    try:
        got, oldest, stale = sd.load_prices(["AAA", "BBB"], now=datetime.now(timezone.utc))
        check(set(got) == {"AAA", "BBB", "SPY"} and stale == 0 and oldest > 0,
              f"a downloading load_prices returns fresh frames: {sorted(got)} stale={stale}")
    except Exception as e:
        check(False, f"load_prices with a download and a clock raised {type(e).__name__}: {e}")
    finally:
        yf.download = _real_download
        if _prior_cache_dir is None:
            os.environ.pop("FINCEPT_CACHE_DIR", None)
        else:
            os.environ["FINCEPT_CACHE_DIR"] = _prior_cache_dir

if failures:
    print(f"{len(failures)} failure(s)")
    sys.exit(1)
print("screener: all checks passed")
