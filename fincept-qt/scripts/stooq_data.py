"""
Stooq Data Fetcher
OHLCV historical data for 21000+ global stocks (US, Japan, Poland, Germany, Hungary)
via the Stooq CSV download endpoint. Parses CSV responses and converts to JSON.
"""
import sys
import json
import os
import io
import csv
import time
import requests
from typing import Dict, Any, Optional, List

API_KEY = os.environ.get('STOOQ_API_KEY', '')
BASE_URL = "https://stooq.com/q/d/l"

session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=3)
session.mount('https://', adapter)
session.mount('http://', adapter)


def _make_request(endpoint: str, params: Dict = None) -> Any:
    url = f"{BASE_URL}/{endpoint}" if not endpoint.startswith('http') else endpoint
    try:
        response = session.get(url, params=params, timeout=30)
        response.raise_for_status()
        return response.text
    except requests.exceptions.HTTPError as e:
        return {"error": f"HTTP {e.response.status_code}: {str(e)}"}
    except requests.exceptions.RequestException as e:
        return {"error": f"Request failed: {str(e)}"}


def _parse_csv(csv_text: str) -> Any:
    if isinstance(csv_text, dict):
        return csv_text
    try:
        reader = csv.DictReader(io.StringIO(csv_text))
        rows = list(reader)
        if not rows:
            return {"error": "No data returned from Stooq"}
        cleaned = []
        for row in rows:
            cleaned.append({k.strip(): v.strip() for k, v in row.items()})
        return {"data": cleaned, "count": len(cleaned)}
    except Exception as e:
        return {"error": f"CSV parse error: {str(e)}"}


def get_historical(symbol: str, start: str = None, end: str = None, interval: str = "d") -> Any:
    params = {"s": symbol.lower(), "i": interval}
    if start:
        params["d1"] = start.replace("-", "")
    if end:
        params["d2"] = end.replace("-", "")
    raw = _make_request("", params)
    return _parse_csv(raw)


def get_quote(symbol: str) -> Any:
    params = {"s": symbol.lower(), "i": "d"}
    raw = _make_request("", params)
    parsed = _parse_csv(raw)
    if "data" in parsed and parsed["data"]:
        return {"symbol": symbol, "latest": parsed["data"][-1]}
    return parsed


def get_index_components(index: str) -> Any:
    index_map = {
        "^spx": "sp500",
        "^djx": "djia",
        "^ndx": "nasdaq100",
        "wig20": "wig20",
        "nikkei": "n225",
    }
    return {"error": "Index component listing not supported by Stooq CSV API — use symbol directly", "known_indices": list(index_map.keys())}


def get_multiple(symbols: str, start: str = None, end: str = None) -> Any:
    symbol_list = [s.strip() for s in symbols.split(",")]
    results = {}
    for sym in symbol_list:
        results[sym] = get_historical(sym, start, end)
    return results


def search_symbol(query: str) -> Any:
    params = {"q": query}
    try:
        response = session.get("https://stooq.com/q/s/", params=params, timeout=30)
        return {"note": "Stooq does not expose a public search API. Use symbol directly (e.g. AAPL.US, MSFT.US, ^SPX, PKN.PL)", "query": query}
    except requests.exceptions.RequestException as e:
        return {"error": f"Request failed: {str(e)}"}


# ── Futures support — used by the FuturesScreen when the user picks Stooq ──
# Stooq uses lowercase root + ".f" suffix for continuous front-month futures.
# Some products that exist on yfinance aren't on Stooq (MET micro-ether,
# certain rate/SOFR contracts) — those return no-data and the caller falls
# back to yfinance.

FUTURES_MAP = {
    "ES": "es.f", "NQ": "nq.f", "YM": "ym.f", "RTY": "rty.f",
    "ZT": "zt.f", "ZF": "zf.f", "ZN": "zn.f", "ZB": "zb.f", "UB": "ub.f",
    "ZQ": "zq.f", "SR3": "sr3.f",
    "CL": "cl.f", "BZ": "b.f", "NG": "ng.f", "RB": "rb.f", "HO": "ho.f",
    "GC": "gc.f", "SI": "si.f", "HG": "hg.f", "PL": "pl.f", "PA": "pa.f",
    "ZC": "zc.f", "ZW": "zw.f", "ZS": "zs.f", "ZL": "zl.f", "ZM": "zm.f",
    "KC": "kc.f", "SB": "sb.f", "CC": "cc.f", "CT": "ct.f",
    "6E": "6e.f", "6J": "6j.f", "6B": "6b.f", "6A": "6a.f", "6C": "6c.f",
    "BTC": "btc.f",
}


def _stooq_for_root(root: str):
    return FUTURES_MAP.get(root.upper())


