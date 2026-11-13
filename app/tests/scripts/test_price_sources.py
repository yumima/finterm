#!/usr/bin/env python3
"""Price sources other than Yahoo: each vendor's answer → the daemon's own
payload, so nothing downstream can tell the difference.

No network: every vendor response below is shaped from that vendor's API
reference, and `_get` is replaced with a function that returns it. The rules
under test are the app's, not the vendors':
  * a daily bar is stamped 00:00 America/New_York of its trading date;
  * adjusted means splits AND dividends (Polygon can only serve raw);
  * quotes keep the caller's order and Yahoo answers what the vendor can't;
  * any vendor failure means "ask Yahoo", never a partial series.
"""

import calendar
import os
import sys
from datetime import date, datetime

from zoneinfo import ZoneInfo

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import price_sources as ps  # noqa: E402

FAILURES = []


def check(cond, msg):
    if not cond:
        FAILURES.append(msg)


def with_env(**env):
    for k in ("FINTERM_PRICE_SOURCE", "TIINGO_API_KEY", "POLYGON_API_KEY", "ALPACA_API_KEY_ID",
              "ALPACA_API_SECRET_KEY", "FINNHUB_API_KEY", "FMP_API_KEY"):
        os.environ.pop(k, None)
    os.environ.update(env)


def fake(responses):
    calls = []

    def _get(url, params=None, headers=None):
        calls.append((url, params or {}, headers or {}))
        for key, resp in responses.items():
            if key in url:
                if isinstance(resp, Exception):
                    raise resp
                return resp
        raise ps.SourceUnavailable("unmocked " + url)

    ps._get = _get
    return calls


def ny_midnight(y, m, d):
    return ps.ny_midnight_epoch(date(y, m, d))


NY = ZoneInfo("America/New_York")
# Evening of Wed 2024-01-03: the latest session is the 3rd.
EVENING_JAN3 = datetime(2024, 1, 3, 20, 0, tzinfo=NY)
MIDDAY_JAN3 = datetime(2024, 1, 3, 12, 0, tzinfo=NY)


def test_configured_needs_source_and_key():
    with_env(FINTERM_PRICE_SOURCE="tiingo")
    check(ps.configured() is None, "tiingo without key must not be configured")
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    check(ps.configured() == "tiingo", "tiingo with key")
    with_env(FINTERM_PRICE_SOURCE="alpaca", ALPACA_API_KEY_ID="i")
    check(ps.configured() is None, "alpaca needs both id and secret")
    with_env(FINTERM_PRICE_SOURCE="bogus", TIINGO_API_KEY="k")
    check(ps.configured() is None, "unknown source")


def test_ny_midnight_stamp_across_dst():
    # 2024-01-02 is EST (UTC-5), 2024-07-01 is EDT (UTC-4).
    check(ny_midnight(2024, 1, 2) == calendar.timegm((2024, 1, 2, 5, 0, 0)), "EST midnight is 05:00 UTC")
    check(ny_midnight(2024, 7, 1) == calendar.timegm((2024, 7, 1, 4, 0, 0)), "EDT midnight is 04:00 UTC")


def test_tiingo_total_and_split_basis():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    # A 4:1 split on the 3rd: raw price drops from 400 to 100.
    rows = [{"date": "2024-01-02T00:00:00.000Z", "open": 400.0, "high": 404.0, "low": 396.0, "close": 400.0,
             "volume": 1000, "adjOpen": 99.0, "adjHigh": 100.0, "adjLow": 98.0, "adjClose": 99.5,
             "adjVolume": 4000, "divCash": 0.0, "splitFactor": 1.0},
            {"date": "2024-01-03T00:00:00.000Z", "open": 100.0, "high": 101.0, "low": 99.0, "close": 100.0,
             "volume": 4100, "adjOpen": 100.0, "adjHigh": 101.0, "adjLow": 99.0, "adjClose": 100.0,
             "adjVolume": 4100, "divCash": 0.0, "splitFactor": 4.0}]
    fake({"/tiingo/daily/aapl/prices": rows})
    tot = ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", True, now=EVENING_JAN3)
    spl = ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", False, now=EVENING_JAN3)
    check(tot[0]["close"] == 99.5 and tot[0]["volume"] == 4000, f"tiingo total basis: {tot}")
    # Split-only = Yahoo's "unadjusted": the pre-split day is divided by 4.
    check(spl[0]["close"] == 100.0 and spl[0]["high"] == 101.0 and spl[0]["volume"] == 4000,
          f"tiingo split basis divides earlier bars by later splits: {spl}")
    check(spl[1]["close"] == 100.0, f"the split day itself is unchanged: {spl}")
    check(tot[0]["timestamp"] == ny_midnight(2024, 1, 2), "tiingo bar stamped NY midnight")


