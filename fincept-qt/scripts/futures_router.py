#!/usr/bin/env python3
"""
Futures Data Router

Single entrypoint for the FuturesScreen. Implements a 3-source cascade:

    Databento (GLBX.MDP3, paid)  →  CME public delayed  →  yfinance (free, delayed)

China commodity futures route directly to akshare_futures (separate fallback).

Output: {"success": bool, "data": [...], "source": str, "error": str|None,
         "timestamp": int}

Endpoints (called as `python futures_router.py <endpoint> <args...>`):

    quotes <symbols-csv>            front-month quotes for a watchlist
    term_structure <product> <n>    front + n back-months for a product
    settlements <product> <date>    daily settlements + OI + volume
    heatmap                         %chg grid across all asset classes
    history <symbol> <period>       continuous chart history
    spread <leg1> <leg2>            calendar/inter-commodity spread
    china_main <exchange?>          Chinese main-month contracts (akshare)
    china_history <symbol>          Chinese contract history (akshare)
"""
import sys
import os
import json
import time
import io
import importlib
from typing import Any, Dict, List, Optional

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, SCRIPTS_DIR)


def _ok(data: Any, source: str) -> Dict[str, Any]:
    return {
        "success": True,
        "data": data,
        "source": source,
        "error": None,
        "timestamp": int(time.time()),
    }


def _err(msg: str, source: str = "router") -> Dict[str, Any]:
    return {
        "success": False,
        "data": [],
        "source": source,
        "error": msg,
        "timestamp": int(time.time()),
    }


def _has_databento_key() -> bool:
    return bool(os.environ.get("DATABENTO_API_KEY"))


