"""SEC's ticker <-> CIK directory (company_tickers.json): the ONE copy.

Every script that needs "which registrant is this ticker" or "is this CIK
exchange-listed yet" reads it here: one request with the SEC contact identity
(SEC refuses the generic project user agent), one cache file under
cache/sec_facts, refreshed daily, the last good copy used when SEC does not
answer (re-asked minutes later). It used to be fetched four ways — a week-long cache in
sec_statement_overlay, an hour-long in-process cache in sec_data, and a fresh
download on every call in pre_ipo_listings and brief_drafts (the first with a
user agent SEC answers 403).

    rows()                -> [{"cik": int, "ticker": "AAPL", "title": "Apple Inc."}, ...]
    by_ticker()           -> {"AAPL": row, ...}   (class shares: BRK-B, as SEC writes them)
    by_cik()              -> {320193: [row, ...]}  (first row = primary listing)
    cik(symbol)           -> "320193" | None
"""

from __future__ import annotations

import json
import os
import threading
import time
from typing import Any, Dict, List, Optional

URL = "https://www.sec.gov/files/company_tickers.json"
# SEC adds a registrant the day its shares trade: a day-old copy is the
# freshest the pre-IPO "has it listed?" check should tolerate.
TTL_S = 24 * 3600
# After a refusal: ask again this soon (the last good copy, if any, is served
# meanwhile; with none, callers hear Unavailable at once instead of each
# waiting out their own request).
RETRY_S = 5 * 60
TIMEOUT = (4, 15)

_lock = threading.Lock()
_memo: Optional[List[Dict[str, Any]]] = None
_memo_at = 0.0           # when _memo stops being trusted, minus TTL_S
_failed_until = 0.0      # no copy and SEC refused: don't ask again before this
_failure = ""
_idx: Dict[str, Any] = {}  # derived indexes of _memo (by_ticker / by_cik)


class Unavailable(RuntimeError):
    """SEC did not answer and no earlier copy exists."""


def _path() -> str:
    import finterm_paths
    return os.path.join(str(finterm_paths.cache_dir("sec_facts")), "directory.json")


def _read(path: str) -> Optional[List[Dict[str, Any]]]:
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, list) and data else None
    except (OSError, ValueError):
        return None


def _write(path: str, rows: List[Dict[str, Any]]) -> None:
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        # The overlay's former ticker -> CIK copy, superseded by this file.
        try:
            os.remove(os.path.join(os.path.dirname(path), "tickers.json"))
        except OSError:
            pass
        tmp = f"{path}.{os.getpid()}.{threading.get_ident()}.tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(rows, f)
        os.replace(tmp, path)
    except OSError:
        pass


def parse(doc: Any) -> List[Dict[str, Any]]:
    """company_tickers.json -> rows, in SEC's order (a registrant's first row
    is its primary listing)."""
    out = []
    if isinstance(doc, dict):
        for k in sorted(doc, key=lambda x: int(x) if str(x).isdigit() else 0):
            v = doc[k]
            if not isinstance(v, dict) or not v.get("ticker") or v.get("cik_str") is None:
                continue
            try:
                cik = int(v["cik_str"])
            except (TypeError, ValueError):
                continue
            out.append({"cik": cik, "ticker": str(v["ticker"]).upper(), "title": str(v.get("title") or "")})
    return out


def _fetch() -> List[Dict[str, Any]]:
    import sec_http  # the one SEC client: contact identity, shared pacing
    r, status = sec_http.get(URL, timeout=TIMEOUT)
    if r is None:
        raise Unavailable("SEC refused the ticker directory (HTTP %s)" % status if status
                          else "SEC did not answer for the ticker directory")
    rows = parse(r.json())
    if not rows:
        raise Unavailable("SEC ticker directory came back empty")
    return rows


def _set(data: List[Dict[str, Any]], trusted_until: float) -> List[Dict[str, Any]]:
    global _memo, _memo_at, _idx
    _memo, _memo_at, _idx = data, trusted_until - TTL_S, {}
    return data


def rows() -> List[Dict[str, Any]]:
    """The directory. Raises Unavailable when SEC will not serve it and no
    earlier copy exists — a refusal, never "not listed"."""
    global _failed_until, _failure
    with _lock:
        now = time.time()
        if _memo is not None and now - _memo_at < TTL_S:
            return _memo
        if _memo is None and now < _failed_until:
            raise Unavailable(_failure)
        path = _path()
        try:
            written = os.path.getmtime(path)
        except OSError:
            written = None
        if written is not None and now - written < TTL_S:
            data = _read(path)
            if data is not None:
                return _set(data, written + TTL_S)
        try:
            data = _fetch()
            _write(path, data)
            return _set(data, now + TTL_S)
        except Exception as e:  # noqa: BLE001 — any refusal: the last copy, re-asked soon
            data = _read(path)
            if data is not None:
                return _set(data, now + RETRY_S)
            _failure = f"SEC ticker directory unavailable ({type(e).__name__}: {e})"
            _failed_until = now + RETRY_S
            raise Unavailable(_failure) from e


def by_ticker() -> Dict[str, Dict[str, Any]]:
    data = rows()
    idx = _idx.get("ticker")
    if idx is None or _idx.get("of") is not data:
        idx = {}
        for r in data:
            idx.setdefault(r["ticker"], r)
        _idx.update({"ticker": idx, "of": data})
    return idx


def by_cik() -> Dict[int, List[Dict[str, Any]]]:
    data = rows()
    idx = _idx.get("cik")
    if idx is None or _idx.get("of_cik") is not data:
        idx = {}
        for r in data:
            idx.setdefault(r["cik"], []).append(r)
        _idx.update({"cik": idx, "of_cik": data})
    return idx


def lookup(symbol: str) -> Optional[Dict[str, Any]]:
    """The directory row for a ticker; BRK.B and BRK-B both find BRK-B."""
    s = (symbol or "").strip().upper()
    t = by_ticker()
    return t.get(s.replace(".", "-")) or t.get(s)


def cik(symbol: str) -> Optional[str]:
    r = lookup(symbol)
    return str(r["cik"]) if r else None


if __name__ == "__main__":
    # `sec_directory.py cik_map` -> {"map": {"AAPL": 320193, ...}} or
    # {"error": ..., "transient": true}: the C++ side's ticker -> CIK lookup
    # (MarketDataService) reads the app's one copy through this.
    import sys
    if len(sys.argv) < 2 or sys.argv[1] != "cik_map":
        print(json.dumps({"error": "usage: sec_directory.py cik_map"}))
        sys.exit(2)
    try:
        print(json.dumps({"map": {t: r["cik"] for t, r in by_ticker().items()}}, separators=(",", ":")))
    except Unavailable as e:
        print(json.dumps({"error": str(e), "transient": True}))
        sys.exit(1)