def test_polygon_serves_split_basis_only():
    with_env(FINTERM_PRICE_SOURCE="polygon", POLYGON_API_KEY="k")
    calls = fake({"/v2/aggs/ticker/AAPL/range/1/day/": {"status": "OK", "results": [
        {"t": ny_midnight(2024, 1, 3) * 1000, "o": 1.0, "h": 2.0, "l": 0.5, "c": 1.5, "v": 10}]}})
    try:
        ps.daily_bars("AAPL", date(2024, 1, 3), date(2024, 1, 3), "1d", True, now=EVENING_JAN3)
        check(False, "polygon must refuse a total-return request (no dividend adjustment)")
    except ps.SourceUnavailable:
        pass
    bars = ps.daily_bars("AAPL", date(2024, 1, 3), date(2024, 1, 3), "1d", False, now=EVENING_JAN3)
    check(bars[0]["close"] == 1.5 and calls[-1][1]["adjusted"] == "true",
          f"polygon split basis uses adjusted=true: {calls[-1][1]}")


def test_share_class_symbols_per_vendor():
    check(ps.vendor_symbol("BRK-B", "polygon") == "BRK.B", "polygon wants BRK.B")
    check(ps.vendor_symbol("BRK-B", "tiingo") == "BRK-B", "tiingo keeps BRK-B")
    check(not ps.is_us_stock("VOD.L") and not ps.is_us_stock("^GSPC") and not ps.is_us_stock("ES=F")
          and not ps.is_us_stock("BTC-USD") and ps.is_us_stock("BRK-B"), "Yahoo-only notation stays on Yahoo")


def test_series_must_reach_latest_session():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    fake({"/tiingo/daily/aapl/prices": [{"date": "2024-01-02", "adjOpen": 1, "adjHigh": 2, "adjLow": 0.5,
                                          "adjClose": 1.5, "adjVolume": 1}]})
    try:  # midday on the 3rd: an end-of-day vendor has no bar for today yet
        ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", True, now=MIDDAY_JAN3)
        check(False, "a series ending yesterday during a session must go to Yahoo")
    except ps.SourceUnavailable:
        pass
    early = datetime(2024, 1, 3, 8, 0, tzinfo=NY)  # before the open, yesterday is the latest session
    bars = ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", True, now=early)
    check(len(bars) == 1, "before the open, a series through yesterday is complete")


def test_history_cap_falls_back():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    fake({"/tiingo/daily/aapl/prices": [{"date": "2024-01-03", "adjOpen": 1, "adjHigh": 2, "adjLow": 0.5,
                                          "adjClose": 1.5, "adjVolume": 1}]})
    try:
        ps.daily_bars("AAPL", date(2019, 1, 2), date(2024, 1, 3), "1d", True, now=EVENING_JAN3)
        check(False, "a series starting years after the asked start must go to Yahoo")
    except ps.SourceUnavailable:
        pass
    try:
        ps.period_to_range("max")
        check(False, "max stays on Yahoo")
    except ps.SourceUnavailable:
        pass


