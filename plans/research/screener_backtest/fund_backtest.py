"""Backtest the fundamental screener strategies, point-in-time, on the same
S&P 500 universe, months, forward returns and costs as backtest.py.

Run collect.py and fund_collect.py first.

Point-in-time rule: the annual record for fiscal year Y (SEC frame CY{Y}) is
visible from its period end + 120 days. Market cap = the 10-K cover-page
public float (dollars, as of fiscal Q2 end) rolled forward by the stock's own
price change to the rebalance date. Public float excludes insider holdings,
so founder-held names read cheaper than they are — a ranking bias, noted.
"""
import os, sys, pickle, bisect
from collections import defaultdict
import numpy as np
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest as B

D = B.D
frames = pickle.load(open(os.path.join(D, "frames.pkl"), "rb"))
cik_of = pickle.load(open(os.path.join(D, "cik.pkl"), "rb"))

FIELDS = {  # output field -> concepts in priority order
    "rev": ["Revenues", "RevenueFromContractWithCustomerExcludingAssessedTax", "SalesRevenueNet"],
    "gp": ["GrossProfit"], "cogs": ["CostOfRevenue", "CostOfGoodsAndServicesSold"],
    "ebit": ["OperatingIncomeLoss"], "ni": ["NetIncomeLoss"],
    "ocf": ["NetCashProvidedByUsedInOperatingActivities"],
    "shares": ["WeightedAverageNumberOfDilutedSharesOutstanding"],
    "buyback": ["PaymentsForRepurchaseOfCommonStock"],
    "div": ["PaymentsOfDividends", "PaymentsOfDividendsCommonStock"],
    "assets": ["Assets"], "ca": ["AssetsCurrent"], "cl": ["LiabilitiesCurrent"],
    "ltd": ["LongTermDebtNoncurrent", "LongTermDebt"],
    "cash": ["CashAndCashEquivalentsAtCarryingValue"], "ppe": ["PropertyPlantAndEquipmentNet"],
    "equity": ["StockholdersEquity"],
}
FLOWS = {"rev", "gp", "cogs", "ebit", "ni", "ocf", "shares", "buyback", "div"}

# rec[cik][year] = {field: value, "end": fiscal year end, "float_end": date}
rec = defaultdict(lambda: defaultdict(dict))
for (concept, period), rows in frames.items():
    year = int(period[2:6])
    for field, concepts in FIELDS.items():
        if concept not in concepts:
            continue
        rank = concepts.index(concept)
        for cik, end, val in rows:
            r = rec[cik][year]
            if (field, "rank") in r and r[(field, "rank")] <= rank:
                continue
            r[field] = float(val)
            r[(field, "rank")] = rank
            # Visible only once EVERY value in the record exists: balances come
            # from the CY..Q4I (Dec 31) frame, which for a June fiscal year is
            # a 10-Q filed months after the 10-K that carries the flows. Dating
            # the record by the flows alone used Dec balances before they existed.
            r["end"] = max(r.get("end", end), end)

# Public float is stamped at the company's fiscal Q2 end, in whichever calendar
# quarter that falls, and becomes public with the 10-K about 8 months later
# (Dec FY: Jun 30 -> filed Feb/Mar). Its own clock, not the fundamentals year.
floats = defaultdict(dict)
for (concept, period), rows in frames.items():
    if concept == "dei/EntityPublicFloat":
        for cik, end, val in rows:
            floats[cik][pd.Timestamp(end)] = float(val)
float_avail = {cik: sorted((d + pd.Timedelta(days=270), d, v) for d, v in m.items() if v > 0)
               for cik, m in floats.items()}

avail = {}  # cik -> sorted [(available date, year)]
for cik, years in rec.items():
    lst = []
    for y, r in years.items():
        end = r.get("end")
        if end:
            lst.append((pd.Timestamp(end) + pd.Timedelta(days=120), y))
    avail[cik] = sorted(lst)

def g(r, k):
    v = r.get(k)
    return np.nan if v is None else v

def gross_profit(r):
    gp = g(r, "gp")
    if np.isfinite(gp):
        return gp
    return g(r, "rev") - g(r, "cogs")

def record_at(cik, t):
    lst = avail.get(cik)
    if not lst:
        return None, None
    i = bisect.bisect_right(lst, (t, 10**6)) - 1
    if i < 0:
        return None, None
    y = lst[i][1]
    return rec[cik][y], rec[cik].get(y - 1)

