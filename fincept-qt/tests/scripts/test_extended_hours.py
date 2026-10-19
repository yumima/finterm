#!/usr/bin/env python3
"""Extended-hours (AFT%) correctness in scripts/yfinance_data.py.

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

def _freeze(hour, minute=0):
    frozen_now = datetime(*TRADING_DAY, hour, minute, tzinfo=ET)

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
    """bars: [(datetime in ET, close)] → a frame shaped like yfinance's."""
    idx = pd.DatetimeIndex([pd.Timestamp(t).tz_convert("UTC") for t, _ in bars])
    return pd.DataFrame({"Close": [c for _, c in bars]}, index=idx)


def _multi(frames):
    """{ticker: frame} → the group_by='ticker' MultiIndex frame download returns."""
    return pd.concat(frames.values(), axis=1, keys=frames.keys(),
                     names=["Ticker", "Price"])


def _stub_downloads(minute_frames, daily_frames=None, fail=False, retry_frame=None):
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


def _quote(symbols, minute_frames, daily_frames=None, hour=18, retry_frame=None):
    """One get_extended_hours_quotes call at a fixed ET hour."""
    _, unfreeze = _freeze(hour)
    restore = _stub_downloads(minute_frames, daily_frames, retry_frame=retry_frame)
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
    """A failed fetch must not look like a successful empty answer."""
    _, unfreeze = _freeze(18)
    restore = _stub_downloads(None, fail=True)
    try:
        raised = None
        try:
            yd.get_extended_hours_quotes(["MSFT"])
        except Exception as exc:  # noqa: BLE001 — the point is that it raises
            raised = exc
        check("download failure propagates", raised is not None,
              "got a normal return instead of an exception")
    finally:
        restore()
        unfreeze()


def test_flat_frame_is_not_shared_across_symbols():
    """One ticker's frame must never answer for a whole book."""
    flat = _minute_frame([(_et(15, 59), 100.0), (_et(18, 0), 93.0)])  # -7%
    rows = _quote(["MSFT", "AAPL", "NVDA"], flat)
    pcts = [r["ext_change_pct"] for r in rows]
    check("flat frame is not fanned out to every symbol",
          all(p is None for p in pcts), f"got {pcts}")


def test_official_close_is_the_denominator():
    """The 16:00 daily close, not the last 1-minute bar, is the reference."""
    minute = _multi({"MSFT": _minute_frame([
        (_et(15, 59), 499.58),   # last regular 1m bar
        (_et(19, 0), 495.87),    # post-market print
    ])})
    daily = _multi({"MSFT": _minute_frame([(_et(16, 0), 499.99)])})  # official
    row = _quote(["MSFT"], minute, daily)[0]
    check("regular is the official close", row["regular"] == 499.99,
          f"got {row['regular']}")
    expected = (495.87 - 499.99) / 499.99 * 100.0
    check("pct is measured against the official close",
          row["ext_change_pct"] is not None
          and abs(row["ext_change_pct"] - expected) < 1e-9,
          f"got {row['ext_change_pct']} want {expected}")


def test_stale_reference_yields_no_move():
    """No regular-hours trade today ⇒ no after-hours move, not a multi-day one."""
    minute = _multi({"FNILX": _minute_frame([
        (_et(16, 0, day_offset=-6), 30.0),   # last trade: nearly a week ago
        (_et(18, 0), 27.9),                  # a fresh extended print
    ])})
    row = _quote(["FNILX"], minute)[0]
    check("a days-old reference reports no move",
          row["ext_change_pct"] is None and row["ext_price"] is None,
          f"got pct={row['ext_change_pct']} ext={row['ext_price']}")
    # The prints have to go too. ExtendedHoursMath.h::quote_from_row rebuilds
    # the percentage from post_market/regular and never reads ext_price, so
    # leaving them in the row let the dashboard and the heatmap compute the
    # exact number this guard withheld — here, (27.9-30.0)/30.0 = -7%.
    check("the unpairable prints are withheld with it",
          row["post_market"] is None and row["pre_market"] is None
          and row["post_market_ts"] is None and row["pre_market_ts"] is None,
          f"got pre={row['pre_market']} post={row['post_market']}")


def test_post_market_move_is_reported():
    """The guards must not suppress the number the column exists for."""
    minute = _multi({"MSFT": _minute_frame([
        (_et(16, 0), 500.0),
        (_et(19, 0), 465.0),    # -7% after hours
    ])})
    row = _quote(["MSFT"], minute)[0]
    check("a real post-market move survives",
          row["ext_change_pct"] is not None
          and abs(row["ext_change_pct"] + 7.0) < 1e-9,
          f"got {row['ext_change_pct']}")
    check("post-market print is carried", row["post_market"] == 465.0,
          f"got {row['post_market']}")