def test_alpaca_bars_need_consolidated_feed():
    with_env(FINTERM_PRICE_SOURCE="alpaca", ALPACA_API_KEY_ID="i", ALPACA_API_SECRET_KEY="s")
    try:  # free IEX feed: one venue's volume and high/low — not served as bars
        ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", True, now=EVENING_JAN3)
        check(False, "alpaca IEX bars must not be served")
    except ps.SourceUnavailable:
        pass
    with_env(FINTERM_PRICE_SOURCE="alpaca_sip", ALPACA_API_KEY_ID="i", ALPACA_API_SECRET_KEY="s")
    pages = [
        {"bars": [{"t": "2024-01-02T05:00:00Z", "o": 1, "h": 2, "l": 0.5, "c": 1.5, "v": 10}],
         "next_page_token": "p2"},
        {"bars": [{"t": "2024-01-03T05:00:00Z", "o": 1.5, "h": 2.5, "l": 1, "c": 2, "v": 20}],
         "next_page_token": None},
    ]
    calls = []

    def _get(url, params=None, headers=None):
        calls.append(params)
        return pages[len(calls) - 1]

    ps._get = _get
    bars = ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), "1d", True, now=EVENING_JAN3)
    check(len(bars) == 2 and calls[1].get("page_token") == "p2", f"alpaca pagination: {calls}")
    check(calls[0]["adjustment"] == "all" and calls[0]["feed"] == "sip", "alpaca total basis, sip feed")
    check(calls[0]["end"] == "2024-01-04", "alpaca end is exclusive: the day after")
    check(bars[1]["timestamp"] == ny_midnight(2024, 1, 3), "alpaca bar stamped NY midnight")


def test_fmp_bars_sorted_ascending():
    with_env(FINTERM_PRICE_SOURCE="fmp", FMP_API_KEY="k")
    rows = [{"symbol": "AAPL", "date": "2024-01-03", "adjOpen": 2, "adjHigh": 3, "adjLow": 1, "adjClose": 2.5,
             "volume": 5},
            {"symbol": "AAPL", "date": "2024-01-02", "adjOpen": 1, "adjHigh": 2, "adjLow": 0.5, "adjClose": 1.5,
             "volume": 4}]
    fake({"historical-price-eod/dividend-adjusted": rows})
    bars = ps.daily_bars("AAPL", date(2024, 1, 2), date(2024, 1, 3), now=EVENING_JAN3)
    check([b["close"] for b in bars] == [1.5, 2.5], f"fmp sorted oldest first: {bars}")


def test_weekly_resample():
    bars = [{"timestamp": ny_midnight(2024, 1, d), "open": d, "high": d + 1, "low": d - 1, "close": d + 0.5,
             "volume": 1} for d in (2, 3, 4, 5, 8)]
    wk = ps.resample(bars, "1wk")
    check(len(wk) == 2, f"two ISO weeks: {wk}")
    check(wk[0]["open"] == 2 and wk[0]["close"] == 5.5 and wk[0]["high"] == 6 and wk[0]["low"] == 1
          and wk[0]["volume"] == 4, f"week aggregate: {wk[0]}")
    check(wk[0]["timestamp"] == ny_midnight(2024, 1, 2), "week stamped at its first session")


def test_period_to_range():
    t = date(2026, 11, 13)
    check(ps.period_to_range("ytd", t) == (date(2026, 1, 1), t), "ytd")
    check(ps.period_to_range("range:2026-01-05:2026-02-01", t) == (date(2026, 1, 5), date(2026, 2, 1)), "range")
    try:
        ps.period_to_range("weird", t)
        check(False, "unknown period must fall back to Yahoo")
    except ps.SourceUnavailable:
        pass


def test_intraday_goes_to_yahoo():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    try:
        ps.daily_bars("AAPL", date(2024, 1, 1), date(2024, 1, 3), "5m", now=EVENING_JAN3)
        check(False, "intraday must not be served by a vendor")
    except ps.SourceUnavailable:
        pass


def test_quotes_skip_yahoo_notation_and_map_fields():
    with_env(FINTERM_PRICE_SOURCE="finnhub", FINNHUB_API_KEY="k")
    calls = fake({"finnhub.io/api/v1/quote": {"c": 110.0, "d": 10.0, "dp": 10.0, "h": 111, "l": 100,
                                              "o": 101, "pc": 100.0, "t": 1700000000}})
    q = ps.quotes(["AAPL", "^GSPC", "ES=F", "BTC-USD"], now=MIDDAY_JAN3)
    check(list(q) == ["AAPL"], f"only stock symbols go to the vendor: {list(q)}")
    check(len(calls) == 1, "one finnhub call per stock symbol")
    a = q["AAPL"]
    check(a["price"] == 110.0 and a["change"] == 10.0 and a["change_percent"] == 10.0
          and a["previous_close"] == 100.0 and a["source"] == "finnhub", f"quote fields: {a}")


