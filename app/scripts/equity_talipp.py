"""
equity_talipp.py — TALIpp indicator computation for the Equity Research TALIpp tab.

A thin CLI shim over scripts/Analytics/talipp_wrapper — the real talipp-backed
implementations whose operation ids the tab's indicator list was built against.
(The previous version of this file was a hand-rolled reimplementation of ~30
indicators under DIFFERENT ids — "BBANDS" where the tab sends "bb", "WILLR"
where it sends "williams" — so over half the tab's catalogue exited with
"unknown indicator", and the error went to stdout where the runner's failure
path never looked. This shim exists so there is exactly ONE implementation of
each indicator, keyed by the ids the UI actually sends.)

Usage:
    python equity_talipp.py <indicator_id> <ohlcv_json> [params_json]

Arguments:
    indicator_id  A talipp_service.dispatch operation id, lowercase — the same
                  id EquityTalippTab stores per indicator (sma, bb, williams,
                  supertrend, zigzag, ...).
    ohlcv_json    JSON array of bars {timestamp, open, high, low, close,
                  volume} — or "@<path>" naming a file holding that JSON
                  (argv caps one argument at 128 KiB on Linux; a long
                  history must ride in a file).
    params_json   Optional JSON object of the tab's parameter spinboxes.

Output (stdout):
    {"success": true, "indicator": str, "values": [float], "timestamps": [int]}
    values/timestamps are PAIRED by index; warm-up (None) entries are dropped
    pairwise so the chart never plots a null as 0.

Errors print {"success": false, "error": ...} to stdout AND the message to
stderr before exit 1 — the C++ failure path surfaces stderr, so the reason
must live there, not only in a stdout document nobody reads on failure.
"""

import json
import math
import os
import sys

# The wrapper package lives under scripts/Analytics/.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "Analytics"))


def die(msg: str) -> None:
    print(json.dumps({"success": False, "error": msg}))
    print(msg, file=sys.stderr)
    sys.exit(1)


# Tab spinbox name → wrapper kwarg, where they differ. Anything not listed
# passes through under its own name; wrapper defaults cover the rest.
PARAM_MAP = {
    "macd": {"fast": "fast_period", "slow": "slow_period", "signal": "signal_period"},
    "stoch": {"k_period": "period", "d_period": "smoothing_period"},
    "parabolic_sar": {"acceleration": "af_start", "maximum": "af_max"},
    "bb": {"stddev": "std_dev"},
    "keltner": {"multiplier": "atr_multi"},
    "chandelier_stop": {"multiplier": "atr_multi"},
    "chaikin_osc": {"fast": "fast_period", "slow": "slow_period"},
    "coppock_curve": {"roc1": "roc1_period", "roc2": "roc2_period", "wma": "wma_period"},
    "kvo": {"fast": "fast_period", "slow": "slow_period", "signal": "signal_period"},
    # Tab says fast=9/slow=25; the wrapper's names are ema_period=9/period=25.
    "mass_index": {"slow": "period", "fast": "ema_period"},
    "uo": {"fast": "short_period", "medium": "medium_period", "slow": "long_period"},
    "stc": {"fast": "fast_period", "slow": "slow_period", "period": "cycle"},
    "zigzag": {"deviation": "change"},
    "adx": {"period": "di_period"},
}

# A single tab knob that feeds two wrapper kwargs.
PARAM_MIRROR = {
    "adx": [("di_period", "adx_period")],
}

# Params that are genuinely fractional — everything else that arrives as an
# integral float (QVariant doubles serialize as 20.0) is cast to int, because
# talipp constructors take period counts, not floats.
FLOAT_PARAMS = {
    "std_dev", "atr_multi", "multiplier", "af_start", "af_max", "af_increment",
    "change", "offset", "sigma", "vfactor",
}

# Indicators whose whole meaning is volume. Bars without volume data make
# these UNAVAILABLE, never a fabricated series — OBV over all-zero volume is
# a flat line that looks exactly like a real measurement.
VOLUME_INDICATORS = {
    "obv", "vwap", "vwma", "mfi", "chaikin_osc", "force_index", "accu_dist",
    "emv", "kvo", "sobv",
}

# The series the tab charts for a multi-output indicator. Single-output
# indicators return their line under 'values' and need no entry.
PRIMARY_SERIES = {
    "macd": "macd",
    "stoch": "k",
    "stoch_rsi": "k",
    "tsi": "tsi",
    "aroon": "up",
    "ichimoku": "tenkan_sen",
    "bb": "middle",
    "keltner": "middle",
    "donchian": "middle",
    "chandelier_stop": "long_stop",
    "kst": "kst",
    "kvo": "kvo",
    "trix": "trix",
    "vtx": "plus",
}


