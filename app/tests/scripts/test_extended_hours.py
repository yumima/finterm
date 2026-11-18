#!/usr/bin/env python3
"""Extended-hours (EXT%) correctness in scripts/yfinance_data.py.

Every case here comes from a number that was on screen and wrong:

  - yfinance.download() is not thread-safe. It parks each ticker's frame in
    module-global dicts and, at the end, rebuilds the result with
    `pd.concat(_DFS.values(), keys=_DFS.keys())` — two reads of a dict that a
    concurrent call has already cleared and refilled. Overlapping downloads
    therefore either raise "dictionary changed size during iteration" (seen in
    production) or, worse, return silently mislabelled frames: one stock's bars
    under another stock's ticker, which is how a foreign after-hours move ends
    up on a row that says MSFT. Verified here by racing the real wrapper.
  - The download failing returned [] — indistinguishable from "nothing to
    report", so the C++ side treated a dead fetch as a successful empty answer
    and kept showing the last good numbers with no indication.
  - A flat (single-level) frame was handed to EVERY requested symbol, so one
    ticker's prices could be reported for the whole book.
  - After the closing bell, a missing post-market print fell back to THIS
    MORNING's pre-market one and measured it against TODAY's close. Pre-market
    belongs to the previous close, so that reports the day's move backwards.
  - The denominator was the last 1-minute regular bar, which stops at 15:59 and
    excludes the closing auction — a systematic ~0.05-0.1% error against
    after-hours moves that are routinely 0.2%.
  - A symbol that didn't trade during regular hours (money-market and mutual
    funds) left the reference bar on an earlier day, turning a multi-day return
    into an "after-hours move".

Self-contained: no network, no pytest. Every download is stubbed, and the
session-dependent cases run against a frozen clock — the pairing rules differ
per session, so a suite that used the wall clock would only ever exercise
whichever branch happened to be live when it ran.
"""

import os
import sys
import threading
import time
import traceback
import types
import datetime as _datetime_mod
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas as pd
except ImportError:
    # 77 = "the app venv (with pandas/yfinance) isn't present" — reported as
    # skipped rather than passed, so a missing dependency can never mask a
    # regression.
    print("SKIP : pandas not available")
    sys.exit(77)

