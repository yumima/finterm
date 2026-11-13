"""
Score the *shipped* rating over history, using the shipped code.

Every other measurement in this package scores a Python signal. That is fine
for research and useless for the question the user actually asks, which is
whether the number on the ER Technicals panel means anything. A Python
re-implementation of the C++ scorer only ever proves things about the
re-implementation.

So this drives `technicals_score_series`, a small tool that links the real
TechnicalRating translation unit and walks it bar by bar over an indicator
series. What comes back is the exact composite the panel would have printed on
each historical date, which is what makes statements like "the shipped rating
agrees with a 40-day trend label 80.1% of the time" statements about the
product rather than about a model of it.

Build the tool first:
    cmake --build build/linux-tests --target technicals_score_series
"""

import json
import os
import subprocess

import pandas as pd

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.abspath(os.path.join(_HERE, "..", ".."))

_CANDIDATE_PATHS = [
    os.path.join(_REPO, "build", "linux-tests", "tests", "technicals_score_series"),
    os.path.join(_REPO, "build", "linux-release", "tests", "technicals_score_series"),
]


def find_tool():
    for p in _CANDIDATE_PATHS:
        if os.path.exists(p):
            return p
    raise FileNotFoundError(
        "technicals_score_series not built. Run:\n"
        "  cmake --build build/linux-tests --target technicals_score_series")


def score_panel(panel, min_bars=300, progress=None):
    """Composite score per (date, symbol), straight from the shipped scorer.

    Returns a DataFrame aligned to `panel['close']`. Symbols whose series is
    too short to warm the indicators up are dropped rather than half-rated.
    """
    import sys
    sys.path.insert(0, os.path.join(_REPO, "scripts"))
    import importlib
    yd = importlib.import_module("yfinance_data")

    tool = find_tool()
    close, high, low, volume = (panel["close"], panel["high"],
                                panel["low"], panel["volume"])
    tz = close.index.tz
    out = {}
    for i, sym in enumerate(close.columns):
        df = pd.DataFrame({
            "timestamp": close.index.astype("int64") // 10 ** 9,
            "open": close[sym], "high": high[sym], "low": low[sym],
            "close": close[sym], "volume": volume[sym],
        }).dropna()
        if len(df) < min_bars:
            continue
        res = yd.compute_technicals_from_candles(df.to_dict("records"))
        if not res.get("success"):
            continue
        blob = json.dumps(yd._sanitize_for_json(res["data"]), allow_nan=False)
        text = subprocess.run([tool], input=blob, capture_output=True,
                              text=True).stdout
        pairs = [ln.split(",") for ln in text.strip().split("\n") if ln]
        if not pairs:
            continue
        out[sym] = pd.Series({
            pd.Timestamp(int(ts), unit="s", tz="UTC").tz_convert(tz).normalize(): float(v)
            for ts, v in pairs})
        if progress and (i + 1) % progress == 0:
            print(f"  scored {i + 1}/{len(close.columns)}", flush=True)

    scored = pd.DataFrame(out).reindex(close.index.normalize())
    scored.index = close.index
    return scored