def test_quotes_only_in_regular_hours():
    with_env(FINTERM_PRICE_SOURCE="finnhub", FINNHUB_API_KEY="k")
    fake({"finnhub.io/api/v1/quote": {"c": 110.0, "pc": 100.0, "t": 1700000000}})
    try:
        ps.quotes(["AAPL"], now=EVENING_JAN3)
        check(False, "after hours a vendor's last trade is an extended-hours print")
    except ps.SourceUnavailable:
        pass


def test_finnhub_keeps_partial_results():
    with_env(FINTERM_PRICE_SOURCE="finnhub", FINNHUB_API_KEY="k")
    answers = iter([{"c": 1.0, "pc": 1.0}, ps.SourceUnavailable("HTTP 403"), {"c": 3.0, "pc": 3.0},
                    ps.SourceUnavailable("HTTP 429")])

    def _get(url, params=None, headers=None):
        a = next(answers)
        if isinstance(a, Exception):
            raise a
        return a

    ps._get = _get
    q = ps.quotes(["A", "B", "C", "D", "E"], now=MIDDAY_JAN3)
    check(sorted(q) == ["A", "C"], f"one symbol's error skips it; a 429 stops but keeps the rest: {sorted(q)}")


def test_iex_quote_volume_left_out():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    fake({"api.tiingo.com/iex": [{"ticker": "aapl", "tngoLast": 110.0, "prevClose": 100.0, "volume": 123,
                                   "timestamp": "2024-01-03T12:00:00.123456789-05:00"}]})
    q = ps.quotes(["AAPL"], now=MIDDAY_JAN3)
    check(q["AAPL"]["volume"] is None and q["AAPL"]["price"] == 110.0, f"IEX volume is one venue's: {q}")


def test_rate_limit_is_unavailable():
    with_env(FINTERM_PRICE_SOURCE="tiingo", TIINGO_API_KEY="k")
    fake({"api.tiingo.com/iex": ps.SourceUnavailable("HTTP 429")})
    try:
        ps.quotes(["AAPL"], now=MIDDAY_JAN3)
        check(False, "a 429 must surface as SourceUnavailable (→ Yahoo)")
    except ps.SourceUnavailable:
        pass


def test_daemon_merges_vendor_and_yahoo_in_order():
    sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
    try:
        import yfinance_data as yd
    except Exception as e:  # yfinance not installed in this interpreter
        print(f"skip  daemon merge ({type(e).__name__})")
        return
    with_env(FINTERM_PRICE_SOURCE="finnhub", FINNHUB_API_KEY="k")
    fake({"finnhub.io/api/v1/quote": {"c": 110.0, "pc": 100.0, "t": 1700000000}})
    real_session_open = ps.regular_session_open
    ps.regular_session_open = lambda now=None: True  # the daemon calls with the real clock
    yd._yahoo_batch_quotes = lambda syms: [{"symbol": s, "price": 1.0} for s in syms]
    out = yd.get_batch_quotes(["^GSPC", "AAPL", "MSFT"])
    check([q["symbol"] for q in out] == ["^GSPC", "AAPL", "MSFT"], f"caller's order kept: {out}")
    check(out[0]["price"] == 1.0 and out[1].get("source") == "finnhub", f"index from Yahoo, stock from vendor: {out}")
    fake({"finnhub.io": ps.SourceUnavailable("HTTP 403")})
    out = yd.get_batch_quotes(["AAPL"])
    check(out == [{"symbol": "AAPL", "price": 1.0}], f"vendor failure falls back to Yahoo: {out}")
    ps.regular_session_open = real_session_open


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            before = len(FAILURES)
            fn()
            print(("ok  " if len(FAILURES) == before else "FAIL") + "  " + name)
    for f in FAILURES:
        print("  -", f)
    if FAILURES:
        sys.exit(1)
    print("\nall price source tests passed")