def test_pre_market_is_not_paired_with_todays_close():
    """After the bell, no post-market print means no number — never pre."""
    minute = _multi({"MSFT": _minute_frame([
        (_et(8, 0), 460.0),     # this morning's pre-market, vs YESTERDAY's close
        (_et(16, 0), 500.0),    # today's close
    ])})
    row = _quote(["MSFT"], minute, hour=18)[0]
    check("pre-market is not measured against today's close",
          row["ext_change_pct"] is None and row["ext_price"] is None,
          f"got pct={row['ext_change_pct']} ext={row['ext_price']}")
    check("the pre-market print is still reported as such",
          row["pre_market"] == 460.0, f"got {row['pre_market']}")


def test_regular_session_shows_this_mornings_pre_market():
    """Mid-session the last extended move is today's pre-market, not yesterday's post."""
    minute = _multi({"MSFT": _minute_frame([
        (_et(16, 0, day_offset=-1), 500.0),   # yesterday's close  = the reference
        (_et(18, 0, day_offset=-1), 490.0),   # yesterday's post-market
        (_et(8, 0), 505.0),                   # today's pre-market
    ])})
    row = _quote(["MSFT"], minute, hour=12)[0]
    check("regular session prefers today's pre-market",
          row["ext_price"] == 505.0, f"got {row['ext_price']}")
    check("measured against yesterday's close", row["regular"] == 500.0,
          f"got {row['regular']}")
    check("and the sign follows the market",
          row["ext_change_pct"] is not None and abs(row["ext_change_pct"] - 1.0) < 1e-9,
          f"got {row['ext_change_pct']}")


def test_pre_session_falls_back_to_the_prior_post_market():
    """Before the open both prints reference the same close, so the fallback is sound."""
    minute = _multi({"MSFT": _minute_frame([
        (_et(16, 0, day_offset=-1), 500.0),   # yesterday's close
        (_et(18, 0, day_offset=-1), 490.0),   # yesterday's post-market
    ])})
    row = _quote(["MSFT"], minute, hour=8)[0]
    check("pre session falls back to the prior post-market",
          row["ext_price"] == 490.0, f"got {row['ext_price']}")
    check("against the same close it was struck from",
          row["ext_change_pct"] is not None and abs(row["ext_change_pct"] + 2.0) < 1e-9,
          f"got {row['ext_change_pct']}")


# ── Index after-hours (futures-implied) ──────────────────────────────────────
# A cash index stops at the bell, so its own "post-market" print is the close
# restamped: +0.00% forever. These pin the replacement — the future's move since
# the CASH close, with the close instant read off the index's own bars.

def _bars(points):
    """Close series of 1m bars from [(tz-aware start, price), ...]."""
    idx = pd.DatetimeIndex([t for t, _ in points]).tz_convert("UTC")
    return pd.Series([v for _, v in points], index=idx, dtype=float)


def _session_bars(day_offset, open_px, close_px, close_h=16, close_m=0):
    """An index session: one bar at the open, one in the last minute."""
    return [(_et(9, 30, day_offset=day_offset), open_px),
            (_et(close_h, close_m, day_offset=day_offset) - timedelta(minutes=1), close_px)]


def _iah(index_pts, proxy_pts, now):
    return yd._index_after_hours_row(_bars(index_pts), _bars(proxy_pts),
                                     "America/New_York", (9, 30), (16, 0), now)


def test_index_after_hours_is_the_future_since_the_cash_close():
    """After the bell: close → latest futures print, against the future AT the close."""
    index = _session_bars(0, 7600.0, 7650.0) + [
        (_et(16, 40), 7650.0),   # Yahoo restamping the close after the bell
    ]
    proxy = [(_et(15, 59), 7700.0), (_et(18, 0), 7777.0)]
    r = _iah(index, proxy, _et(18, 5))
    check("index after-hours is measured", r is not None, "got None")
    check("against the future at the bell, not the cash level (basis cancels)",
          r and abs(r["pct"] - 1.0) < 1e-9, f"got {r and r['pct']}")
    check("restamped closing bars don't move the reference",
          r and r["ref_ts"] == int(_et(16, 0).timestamp()), f"got {r and r['ref_ts']}")
    check("same evening reads POST", r and r["session"] == "POST", f"got {r and r['session']}")
    check("and the cash market reads closed", r and not r["cash_open"], "")


def test_index_after_hours_freezes_at_the_open():
    """During the session the number is the move INTO today's open, not a live one."""
    index = _session_bars(-1, 7600.0, 7650.0) + [(_et(9, 30), 7700.0), (_et(11, 0), 7720.0)]
    proxy = [(_et(15, 59, day_offset=-1), 7700.0), (_et(9, 29), 7623.0), (_et(11, 0), 9999.0)]
    r = _iah(index, proxy, _et(11, 1))
    check("open-session move is measured", r is not None, "got None")
    check("previous close → open, ignoring prints after the open",
          r and abs(r["pct"] + 1.0) < 1e-9, f"got {r and r['pct']}")
    check("labelled PRE", r and r["session"] == "PRE" and r["cash_open"], f"got {r}")


