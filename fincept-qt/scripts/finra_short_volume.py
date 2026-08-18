#!/usr/bin/env python3
"""
FINRA daily short-sale volume — the one institutional-flow series that is daily.

WHY THIS EXISTS
    13F is quarterly and cannot answer "what is big money doing this week" for
    anyone, including Bloomberg. The exchanges publish something that can: for
    every consolidated-tape symbol, every trading day, how much of that day's
    reported volume was sold short. It lands the next morning, it is free, and
    it needs no key.

WHAT IT IS AND IS NOT
    It is SHORT VOLUME, not short INTEREST. Short volume counts the sell side
    of trades executed as short during the day — much of it is market-maker
    inventory that is flat again by the close, so a 45% ratio does not mean 45%
    of the float is short. Short interest, the settled position, is reported
    twice a month and is a different number entirely.

    What the series IS good for is its own trend: a symbol's short-volume ratio
    against its own recent range says whether selling pressure is building. It
    is compared to itself, never across symbols, and never presented as a
    position.

ACTIONS
    series {"symbol", "days"?}   daily short/total volume and the ratio
"""

import io
import json
import sys
import time
import zipfile
from datetime import date, timedelta

try:
    import requests
except ImportError:
    requests = None

UA = {"User-Agent": "FinceptTerminal research@hanlexon.com",
      "Accept-Encoding": "gzip, deflate"}

# Consolidated NMS file — every tape, one row per symbol per day.
DAILY = "https://cdn.finra.org/equity/regsho/daily/CNMSshvol{}.txt"

_LAST = 0.0
_GAP = 0.15


def _get(url, timeout=20):
    global _LAST
    if requests is None:
        return None
    d = time.time() - _LAST
    if d < _GAP:
        time.sleep(_GAP - d)
    try:
        r = requests.get(url, timeout=timeout, headers=UA)
        _LAST = time.time()
        if r.status_code == 404:
            return None          # not a trading day, or not published yet
        r.raise_for_status()
        return r
    except Exception:
        _LAST = time.time()
        return None


def series(symbol, days=90):
    """Daily short-volume rows for one symbol, oldest first.

    One file per trading day, so a 90-day window is ~62 requests. Weekends and
    holidays simply 404 and are skipped rather than guessed at — a missing day
    is a day the market was shut, not a zero.
    """
    if requests is None:
        return {"error": "requests not available", "symbol": symbol}
    sym = str(symbol).upper().strip()
    if not sym:
        return {"error": "symbol required"}

    out = []
    misses = 0
    d = date.today()
    scanned = 0
    while len(out) < int(days) and scanned < int(days) * 2 + 10:
        scanned += 1
        if d.weekday() < 5:      # skip weekends without asking FINRA
            r = _get(DAILY.format(d.strftime("%Y%m%d")))
            if r is None:
                misses += 1
            else:
                for line in r.text.splitlines():
                    parts = line.split("|")
                    if len(parts) < 5 or parts[1] != sym:
                        continue
                    try:
                        short = float(parts[2])
                        total = float(parts[4])
                    except ValueError:
                        break
                    if total > 0:
                        out.append({"date": d.isoformat(), "short": short,
                                    "total": total, "ratio": short / total})
                    break
        d -= timedelta(days=1)

    out.reverse()
    if not out:
        return {"symbol": sym, "error": "no FINRA short-volume rows for this symbol",
                "days_missing": misses}

    ratios = [r["ratio"] for r in out]
    latest = out[-1]

    def avg(n):
        w = ratios[-n:] if len(ratios) >= n else ratios
        return sum(w) / len(w)

    # Compared against its OWN range. A 45% ratio is unremarkable for one
    # symbol and extreme for another, so a cross-symbol threshold would be
    # meaningless.
    lo, hi = min(ratios), max(ratios)
    span = hi - lo
    return {
        "symbol": sym,
        "as_of": latest["date"],
        "rows": out,
        "latest_ratio": latest["ratio"],
        "avg_5": avg(5), "avg_20": avg(20), "avg_60": avg(60),
        "min_ratio": lo, "max_ratio": hi,
        # Where today sits in the window, 0..1. The honest summary: not "high",
        # but "high for this symbol lately".
        "percentile": ((latest["ratio"] - lo) / span) if span > 1e-9 else 0.5,
        "days": len(out), "days_missing": misses,
        "note": "short VOLUME, not short interest — much of it is market-maker "
                "inventory that is flat by the close; read the trend, not the level",
    }


def handle_action(action, payload):
    if action == "series":
        return series(payload.get("symbol") or "", int(payload.get("days") or 90))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "series"
    pl = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, pl), indent=2, default=str))
