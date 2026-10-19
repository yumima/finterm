"""
Stock screener — universe, metrics and the strategy screens for the SCREENER tab.

    screener_data.py scan '{"min_mcap": 2e9, "force": false}'

stdout: one JSON document. stderr: progress lines, `PROGRESS <done> <total> <text>`.

Where the strategies come from, and what they are worth
-------------------------------------------------------
Read plans/research/screener_research_2026-10.md before changing a rule. In
short: the screens are the published, rule-exact versions of strategies from
investing-competition winners (Minervini, CAN SLIM, Kullamaggie) and from the
academic literature (momentum, Magic Formula, Piotroski, Novy-Marx, Trending
Value, low volatility). Measured on point-in-time S&P 500 membership 2010-2026
NONE beat the equal-weighted index with statistical significance; the best
were inside noise and earned their excess in 2023-26 alone. Every strategy
therefore carries its measured record in EVIDENCE, and the tab shows it next
to the list. A screen finds the names a method would look at. It is not
evidence the method pays — do not relabel any of these "winning".

Data
----
- Universe and fundamentals: one TradingView scanner request for every US
  primary common stock above the market-cap floor (incl. a computed Piotroski
  score). TradingView reports ratios as PERCENTAGES; they are converted to the
  app's fraction contract here, at the producer, once.
- Prices: Yahoo daily bars, two years, bulk download in chunks. This runs as
  its own process, so it is the only yfinance user in it and the daemon's
  download gate does not apply; yfinance_data is imported for its split
  repair (the day after a split Yahoo returns the old bars — see
  _repair_unadjusted_splits), which also installs that gate harmlessly.
- Absent is null, never 0. A vendor null that becomes 0.0 is this repo's
  recurring silent-corruption seam: a missing EBIT must not rank, a missing
  ROE must not fail a test it was never measured on.

price_signals() is the single definition of every price metric: the backtest
(plans/research/screener_backtest/backtest.py) imports it, so the numbers on
the evidence card were measured with this code.
"""

import json
import math
import os
import pickle
import re
import sys
import time
from datetime import datetime, timedelta, timezone

import numpy as np
import pandas as pd

from finterm_paths import cache_dir

CACHE_VERSION = 1
PRICE_PERIOD = "2y"   # 252 + 21 bars for 12-1, 200 + 21 for a rising 200-day
CHUNK = 100
TV_URL = "https://scanner.tradingview.com/america/scan"
BENCHMARK = "SPY"

# TradingView column -> (output key, scale). scale 0.01 converts a percentage
# to a fraction; None leaves the value as reported.
TV_COLUMNS = {
    "name": ("tv_name", None),
    "description": ("name", None),
    "sector": ("sector", None),
    "industry": ("industry", None),
    "exchange": ("exchange", None),
    "market_cap_basic": ("mcap", None),
    "enterprise_value_current": ("ev", None),
    "ebit_ttm": ("ebit", None),
    "ebitda_ttm": ("ebitda", None),
    "gross_profit_ttm": ("gross_profit", None),
    "total_assets_fq": ("total_assets", None),
    "total_revenue_ttm": ("revenue", None),
    "net_income_ttm": ("net_income", None),
    "cash_f_operating_activities_ttm": ("ocf", None),
    "piotroski_f_score_ttm": ("piotroski", None),
    "return_on_invested_capital_fq": ("roic", 0.01),
    "return_on_equity_fq": ("roe", 0.01),
    "gross_margin_ttm": ("gross_margin", 0.01),
    "earnings_per_share_diluted_yoy_growth_fq": ("eps_growth_q", 0.01),
    "earnings_per_share_diluted_yoy_growth_fy": ("eps_growth_fy", 0.01),
    "total_revenue_yoy_growth_ttm": ("rev_growth", 0.01),
    "price_earnings_ttm": ("pe", None),
    "price_book_fq": ("pb", None),
    "price_sales_current": ("ps", None),
    "enterprise_value_ebitda_ttm": ("ev_ebitda", None),
    "dividends_yield_current": ("div_yield", 0.01),
    "buyback_yield": ("buyback_yield", 0.01),
    "average_volume_30d_calc": ("avg_volume_30d", None),
    "earnings_release_next_date": ("next_earnings", None),
    # today's bar, for intraday refreshes without a Yahoo request per name.
    # TradingView marks it "delayed_streaming_900": 15 minutes behind.
    "open": ("q_open", None),
    "high": ("q_high", None),
    "low": ("q_low", None),
    "close": ("q_close", None),
    "volume": ("q_volume", None),
    "time": ("q_time", None),          # the bar's session open, unix
    "update_time": ("q_update", None),  # when the (delayed) quote was last SENT, unix — not the price's time
    "update_mode": ("q_mode", None),
    "change": ("q_change", 0.01),       # TradingView's own % change vs ITS previous close
}

# Sectors where EBIT / enterprise value / gross profit do not mean what the
# formulas assume (Greenblatt excludes both; banks have no gross profit).
NON_OPERATING_SECTORS = {"Finance", "Utilities"}


def log_progress(done, total, text):
    print(f"PROGRESS {done} {total} {text}", file=sys.stderr, flush=True)


def _num(v):
    """A vendor value as a float, or None. Never 0 for absent."""
    if v is None or isinstance(v, bool):
        return None
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) else None


# ── Universe + fundamentals ──────────────────────────────────────────────────