# ── Continuous symbol map ────────────────────────────────────────────────────
# yfinance uses "ROOT=F" for continuous front-month. CME uses product codes.
# Each entry: (yfinance_continuous, cme_product_code, display_name, asset_class)
CONTRACTS: Dict[str, Dict[str, Any]] = {
    # US Equity Index
    "ES":  {"yf": "ES=F",  "cme": "ES",  "name": "E-mini S&P 500",   "class": "INDEX",  "tick": 0.25,  "mult": 50},
    "NQ":  {"yf": "NQ=F",  "cme": "NQ",  "name": "E-mini Nasdaq 100","class": "INDEX",  "tick": 0.25,  "mult": 20},
    "YM":  {"yf": "YM=F",  "cme": "YM",  "name": "E-mini Dow",       "class": "INDEX",  "tick": 1.0,   "mult": 5},
    "RTY": {"yf": "RTY=F", "cme": "RTY", "name": "E-mini Russell 2000","class": "INDEX","tick": 0.10, "mult": 50},
    # US Rates
    "ZT":  {"yf": "ZT=F",  "cme": "ZT",  "name": "2-Year T-Note",  "class": "RATES", "tick": 0.0078125, "mult": 2000},
    "ZF":  {"yf": "ZF=F",  "cme": "ZF",  "name": "5-Year T-Note",  "class": "RATES", "tick": 0.0078125, "mult": 1000},
    "ZN":  {"yf": "ZN=F",  "cme": "ZN",  "name": "10-Year T-Note", "class": "RATES", "tick": 0.015625,  "mult": 1000},
    "ZB":  {"yf": "ZB=F",  "cme": "ZB",  "name": "30-Year T-Bond", "class": "RATES", "tick": 0.03125,   "mult": 1000},
    "UB":  {"yf": "UB=F",  "cme": "UB",  "name": "Ultra T-Bond",   "class": "RATES", "tick": 0.03125,   "mult": 1000},
    "ZQ":  {"yf": "ZQ=F",  "cme": "ZQ",  "name": "30-Day Fed Funds","class": "RATES","tick": 0.0025,    "mult": 4167},
    "SR3": {"yf": "SR3=F", "cme": "SR3", "name": "3-Month SOFR",   "class": "RATES", "tick": 0.0025,    "mult": 2500},
    # Energy
    "CL":  {"yf": "CL=F",  "cme": "CL",  "name": "WTI Crude Oil",   "class": "ENERGY", "tick": 0.01, "mult": 1000},
    "BZ":  {"yf": "BZ=F",  "cme": "BZ",  "name": "Brent Crude",     "class": "ENERGY", "tick": 0.01, "mult": 1000},
    "NG":  {"yf": "NG=F",  "cme": "NG",  "name": "Henry Hub Nat Gas","class": "ENERGY","tick": 0.001,"mult": 10000},
    "RB":  {"yf": "RB=F",  "cme": "RB",  "name": "RBOB Gasoline",   "class": "ENERGY", "tick": 0.0001,"mult": 42000},
    "HO":  {"yf": "HO=F",  "cme": "HO",  "name": "Heating Oil",     "class": "ENERGY", "tick": 0.0001,"mult": 42000},
    # Metals
    "GC":  {"yf": "GC=F",  "cme": "GC",  "name": "Gold",       "class": "METALS", "tick": 0.10,  "mult": 100},
    "SI":  {"yf": "SI=F",  "cme": "SI",  "name": "Silver",     "class": "METALS", "tick": 0.005, "mult": 5000},
    "HG":  {"yf": "HG=F",  "cme": "HG",  "name": "Copper",     "class": "METALS", "tick": 0.0005,"mult": 25000},
    "PL":  {"yf": "PL=F",  "cme": "PL",  "name": "Platinum",   "class": "METALS", "tick": 0.10,  "mult": 50},
    "PA":  {"yf": "PA=F",  "cme": "PA",  "name": "Palladium",  "class": "METALS", "tick": 0.05,  "mult": 100},
    # Agriculture / Softs
    "ZC":  {"yf": "ZC=F",  "cme": "ZC",  "name": "Corn",       "class": "AGS", "tick": 0.25,  "mult": 50},
    "ZW":  {"yf": "ZW=F",  "cme": "ZW",  "name": "Wheat",      "class": "AGS", "tick": 0.25,  "mult": 50},
    "ZS":  {"yf": "ZS=F",  "cme": "ZS",  "name": "Soybeans",   "class": "AGS", "tick": 0.25,  "mult": 50},
    "ZL":  {"yf": "ZL=F",  "cme": "ZL",  "name": "Soybean Oil","class": "AGS", "tick": 0.0001,"mult": 60000},
    "ZM":  {"yf": "ZM=F",  "cme": "ZM",  "name": "Soybean Meal","class": "AGS","tick": 0.10,  "mult": 100},
    "KC":  {"yf": "KC=F",  "cme": "KC",  "name": "Coffee",     "class": "AGS", "tick": 0.05,  "mult": 37500},
    "SB":  {"yf": "SB=F",  "cme": "SB",  "name": "Sugar #11",  "class": "AGS", "tick": 0.01,  "mult": 112000},
    "CC":  {"yf": "CC=F",  "cme": "CC",  "name": "Cocoa",      "class": "AGS", "tick": 1.0,   "mult": 10},
    "CT":  {"yf": "CT=F",  "cme": "CT",  "name": "Cotton",     "class": "AGS", "tick": 0.01,  "mult": 50000},
    # FX
    "6E":  {"yf": "6E=F",  "cme": "6E",  "name": "Euro FX",       "class": "FX", "tick": 0.00005, "mult": 125000},
    "6J":  {"yf": "6J=F",  "cme": "6J",  "name": "Japanese Yen",  "class": "FX", "tick": 0.0000005,"mult": 12500000},
    "6B":  {"yf": "6B=F",  "cme": "6B",  "name": "British Pound", "class": "FX", "tick": 0.0001,  "mult": 62500},
    "6A":  {"yf": "6A=F",  "cme": "6A",  "name": "Australian Dollar","class": "FX","tick": 0.0001,"mult": 100000},
    "6C":  {"yf": "6C=F",  "cme": "6C",  "name": "Canadian Dollar","class": "FX","tick": 0.00005, "mult": 100000},
    # Crypto (CME)
    "BTC": {"yf": "BTC=F", "cme": "BTC", "name": "Bitcoin Futures",     "class": "CRYPTO","tick": 5.0, "mult": 5},
    "MET": {"yf": "ETH=F", "cme": "MET", "name": "Micro Ether Futures", "class": "CRYPTO","tick": 0.25,"mult": 0.1},
}


