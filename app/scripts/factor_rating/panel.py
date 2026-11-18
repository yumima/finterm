"""
Price panel for the cross-sectional factor rating.

The indicator rating under ER Technicals reads one stock in isolation: it can
say "this is above its averages" but not "…and so is everything else, so that
tells you nothing." Every factor here is scored *relative to a universe on the
same date*, which is what makes a rating comparable between instruments and
across time. That needs a panel — many symbols, one aligned date index — rather
than the single series the indicator path fetches.

Downloads are cached to disk because the walk-forward validation re-reads the
same decade of history on every run, and re-pulling it from Yahoo each time is
both slow and rude.

Survivorship: the universe is today's large caps, so the panel cannot see names
that were large and then died. That biases *absolute* return studies upward.
The validation deliberately reports rank-based statistics (information
coefficient, decile spread) computed within each date, which compare names
against their contemporaries and are far less sensitive to that bias — but it
does not eliminate it, and no conclusion here should be read as a live-tradeable
backtest.
"""

import hashlib
import os
import time

import pandas as pd
from finterm_paths import cache_dir

CACHE_DIR = os.environ.get(
    "FACTOR_CACHE_DIR",
    str(cache_dir("factor_panel")),
)

BENCHMARK = "SPY"


def _cache_path(name):
    return os.path.join(CACHE_DIR, name + ".parquet")


def download_panel(symbols, years=12, force=False):
    """Daily OHLCV for `symbols` plus the benchmark, aligned on one date index.

    Returns a dict of field -> DataFrame (index=date, columns=symbol) for
    close / high / low / volume. Adjusted prices (yfinance auto_adjust default),
    so splits and dividends are already handled.
    """
    import yfinance as yf

    os.makedirs(CACHE_DIR, exist_ok=True)
    tickers = sorted(set(list(symbols) + [BENCHMARK]))
    # md5, not hash(): Python string hashing is salted per process, so a
    # hash()-based key changed on every run and the cache never hit — the
    # exact re-download-everything behaviour this cache exists to prevent.
    digest = hashlib.md5(",".join(tickers).encode()).hexdigest()[:10]
    key = f"panel_{years}y_{len(tickers)}_{digest}"
    path = _cache_path(key)

    if os.path.exists(path) and not force:
        age_days = (time.time() - os.path.getmtime(path)) / 86400
        if age_days < 1:
            raw = pd.read_parquet(path)
            _prune_panels(keep=path)
            return _split_fields(raw)

    raw = yf.download(tickers, period=f"{years}y", interval="1d",
                      group_by="column", auto_adjust=True, progress=False,
                      threads=True)
    # yfinance returns a column MultiIndex (field, symbol); flatten for parquet.
    raw.columns = [f"{a}|{b}" for a, b in raw.columns]
    raw.to_parquet(path)
    _prune_panels(keep=path)
    return _split_fields(raw)


def _prune_panels(keep, max_age_days=1):
    """Drop cached panels nothing will read: a panel is reused for a day only
    (see load), and every universe change or key-scheme change writes a NEW
    file — without this the directory only ever grows (437 MB of unreadable
    hash()-keyed panels were found)."""
    cutoff = time.time() - max_age_days * 86400
    for name in os.listdir(CACHE_DIR):
        p = os.path.join(CACHE_DIR, name)
        if p == keep or not name.startswith("panel_") or not name.endswith(".parquet"):
            continue
        try:
            if os.path.getmtime(p) < cutoff:
                os.remove(p)
        except OSError:
            pass


def _split_fields(raw):
    fields = {}
    for col in raw.columns:
        field, sym = col.split("|", 1)
        fields.setdefault(field.lower(), {})[sym] = raw[col]
    out = {}
    for field, cols in fields.items():
        df = pd.DataFrame(cols).sort_index()
        out[field] = df
    return out


def clean(panel, min_history=260, min_price=1.0):
    """Drop columns too short, too cheap or too gappy to carry a signal.

    A trend reading on a $0.40 stock is mostly tick size, and one on a series
    with six months of history is mostly warm-up. Excluding them is not
    cosmetic: they are exactly the names that produce the largest, least
    reliable factor values and would otherwise dominate the extremes of every
    cross-section.
    """
    close = panel["close"]
    keep = []
    for sym in close.columns:
        s = close[sym].dropna()
        if len(s) < min_history:
            continue
        if s.iloc[-1] < min_price:
            continue
        # More than 10% missing days over the covered span is a broken series.
        span = close.loc[s.index[0]:s.index[-1], sym]
        if span.isna().mean() > 0.10:
            continue
        keep.append(sym)
    return {f: df[[c for c in keep if c in df.columns]] for f, df in panel.items()}