def fetch_universe(min_mcap):
    import urllib.request
    payload = {
        "filter": [
            {"left": "market_cap_basic", "operation": "greater", "right": float(min_mcap)},
            {"left": "type", "operation": "equal", "right": "stock"},
            {"left": "subtype", "operation": "equal", "right": "common"},
            {"left": "is_primary", "operation": "equal", "right": True},
            {"left": "exchange", "operation": "in_range", "right": ["NASDAQ", "NYSE", "AMEX"]},
        ],
        "columns": list(TV_COLUMNS),
        "sort": {"sortBy": "market_cap_basic", "sortOrder": "desc"},
        "range": [0, 6000],
    }
    req = urllib.request.Request(
        TV_URL, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    data = json.loads(urllib.request.urlopen(req, timeout=60).read())
    return parse_universe(data.get("data") or [])


def yahoo_symbol(tv_name):
    """TradingView 'BRK.B' -> Yahoo 'BRK-B'."""
    return tv_name.replace(".", "-").replace("/", "-")


_CLASS_SUFFIX = re.compile(
    r"\s*[-,(]?\s*\b(class|series|cl\.?)\s+[a-z0-9]\b\)?|\s+(ordinary|common)\s+shares?\b",
    re.IGNORECASE)


def company_key(name):
    """'Berkshire Hathaway Inc. Class A' and '... Class B' -> one company."""
    return _CLASS_SUFFIX.sub("", name).strip().lower()


def parse_universe(rows):
    """TradingView scanner rows -> {yahoo symbol: fundamentals dict}.

    One row per company: share classes (GOOG/GOOGL, BRK.A/BRK.B) are the same
    business and would be ranked twice, so the more traded class is kept.
    """
    cols = list(TV_COLUMNS)
    by_company = {}
    for r in rows:
        vals = r.get("d") or []
        rec = {}
        for col, v in zip(cols, vals):
            key, scale = TV_COLUMNS[col]
            if key in ("tv_name", "name", "sector", "industry", "exchange", "q_mode"):
                rec[key] = v if isinstance(v, str) else None
                continue
            f = _num(v)
            rec[key] = f * scale if (f is not None and scale is not None) else f
        if not rec.get("tv_name"):
            continue
        rec["symbol"] = yahoo_symbol(rec["tv_name"])
        company = company_key(rec.get("name") or rec["symbol"])
        prev = by_company.get(company)
        if prev is None or (rec.get("avg_volume_30d") or 0) > (prev.get("avg_volume_30d") or 0):
            by_company[company] = rec
    return {rec["symbol"]: rec for rec in by_company.values()}


# ── Prices ───────────────────────────────────────────────────────────────────

def _cache_path():
    return os.path.join(str(cache_dir("screener")), f"prices_v{CACHE_VERSION}.pkl")


ET = None


def _et():
    global ET
    if ET is None:
        from zoneinfo import ZoneInfo
        ET = ZoneInfo("America/New_York")
    return ET


def _last_close_utc(now=None):
    """The most recent US cash close (16:15 ET, weekdays) as a UTC instant.

    America/New_York, so DST is right both sides of the change (a fixed
    UTC-4 put the winter close an hour early and cached a 15:15 bar as final).
    Holidays are not modelled: on one the cache refetches and gets the same
    bars, which costs a download and never shows a wrong number.
    """
    now = (now or datetime.now(timezone.utc)).astimezone(_et())
    close = now.replace(hour=16, minute=15, second=0, microsecond=0)
    if now < close:
        close -= timedelta(days=1)
    while close.weekday() >= 5:
        close -= timedelta(days=1)
    return close.astimezone(timezone.utc)


def in_session(now=None):
    """Weekday 09:30-16:15 ET: today's bar is still moving."""
    now = (now or datetime.now(timezone.utc)).astimezone(_et())
    if now.weekday() >= 5:
        return False
    return (now.hour, now.minute) >= (9, 30) and now < now.replace(hour=16, minute=15, second=0, microsecond=0)


def is_fresh(fetched_ts, now=None):
    """Yahoo HISTORY is current once fetched after the last close: it changes
    once a day. During the session today's bar comes from TradingView's quote
    (apply_live_bars), so a mid-session refresh costs no Yahoo request per
    name — before, every name was re-downloaded every 15 minutes."""
    now = now or datetime.now(timezone.utc)
    return fetched_ts >= _last_close_utc(now).timestamp()


def _locked(path):
    """An exclusive advisory lock beside the cache: a universe switch can leave
    two scans running, and both rewrite the same file."""
    import fcntl
    fh = open(path + ".lock", "w")
    fcntl.flock(fh, fcntl.LOCK_EX)
    return fh


def _read_cache(path):
    try:
        with open(path, "rb") as fh:
            c = pickle.load(fh)
        if isinstance(c, dict) and "frames" in c and "fetched" in c:
            return c
    except Exception:
        pass
    return {"frames": {}, "fetched": {}}


def _mtime(path):
    """The file's identity: os.replace installs a new inode on every write, so
    (inode, mtime, size) changes even when two writes share a coarse mtime."""
    try:
        st = os.stat(path)
        return (st.st_ino, st.st_mtime_ns, st.st_size)
    except OSError:
        return None


def _persist(cache, path, state):
    """Merge into the on-disk cache under the lock (newest fetch per symbol
    wins — including over ours, when a concurrent scan fetched later) and write
    atomically. The file is re-read only if another process changed it since
    we last read or wrote it: it holds every universe's frames, and a second
    full unpickle per write was most of a warm refresh's I/O."""
    try:
        lock = _locked(path)
        try:
            if _mtime(path) != state["mtime"]:
                disk = _read_cache(path)
                for s, ts in disk["fetched"].items():
                    if ts > cache["fetched"].get(s, 0) and s in disk["frames"]:
                        cache["frames"][s], cache["fetched"][s] = disk["frames"][s], ts
            tmp = f"{path}.{os.getpid()}.tmp"
            with open(tmp, "wb") as fh:
                pickle.dump(cache, fh)
            os.replace(tmp, path)
            state["mtime"] = _mtime(path)
            state["saved"] = time.time()
        finally:
            lock.close()
    except Exception as e:
        print(f"price cache not written: {e}", file=sys.stderr)


def load_prices(symbols, force=False, now=None):
    """{symbol: DataFrame(Open, High, Low, Close, Volume)} for `symbols` + SPY.

    Cached on disk (is_fresh decides); only stale or missing symbols are
    downloaded. The write MERGES under a lock, newest fetch per symbol wins, so
    a concurrent scan's frames are kept rather than overwritten.
    Returns (frames, oldest fetch timestamp among them, count served stale).
    """
    import yfinance as yf
    import yfinance_data as yd

    path = _cache_path()
    # force starts from nothing but must still MERGE on write — the file holds
    # every universe's frames — so it never claims to have read the disk.
    state = {"mtime": None if force else _mtime(path), "saved": time.time()}
    cache = {"frames": {}, "fetched": {}} if force else _read_cache(path)
    want = list(dict.fromkeys(list(symbols) + [BENCHMARK]))
    # Once per close (is_fresh). Measured: Yahoo's cost is the request per
    # name, not the payload — a month of bars took as long as two years (300
    # names: 6.7 s vs 6.2 s) — so an incremental "append the last month" path
    # bought nothing and was removed; 100-name chunks beat 300 (5.6 s vs 13.4 s).
    todo = [s for s in want if not is_fresh(cache["fetched"].get(s, 0), now)]
    total = len(todo)
    dirty = False
    for i in range(0, total, CHUNK):
        chunk = todo[i:i + CHUNK]
        log_progress(i, total, f"Downloading prices {i}/{total}")
        try:
            data = yf.download(chunk, period=PRICE_PERIOD, interval="1d", auto_adjust=True,
                               actions=True, group_by="ticker", threads=True, progress=False)
        except Exception as e:
            print(f"download failed for chunk at {i}: {e}", file=sys.stderr)
            continue
        now = time.time()
        for s in chunk:
            frame = _ticker_frame(data, s)
            if frame is None:
                continue
            frame = yd._repair_unadjusted_splits(frame, s)
            cache["frames"][s] = frame[["Open", "High", "Low", "Close", "Volume"]].copy()
            cache["fetched"][s] = now
            dirty = True
        # Persist every 90 s, not only at the end: a cold $300M universe can
        # hit the screen's 10-minute timeout, and a kill before a single write
        # threw every chunk away, so each retry started from zero. Time, not
        # chunk count: each write pickles every universe's frames.
        if dirty and time.time() - state["saved"] >= 90:
            _persist(cache, path, state)
            dirty = False
    log_progress(total, total, "Prices ready")
    if dirty:
        _persist(cache, path, state)
    got = {s: cache["frames"][s] for s in want if s in cache["frames"]}
    oldest = min((cache["fetched"].get(s, 0) for s in got), default=0)
    # served from cache although not current: their chunk's download failed
    stale = sum(1 for s in got if not is_fresh(cache["fetched"].get(s, 0), now))
    return got, oldest, stale


def _ticker_frame(data, sym):
    """One ticker's frame from a bulk download. group_by="ticker" returns a
    (Ticker, Price) MultiIndex even for ONE ticker, so look the ticker up on
    either level; a slice is copied before anything writes to it."""
    if data is None or data.empty:
        return None
    cols = data.columns
    try:
        if isinstance(cols, pd.MultiIndex):
            if sym in cols.get_level_values(0):
                f = data[sym]
            elif sym in cols.get_level_values(1):
                f = data.xs(sym, axis=1, level=1)
            else:
                return None
        else:
            f = data
        f = f.dropna(how="all").copy()
    except Exception:
        return None
    if f.empty or "Close" not in f.columns or f["Close"].notna().sum() < 30:
        return None
    return f


def panels(frames):
    """Aligned (close, high, low, volume) panels, columns = symbols."""
    def p(col):
        d = {s: f[col] for s, f in frames.items() if col in f.columns}
        out = pd.DataFrame(d).sort_index()
        out.index = pd.to_datetime(out.index)
        if out.index.tz is not None:
            out.index = out.index.tz_localize(None)
        return out
    close = p("Close")
    return close, p("High").reindex(close.index), p("Low").reindex(close.index), \
        p("Volume").reindex(close.index)


# ── Price signals — the ONE definition, shared with the backtest ────────────

def price_signals(close, high, low, volume=None):
    """Every price metric the screens use, as panels shaped like `close`.

    Highs/lows over 52 weeks are taken on closes, as in the backtest.

    Interior gaps of up to 5 sessions are carried forward first. Every window
    here needs a FULL window (min_periods = length) on a union-date panel, so
    one missing bar — a halt, a Yahoo hole — would otherwise blank a stock's
    52-week and moving-average signals for up to a year and silently drop it
    from every trend screen. A plain carry-forward, deliberately: deciding
    by whether a LATER bar exists would be look-ahead in the backtest (a halt
    that ends in a delisting would be treated differently from one that
    resumes). A series that ended is carried 5 sessions and then goes NaN;
    the backtest's eligibility still requires a real bar on the date.
    Returns (hence volatility) use the UNFILLED close: a carried bar is not a
    0% day.
    """
    ret = close.pct_change(fill_method=None)
    # ADR on real bars too: a carried bar would repeat the previous day's
    # range. The mean of the real bars among the last 20, at least 15 of them.
    adr = (high / low - 1).rolling(20, min_periods=15).mean()
    close, high, low = close.ffill(limit=5), high.ffill(limit=5), low.ffill(limit=5)
    s = {}
    s["sma10"] = close.rolling(10, min_periods=10).mean()
    s["sma20"] = close.rolling(20, min_periods=20).mean()
    s["sma50"] = close.rolling(50, min_periods=50).mean()
    s["sma150"] = close.rolling(150, min_periods=150).mean()
    s["sma200"] = close.rolling(200, min_periods=200).mean()
    s["hi52"] = close.rolling(252, min_periods=252).max()
    s["lo52"] = close.rolling(252, min_periods=252).min()
    for k, n in (("r1m", 21), ("r3m", 63), ("r6m", 126), ("r9m", 189), ("r12m", 252)):
        s[k] = close / close.shift(n) - 1
    s["mom12_1"] = close.shift(21) / close.shift(252) - 1
    # IBD-style relative-strength composite: the last quarter double-weighted
    s["rs_raw"] = 0.4 * s["r3m"] + 0.2 * s["r6m"] + 0.2 * s["r9m"] + 0.2 * s["r12m"]
    s["vol252"] = ret.rolling(252, min_periods=200).std() * np.sqrt(252)
    s["adr20"] = adr
    s["off_high"] = close / s["hi52"] - 1
    s["above_low"] = close / s["lo52"] - 1
    sma200_up = s["sma200"] > s["sma200"].shift(21)
    crit = [
        close > s["sma150"], close > s["sma200"],              # 1
        s["sma150"] > s["sma200"],                              # 2
        sma200_up,                                              # 3
        (s["sma50"] > s["sma150"]) & (s["sma50"] > s["sma200"]),  # 4
        close > s["sma50"],                                     # 5
        close >= 1.30 * s["lo52"],                              # 6
        close >= 0.75 * s["hi52"],                              # 7
    ]
    # criterion 1 is two comparisons; count it once
    c1 = crit[0] & crit[1]
    parts = [c1] + crit[2:]
    s["tt_core"] = parts[0]
    for c in parts[1:]:
        s["tt_core"] = s["tt_core"] & c
    s["tt_count"] = sum(c.astype(float) for c in parts)  # 0..7 price criteria
    if volume is not None:
        s["dollar_vol50"] = (close * volume).rolling(50, min_periods=40).mean()
    s.update(pullback_signals(close, high, low))
    return s


# Martin Luk's pullback (US Investing Champion 2025, +969.8%), mechanically, on
# daily bars. His trigger is intraday (dip, turn, break the prior candle's
# high on a 1-15 min chart); the daily proxy is a buy stop at today's high,
# filled when the next session trades through it. Leadership (top 25% by
# 1/3/6-month return and up 30%+) is cross-sectional, so the CALLER ranks it
# over its own universe; these are the per-stock parts.
PULLBACK_MAX_RISK = 0.03   # his stops: "usually within 2.5 to 3%"
PULLBACK_MIN_RISK = 0.01   # a stop closer than 1% is inside a normal day's noise


def pullback_leaders(S, universe):
    """Step 1, cross-sectional: top 25% of `universe` (a boolean panel) by the
    combined 1/3/6-month return rank, up 30%+ over one of them, 50-day dollar
    volume >= $20M. Returns (leader panel, combined rank panel)."""
    def xrank(df):
        return df.where(universe).rank(axis=1, pct=True)
    # rank the COMBINED score again: an average of three uniform ranks bunches
    # toward 0.5, so ">= 0.75" on the average passed far fewer than a quarter
    combo = xrank((xrank(S["r1m"]) + xrank(S["r3m"]) + xrank(S["r6m"])) / 3)
    up30 = (S["r1m"] >= 0.30) | (S["r3m"] >= 0.30) | (S["r6m"] >= 0.30)
    liquid = S["dollar_vol50"] >= 20e6 if "dollar_vol50" in S else True
    return (combo >= 0.75) & up30 & liquid & universe, combo


def pullback_signals(close, high, low):
    out = {}
    ema = {n: close.ewm(span=n, adjust=False, min_periods=n).mean() for n in (9, 21, 50)}
    out["ema9"], out["ema21"], out["ema50"] = ema[9], ema[21], ema[50]
    rng = high / low - 1
    # resting, not falling: each pullback low higher than the last, swings
    # getting smaller, back above the 9 and 21 EMAs, the trend still up
    higher_lows = low.rolling(10, min_periods=10).min() > low.shift(10).rolling(10, min_periods=10).min()
    contracting = rng.rolling(10, min_periods=8).mean() < rng.rolling(30, min_periods=24).mean()
    above = (close > ema[9]) & (close > ema[21])
    trend = ema[21] > ema[50]
    # the zone: on one of the last 3 sessions the low reached THAT day's 21
    # EMA (within 2%) and held above that day's 50. (Comparing the 3-day low
    # with TODAY's EMA let a fast riser "touch" support it never visited: the
    # average had climbed up to an old low — KOD, 2026-10-19.)
    pb_low = low.rolling(3, min_periods=3).min()
    touch_day = (low <= ema[21] * 1.02) & (low >= ema[50] * 0.98)
    touched = touch_day.astype(float).rolling(3, min_periods=3).max() > 0
    entry = high                                     # buy stop: break today's high
    # just under the pullback low — never inside the pullback. A low more than
    # 3% under the entry is not his trade (stop "within 2.5 to 3%"): skipped,
    # not tightened (a clamp put the stop above the low it is meant to sit
    # under). Under 1% it is widened to 1%.
    stop = (pb_low * 0.999).clip(upper=entry * (1 - PULLBACK_MIN_RISK))
    tight_enough = stop >= entry * (1 - PULLBACK_MAX_RISK)
    out["pb_rest"] = higher_lows & contracting & above & trend & touched & tight_enough
    out["pb_entry"], out["pb_stop"] = entry, stop
    return out


def pct_rank(series):
    """Percentile 0..1 among the names that have a value; NaN stays NaN."""
    return series.rank(pct=True)


# ── Strategies ───────────────────────────────────────────────────────────────
# Measured numbers are NOT written here. `backtest` names the strategy's
# column in plans/research/screener_backtest; export_evidence.py writes those
# results to screener_evidence.json beside this file and evidence() reads
# them. A hand-copied card once carried a mistyped sub-period, and measured an
# inverted Magic Formula rank, unnoticed. `backtest: None` = no test we could
# run; `note` holds only what is not measured here (literature, caveats).

EVIDENCE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "screener_evidence.json")