def _safe_yf(func, *args, **kwargs):
    try:
        import yfinance as yf  # noqa: F401
        return func(*args, **kwargs)
    except Exception as e:
        return _err(f"yfinance error: {e}", source="yfinance")


def _yf_quotes(symbols: List[str]) -> Dict[str, Any]:
    """Front-month quotes via yfinance (delayed, free). Returns rows."""
    try:
        import yfinance as yf
    except Exception as e:
        return _err(f"yfinance not installed: {e}", source="yfinance")

    rows: List[Dict[str, Any]] = []
    yf_syms = []
    sym_lookup = {}
    for s in symbols:
        meta = CONTRACTS.get(s)
        if not meta:
            continue
        yf_syms.append(meta["yf"])
        sym_lookup[meta["yf"]] = s

    if not yf_syms:
        return _ok([], "yfinance")

    try:
        # Single batched download — 5d/1d gives us last + prev close
        df = yf.download(
            tickers=" ".join(yf_syms),
            period="5d",
            interval="1d",
            group_by="ticker",
            progress=False,
            threads=True,
            auto_adjust=False,
        )
    except Exception as e:
        return _err(f"yfinance download failed: {e}", source="yfinance")

    for yf_sym in yf_syms:
        root = sym_lookup[yf_sym]
        meta = CONTRACTS[root]
        try:
            sub = df[yf_sym] if (yf_sym in df.columns.get_level_values(0) if hasattr(df.columns, "levels") else False) else df
            closes = sub["Close"].dropna()
            if len(closes) < 1:
                continue
            last = float(closes.iloc[-1])
            prev = float(closes.iloc[-2]) if len(closes) >= 2 else last
            chg = last - prev
            chg_pct = (chg / prev * 100.0) if prev else 0.0
            vol = float(sub["Volume"].iloc[-1]) if "Volume" in sub.columns and len(sub["Volume"]) else 0.0
            high = float(sub["High"].iloc[-1]) if "High" in sub.columns else last
            low = float(sub["Low"].iloc[-1]) if "Low" in sub.columns else last
            rows.append({
                "symbol": root,
                "name": meta["name"],
                "class": meta["class"],
                "last": last,
                "change": chg,
                "change_pct": chg_pct,
                "volume": vol,
                "open_interest": 0,  # yfinance doesn't expose OI
                "high": high,
                "low": low,
            })
        except Exception:
            continue

    return _ok(rows, "yfinance")


def _yf_history(symbol: str, period: str = "6mo", interval: str = "1d") -> Dict[str, Any]:
    try:
        import yfinance as yf
    except Exception as e:
        return _err(f"yfinance not installed: {e}", source="yfinance")
    meta = CONTRACTS.get(symbol)
    if not meta:
        return _err(f"unknown symbol: {symbol}")
    try:
        t = yf.Ticker(meta["yf"])
        df = t.history(period=period, interval=interval, auto_adjust=False)
        if df.empty:
            return _ok([], "yfinance")
        rows = []
        for ts, row in df.iterrows():
            rows.append({
                "timestamp": int(ts.timestamp()),
                "open": float(row.get("Open", 0) or 0),
                "high": float(row.get("High", 0) or 0),
                "low": float(row.get("Low", 0) or 0),
                "close": float(row.get("Close", 0) or 0),
                "volume": float(row.get("Volume", 0) or 0),
            })
        return _ok(rows, "yfinance")
    except Exception as e:
        return _err(f"yfinance history failed: {e}", source="yfinance")