def test_index_after_hours_half_day_closes_at_its_last_bar():
    """A 13:00 close is read off the bars — no calendar, no 16:00 assumption."""
    index = _session_bars(0, 7600.0, 7650.0, close_h=13)
    proxy = [(_et(12, 59), 7700.0), (_et(14, 0), 7700.0 * 1.005)]
    r = yd._index_after_hours_row(_bars(index), _bars(proxy), "America/New_York", (9, 30), (16, 0),
                                  _et(14, 30), None, (13, 0))
    check("half-day is closed by 14:30", r and not r["cash_open"], f"got {r}")
    check("reference is the 13:00 close",
          r and r["ref_ts"] == int(_et(13, 0).timestamp()), f"got {r and r['ref_ts']}")
    check("and the move runs from there", r and abs(r["pct"] - 0.5) < 1e-9, f"got {r and r['pct']}")


def test_index_after_hours_needs_the_future_trading_at_the_close():
    """A futures bar from hours before the bell would fold in the session's own move."""
    index = _session_bars(0, 7600.0, 7650.0)
    proxy = [(_et(12, 0), 7700.0), (_et(18, 0), 7777.0)]
    check("no reference print near the close → no number",
          _iah(index, proxy, _et(18, 5)) is None, "")


def test_index_after_hours_with_nothing_after_the_close():
    """Seconds after the bell there is no after-hours print yet — not a 0.00%."""
    index = _session_bars(0, 7600.0, 7650.0)
    proxy = [(_et(15, 59), 7700.0)]
    check("no print after the close → no number",
          _iah(index, proxy, _et(16, 0) + timedelta(seconds=30)) is None, "")


def test_index_after_hours_lunch_break_is_not_the_close():
    """Tokyo's lunch: no bars for an hour, but the session is not over."""
    jst = ZoneInfo("Asia/Tokyo")
    day = datetime(2026, 8, 13, tzinfo=jst)
    at = lambda h, m, d=0: day.replace(hour=h, minute=m) + timedelta(days=d)
    index = [(at(9, 0, -1), 100.0), (at(15, 29, -1), 101.0),
             (at(9, 0), 102.0), (at(11, 29), 103.0)]
    proxy = [(at(15, 29, -1), 200.0), (at(8, 59), 202.0), (at(12, 0), 210.0)]
    r = yd._index_after_hours_row(_bars(index), _bars(proxy), "Asia/Tokyo", (9, 0), (15, 30),
                                  at(12, 10), ((11, 30), (12, 30)))
    check("lunch break reads as open", r and r["cash_open"], f"got {r}")
    check("and the number is the move into the open, not since lunch",
          r and abs(r["pct"] - 1.0) < 1e-9, f"got {r and r['pct']}")


def test_index_after_hours_stalled_feed_is_not_a_close():
    """Index bars stop at 10:30 on a normal day: a feed stall, not the bell."""
    index = _session_bars(-1, 7600.0, 7650.0) + [(_et(9, 30), 7700.0), (_et(10, 30), 7710.0)]
    proxy = [(_et(15, 59, day_offset=-1), 7700.0), (_et(9, 29), 7623.0), (_et(11, 0), 7800.0)]
    r = yd._index_after_hours_row(_bars(index), _bars(proxy), "America/New_York", (9, 30), (16, 0),
                                  _et(11, 0), None, (13, 0))
    check("a stalled index feed mid-session gives no number", r is None, f"got {r}")


def test_index_after_hours_before_the_first_bar_of_the_day():
    """09:35 with Yahoo's first index bar not in yet: open by the clock, cut at 09:30."""
    index = _session_bars(-1, 7600.0, 7650.0)
    proxy = [(_et(15, 59, day_offset=-1), 7700.0), (_et(9, 29), 7623.0), (_et(9, 33), 9999.0)]
    r = _iah(index, proxy, _et(9, 35))
    check("open by the clock before the first bar", r and r["cash_open"], f"got {r}")
    check("and nothing after the open leaks in", r and abs(r["pct"] + 1.0) < 1e-9, f"got {r and r['pct']}")


def test_index_after_hours_holiday_is_closed():
    """A weekday with no index bars an hour after the open was not a session."""
    index = _session_bars(-1, 7600.0, 7650.0)
    proxy = [(_et(15, 59, day_offset=-1), 7700.0), (_et(10, 29), 7777.0)]
    r = _iah(index, proxy, _et(10, 30))
    check("holiday reads as closed, measured from the last real close",
          r and not r["cash_open"] and abs(r["pct"] - 1.0) < 1e-9, f"got {r}")


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