for _name in ("yfinance", "numpy", "requests", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

# The stub above is enough to import yfinance_data, but the serialisation tests
# reach into yfinance.multi / yfinance.base for real. Skip rather than fail:
# a hard failure on a machine that simply lacks the dependency is noise that
# trains people to ignore the suite.
try:
    from yfinance import multi as _yf_multi  # noqa: F401
    from yfinance import base as _yf_base    # noqa: F401
    HAVE_YFINANCE = True
except ImportError:
    HAVE_YFINANCE = False

import yfinance_data as yd  # noqa: E402
import exchange_sessions as xs  # noqa: E402

ET = ZoneInfo("America/New_York")
# A plain Thursday: not a holiday, not adjacent to one.
TRADING_DAY = (2026, 8, 13)
FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# ── frozen clock ─────────────────────────────────────────────────────────────
#
# get_extended_hours_quotes imports datetime inside the function body, so the
# name is resolved from sys.modules on every call and a shim module reaches it.
# exchange_sessions bound the class at ITS import time, so it needs patching
# separately — miss that and previous_session_end_utc keeps answering from the
# real clock while everything else is frozen.

def _freeze(hour, minute=0, day=None):
    frozen_now = datetime(*(day or TRADING_DAY), hour, minute, tzinfo=ET)

    class FrozenDatetime(_datetime_mod.datetime):
        @classmethod
        def now(cls, tz=None):
            return frozen_now.astimezone(tz) if tz else frozen_now.replace(tzinfo=None)

    shim = types.ModuleType("datetime")
    for attr in dir(_datetime_mod):
        if not attr.startswith("__"):
            setattr(shim, attr, getattr(_datetime_mod, attr))
    shim.datetime = FrozenDatetime

    real_module = sys.modules["datetime"]
    real_xs_datetime = xs.datetime
    sys.modules["datetime"] = shim
    xs.datetime = FrozenDatetime

    def restore():
        sys.modules["datetime"] = real_module
        xs.datetime = real_xs_datetime

    return frozen_now, restore


def _et(hour, minute=0, day_offset=0):
    return datetime(*TRADING_DAY, hour, minute, tzinfo=ET) + timedelta(days=day_offset)


# ── frame builders ───────────────────────────────────────────────────────────

def _minute_frame(bars):
    """bars: [(datetime in ET, close)] → a frame shaped like yfinance's.

    As in Yahoo's own series, a day's regular minutes end at 15:59: the bar
    stamped 16:00 is the first after-hours minute (extended_rules), so a
    fixture's "close" without a daily frame is a 15:59 bar."""
    idx = pd.DatetimeIndex([pd.Timestamp(t).tz_convert("UTC") for t, _ in bars])
    return pd.DataFrame({"Close": [c for _, c in bars]}, index=idx)


def _multi(frames):
    """{ticker: frame} → the group_by='ticker' MultiIndex frame download returns."""
    return pd.concat(frames.values(), axis=1, keys=frames.keys(),
                     names=["Ticker", "Price"])


def _stub_downloads(minute_frames, daily_frames=None, fail=False, retry_frame=None, retry_error=None):
    """Point yfinance at canned frames; returns a restore callable.

    Ticker() is stubbed alongside download() because symbols the bulk frame
    doesn't cover fall through to a per-symbol re-ask — which would otherwise
    reach the network and make these tests depend on the live market.
    """
    yf = sys.modules["yfinance"]
    original = getattr(yf, "download", None)
    original_ticker = getattr(yf, "Ticker", None)

    def fake(symbols, **kw):
        if fail:
            raise RuntimeError("simulated Yahoo failure")
        table = daily_frames if kw.get("interval") == "1d" else minute_frames
        if table is None:
            return pd.DataFrame()
        return table

    class FakeTicker:
        def __init__(self, sym):
            self.sym = sym

        def history(self, **kw):
            if retry_error is not None:
                raise retry_error
            return retry_frame if retry_frame is not None else pd.DataFrame()

    yf.download = fake
    yf.Ticker = FakeTicker

    def restore():
        for name, value in (("download", original), ("Ticker", original_ticker)):
            if value is None:
                yf.__dict__.pop(name, None)
            else:
                setattr(yf, name, value)
    return restore


def _quote(symbols, minute_frames, daily_frames=None, hour=18, retry_frame=None, retry_error=None):
    """One get_extended_hours_quotes call at a fixed ET hour."""
    _, unfreeze = _freeze(hour)
    restore = _stub_downloads(minute_frames, daily_frames, retry_frame=retry_frame,
                              retry_error=retry_error)
    try:
        return yd.get_extended_hours_quotes(symbols)
    finally:
        restore()
        unfreeze()


# ── tests ────────────────────────────────────────────────────────────────────

def test_clock_freeze_reaches_the_code_under_test():
    """If the freeze slips, every session assertion below is meaningless."""
    _, unfreeze = _freeze(18)
    try:
        frozen_now = sys.modules["datetime"].datetime.now(ET)
        check("the datetime shim reaches a fresh `from datetime import datetime`",
              (frozen_now.year, frozen_now.month, frozen_now.day,
               frozen_now.hour) == (*TRADING_DAY, 18),
              f"got {frozen_now.isoformat()}")
        check("session label follows the frozen clock",
              yd._ext_session_label(frozen_now) == "POST",
              f"got {yd._ext_session_label(frozen_now)}")
        end = xs.previous_session_end_utc("MSFT").astimezone(ET)
        check("session end follows the frozen clock",
              (end.year, end.month, end.day, end.hour) == (*TRADING_DAY, 16),
              f"got {end.isoformat()}")
    finally:
        unfreeze()


def test_downloads_are_serialised():
    """Concurrent yfinance.download calls must never overlap."""
    if not HAVE_YFINANCE:
        print("SKIP : test_downloads_are_serialised (yfinance not installed)")
        return
    yf = sys.modules["yfinance"]
    original = getattr(yf, "download", None)
    from yfinance import multi as yf_multi
    original_multi = getattr(yf_multi, "download", None)

    overlaps = []
    live = []
    lock = threading.Lock()

    def slow(*a, **kw):
        with lock:
            live.append(1)
            if len(live) > 1:
                overlaps.append(len(live))
        time.sleep(0.02)
        with lock:
            live.pop()
        return "ok"

    yf_multi.download = slow
    try:
        yd._install_download_serializer()
        threads = [threading.Thread(target=lambda: yf_multi.download(["X"]))
                   for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        check("concurrent downloads never overlap", not overlaps,
              f"saw {overlaps} concurrent callers inside download()")
        check("wrapper marks itself installed",
              getattr(yf_multi.download, "_finterm_serialized", False))
    finally:
        if original_multi is None:
            yf_multi.__dict__.pop("download", None)
        else:
            yf_multi.download = original_multi
        if original is None:
            yf.__dict__.pop("download", None)
        else:
            yf.download = original


def test_history_yields_to_a_download_but_workers_do_not():
    """The gate must exclude standalone history() calls — and only those.

    Ticker.history() writes shared._DFS on its failure paths, which is what a
    concurrent download is iterating. But download fetches BY calling
    Ticker.history() on its own worker threads, so a naive lock deadlocks: this
    pins both halves.
    """
    if not HAVE_YFINANCE:
        print("SKIP : test_history_yields_to_a_download_but_workers_do_not "
              "(yfinance not installed)")
        return
    import yfinance as yf
    from yfinance import base as yf_base, multi as yf_multi

    saved = (yf_base.TickerBase.history, yf_multi._download_one,
             yf_multi.download, getattr(yf, "download", None))
    events = []
    ev_lock = threading.Lock()

    def record(tag):
        with ev_lock:
            events.append(tag)

    class RawTicker:
        def __init__(self, sym):
            self.sym = sym

        def history(self, *a, **kw):
            record("history")
            time.sleep(0.05)
            return "hist"

    def raw_download_one(ticker, *a, **kw):
        # Exactly what yfinance does: fetch by calling the public history().
        return yf_base.TickerBase.history(RawTicker(ticker))

    def raw_download(tickers, *a, **kw):
        record("download-start")
        threads = [threading.Thread(target=yf_multi._download_one, args=(t,))
                   for t in tickers]
        for t in threads:
            t.start()
        for t in threads:
            t.join(10)
        stuck = [t.is_alive() for t in threads]
        record("download-end")
        return stuck

    try:
        yf_base.TickerBase.history = RawTicker.history
        yf_multi._download_one = raw_download_one
        yf_multi.download = raw_download
        yd._install_download_serializer()

        stuck = []
        dl = threading.Thread(target=lambda: stuck.extend(yf_multi.download(["A", "B"])))
        dl.start()
        time.sleep(0.02)   # let the download take the gate
        outsider = threading.Thread(target=lambda: yf_base.TickerBase.history(RawTicker("Z")))
        outsider.start()
        dl.join(20)
        outsider.join(20)

        check("download's own workers are not blocked by the gate",
              not dl.is_alive() and not any(stuck),
              "the download deadlocked on its own history() calls")
        check("an outside history() waits for the download to finish",
              not outsider.is_alive()
              and events.index("download-end") < len(events) - 1,
              f"event order was {events}")
    finally:
        (yf_base.TickerBase.history, yf_multi._download_one,
         yf_multi.download, top) = saved
        if top is None:
            yf.__dict__.pop("download", None)
        else:
            yf.download = top


def test_serializer_is_idempotent():
    """Re-installing must not stack wrappers (the crumb-reset path re-runs it)."""
    if not HAVE_YFINANCE:
        print("SKIP : test_serializer_is_idempotent (yfinance not installed)")
        return
    import yfinance as yf
    from yfinance import multi as yf_multi
    original = getattr(yf_multi, "download", None)
    # _install_download_serializer rebinds the top-level name too, so restoring
    # only yfinance.multi.download leaves this test's throwaway lambda wrapped
    # and reachable as yfinance.download for everything that runs after it.
    original_top = getattr(yf, "download", None)
    try:
        yf_multi.download = lambda *a, **k: "raw"
        yd._install_download_serializer()
        first = yf_multi.download
        yd._install_download_serializer()
        check("second install is a no-op", yf_multi.download is first)
    finally:
        for module, value in ((yf_multi, original), (yf, original_top)):
            if value is None:
                module.__dict__.pop("download", None)
            else:
                module.download = value


def test_download_failure_raises():
    """A failed bar fetch must not look like a successful empty answer."""
    _, unfreeze = _freeze(18)
    restore = _stub_downloads(None, fail=True)
    try:
        raised = None
        try:
            yd.get_extended_hours_quotes(["ES=F"])
        except Exception as exc:  # noqa: BLE001 — the point is that it raises
            raised = exc
        check("download failure propagates", raised is not None,
              "got a normal return instead of an exception")
    finally:
        restore()
        unfreeze()


def test_missing_rules_module_raises():
    """A broken install (the shared rules unloadable) is not an empty answer."""
    saved = sys.modules.get("extended_rules")
    sys.modules["extended_rules"] = None   # makes `import extended_rules` raise
    try:
        raised = None
        try:
            yd.get_extended_hours_quotes(["MSFT"])
        except ImportError as exc:
            raised = exc
        check("an unloadable extended_rules raises, never returns []", raised is not None)
    finally:
        if saved is None:
            sys.modules.pop("extended_rules", None)
        else:
            sys.modules["extended_rules"] = saved


def test_flat_frame_is_not_shared_across_symbols():
    """One ticker's frame must never answer for a whole book."""
    flat = _minute_frame([(_et(15, 59), 100.0), (_et(18, 0), 93.0)])  # -7%
    rows = _quote(["ES=F", "NQ=F", "CL=F"], flat)
    check("flat frame is not fanned out to every symbol",
          all(r["post"] is None for r in rows), f"got {[r['post'] for r in rows]}")


def test_stale_reference_yields_no_move():
    """No regular trade in the last session ⇒ no move, not a multi-day one."""
    minute = _multi({"ES=F": _minute_frame([
        (_et(15, 59, day_offset=-6), 30.0),   # last bar: nearly a week ago
        (_et(18, 0), 27.9),                  # a fresh extended print
    ])})
    row = _quote(["ES=F"], minute)[0]
    check("a days-old reference reports no session",
          row["post"] is None and row["pre"] is None, f"got {row}")
    check("a stale reference is flagged declined", row.get("declined") is True,
          f"got {row.get('declined')}")


class _RateLimited(Exception):
    """Stands in for YFRateLimitError: any refusal that isn't 'no data'."""


def test_refusal_is_unavailable_not_declined():
    """A 429 says nothing about the prints: ask again, and let a live print in."""
    minute = _multi({"ES=F": _minute_frame([(_et(15, 59), 500.0), (_et(19, 0), 505.0)])})
    rows = {r["symbol"]: r for r in _quote(["ES=F", "NQ=F"], minute, retry_error=_RateLimited("429"))}
    check("a symbol the source refused is unavailable", rows["NQ=F"].get("unavailable") is True,
          f"got {rows['NQ=F']}")
    check("a refusal is not a decline", rows["NQ=F"].get("declined") is not True,
          f"got {rows['NQ=F'].get('declined')}")
    check("an answered symbol is neither", not rows["ES=F"].get("unavailable")
          and rows["ES=F"].get("declined") is False, f"got {rows['ES=F']}")


def test_no_bars_is_an_answer():
    """No intraday bars (empty frame, no exception) is answered."""
    minute = _multi({"ES=F": _minute_frame([(_et(15, 59), 500.0), (_et(19, 0), 505.0)])})
    rows = {r["symbol"]: r for r in _quote(["ES=F", "CL=F"], minute)}
    check("no price data is not 'unavailable'", not rows["CL=F"].get("unavailable"),
          f"got {rows['CL=F']}")


def test_empty_download_is_unavailable():
    """Nothing at all back (a rate-limited burst) marks every row unavailable."""
    rows = _quote(["ES=F", "GC=F"], pd.DataFrame())
    check("every row of an empty download is unavailable",
          all(r.get("unavailable") is True for r in rows), f"got {rows}")


# ── US stocks: Yahoo's batch quote ───────────────────────────────────────────
#
# Each stub is the v7 /finance/quote row shape (formatted=false), with the
# marketState Yahoo reports at that hour. The pairing under test: after-hours
# of day D against D's close, pre-market of D against the close before D,
# regularMarketPrice a close only once its bell has rung.

def _t(hour, minute=0, day_offset=0, day=None):
    base = datetime(*(day or TRADING_DAY), hour, minute, tzinfo=ET)
    return int((base + timedelta(days=day_offset)).timestamp())


def _v7(rows=None, fail=None):
    """Point the batch quote at canned rows (or a refusal); returns a restore
    callable. yf.download is made to blow up meanwhile: a US-only request
    must never touch the bar path (or its process-wide lock)."""
    real_v7 = yd._v7_quotes
    yf = sys.modules["yfinance"]
    real_download = getattr(yf, "download", None)
    asked = []

    def fake(symbols, fields, chunk=50):
        asked.append(list(symbols))
        if fail is not None:
            raise fail
        return [r for r in (rows or []) if r.get("symbol") in symbols]

    def no_download(*a, **kw):
        raise AssertionError("a US-only request reached yf.download")

    yd._v7_quotes = fake
    yf.download = no_download

    def restore():
        yd._v7_quotes = real_v7
        if real_download is None:
            yf.__dict__.pop("download", None)
        else:
            yf.download = real_download
    return restore, asked


def _us(symbols, rows, hour, minute=0, day=None, fail=None):
    _, unfreeze = _freeze(hour, minute, day=day)
    restore, asked = _v7(rows, fail)
    try:
        return {r["symbol"]: r for r in yd.get_extended_hours_quotes(symbols)}, asked
    finally:
        restore()
        unfreeze()


def _close_ts(day_offset=0, day=None, bell=(16, 0)):
    return _t(*bell, day_offset=day_offset, day=day) - 1


def test_v7_post_session_pairs_each_print_with_its_own_close():
    """18:00, marketState POST: after-hours against today's close, this
    morning's pre-market against yesterday's — both kept, never crossed."""
    q = {"symbol": "MSFT", "marketState": "POST",
         "regularMarketPrice": 499.99, "regularMarketTime": _t(16, 0),
         "regularMarketPreviousClose": 495.0,
         "postMarketPrice": 495.87, "postMarketTime": _t(17, 59),
         "preMarketPrice": 497.0, "preMarketTime": _t(9, 0)}
    rows, _ = _us(["MSFT"], [q], 18)
    r = rows["MSFT"]
    check("POST: the last close is today's official one", r["regular"] == 499.99
          and r["regular_ts"] == _close_ts(), f"got {r}")
    p = r["post"]
    check("POST: after-hours against today's close",
          p and p["price"] == 495.87 and p["ref"] == 499.99 and p["ref_date"] == "2026-08-13", f"got {p}")
    pre = r["pre"]
    check("POST: this morning's pre-market against YESTERDAY's close, not today's",
          pre and pre["ref"] == 495.0 and pre["ref_date"] == "2026-08-12", f"got {pre}")
    check("POST: not declined", r["declined"] is False and r["unavailable"] is False, f"got {r}")


def test_v7_pre_session():
    """08:00, marketState PRE: regularMarketPrice is yesterday's close."""
    q = {"symbol": "MSFT", "marketState": "PRE",
         "regularMarketPrice": 500.0, "regularMarketTime": _t(16, 0, day_offset=-1),
         "regularMarketPreviousClose": 490.0,
         "postMarketPrice": 505.0, "postMarketTime": _t(19, 0, day_offset=-1),
         "preMarketPrice": 510.0, "preMarketTime": _t(7, 59)}
    r = _us(["MSFT"], [q], 8)[0]["MSFT"]
    check("PRE: last close is yesterday's", r["regular"] == 500.0 and r["regular_ts"] == _close_ts(-1), f"got {r}")
    check("PRE: pre-market against yesterday's close",
          r["pre"] and r["pre"]["ref"] == 500.0 and r["pre"]["price"] == 510.0, f"got {r['pre']}")
    check("PRE: yesterday's after-hours against the same close",
          r["post"] and r["post"]["ref"] == 500.0 and r["post"]["day"] == "2026-08-12", f"got {r['post']}")


def test_v7_regular_session_never_takes_the_live_price_as_a_close():
    """12:00, marketState REGULAR: regularMarketPrice is the live price."""
    q = {"symbol": "MSFT", "marketState": "REGULAR",
         "regularMarketPrice": 520.0, "regularMarketTime": _t(11, 59),
         "regularMarketPreviousClose": 500.0,
         "preMarketPrice": 505.0, "preMarketTime": _t(9, 0)}
    r = _us(["MSFT"], [q], 12)[0]["MSFT"]
    check("REGULAR: the last close is the previous close, not the live price",
          r["regular"] == 500.0 and r["regular_ts"] == _close_ts(-1), f"got {r}")
    check("REGULAR: this morning's pre-market against it",
          r["pre"] and r["pre"]["ref"] == 500.0, f"got {r['pre']}")
    check("REGULAR: no after-hours", r["post"] is None, f"got {r['post']}")


def test_v7_prepre_after_20():
    """22:00, marketState PREPRE: today's after-hours is still the session."""
    q = {"symbol": "NVDA", "marketState": "PREPRE",
         "regularMarketPrice": 155.46, "regularMarketTime": _t(16, 0),
         "regularMarketPreviousClose": 156.14,
         "postMarketPrice": 162.94, "postMarketTime": _t(19, 59)}
    r = _us(["NVDA"], [q], 22)[0]["NVDA"]
    p = r["post"]
    check("PREPRE: after-hours against today's close",
          p and abs((p["price"] - p["ref"]) / p["ref"] * 100 - 4.81) < 0.01, f"got {p}")
    check("PREPRE: session label CLOSED", r["session"] == "CLOSED", f"got {r['session']}")


def test_v7_weekend():
    """Saturday, marketState CLOSED: Friday's after-hours against Friday's close."""
    q = {"symbol": "AAPL", "marketState": "CLOSED",
         "regularMarketPrice": 230.0, "regularMarketTime": _t(16, 0, day_offset=1),
         "regularMarketPreviousClose": 228.0,
         "postMarketPrice": 231.0, "postMarketTime": _t(19, 50, day_offset=1)}
    r = _us(["AAPL"], [q], 11, day=(2026, 8, 15))[0]["AAPL"]
    check("weekend: Friday's after-hours is current", r["post"] and r["post"]["ref"] == 230.0
          and r["declined"] is False, f"got {r}")


def test_v7_early_close():
    """The day after Thanksgiving closes at 13:00; after-hours runs 13:00-17:00."""
    day = (2026, 11, 27)
    q = {"symbol": "AAPL", "marketState": "POST",
         "regularMarketPrice": 240.0, "regularMarketTime": _t(13, 0, day=day),
         "regularMarketPreviousClose": 238.0,
         "postMarketPrice": 241.0, "postMarketTime": _t(13, 30, day=day)}
    r = _us(["AAPL"], [q], 14, day=day)[0]["AAPL"]
    check("early close: 13:00 price is the close by 14:00",
          r["regular"] == 240.0 and r["regular_ts"] == _close_ts(day=day, bell=(13, 0)), f"got {r}")
    check("early close: 13:30 print is after-hours of that close",
          r["post"] and r["post"]["ref"] == 240.0, f"got {r['post']}")
    # Previous session before Black Friday is Wednesday (Thursday is the holiday).
    check("early close: the previous close is Wednesday's",
          _xr_quote_closes(q, (2026, 11, 27, 14)).get(datetime(2026, 11, 25).date(), (None,))[0] == 238.0,
          "")


def _xr_quote_closes(q, at):
    import extended_rules as xr
    return xr.quote_closes(q, datetime(*at, tzinfo=ET))


def test_v7_missing_fields_are_absent_not_zero():
    """A field Yahoo leaves out is absent — never a 0 that reads as a price."""
    base = {"symbol": "MSFT", "regularMarketPrice": 499.99, "regularMarketTime": _t(16, 0)}
    rows, _ = _us(["MSFT"], [dict(base, postMarketPrice=495.0)], 18)
    check("no postMarketTime → no after-hours", rows["MSFT"]["post"] is None, f"got {rows['MSFT']}")
    rows, _ = _us(["MSFT"], [dict(base, preMarketPrice=497.0, preMarketTime=_t(9, 0))], 18)
    check("no previous close → pre-market cannot be paired", rows["MSFT"]["pre"] is None,
          f"got {rows['MSFT']['pre']}")
    rows, _ = _us(["MSFT"], [dict(base, postMarketPrice=0, postMarketTime=_t(17, 0))], 18)
    check("a zero price is not a print", rows["MSFT"]["post"] is None, f"got {rows['MSFT']['post']}")
    rows, _ = _us(["MSFT"], [{"symbol": "MSFT", "regularMarketPrice": 499.99,
                              "postMarketPrice": 495.0, "postMarketTime": _t(17, 0)}], 18)
    r = rows["MSFT"]
    check("no regularMarketTime → no close, no session, not declined",
          r["regular"] is None and r["post"] is None and r["declined"] is False, f"got {r}")


def test_v7_print_at_or_before_its_close_is_not_after_hours():
    q = {"symbol": "MSFT", "regularMarketPrice": 499.99, "regularMarketTime": _t(16, 0),
         "regularMarketPreviousClose": 495.0, "postMarketPrice": 499.0, "postMarketTime": _t(15, 59)}
    r = _us(["MSFT"], [q], 18)[0]["MSFT"]
    check("a print before the bell is not after-hours", r["post"] is None, f"got {r['post']}")


def test_v7_stale_close_is_declined():
    """A fund with no trade in the last session: its close is days old."""
    q = {"symbol": "FNILX", "regularMarketPrice": 30.0, "regularMarketTime": _t(16, 0, day_offset=-3),
         "regularMarketPreviousClose": 29.0, "postMarketPrice": 27.9, "postMarketTime": _t(17, 0)}
    r = _us(["FNILX"], [q], 18)[0]["FNILX"]
    check("stale close → declined, no sessions",
          r["declined"] is True and r["post"] is None and r["pre"] is None, f"got {r}")


def test_v7_refusal_is_unavailable():
    rows, _ = _us(["MSFT", "AAPL"], [], 18, fail=RuntimeError("429 Too Many Requests"))
    check("refused batch quote → every row unavailable, none declined",
          all(r["unavailable"] is True and r["declined"] is False for r in rows.values()), f"got {rows}")


def test_v7_symbol_left_out_is_asked_again():
    """A row missing from the batch (dropped, or an unknown ticker) is not an
    answer: unavailable, so the service asks again on its back-off."""
    q = {"symbol": "MSFT", "regularMarketPrice": 499.99, "regularMarketTime": _t(16, 0)}
    rows, _ = _us(["MSFT", "ZZZZQ"], [q], 18)
    r = rows["ZZZZQ"]
    check("a symbol left out of the batch is unavailable, not declined",
          r["unavailable"] is True and r["declined"] is False and r["post"] is None, f"got {r}")
    check("the others are answered", rows["MSFT"]["unavailable"] is False, f"got {rows['MSFT']}")


def test_v7_waits_for_the_official_close():
    """16:00:20: regularMarketPrice is still the last trade before the closing
    auction. After-hours is not paired with it, and nothing is declined."""
    import extended_rules as xr
    q = {"symbol": "MSFT", "marketState": "POST",
         "regularMarketPrice": 499.58, "regularMarketTime": _t(15, 59) + 58,
         "regularMarketPreviousClose": 495.0,
         "postMarketPrice": 498.0, "postMarketTime": _t(16, 0) + 15}
    r = xr.quote_sessions(q, datetime(*TRADING_DAY, 16, 0, 20, tzinfo=ET))
    check("before the auction print: no after-hours pairing", r["post"] is None, f"got {r}")
    check("before the auction print: the last close is still yesterday's, not declined",
          r["regular"] == 495.0 and r["declined"] is False, f"got {r}")
    # The auction print lands, stamped at the bell.
    q2 = dict(q, regularMarketPrice=499.99, regularMarketTime=_t(16, 0))
    r2 = xr.quote_sessions(q2, datetime(*TRADING_DAY, 16, 0, 40, tzinfo=ET))
    check("auction print in: after-hours against the official close",
          r2["post"] and r2["post"]["ref"] == 499.99, f"got {r2}")
    # A thin name with no auction print: its last trade is the close after the settle time.
    r3 = xr.quote_sessions(q, datetime(*TRADING_DAY, 16, 11, tzinfo=ET))
    check("no auction print: the last trade is the close after 10 minutes",
          r3["regular"] == 499.58 and r3["post"] and r3["post"]["ref"] == 499.58, f"got {r3}")


def test_v7_one_stale_name_does_not_close_the_market():
    """A single-name request (the ER tab) whose quote lags is judged by the
    calendar, not taken as evidence that today was closed."""
    q = {"symbol": "THIN", "regularMarketPrice": 10.0, "regularMarketTime": _t(15, 30, day_offset=-1),
         "regularMarketPreviousClose": 9.9}
    rows, _ = _us(["THIN"], [q], 10, 30)
    check("one lagging name: today is still a session", rows["THIN"]["session"] == "REGULAR",
          f"got {rows['THIN']['session']}")


def test_v7_share_class_is_asked_in_yahoo_form():
    q = {"symbol": "BRK-B", "regularMarketPrice": 586.76, "regularMarketTime": _t(16, 0),
         "regularMarketPreviousClose": 592.85, "postMarketPrice": 584.09, "postMarketTime": _t(19, 0)}
    rows, asked = _us(["BRK.B"], [q], 20, 30)
    check("BRK.B asked as BRK-B, answered under BRK.B",
          asked == [["BRK-B"]] and rows["BRK.B"]["post"] and rows["BRK.B"]["post"]["price"] == 584.09,
          f"asked={asked} got={rows}")


def test_v7_one_request_for_the_book():
    qs = [{"symbol": s, "regularMarketPrice": 100.0, "regularMarketTime": _t(16, 0)} for s in ("A", "B", "C")]
    _, asked = _us(["A", "B", "C"], qs, 18)
    check("one batch quote for the whole book", len(asked) == 1 and sorted(asked[0]) == ["A", "B", "C"],
          f"asked={asked}")


# ── Index futures ────────────────────────────────────────────────────────────
# A cash index stops at the bell, so its own "post-market" print is the close
# restamped: +0.00% for ever. The FUT% column is the index future's quote
# instead: last price against the previous settle, read from one contract.

def _day(offset):
    return (_et(0, 0, day_offset=offset)).date()


def _q(price, prev, t):
    """A batch-quote row for a contract: last price, previous close, time of the price."""
    return {"symbol": "ESZ26.CME", "regularMarketPrice": price, "regularMarketPreviousClose": prev,
            "regularMarketTime": int(t.timestamp())}


def test_index_future_is_measured_from_the_previous_close():
    """Mid-session: last price against the contract's previous close, one snapshot."""
    r, why = yd._index_future_row(_q(7777.0, 7700.0, _et(11, 0)), _et(11, 5))
    check("future measured", r is not None, why)
    check("against the previous close", r and abs(r["pct"] - 1.0) < 1e-9, f"got {r}")
    check("price is the last print", r and r["price"] == 7777.0 and r["change"] == 77.0, f"got {r}")
    check("session is today's", r and r["session_date"] == _day(0).isoformat(), f"got {r}")


def test_index_future_evening_is_not_measured_against_itself():
    """19:00 ET, the reopened session. Yahoo stamps it with the date it OPENED
    (today), so the old "last daily bar before the print's trading day" lookup
    picked the in-progress bar and paired the live price with itself: +0.00%
    on every index from 18:00 ET to midnight. The quote's own previous close
    is today's settle."""
    r, why = yd._index_future_row(_q(7636.5, 7615.25, _et(19, 0)), _et(19, 5))
    check("evening measured from today's close", r and abs(r["change"] - 21.25) < 1e-9, f"got {r} {why}")
    check("evening session belongs to the next trading day",
          r and r["session_date"] == _day(1).isoformat(), f"got {r}")


def test_index_future_reopen_waits_for_the_new_sessions_print():
    """18:03 ET: Yahoo's ~10-minute delay still shows the 16:55 print while the
    previous close may already be that session's own settle — ~0.00%. No number
    until the reopened session prints."""
    r, why = yd._index_future_row(_q(7640.5, 7640.5, _et(16, 55)), _et(18, 3))
    check("lagging price across the reopen → no number",
          r is None and "reopened session" in (why or ""), f"got {r} {why}")
    r, why = yd._index_future_row(_q(7641.0, 7640.5, _et(18, 1)), _et(18, 12))
    check("first print of the new session → measured", r is not None and r["change"] == 0.5, f"got {r} {why}")


def test_index_future_dead_feed_is_a_dash_not_old_news():
    """Globex open on a weekday, last print hours ago: not today's quote."""
    r, why = yd._index_future_row(_q(7777.0, 7700.0, _et(15, 0, day_offset=-1)), _et(11, 0))
    check("stale contract → no number", r is None and "no prints since" in (why or ""), f"got {r} {why}")


def test_index_future_missing_contract_says_so():
    r, why = yd._index_future_row(None, _et(11, 0))
    check("missing quote is reported as missing", r is None and "no quote" in why, why)
    r, why = yd._index_future_row(_q(7777.0, None, _et(11, 0)), _et(11, 5))
    check("no previous close → no number, never a 0.00%", r is None and "previous close" in why, why)


def test_index_future_contract_is_pinned_across_the_roll():
    """One contract for both prints; the next quarter from eight days before expiry."""
    from datetime import date
    check("mid-quarter → front", yd._index_future_contract("ES=F", date(2026, 10, 19)) == "ESZ26.CME",
          yd._index_future_contract("ES=F", date(2026, 10, 19)))
    # Dec 2026 third Friday = 18th; roll from the 10th.
    check("day before the roll → front",
          yd._index_future_contract("ES=F", date(2026, 12, 9)) == "ESZ26.CME", "")
    check("roll day → next quarter, across the year",
          yd._index_future_contract("ES=F", date(2026, 12, 10)) == "ESH27.CME",
          yd._index_future_contract("ES=F", date(2026, 12, 10)))
    check("Dow trades on CBOT", yd._index_future_contract("YM=F", date(2026, 10, 19)) == "YMZ26.CBT", "")
    check("unknown roots pass through", yd._index_future_contract("CL=F", date(2026, 10, 19)) == "CL=F", "")


def test_quote_streamer_fast_lane():
    """Cash indices never tick on the push feed, so they ride the batch-quote
    fast lane — with the snapshot's own previous close, never a price paired
    with some other source's close. ROOT=F aliases ride their contract's
    push ticks instead (polling is what Yahoo throttles per IP)."""
    QS = yd._QuoteStreamer
    check("index → fast lane", QS.fast_lane("^GSPC"), "")
    check("continuous future → NOT polled", not QS.fast_lane("GC=F") and QS.is_alias("GC=F"), "")
    check("dated contract → push feed (it ticks)", not QS.fast_lane("ESZ26.CME"), "")
    s = QS(lambda f: None)
    s._alias = {"GC=F": "GCZ26.CMX"}
    check("push set: stocks as-is, aliases as their contract, indices not at all",
          s._stream_set({"AAPL", "GC=F", "CL=F", "^GSPC"}) == {"AAPL", "GCZ26.CMX"},
          s._stream_set({"AAPL", "GC=F", "CL=F", "^GSPC"}))
    t = QS.alias_tick("GC=F", {"id": "GCZ26.CMX", "price": 3922.8, "change": 37.5, "time": "1", "market_hours": 1,
                               "exchange": "CMX"})
    check("alias tick: relabelled, Yahoo's change kept as the pair, marked",
          t["id"] == "GC=F" and t["change"] == 37.5 and t["source"] == "alias" and "exchange" not in t, t)
    check("stock / FX / coin → push feed",
          not any(QS.fast_lane(x) for x in ("AAPL", "EURUSD=X", "BTC-USD")), "")

    row = {"symbol": "GC=F", "regularMarketPrice": 3916.9, "regularMarketPreviousClose": 3885.3,
           "regularMarketTime": 1794537746, "regularMarketVolume": 120345, "marketState": "REGULAR"}
    t = QS.fast_tick(row)
    check("snapshot tick carries its own previous close",
          t and t["price"] == 3916.9 and t["prev_close"] == 3885.3 and t["source"] == "quote", t)
    check("time in ms as a string, like the push feed's int64s", t and t["time"] == "1794537746000", t)
    check("regular-session tick", t and t["market_hours"] == 1, t)
    check("volume carried", t and t["day_volume"] == "120345", t)
    for bad, why in ((dict(row, regularMarketPreviousClose=None), "no previous close"),
                     (dict(row, regularMarketPreviousClose=0), "zero previous close"),
                     (dict(row, regularMarketPrice=None), "no price"),
                     (dict(row, symbol=None), "no symbol")):
        check(f"no tick without a sound pair: {why}", QS.fast_tick(bad) is None, QS.fast_tick(bad))


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all extended-hours tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