def _fetch_one_futures_quote(root, start, end):
    """Fetch a single root's last+prev close. Returns row dict or None."""
    stooq_sym = _stooq_for_root(root)
    if not stooq_sym:
        return None
    raw = _make_request("", {"s": stooq_sym, "i": "d", "d1": start, "d2": end})
    if isinstance(raw, dict):
        return None
    parsed = _parse_csv(raw)
    if "data" not in parsed or not parsed["data"]:
        return None
    records = parsed["data"]
    try:
        last = records[-1]
        prev = records[-2] if len(records) >= 2 else last
        last_close = float(last.get("Close") or 0)
        prev_close = float(prev.get("Close") or last_close)
        change = last_close - prev_close
        change_pct = (change / prev_close * 100.0) if prev_close else 0.0
        return {
            "symbol": root,
            "name": root,
            "class": "",
            "last": last_close,
            "change": change,
            "change_pct": change_pct,
            "volume": float(last.get("Volume") or 0),
            "open_interest": 0,
            "high": float(last.get("High") or last_close),
            "low": float(last.get("Low") or last_close),
        }
    except (ValueError, KeyError):
        return None


def get_futures_quotes(symbols_csv: str) -> Any:
    """Last + previous close + volume for each futures root via daily CSV.

    Stooq has no batch API, so each symbol is one HTTP request — but the
    `requests.Session` is connection-pooled and Stooq's CSV endpoint is
    fast (~200ms), so we fan out across a thread pool. For a 35-contract
    refresh this drops wall time from ~10s to ~1s.
    """
    from concurrent.futures import ThreadPoolExecutor
    from datetime import datetime, timedelta
    today = datetime.utcnow()
    start = (today - timedelta(days=14)).strftime("%Y%m%d")
    end = today.strftime("%Y%m%d")
    syms = [s.strip().upper() for s in symbols_csv.split(",") if s.strip()]

    rows = []
    # 8 workers is a polite cap — Stooq has no published rate limit but bans
    # aggressive scrapers. With 35 symbols and 8 workers, ~5 sequential
    # rounds of ~200ms each ≈ 1s wall.
    with ThreadPoolExecutor(max_workers=8) as ex:
        for row in ex.map(lambda r: _fetch_one_futures_quote(r, start, end), syms):
            if row is not None:
                rows.append(row)

    return {"success": True, "data": rows, "source": "stooq", "error": None,
            "timestamp": int(time.time())}


def get_futures_history(symbol: str, period: str = "6mo") -> Any:
    """Daily history for a futures root — period: 1mo/3mo/6mo/1y/2y/5y."""
    from datetime import datetime, timedelta
    days_map = {"1mo": 31, "3mo": 92, "6mo": 183, "1y": 366, "2y": 731, "5y": 1827}
    days = days_map.get(period, 183)
    today = datetime.utcnow()
    start = (today - timedelta(days=days)).strftime("%Y%m%d")
    end = today.strftime("%Y%m%d")
    stooq_sym = _stooq_for_root(symbol)
    if not stooq_sym:
        return {"success": False, "data": [], "source": "stooq",
                "error": f"unknown symbol: {symbol}", "timestamp": int(time.time())}
    raw = _make_request("", {"s": stooq_sym, "i": "d", "d1": start, "d2": end})
    if isinstance(raw, dict):
        return {"success": False, "data": [], "source": "stooq",
                "error": raw.get("error", "request failed"), "timestamp": int(time.time())}
    parsed = _parse_csv(raw)
    if "data" not in parsed:
        return {"success": False, "data": [], "source": "stooq",
                "error": parsed.get("error", "no data"), "timestamp": int(time.time())}
    pts = []
    for r in parsed["data"]:
        try:
            from datetime import datetime as _dt
            ts = int(_dt.strptime(r["Date"], "%Y-%m-%d").timestamp())
            pts.append({
                "timestamp": ts,
                "open": float(r.get("Open") or 0),
                "high": float(r.get("High") or 0),
                "low": float(r.get("Low") or 0),
                "close": float(r.get("Close") or 0),
                "volume": float(r.get("Volume") or 0),
            })
        except (ValueError, KeyError):
            continue
    return {"success": True, "data": pts, "source": "stooq", "error": None,
            "timestamp": int(time.time())}


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if not args:
        print(json.dumps({"error": "No command provided"}))
        return
    command = args[0]
    result = {"error": f"Unknown command: {command}"}

    if command == "historical":
        symbol = args[1] if len(args) > 1 else "AAPL.US"
        start = args[2] if len(args) > 2 else None
        end = args[3] if len(args) > 3 else None
        interval = args[4] if len(args) > 4 else "d"
        result = get_historical(symbol, start, end, interval)
    elif command == "quote":
        symbol = args[1] if len(args) > 1 else "AAPL.US"
        result = get_quote(symbol)
    elif command == "index":
        index = args[1] if len(args) > 1 else "^SPX"
        result = get_index_components(index)
    elif command == "multiple":
        symbols = args[1] if len(args) > 1 else "AAPL.US,MSFT.US"
        start = args[2] if len(args) > 2 else None
        end = args[3] if len(args) > 3 else None
        result = get_multiple(symbols, start, end)
    elif command == "search":
        query = args[1] if len(args) > 1 else "apple"
        result = search_symbol(query)
    elif command == "futures_quotes":
        symbols = args[1] if len(args) > 1 else ""
        result = get_futures_quotes(symbols)
    elif command == "futures_history":
        symbol = args[1] if len(args) > 1 else "ES"
        period = args[2] if len(args) > 2 else "6mo"
        result = get_futures_history(symbol, period)

    print(json.dumps(result))


if __name__ == "__main__":
    main()