def _cme_term_structure(product: str, n_months: int) -> Dict[str, Any]:
    """CME public delayed quotes — returns front + back months."""
    try:
        cme = importlib.import_module("cme_data")
        info = cme.get_delayed_quotes(product)
        if isinstance(info, dict) and info.get("error"):
            return _err(info["error"], source="cme_public")
        # CME quotes payloads usually include `quotes` array of contract months
        if isinstance(info, dict) and "quotes" in info:
            quotes = info["quotes"][:n_months]
            return _ok(quotes, "cme_public")
        return _ok(info if isinstance(info, list) else [info], "cme_public")
    except Exception as e:
        return _err(f"cme_public error: {e}", source="cme_public")


def _cme_settlements(product: str, date: str) -> Dict[str, Any]:
    try:
        cme = importlib.import_module("cme_data")
        s = cme.get_settlement_prices(product, date)
        v = cme.get_volume(product, date)
        oi = cme.get_open_interest(product, date)
        return _ok({"settlements": s, "volume": v, "open_interest": oi}, "cme_public")
    except Exception as e:
        return _err(f"cme_public error: {e}", source="cme_public")


def _yf_term_structure_proxy(product: str, n_months: int) -> Dict[str, Any]:
    """
    yfinance only exposes the continuous front-month, not individual months.
    Best-effort proxy: return the continuous quote labelled as "FRONT". Caller
    will treat single-row term structures as "data not available, see CME tab".
    """
    q = _yf_quotes([product])
    if not q.get("success") or not q.get("data"):
        return _err("yf term structure unavailable", source="yfinance")
    row = q["data"][0]
    return _ok([{
        "month": "FRONT",
        "last": row["last"],
        "change": row["change"],
        "volume": row["volume"],
        "open_interest": row["open_interest"],
    }], "yfinance")


# ── Databento bridge (only used when key present) ────────────────────────────

def _databento_quotes(symbols: List[str]) -> Optional[Dict[str, Any]]:
    if not _has_databento_key():
        return None
    try:
        # The existing databento_provider.py is a heavy class — keep this thin.
        # We import lazily to avoid the import cost when no key is set.
        dbn = importlib.import_module("databento_provider")
        prov = dbn.DatabentoProvider()
        rows = []
        for s in symbols:
            meta = CONTRACTS.get(s)
            if not meta:
                continue
            try:
                data = prov.get_futures_data([meta["cme"]], days=2, schema="ohlcv-1d")
                if not data:
                    continue
                last_bar = data[-1]
                prev_bar = data[-2] if len(data) >= 2 else last_bar
                last = float(last_bar.get("close", 0))
                prev = float(prev_bar.get("close", 0))
                chg = last - prev
                rows.append({
                    "symbol": s,
                    "name": meta["name"],
                    "class": meta["class"],
                    "last": last,
                    "change": chg,
                    "change_pct": (chg / prev * 100.0) if prev else 0.0,
                    "volume": float(last_bar.get("volume", 0)),
                    "open_interest": float(last_bar.get("open_interest", 0) or 0),
                    "high": float(last_bar.get("high", last)),
                    "low": float(last_bar.get("low", last)),
                })
            except Exception:
                continue
        return _ok(rows, "databento")
    except Exception as e:
        return _err(f"databento error: {e}", source="databento")


# ── Public endpoints ────────────────────────────────────────────────────────

def cmd_quotes(symbols_csv: str) -> Dict[str, Any]:
    symbols = [s.strip().upper() for s in symbols_csv.split(",") if s.strip()]
    if not symbols:
        return _err("no symbols")
    res = _databento_quotes(symbols)
    if res and res.get("success") and res.get("data"):
        return res
    return _yf_quotes(symbols)


def cmd_term_structure(product: str, n: str = "8") -> Dict[str, Any]:
    n_months = max(1, int(n))
    product = product.upper()
    # Prefer CME public for term structure (yfinance has only continuous)
    res = _cme_term_structure(product, n_months)
    if res.get("success") and res.get("data"):
        return res
    return _yf_term_structure_proxy(product, n_months)


def cmd_settlements(product: str, date: str = "") -> Dict[str, Any]:
    if not date:
        date = time.strftime("%Y%m%d", time.localtime(time.time() - 86400))
    return _cme_settlements(product.upper(), date)


