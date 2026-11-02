#!/usr/bin/env python3
"""A refused chart request is not an empty chart.

Clicking a row on the Markets screen showed "No data available for this
period" for USDJPY=X, GBPUSD=X and ^FTSE — symbols Yahoo has years of bars
for. Yahoo was answering 429 (rate limited). yfinance raises YFRateLimitError
for that; get_historical_period caught it and RETURNED {"error": ...}; the
daemon sent that as ok:true; and every C++ caller read the missing "_value"
as an empty series. A 401 on the timezone probe goes the other way round —
yfinance logs it and returns an empty frame — and read as "no bars" too.

historical_period must fail as a REPLY (the daemon raises → ok:false) when
Yahoo refused, and still return [] for a genuine empty answer. Offline:
canned responses through yfinance's real request path.
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.normpath(os.path.join(HERE, "..", "..", "scripts"))

failed = []


def check(cond, label):
    print(("PASS : " if cond else "FAIL : ") + label)
    if not cond:
        failed.append(label)


def main():
    sys.path.insert(0, SCRIPTS)
    try:
        import pandas  # noqa: F401
        import curl_cffi.requests.exceptions as cx
        import yfinance_data as yd
        from yfinance.data import YfData
    except Exception as exc:  # noqa: BLE001
        print(f"SKIP : yfinance_data not importable here ({exc})")
        return 77

    class Resp:
        def __init__(self, status, url, payload):
            self.status_code, self.url, self._payload = status, url, payload
            self.headers, self.text = {}, json.dumps(payload)

        def raise_for_status(self):
            if self.status_code >= 400:
                raise cx.HTTPError(f"HTTP Error {self.status_code}", response=self)

        def json(self):
            return self._payload

    now = int(time.time()) // 86400 * 86400 - 86400 * 10
    ts = [now + 86400 * i for i in range(5)]
    META = {"currency": "JPY", "symbol": "X", "exchangeName": "CCY", "instrumentType": "CURRENCY",
            "exchangeTimezoneName": "Europe/London", "timezone": "GMT", "gmtoffset": 0,
            "regularMarketPrice": 150.0, "dataGranularity": "1d", "range": "1mo",
            "validRanges": ["1d", "5d", "1mo", "3mo", "6mo", "1y", "max"]}
    BARS = {"chart": {"result": [{"meta": META, "timestamp": ts, "indicators": {
        "quote": [{"open": [150.0] * 5, "high": [151.0] * 5, "low": [149.0] * 5,
                   "close": [150.5] * 5, "volume": [0] * 5}],
        "adjclose": [{"adjclose": [150.5] * 5}]}}], "error": None}}
    NO_BARS = {"chart": {"result": [{"meta": META, "indicators": {"quote": [{}]}}], "error": None}}
    UNAUTH = {"chart": {"result": None, "error": {"code": "Unauthorized", "description": "Invalid Crumb"}}}
    TOO_MANY = {"error": "Too Many Requests"}

    class Session:
        def __init__(self, route):
            self.route = route

            class Cookies:
                jar = []
            self.cookies = Cookies()

        def get(self, url, params=None, timeout=30, **_):
            status, payload = self.route
            return Resp(status, url, payload)

    YfData._get_cookie_and_crumb = lambda self, timeout=30: ("crumb", "basic")
    YfData._set_cookie_strategy = lambda self, strategy, have_lock=False: None
    data = YfData()

    def dispatch(route, sym):
        data._session = Session(route)
        yd._cache.clear()
        try:
            return "ok", yd._daemon_dispatch("historical_period",
                                             {"symbol": sym, "period": "1mo", "interval": "1d"})
        except Exception as e:  # noqa: BLE001
            return "raised", str(e)

    how, r = dispatch((429, TOO_MANY), "USDJPY=X")
    check(how == "raised", f"429 (rate limited) -> failed reply, not an empty series [{how}: {r!s:.80}]")

    how, r = dispatch((401, UNAUTH), "GBPUSD=X")
    check(how == "raised", f"401 -> failed reply, not an empty series [{how}: {r!s:.80}]")

    how, r = dispatch((200, BARS), "EURUSD=X")
    check(how == "ok" and isinstance(r, list) and len(r) == 5, f"bars answered -> the bars [{how}: {str(r)[:80]}]")

    how, r = dispatch((200, NO_BARS), "ZZZ")
    check(how == "ok" and r == [], f"an answer with no bars -> [] (a real 'no data') [{how}: {str(r)[:80]}]")

    # A refusal is not cached for the TTL: the next click asks again.
    dispatch((429, TOO_MANY), "AUDUSD=X")
    data._session = Session((200, BARS))
    r = yd._daemon_dispatch("historical_period", {"symbol": "AUDUSD=X", "period": "1mo", "interval": "1d"})
    check(isinstance(r, list) and len(r) == 5, "after a 429, the same request recovers once Yahoo answers")

    # The hub's batch_all histories: a refusal carries "error" (skipped by
    # MarketDataService::refresh), not points: [] published as "no data".
    data._session = Session((429, TOO_MANY))
    yd._cache.clear()
    out = yd.get_batch_all({"histories": [{"symbol": "NZDUSD=X", "period": "1mo", "interval": "1d"}]})
    h = (out.get("histories") or [{}])[0]
    check(bool(h.get("error")), f"batch_all history refused -> entry carries error [{h}]")

    if failed:
        print(f"\n{len(failed)} FAILED: " + ", ".join(failed))
        return 1
    print("\nA refused chart request is never an empty chart.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