close = B.close
# Split-adjusted, NOT dividend-adjusted (collect_raw.py): the float roll-forward
# is a PRICE change; total-return closes overstated payers' caps.
raw_close = pickle.load(open(os.path.join(D, "closes_raw.pkl"), "rb"))
for _s, _c in raw_close.items():
    _c.index = _c.index.tz_localize(None) if _c.index.tz is not None else _c.index

def mcap_at(sym, cik, t):
    lst = float_avail.get(cik)
    if not lst:
        return np.nan
    i = bisect.bisect_right(lst, (t, pd.Timestamp.max, np.inf)) - 1
    if i < 0:
        return np.nan
    _, fe, f = lst[i]
    rc = raw_close.get(sym)
    if rc is None:
        return np.nan
    p0, p1 = rc.loc[:fe], rc.loc[:t]
    if p0.empty or p1.empty or p1.index[-1] < t - pd.Timedelta(days=7):
        return np.nan
    return f * p1.iloc[-1] / p0.iloc[-1]

def piotroski(r, p):
    if p is None:
        return np.nan
    a, a0 = g(r, "assets"), g(p, "assets")
    ni, ni0, ocf = g(r, "ni"), g(p, "ni"), g(r, "ocf")
    if not all(np.isfinite([a, a0, ni, ni0, ocf])) or a <= 0 or a0 <= 0:
        return np.nan
    roa, roa0 = ni / a, ni0 / a0
    s = (roa > 0) + (ocf > 0) + (roa > roa0) + (ocf / a > roa)
    # no long-term debt tagged = none carried; that is the only case a
    # missing value is read as zero, and only when both years lack it
    l, l0 = g(r, "ltd"), g(p, "ltd")
    l = 0.0 if not np.isfinite(l) and not np.isfinite(l0) else l
    l0 = 0.0 if not np.isfinite(l0) and l == 0.0 else l0
    s += (np.isfinite(l) and np.isfinite(l0) and l / a <= l0 / a0)
    ca, cl, ca0, cl0 = g(r, "ca"), g(r, "cl"), g(p, "ca"), g(p, "cl")
    s += bool(all(np.isfinite([ca, cl, ca0, cl0])) and cl > 0 and cl0 > 0 and ca / cl > ca0 / cl0)
    sh, sh0 = g(r, "shares"), g(p, "shares")
    s += bool(np.isfinite(sh) and np.isfinite(sh0) and sh <= sh0)
    rev, rev0 = g(r, "rev"), g(p, "rev")
    gp, gp0 = gross_profit(r), gross_profit(p)
    s += bool(all(np.isfinite([gp, gp0, rev, rev0])) and rev > 0 and rev0 > 0 and gp / rev > gp0 / rev0)
    s += bool(np.isfinite(rev) and np.isfinite(rev0) and rev / a > rev0 / a0)
    return float(s)

cache = {}
def metrics(t, u):
    key = pd.Timestamp(t)
    if key in cache:
        return cache[key]
    rows = {}
    for s in u:
        cik = cik_of.get(s)
        if cik is None:
            continue
        r, p = record_at(cik, key)
        if r is None:
            continue
        a = g(r, "assets")
        mc = mcap_at(s, cik, key)
        ebit = g(r, "ebit")
        ltd = g(r, "ltd"); ltd = 0.0 if not np.isfinite(ltd) else ltd
        cash = g(r, "cash"); cash = 0.0 if not np.isfinite(cash) else cash
        ev = mc + ltd - cash
        cap = g(r, "ca") - g(r, "cl") + g(r, "ppe")
        div = g(r, "div"); bb = g(r, "buyback")
        # One untagged concept is "none paid" (XBRL filers omit zero lines);
        # BOTH untagged is unknown, not a confident zero yield.
        shy = (np.nan if not (np.isfinite(div) or np.isfinite(bb))
               else (0 if not np.isfinite(div) else div) + (0 if not np.isfinite(bb) else bb))
        rows[s] = {
            "gpa": gross_profit(r) / a if a and a > 0 else np.nan,
            "f": piotroski(r, p),
            "ey": ebit / ev if np.isfinite(ev) and ev > 0 else np.nan,
            "roc": ebit / cap if np.isfinite(cap) and cap > 0 else np.nan,
            "ep": g(r, "ni") / mc, "bp": g(r, "equity") / mc, "sp": g(r, "rev") / mc,
            "cfp": g(r, "ocf") / mc, "shy": shy / mc if np.isfinite(mc) else np.nan,
            "mc": mc,
        }
    m = pd.DataFrame.from_dict(rows, orient="index")
    if not m.empty:
        vals = m[["ep", "bp", "sp", "cfp", "ey", "shy"]].rank(pct=True)
        m["value"] = vals.mean(axis=1).where(vals.notna().sum(axis=1) >= 4)
        # higher = better: ascending ranks give the HIGHEST yield / return the
        # largest number. (Negating this — as a first draft did — selected the
        # 30 WORST names and reported their record as the formula's.)
        # Ranked within names that have BOTH, as the shipped screen does —
        # ranking each over its own population offsets the sum.
        both = m["ey"].notna() & m["roc"].notna()
        m["magic"] = (m["ey"][both].rank() + m["roc"][both].rank()).reindex(m.index)
    cache[key] = m
    return m