def cmd_heatmap() -> Dict[str, Any]:
    """%chg grid for all configured contracts, grouped by asset class."""
    syms = list(CONTRACTS.keys())
    res = cmd_quotes(",".join(syms))
    if not res.get("success"):
        return res
    by_class: Dict[str, List[Dict[str, Any]]] = {}
    for r in res["data"]:
        by_class.setdefault(r["class"], []).append(r)
    return _ok({"groups": by_class}, res.get("source", "yfinance"))


def cmd_history(symbol: str, period: str = "6mo", interval: str = "1d") -> Dict[str, Any]:
    return _yf_history(symbol.upper(), period, interval)


def cmd_spread(leg1: str, leg2: str) -> Dict[str, Any]:
    """Inter-commodity or calendar spread = leg1.last - leg2.last."""
    res = cmd_quotes(f"{leg1},{leg2}")
    if not res.get("success") or len(res.get("data", [])) < 2:
        return _err("insufficient data for spread")
    rows = {r["symbol"]: r for r in res["data"]}
    a = rows.get(leg1.upper())
    b = rows.get(leg2.upper())
    if not (a and b):
        return _err("legs not found")
    return _ok({
        "leg1": a, "leg2": b,
        "spread": a["last"] - b["last"],
        "spread_pct": (a["change_pct"] - b["change_pct"]),
    }, res.get("source", "yfinance"))


def cmd_china_main(exchange: str = "all") -> Dict[str, Any]:
    """Chinese main-month contracts via akshare."""
    try:
        ak_mod = importlib.import_module("akshare_futures")
    except Exception as e:
        return _err(f"akshare_futures not available: {e}", source="akshare")
    try:
        if exchange == "all":
            r = ak_mod.get_futures_main_sina()
        elif exchange.lower() == "shfe":
            r = ak_mod.get_futures_contract_info_shfe()
        elif exchange.lower() == "dce":
            r = ak_mod.get_futures_contract_info_dce()
        elif exchange.lower() == "czce":
            r = ak_mod.get_futures_contract_info_czce()
        elif exchange.lower() == "ine":
            r = ak_mod.get_futures_contract_info_ine()
        else:
            return _err(f"unknown exchange: {exchange}")
        return _ok(r.get("data", []), "akshare")
    except Exception as e:
        return _err(f"akshare error: {e}", source="akshare")


def cmd_china_history(symbol: str) -> Dict[str, Any]:
    try:
        ak_mod = importlib.import_module("akshare_futures")
        r = ak_mod.get_futures_zh_daily_sina(symbol=symbol)
        return _ok(r.get("data", []), "akshare")
    except Exception as e:
        return _err(f"akshare error: {e}", source="akshare")


def cmd_contracts() -> Dict[str, Any]:
    """Return the contract catalog grouped by asset class."""
    by_class: Dict[str, List[Dict[str, Any]]] = {}
    for sym, meta in CONTRACTS.items():
        by_class.setdefault(meta["class"], []).append({
            "symbol": sym,
            "name": meta["name"],
            "tick": meta["tick"],
            "mult": meta["mult"],
        })
    return _ok({"groups": by_class}, "static")


COMMANDS = {
    "quotes": cmd_quotes,
    "term_structure": cmd_term_structure,
    "settlements": cmd_settlements,
    "heatmap": cmd_heatmap,
    "history": cmd_history,
    "spread": cmd_spread,
    "china_main": cmd_china_main,
    "china_history": cmd_china_history,
    "contracts": cmd_contracts,
}


def main():
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
    if len(sys.argv) < 2:
        print(json.dumps(_err("no command")))
        sys.exit(1)
    cmd = sys.argv[1]
    args = sys.argv[2:]
    fn = COMMANDS.get(cmd)
    if not fn:
        print(json.dumps(_err(f"unknown command: {cmd}")))
        sys.exit(1)
    try:
        result = fn(*args)
    except TypeError as e:
        result = _err(f"bad args for {cmd}: {e}")
    except Exception as e:
        result = _err(f"{cmd} failed: {e}")
    print(json.dumps(result, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()
