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
import urllib.error
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
        # What the C++ side receives: the daemon's reply frame.
        try:
            res = yd._daemon_dispatch("historical_period", {"symbol": sym, "period": "1mo", "interval": "1d"})
        except Exception as e:  # noqa: BLE001
            return "failed", str(e)
        frame = yd._reply_for(7, res)
        return ("ok", frame["result"]) if frame["ok"] else ("failed", frame)

    how, r = dispatch((429, TOO_MANY), "USDJPY=X")
    check(how == "failed" and isinstance(r, dict) and r["result"].get("transient") is True,
          f"429 (rate limited) -> failed reply carrying transient, not an empty series [{how}: {r!s:.120}]")

    how, r = dispatch((401, UNAUTH), "GBPUSD=X")
    check(how == "failed", f"401 -> failed reply, not an empty series [{how}: {r!s:.80}]")

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

    # A refused SEARCH (Yahoo 429 on the search endpoint, exact-symbol
    # fallback empty) is a failed reply — not "no matches" for AAPL.
    import urllib.request
    real_urlopen = urllib.request.urlopen
    def refuse(*a, **k):
        raise urllib.error.HTTPError("https://query2.finance.yahoo.com", 429, "Too Many Requests", {}, None)
    urllib.request.urlopen = refuse
    data._session = Session((429, TOO_MANY))
    try:
        frame = yd._reply_for(3, yd.search_symbols("AAPL"))
        check(frame["ok"] is False and frame["result"].get("transient") is True,
              f"search refused -> failed reply, not 'no results' [{str(frame)[:120]}]")
    finally:
        urllib.request.urlopen = real_urlopen

    # A rate-limited QUOTE download: yf.download returns an empty frame and
    # says why only in shared._ERRORS — a failed reply, not "no quotes".
    data._session = Session((429, TOO_MANY))
    yd._cache.clear()
    q = yd.get_batch_quotes(["AAA", "BBB"])
    frame = yd._reply_for(4, q)
    check(frame["ok"] is False and isinstance(q, dict) and q.get("transient") is True,
          f"batch_quotes 429 -> failed reply, not [] [{str(q)[:120]}]")

    # What counts as "Yahoo refused" in yfinance's per-ticker error text —
    # a 401 inside a timestamp or a ticker is not one.
    check(yd._is_refusal_text("YFRateLimitError('Too Many Requests')"), "rate limit text is a refusal")
    check(yd._is_refusal_text("HTTP Error 503: x"), "HTTP 503 is a refusal")
    check(not yd._is_refusal_text("possibly delisted; no price data found 1704012000"),
          "a delisted ticker (digits incl. 401) is not a refusal")
    check(not yd._is_refusal_text("4012.T: KeyError"), "a ticker containing 401 is not a refusal")

    # An action that RAISED: the failed reply carries a body saying whether
    # Yahoo refused (retry) or it was a bug (don't) — not body-less, which
    # the C++ side reads as "no answer, try again".
    yd._trace_reset()   # as the daemon does per request (_handle)
    r = yd._exception_reply(9, KeyError("x"))
    check(r["ok"] is False and r["result"]["transient"] is False, "a bug -> failed reply, not transient")
    r = yd._exception_reply(9, RuntimeError("YFRateLimitError: Too Many Requests"))
    check(r["result"]["transient"] is True, "a raised rate limit -> transient")

    # The frame's "refused" flag (the worker's background back-off reads it):
    # refused rows / sections count; plain "no data" doesn't.
    check(yd._reply_for(1, [{"symbol": "A", "error": "x", "transient": True}])["refused"] is True,
          "a refused row -> frame refused")
    check(yd._reply_for(1, {"quotes": [], "quotes_error": "x", "quotes_transient": True})["refused"] is True,
          "a refused section -> frame refused")
    check(yd._reply_for(1, {"error": "No data"})["refused"] is False, "'No data' -> not refused")
    check(yd._reply_for(1, [])["refused"] is False, "an empty answer -> not refused")

    # Every action, not just history: a top-level error is a failed reply;
    # "<section>_error" partials and plain data stay ok.
    check(yd._reply_for(1, {"error": "x", "transient": True})["ok"] is False, "any {error} -> ok:false")
    check(yd._reply_for(1, {"error": "x"})["result"] == {"error": "x"}, "the failed reply still carries the body")
    check(yd._reply_for(1, {"symbol": "A", "holders_error": "x"})["ok"] is True, "partial <section>_error stays ok")
    check(yd._reply_for(1, [])["ok"] is True and yd._reply_for(1, {"error": None, "v": 1})["ok"] is True,
          "empty list / null error stay ok")

    if failed:
        print(f"\n{len(failed)} FAILED: " + ", ".join(failed))
        return 1
    print("\nA refused chart request is never an empty chart.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