def top(s, n=None, frac=None):
    s = s.dropna().sort_values(ascending=False, kind="stable")
    if frac is not None:
        n = max(1, int(round(len(s) * frac)))
    return list(s.index[:n])

STRATS = {}
def strat(name):
    def deco(fn):
        STRATS[name] = fn
        return fn
    return deco

@strat("Gross profitability GP/A (top decile)")
def _(t, u): return top(metrics(t, u)["gpa"], frac=0.10)

@strat("Piotroski F >= 8 (all)")
def _(t, u):
    m = metrics(t, u); return list(m.index[m["f"] >= 8])

@strat("Piotroski F >= 8 in cheapest 30% B/M")
def _(t, u):
    m = metrics(t, u)
    cheap = m["bp"] >= m["bp"].quantile(0.70)
    return list(m.index[cheap & (m["f"] >= 8)])

@strat("Magic Formula (top 30)")
def _(t, u): return top(metrics(t, u)["magic"].where(metrics(t, u)[["ey", "roc"]].notna().all(axis=1)), n=30)

@strat("Value composite (top decile)")
def _(t, u): return top(metrics(t, u)["value"], frac=0.10)

@strat("Trending Value: value top decile -> top 25 by 6m")
def _(t, u):
    pool = top(metrics(t, u)["value"], frac=0.10)
    return top(B.r["6m"].loc[t, pool], n=25)

@strat("Quality+Momentum: GP/A top 30% -> top 25 by 12-1")
def _(t, u):
    pool = top(metrics(t, u)["gpa"], frac=0.30)
    return top(B.mom12_1.loc[t, pool], n=25)

def ic(col):
    def f(t, u):
        return metrics(t, u)[col].reindex(u)
    return f

IC = {"GP/A": ic("gpa"), "Piotroski F": ic("f"), "Magic (rank sum)": ic("magic"),
      "Value composite": ic("value")}

if __name__ == "__main__":
    res, ics, cov = B.run(STRATS, IC)
    B.report(res, ics, list(STRATS))
    res.to_pickle(os.path.join(D, "fund_results.pkl"))
    # Direction guard: every ranked screen must pick names that are BETTER on
    # its own metric than the universe (the inverted Magic rank passed every
    # other check). Medians over the last 24 rebalances.
    keys = sorted(cache)[-24:]
    for name, metric in [("Magic Formula (top 30)", "ey"), ("Magic Formula (top 30)", "roc"),
                         ("Gross profitability GP/A (top decile)", "gpa"),
                         ("Value composite (top decile)", "value")]:
        fn = STRATS[name]
        better = 0
        for k in keys:
            m = cache[k]
            picks = fn(k, list(m.index))
            better += m.loc[picks, metric].median() > m[metric].median()
        assert better == len(keys), f"{name}: picks not better on {metric} ({better}/{len(keys)})"
    print("\ndirection guard: picks beat the universe median on their own metric — ok")
    # how much of the universe each month had a usable record
    n = [len(cache[k].dropna(subset=["gpa"])) for k in sorted(cache)]
    print(f"\nnames with a point-in-time record: median {int(np.median(n))} "
          f"(min {min(n)}, max {max(n)}) of a median {int(res['n_univ'].median())} eligible")
    # market-cap sanity: rolled-forward public float vs the price panel's own scale
    last = sorted(cache)[-1]
    print("\nlatest month market caps ($B, float-based):")
    print((cache[last]["mc"].dropna().sort_values(ascending=False).head(8) / 1e9).round(0).to_string())