def load_evidence():
    try:
        with open(EVIDENCE_FILE, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:
        return {}


def evidence(strategy, ev_doc):
    """The card's record for one strategy: measured numbers from the export,
    or tested=False when there is no backtest or the export lacks it."""
    out = {"tested": False, "note": strategy.get("note", "")}
    col = strategy.get("backtest")
    row = (ev_doc.get("backtests") or {}).get(col) if col else None
    if row:
        # _clean: a NaN in the file (an empty sub-period) must read as absent,
        # not crash the scan's allow_nan=False dump
        out.update({"tested": True, "excess": _clean(row.get("excess")), "t": _clean(row.get("t")),
                    "max_dd": _clean(row.get("max_dd")), "hit": _clean(row.get("hit")),
                    "subperiods": [_clean(x) for x in (row.get("subperiods") or [])]})
        for k in ("trades", "win_rate", "avg_R"):  # trade-simulated strategies only
            if row.get(k) is not None:
                out[k] = _clean(row[k])
        curve = (ev_doc.get("curves") or {}).get(col)
        if curve:
            out["curve"] = [_clean(x) for x in curve]  # growth of $1, monthly
    elif col:
        out["note"] = "Backtest results unavailable. " + out["note"]
    return out

STRATEGIES = [
    {
        "id": "trend_template", "name": "Minervini Trend Template", "family": "Practitioner",
        "source": "Mark Minervini — US Investing Champion 2021 (+334.8%), 1997",
        "summary": "Stage-2 uptrend filter: price above rising long averages, near its high, strong RS.",
        "rules": ["Close > 150-day and 200-day SMA", "150-day SMA > 200-day SMA",
                  "200-day SMA rising (vs 21 sessions ago)", "50-day SMA > 150-day and 200-day",
                  "Close > 50-day SMA", "Close ≥ 30% above 52-week low",
                  "Close within 25% of 52-week high", "RS rating ≥ 70"],
        "rank_by": "RS rating, highest first",
        "columns": ["rs", "off_high", "above_low", "r3m", "r12m", "adr20"],
        "backtest": 'Minervini Trend Template (all passers)',
        "note": 'A filter, not a ranking: holding every passer did not beat the index.',
    },
    {
        "id": "rs_leaders", "name": "RS Leaders (Trend Template top 25)", "family": "Practitioner",
        "source": "Minervini / IBD leadership — the strongest names that pass the template",
        "summary": "The 25 highest-RS stocks among Trend Template passers.",
        "rules": ["Passes all 8 Trend Template criteria", "Top 25 by RS rating"],
        "rank_by": "RS rating",
        "columns": ["rs", "off_high", "r1m", "r3m", "r12m", "adr20"],
        "backtest": 'Trend Template, top 25 by RS',
        "note": 'Its excess came in 2023-26; it lost 2015-19.',
    },
    {
        "id": "momentum", "name": "Momentum 12-1", "family": "Academic",
        "source": "Jegadeesh & Titman (1993); AQR 'Fact, Fiction and Momentum'",
        "summary": "Past 12-month return skipping the latest month — the classic cross-sectional momentum.",
        "rules": ["Return from 12 months ago to 1 month ago", "Top decile of the universe"],
        "rank_by": "12-1 return",
        "columns": ["mom12_1", "r1m", "rs", "off_high", "vol252"],
        "backtest": 'Momentum 12-1 (top decile)',
        "note": 'Lost in every period before 2023. Momentum crashes in sharp rebounds (2009: -73%, Daniel & Moskowitz).',
    },
    {
        "id": "canslim", "name": "CAN SLIM (partial)", "family": "Practitioner",
        "source": "William O'Neil; David Ryan — US Investing Champion 1985-87",
        "summary": "Growth leaders: accelerating earnings, near highs, strong RS, in a healthy market.",
        "rules": ["C: latest-quarter EPS ≥ +25% y/y", "A: fiscal-year EPS ≥ +25% and ROE ≥ 17%",
                  "N: within 15% of the 52-week high", "L: RS rating ≥ 80",
                  "M: market (SPY) above its 50- and 200-day — shown in the header, not a filter",
                  "S and I not screened (float-turnover and fund-sponsorship data unavailable)"],
        "rank_by": "RS rating",
        "columns": ["eps_growth_q", "eps_growth_fy", "roe", "rs", "off_high"],
        "backtest": None,
        "note": "Not backtested here (needs point-in-time quarterly EPS). AAII's hypothetical "
                "record: 23.8%/yr 1998-2007, before costs; the original screen often passes 2 stocks or fewer.",
    },
    {
        "id": "breakout", "name": "Breakout Leaders (Kullamaggie-style)", "family": "Practitioner",
        "source": "Kristjan Kullamägi — self-reported; no audited record",
        "summary": "The biggest recent movers that are volatile enough to trade and resting near their highs.",
        "rules": ["Top 5% by combined 1/3/6-month return rank", "ADR% (20-day) ≥ 4%",
                  "Within 15% of the 52-week high", "Close above 10- and 20-day SMA",
                  "50-day average dollar volume ≥ $20M"],
        "rank_by": "Combined 1/3/6-month rank",
        "columns": ["r1m", "r3m", "r6m", "adr20", "off_high", "dollar_vol50"],
        "backtest": 'Leaders: top 2% by 1/3/6m (Qullamaggie-style)',
        "note": "Tested without the ADR / consolidation filters (top 2% by 1/3/6-month rank): no edge and the deepest drawdown of any screen. The method's edge, if any, is in entries and stops, which a screen does not do.",
    },
    {
        "id": "magic_formula", "name": "Magic Formula", "family": "Academic",
        "source": "Joel Greenblatt, 'The Little Book That Beats the Market' (2005)",
        "summary": "Good businesses at cheap prices: high earnings yield plus high return on capital.",
        "rules": ["Earnings yield = EBIT / enterprise value", "Return on capital ≈ ROIC",
                  "Rank each, add the ranks, take the 30 best",
                  "Excludes Finance and Utilities"],
        "rank_by": "Combined rank",
        "columns": ["earnings_yield", "roic", "pe", "ev_ebitda", "r6m"],
        "backtest": 'Magic Formula (top 30)',
        "note": "Independent tests: 11.4% vs 8.7% for the S&P 2003-15, with long losing stretches — far below the book's ~30%.",
    },
    {
        "id": "piotroski", "name": "Piotroski F-Score ≥ 8", "family": "Academic",
        "source": "Joseph Piotroski (2000), 'Value Investing: The Use of Historical Financial Statement Information'",
        "summary": "Nine accounting tests of improving health; 8-9 passes. Cheapest first.",
        "rules": ["Profitability: ROA > 0, CFO > 0, ROA up, CFO > net income",
                  "Leverage/liquidity: leverage down, current ratio up, no new shares",
                  "Efficiency: gross margin up, asset turnover up",
                  "Score ≥ 8 (TradingView's computed TTM score)", "Sorted by price/book, cheapest first"],
        "rank_by": "Price/book, cheapest first",
        "columns": ["piotroski", "pb", "pe", "roe", "r6m"],
        "backtest": 'Piotroski F >= 8 (all)',
        "note": "A junk-avoidance filter, not alpha. Piotroski's own version (8-9 in the cheapest book/market quintile) left about 3 large caps a month here and lost money.",
    },
    {
        "id": "quality_momentum", "name": "Quality + Momentum", "family": "Academic",
        "source": "Novy-Marx (2013) gross profitability, combined with 12-1 momentum",
        "summary": "Most profitable third of the market by gross profit / assets, then the strongest 25 by momentum.",
        "rules": ["Gross profit / total assets in the top 30%", "Then top 25 by 12-1 momentum",
                  "Excludes Finance and Utilities"],
        "rank_by": "12-1 return",
        "columns": ["gpa", "mom12_1", "gross_margin", "roic", "off_high"],
        "backtest": 'Quality+Momentum: GP/A top 30% -> top 25 by 12-1',
        "note": 'Novy-Marx: gross profitability predicts about as well as book/market. Combined with momentum here: not significant.',
    },
    {
        "id": "trending_value", "name": "Trending Value", "family": "Academic",
        "source": "James O'Shaughnessy, 'What Works on Wall Street' (4th ed.)",
        "summary": "Cheapest decile on six value ratios, then the 25 with the best 6-month return.",
        "rules": ["Value composite: average percentile of E/P, B/P, S/P, CFO/P, EBIT/EV and "
                  "shareholder yield (needs ≥ 4 of 6; a loss ranks as expensive)", "Cheapest 10%", "Then top 25 by 6-month return"],
        "rank_by": "6-month return",
        "columns": ["value_score", "pe", "pb", "ev_ebitda", "shareholder_yield", "r6m"],
        "backtest": 'Trending Value: value top decile -> top 25 by 6m',
        "note": 'Book claim 21.2%/yr (1964-2009, in-sample, by the author).',
    },
    {
        "id": "luk_pullback", "name": "Leader Pullback (Martin Luk)", "family": "Practitioner",
        "source": "Martin Luk — US Investing Champion 2025 (+969.8%), 2024 6th (+283.1%)",
        "summary": "A strong stock resting at support, ready to turn: buy the break of the prior "
                   "day's high with a 1-3% stop, risking 0.5% of the account.",
        "rules": ["Leader: top 25% by 1/3/6-month return, up 30%+, $20M+ daily volume",
                  "Resting: higher lows, daily ranges contracting, above the 9 and 21 EMA, 21 > 50 EMA",
                  "Zone: a low in the last 3 sessions at the 21 EMA (within 2%), above the 50",
                  "Entry: a buy stop at today's high (his trigger is intraday: dip, turn, break the prior candle)",
                  "Stop: just under the 3-session pullback low (at least 1% away); a setup needing more than 3% is skipped",
                  "Size: shares so the stop costs 0.5% of the account",
                  "Exits: a third at 3R, a third at 5R, the rest trails the 9 EMA"],
        "rank_by": "Combined 1/3/6-month rank",
        "columns": ["entry", "stop", "risk_pct", "target3", "rs", "group_rs", "adr20"],
        "backtest": "Luk pullback (trades, 0.5% risk)",
        "note": "Measured as trades, not a list. Mechanically on daily bars it did WORSE than random "
                "entries with the same exits, on the S&P 500 and on today's small and mid caps alike: "
                "losses run past 1R because stocks gap through a 1-3% stop, and the rest-at-support "
                "filter picked worse moments than chance. Whatever his edge is lives in the intraday "
                "trigger and theme judgment, which a screen does not have. Use the list for candidates "
                "and the plan for sizing; the plan is the part with a defined risk.",
    },
    {
        "id": "low_vol", "name": "Low Volatility", "family": "Academic",
        "source": "Blitz & van Vliet (2007), 'The Volatility Effect'",
        "summary": "The calmest decile by one-year realised volatility — a defensive tilt.",
        "rules": ["Annualised volatility of daily returns, past year", "Lowest 10% of the universe"],
        "rank_by": "Volatility, lowest first",
        "columns": ["vol252", "r12m", "div_yield", "pe", "off_high"],
        "backtest": 'Low volatility (lowest decile)',
        "note": 'Lagged in a period led by high-beta tech, with the smallest drawdown of any screen: a risk choice, not a return one.',
    },
]


def _top(series, n=None, frac=None):
    s = series.dropna().sort_values(ascending=False, kind="stable")
    if frac is not None:
        n = max(1, int(round(len(s) * frac)))
    return list(s.index[:n])


def fetch_quotes(tv_symbols):
    """TradingView quotes for explicit symbols (SPY is not in the stock
    universe) -> {yahoo symbol: record}, same fields as parse_universe."""
    import urllib.request
    payload = {"symbols": {"tickers": list(tv_symbols)}, "columns": list(TV_COLUMNS)}
    req = urllib.request.Request(
        TV_URL, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    rows = json.loads(urllib.request.urlopen(req, timeout=30).read()).get("data") or []
    return parse_universe(rows)  # the one decoder for TradingView rows


# A quote is spliced onto the Yahoo history only if OUR change vs Yahoo's
# previous close agrees with TradingView's OWN change vs its previous close.
# A real move — a +35% earnings gap — agrees, so it is kept; a split Yahoo has
# not absorbed yet makes them disagree by >= 33 points (2:1: -50% vs ~0%).
# History is fetched once per close (the evening before), so its previous
# close is TradingView's previous close and the two changes agree to rounding.
# 1.5 points of slack separates even a 20:19 split (5 points) and a 10% stock
# dividend (9). What it does NOT cover is a history fetched this MORNING on an
# ex-dividend day (Yahoo re-bases yesterday by the dividend) — such a name falls
# back to its last close and is counted: truth over coverage. No gap-size
# rule: the discriminator is consistency, not magnitude.
LIVE_CHANGE_TOL = 0.015


def _delay_minutes(mode):
    """TradingView's update_mode -> minutes of delay: "delayed_streaming_900"
    -> 15, "streaming" -> 0, anything unrecognised -> None (unknown)."""
    if not isinstance(mode, str):
        return None
    if mode == "streaming":
        return 0
    m = re.search(r"delayed\w*?_(\d+)$", mode)
    return int(m.group(1)) // 60 if m else None


def apply_live_bars(frames, quotes, now=None):
    """During the session, today's bar from TradingView's quote replaces or
    appends today's row of each Yahoo history — an intraday refresh then needs
    no Yahoo request per name. A quote is used only if its bar is TODAY's
    session, O/H/L/C are present and consistent (low <= open, close <= high),
    and our change vs the previous close agrees with TradingView's own change
    (LIVE_CHANGE_TOL) — a split the history has not absorbed makes them
    disagree; a genuine big move does not.

    Every OTHER name loses today's partial Yahoo row, if it has one: it then
    shows its last real close, never a bar frozen at whenever Yahoo was last
    asked. Returns (frames, info) with the counts, the WORST delay among the
    quotes used, and the time their prices are from (the median quote's update
    time minus that delay — update_time is when the delayed quote was sent).
    """
    now = now or datetime.now(timezone.utc)
    info = {"applied": 0, "on_last_close": 0, "price_time": None, "delayed_min": None}
    if not in_session(now):
        return frames, info
    today = now.astimezone(_et()).date()
    # The previous session = the most common last date (before today) across
    # all histories — holidays included, and robust to any one series being
    # stale (SPY alone was a single point of failure: its failed chunk made
    # every current name look stale and let the stale ones through). A history
    # that does not end there is missing a day; no live bar across the hole.
    from collections import Counter
    lasts = Counter()
    for f in frames.values():
        if f is None or f.empty:
            continue
        before_dates = [d for d in f.index if d.date() < today]
        if before_dates:
            lasts[before_dates[-1].date()] += 1
    prev_session = lasts.most_common(1)[0][0] if lasts else None
    updates, delays = [], []
    out = dict(frames)
    for sym, f in frames.items():
        if f is None or f.empty:
            continue
        idx = f.index
        day = pd.Timestamp(today)
        if idx.tz is not None:
            day = day.tz_localize(idx.tz)
        before = f[idx < day]
        q = quotes.get(sym) or {}
        t = q.get("q_time")
        o, h, l, c, v = (q.get(k) for k in ("q_open", "q_high", "q_low", "q_close", "q_volume"))
        tv_change = q.get("q_change")
        prev = before["Close"].dropna()
        usable = (
            t is not None
            and datetime.fromtimestamp(t, timezone.utc).astimezone(_et()).date() == today
            and None not in (o, h, l, c) and tv_change is not None
            and 0 < l <= min(o, c) and max(o, c) <= h
            and not prev.empty and abs((c / prev.iloc[-1] - 1) - tv_change) <= LIVE_CHANGE_TOL
            and (prev_session is None or prev.index[-1].date() == prev_session)
        )
        if usable:
            g = before.copy()
            g.loc[day] = {"Open": o, "High": h, "Low": l, "Close": c,
                          "Volume": v if v is not None else np.nan}
            out[sym] = g
            if sym != BENCHMARK:
                info["applied"] += 1
            if q.get("q_update"):
                updates.append(q["q_update"])
            delays.append(_delay_minutes(q.get("q_mode")))
        else:
            if len(before) < len(f):
                out[sym] = before  # drop the partial: its last real close stands
            if sym != BENCHMARK:
                info["on_last_close"] += 1
    if delays:
        info["delayed_min"] = None if any(d is None for d in delays) else max(delays)
    if updates and info["delayed_min"] is not None:
        # unknown delay -> unknown price time: the send time is NOT the price time
        med = float(np.median(updates)) - 60 * info["delayed_min"]
        info["price_time"] = datetime.fromtimestamp(med, timezone.utc).astimezone(_et()).strftime("%H:%M ET")
    return out, info


def compute(fund, frames, extras=None, now=None):
    """Metrics for every name with prices, and each strategy's ranked list.

    `fund` = parse_universe() output, `frames` = load_prices() output.
    """
    close, high, low, volume = panels(frames)
    # Keep only sessions most names traded. One frame with an extra row — a
    # pre-market stub with volume and no prices, a holiday bar, SPY's own
    # dates — becomes a union-index row that is NaN for everyone else; read as
    # "the last session" it blanked every 1D change and dated the scan to a
    # day that had not traded.
    # Near-empty rows only: mid-session, a real day can have today's bar for
    # well under half the names (a failed chunk serves the rest stale), and
    # dropping it would discard the fresh prices and date the scan yesterday.
    real = close.notna().sum(axis=1) >= max(3, 0.05 * close.shape[1])
    close, high, low, volume = close[real], high[real], low[real], volume[real]
    spy = close.pop(BENCHMARK) if BENCHMARK in close.columns else None
    for p in (high, low, volume):
        if BENCHMARK in p.columns:
            p.drop(columns=BENCHMARK, inplace=True)
    sig = price_signals(close, high, low, volume)
    last = {k: v.iloc[-1] for k, v in sig.items()}

    syms = sorted(s for s in close.columns if s in fund)
    df = pd.DataFrame(index=syms)
    px = close.ffill().iloc[-1]
    df["price"] = px.reindex(syms)
    # Unfilled: a name with no bar on the last session has NO 1-day change —
    # yesterday's close twice would print a confident +0.0%.
    if len(close) > 1:
        df["chg_1d"] = (close.iloc[-1] / close.iloc[-2] - 1).reindex(syms)
    else:
        df["chg_1d"] = np.nan
    for k in ("r1m", "r3m", "r6m", "r12m", "mom12_1", "vol252", "adr20", "off_high",
              "above_low", "dollar_vol50", "tt_count", "sma10", "sma20", "rs_raw"):
        df[k] = last[k].reindex(syms)
    df["tt_core"] = last["tt_core"].reindex(syms).fillna(False).astype(bool)
    # RS rating 1..99 — a percentile against THIS universe, like IBD's
    df["rs"] = (pct_rank(df["rs_raw"]) * 98 + 1).round()

    f = pd.DataFrame.from_dict(fund, orient="index").reindex(syms)
    for k in ("name", "sector", "industry", "mcap", "piotroski", "roic", "roe", "gross_margin",
              "eps_growth_q", "eps_growth_fy", "rev_growth", "pe", "pb", "ps", "ev_ebitda",
              "div_yield", "next_earnings"):
        df[k] = f[k] if k in f.columns else np.nan

    num = lambda c: pd.to_numeric(f[c], errors="coerce") if c in f.columns else pd.Series(np.nan, index=syms)
    operating = ~f["sector"].isin(NON_OPERATING_SECTORS) if "sector" in f.columns else pd.Series(True, index=syms)
    ev, ebit, assets, mcap = num("ev"), num("ebit"), num("total_assets"), num("mcap")
    df["earnings_yield"] = (ebit / ev).where((ev > 0) & operating)
    df["gpa"] = (num("gross_profit") / assets).where((assets > 0) & operating)
    # TradingView reports an explicit 0 for a non-payer / non-repurchaser, so a
    # null is UNKNOWN: either part absent makes the sum absent, never "0 + x".
    df["shareholder_yield"] = num("div_yield") + num("buyback_yield")
    # Value composite: higher = cheaper. Yields, not multiples, so a loss or a
    # negative book ranks as the MOST expensive (a negative yield) instead of
    # dropping out and letting the name qualify on the ratios that flatter it.
    # Same six as the backtest that supplies the evidence: E/P, B/P, S/P,
    # CFO/P, EBIT/EV (O'Shaughnessy uses EBITDA/EV; the SEC test had no D&A),
    # shareholder yield.
    pb = num("pb")
    ocf, rev, ni = num("ocf"), num("revenue"), num("net_income")
    pos = mcap > 0
    value = pd.DataFrame({
        "ep": (ni / mcap).where(pos), "bp": (1 / pb).where(pb != 0), "sp": (rev / mcap).where(pos),
        "cfp": (ocf / mcap).where(pos), "ey": (ebit / ev).where(ev > 0),
        "shy": df["shareholder_yield"],
    })
    vr = value.rank(pct=True)
    df["value_n"] = vr.notna().sum(axis=1)  # ratios present (a loss still counts)
    df["value_score"] = vr.mean(axis=1).where(df["value_n"] >= 4)

    lists = {}
    rs = df["rs"]
    tt = df["tt_core"] & (rs >= 70)
    lists["trend_template"] = _top(rs[tt])
    lists["rs_leaders"] = _top(rs[tt], n=25)
    lists["momentum"] = _top(df["mom12_1"], frac=0.10)
    can = ((df["eps_growth_q"] >= 0.25) & (df["eps_growth_fy"] >= 0.25) & (df["roe"] >= 0.17)
           & (df["off_high"] >= -0.15) & (rs >= 80))
    lists["canslim"] = _top(rs[can])
    combo = pct_rank(df["r1m"]) + pct_rank(df["r3m"]) + pct_rank(df["r6m"])
    leaders = combo >= combo.quantile(0.95)
    brk = (leaders & (df["adr20"] >= 0.04) & (df["off_high"] >= -0.15)
           & (df["price"] > df["sma10"]) & (df["price"] > df["sma20"])
           & (df["dollar_vol50"] >= 20e6))
    lists["breakout"] = _top(combo[brk])
    both = df["earnings_yield"].notna() & df["roic"].notna()
    magic = -(df["earnings_yield"][both].rank(ascending=False) + df["roic"][both].rank(ascending=False))
    lists["magic_formula"] = _top(magic, n=30)
    pio = df["piotroski"] >= 8
    lists["piotroski"] = _top((-df["pb"].where(df["pb"] > 0))[pio])
    gpa_pool = _top(df["gpa"], frac=0.30)
    lists["quality_momentum"] = _top(df["mom12_1"].reindex(gpa_pool), n=25)
    cheap = _top(df["value_score"], frac=0.10)
    lists["trending_value"] = _top(df["r6m"].reindex(cheap), n=25)
    lists["low_vol"] = _top(-df["vol252"], frac=0.10)

    # Leader pullback: the same leader rank the trade backtest uses, over THIS
    # universe, on the last COMPLETED session. Mid-session the last bar is
    # still moving — its high (the entry) and low (the stop) change by the
    # minute, and names without today's bar yet would drop out of the rank.
    now = now or datetime.now(timezone.utc)
    k = -1
    if len(close.index) > 1 and in_session(now) and \
            close.index[-1].strftime("%Y-%m-%d") == now.astimezone(_et()).strftime("%Y-%m-%d"):
        k = -2
    plan_asof = close.index[k].strftime("%Y-%m-%d") if len(close.index) else None
    one = {n: sig[n].iloc[[k]].reindex(columns=syms) for n in ("r1m", "r3m", "r6m", "dollar_vol50")}
    lead, combo_pb = pullback_leaders(one, pd.DataFrame(True, index=one["r1m"].index, columns=syms))
    rest = sig["pb_rest"].iloc[k].reindex(syms).fillna(False).astype(bool)
    pb = lead.iloc[-1] & rest
    if k == -2:
        # Mid-session the plan is yesterday's — and today may already have
        # acted on it: a high through the entry means the buy stop fired (a new
        # order would fill at the market, risk far past 1R), a low through the
        # stop means the setup failed. Neither is a pending plan.
        th, tl = high.iloc[-1].reindex(syms), low.iloc[-1].reindex(syms)
        e_, s_ = sig["pb_entry"].iloc[k].reindex(syms), sig["pb_stop"].iloc[k].reindex(syms)
        pb &= ~(th >= e_).fillna(False) & ~(tl <= s_).fillna(False)
    df["entry"] = sig["pb_entry"].iloc[k].reindex(syms).where(pb)
    df["stop"] = sig["pb_stop"].iloc[k].reindex(syms).where(pb)
    df["risk_pct"] = 1 - df["stop"] / df["entry"]
    df["target3"] = df["entry"] + 3 * (df["entry"] - df["stop"])
    df["target5"] = df["entry"] + 5 * (df["entry"] - df["stop"])
    lists["luk_pullback"] = _top(combo_pb.iloc[-1][pb])

    # Industry strength ("hot themes": several names in one group running
    # together): an industry's median RS, as a 1..99 percentile across
    # industries with >= 3 names, and how many of its names have RS >= 80.
    if "industry" in df.columns:
        g = df.groupby("industry")["rs"]
        med = g.median().where(g.count() >= 3)
        df["group_rs"] = df["industry"].map((med.rank(pct=True) * 98 + 1).round())
        # 0 only where the industry is KNOWN; an unknown industry is absent
        df["group_leaders"] = df["industry"].map(df[df["rs"] >= 80].groupby("industry").size()) \
            .where(df["industry"].isna(), lambda x: x.fillna(0))

    market = {}
    if spy is not None and spy.notna().sum() > 200:
        sp = spy.dropna()
        market = {"spy": float(sp.iloc[-1]),
                  "above50": bool(sp.iloc[-1] > sp.rolling(50).mean().iloc[-1]),
                  "above200": bool(sp.iloc[-1] > sp.rolling(200).mean().iloc[-1])}
    asof = close.index[-1].strftime("%Y-%m-%d") if len(close.index) else None
    if extras is not None:
        # NOT df.attrs: pandas deep-copies attrs into every derived frame
        extras["closes"] = close.iloc[-CHART_BARS:]
        extras["plan_asof"] = plan_asof
    return df, lists, market, asof


def _clean(v):
    if v is None:
        return None
    if isinstance(v, (bool, np.bool_)):
        return bool(v)
    if isinstance(v, (int, float, np.integer, np.floating)):
        f = float(v)
        return f if math.isfinite(f) else None
    return v


OUT_FIELDS = ["name", "sector", "industry", "price", "chg_1d", "mcap", "rs", "r1m", "r3m", "r6m",
              "r12m", "mom12_1", "off_high", "above_low", "vol252", "adr20", "dollar_vol50",
              "tt_count", "earnings_yield", "roic", "roe", "gpa", "gross_margin", "piotroski",
              "eps_growth_q", "eps_growth_fy", "rev_growth", "pe", "pb", "ps", "ev_ebitda",
              "div_yield", "shareholder_yield", "value_score", "next_earnings",
              "entry", "stop", "risk_pct", "target3", "target5", "group_rs", "group_leaders"]
CHART_BARS = 126  # closes per listed name: the table's 6-month trend line (kSparkBars)


def scan(min_mcap=2e9, force=False):
    log_progress(0, 1, "Fetching universe")
    fund = fetch_universe(min_mcap)
    if not fund:
        return {"error": "TradingView scanner returned no stocks"}
    # ONE clock for the whole scan, taken BEFORE the price load: a scan that
    # straddles 16:15 must not judge freshness by one clock and live bars by
    # another (a morning partial bar then went out as the close)
    now = datetime.now(timezone.utc)
    frames, fetched_ts, stale = load_prices(sorted(fund), force=force, now=now)
    quotes = dict(fund)
    if in_session(now):
        try:
            quotes.update(fetch_quotes(["AMEX:SPY"]))
        except Exception as e:  # the market line falls back to SPY's last close
            print(f"SPY quote failed: {e}", file=sys.stderr)
    frames, live = apply_live_bars(frames, quotes, now)
    log_progress(1, 1, "Computing screens")
    extras = {}
    df, lists, market, asof = compute(fund, frames, extras, now)
    in_any = sorted({s for v in lists.values() for s in v})
    rows = {s: {k: _clean(df.at[s, k]) for k in OUT_FIELDS if k in df.columns} for s in in_any}
    closes = extras.get("closes")
    if closes is not None:
        tail = closes.iloc[-CHART_BARS:]
        for s in in_any:
            if s in tail.columns:
                # 4 significant digits: the chart needs shape, not cents
                rows[s]["closes"] = [None if not np.isfinite(v) else float(f"{v:.4g}")
                                     for v in tail[s].to_numpy()]
    doc = meta(lists)
    doc.update({
        "asof": asof,
        # today's bar is still moving: the label must not say "close"
        "intraday": bool(in_session(now)) and asof == now.astimezone(_et()).strftime("%Y-%m-%d"),
        # the OLDEST fetch behind the lists, with its date: a failed chunk
        # leaves days-old frames in play, and an undated time implies today
        "prices_fetched": datetime.fromtimestamp(fetched_ts, timezone.utc).astimezone(_et())
        .strftime("%Y-%m-%d %H:%M ET") if fetched_ts else None,
        "stale_prices": int(stale),
        # today's bar during the session: TradingView's quote, as delayed as it says
        "live_bar": live,
        "universe": {"n": len(fund), "priced": int(df["price"].notna().sum()),
                     "min_mcap": float(min_mcap)},
        "market": market,
        "plan_asof": extras.get("plan_asof"),  # the completed session the pullback plans use
        "rows": rows,
    })
    return doc


def meta(lists=None):
    """Everything about the strategies that needs no market data — rules,
    evidence, the equity curves. `screener_data.py meta` prints it in well
    under a second, so the tab shows its strategies and their records at once
    while the first scan (a minute on a cold cache) downloads."""
    ev_doc = load_evidence()
    curves = ev_doc.get("curves") or {}
    strategies = []
    for st in STRATEGIES:
        out = {k: st[k] for k in ("id", "name", "family", "source", "summary", "rules",
                                  "rank_by", "columns")}
        out["evidence"] = evidence(st, ev_doc)
        out["symbols"] = (lists or {}).get(st["id"], [])
        strategies.append(out)
    return {
        "evidence_basis": ev_doc.get("basis", ""),
        "subperiods": ev_doc.get("subperiods", []),
        # the index lines every strategy's curve is drawn against
        "curve_months": curves.get("months", []),
        "curve_ew": [_clean(x) for x in curves.get("EW", [])],
        "curve_spy": [_clean(x) for x in curves.get("SPY", [])],
        "strategies": strategies,
    }


def main(argv):
    if len(argv) >= 2 and argv[1] == "meta":
        print(json.dumps(meta(), allow_nan=False, default=_clean))
        return 0
    if len(argv) < 2 or argv[1] != "scan":
        print(json.dumps({"error": "usage: screener_data.py meta | scan '{json}'"}))
        return 2
    try:
        args = json.loads(argv[2]) if len(argv) > 2 else {}
    except ValueError:
        args = {}
    try:
        out = json.dumps(scan(float(args.get("min_mcap") or 2e9), bool(args.get("force"))),
                         allow_nan=False, default=_clean)
    except Exception as e:  # one JSON error, never a traceback on stdout
        out = json.dumps({"error": f"{type(e).__name__}: {e}"})
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