def main() -> None:
    if len(sys.argv) < 3:
        die("Usage: equity_talipp.py <indicator> <ohlcv_json> [params_json]")

    indicator = sys.argv[1].lower()
    ohlcv_raw = sys.argv[2]
    if ohlcv_raw.startswith("@"):
        try:
            with open(ohlcv_raw[1:], "r", encoding="utf-8") as fh:
                ohlcv_raw = fh.read()
        except OSError as e:
            die(f"cannot read OHLCV payload file: {e}")
            return
    try:
        rows = json.loads(ohlcv_raw)
        raw_params = json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}
    except json.JSONDecodeError as e:
        die(f"Bad JSON argument: {e}")
        return

    if not isinstance(rows, list) or not rows:
        die("OHLCV data must be a non-empty JSON array")

    # A missing price field is an error, never a 0 bar: a zero price would
    # silently corrupt every indicator value downstream of it. Volume is the
    # one field some instruments genuinely lack; the wrapper treats absent
    # volume as 0 and only the volume-driven indicators care.
    def field(r: dict, key: str) -> float:
        v = r.get(key)
        if v is None:
            raise ValueError(f"OHLCV bar missing '{key}': {r}")
        return float(v)

    timestamps = []
    bars = []
    try:
        for r in rows:
            ts = r.get("timestamp")
            # None AND non-positive are both rejected, deliberately more
            # strict than the price fields: a bar stamped 0 is the epoch —
            # an upstream decoding bug, not a 1970 session — and one such
            # pair would stretch the chart's x-axis back 55 years.
            if ts is None or int(ts) <= 0:
                raise ValueError(f"OHLCV bar missing or epoch 'timestamp': {r}")
            timestamps.append(int(ts))
            bars.append({
                "open": field(r, "open"),
                "high": field(r, "high"),
                "low": field(r, "low"),
                "close": field(r, "close"),
                "volume": float(r.get("volume") or 0.0),
                # Per-event indicators (zigzag) stamp their pivots from this.
                "timestamp": int(ts),
            })
    except (TypeError, ValueError) as e:
        die(str(e))
        return

    # bars are built above with "volume" always present as a float (absent/null
    # coerced to 0.0), so the direct index cannot raise.
    if indicator in VOLUME_INDICATORS and not any(b["volume"] > 0 for b in bars):
        die(f"'{indicator}' is a volume indicator and these bars carry no volume "
            f"data — unavailable for this symbol")
        return

    # Map the tab's knob names onto the wrapper's kwargs and restore integer
    # types the QVariant round-trip floated.
    name_map = PARAM_MAP.get(indicator, {})
    params = {}
    for k, v in (raw_params or {}).items():
        key = name_map.get(k, k)
        if isinstance(v, float) and v.is_integer() and key not in FLOAT_PARAMS:
            v = int(v)
        params[key] = v
    for src, dst in PARAM_MIRROR.get(indicator, []):
        if src in params:
            params[dst] = params[src]

    try:
        from talipp_wrapper.talipp_service import dispatch
    except ImportError as e:
        die(f"talipp wrapper unavailable: {e}")
        return

    data = {"prices": [b["close"] for b in bars], "ohlcv": bars, **params}
    result = dispatch(indicator, data)
    if not isinstance(result, dict):
        die(f"indicator '{indicator}' returned no result")
        return
    if result.get("error"):
        die(f"{indicator}: {result['error']}")
        return

    key = PRIMARY_SERIES.get(indicator, "values")
    series = result.get(key)
    if not isinstance(series, list):
        die(f"indicator '{indicator}' returned no '{key}' series")
        return
    # An indicator that emits per-EVENT rather than per-bar (zigzag's pivots)
    # supplies its own timestamps; everything else is 1:1 with the input bars,
    # and a mismatch means the pairing below would chart values against the
    # wrong dates.
    own_ts = result.get("timestamps")
    if isinstance(own_ts, list):
        timestamps = own_ts
    if len(series) != len(timestamps):
        die(f"indicator '{indicator}' returned {len(series)} values for "
            f"{len(timestamps)} timestamps — cannot align")
        return

    # Drop warm-up / gap entries PAIRWISE: a null serialized to the chart
    # becomes 0.0 there, which reads as a real value at $0.
    out_t, out_v = [], []
    for t, v in zip(timestamps, series):
        if v is None:
            continue
        f = float(v)
        if math.isnan(f) or math.isinf(f):
            continue
        out_t.append(t)
        out_v.append(f)

    if not out_v:
        die(f"indicator '{indicator}' produced no values — the series is likely "
            f"shorter than the indicator's warm-up period")
        return

    print(json.dumps({
        "success": True,
        "indicator": indicator.upper(),
        "values": out_v,
        "timestamps": out_t,
    }))


if __name__ == "__main__":
    main()
