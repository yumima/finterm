#!/usr/bin/env python3
"""The two extended-hours views must agree about the same session.

yfinance_data.get_extended_hours_quotes (the AFT% column) and
extended_sessions.fetch (the ER Overview's Extended Hours box) present Yahoo's
1-minute prepost bars differently — one row against the last completed close,
versus each session against its own close — but both take the facts from
extended_rules. Here the SAME stubbed frames go through both real entry points
at a frozen clock, and the after-hours price, the pre-market price and the
close each is measured against must come out identical.

Before the rules were shared they did not: the AFT row counted the 16:00 bar
as the close when the daily frame lacked the symbol (its close is an
after-hours print) and never as after-hours, the ER box the other way round;
on an early close the AFT row kept the 16:00 bell; the ER box had no
minute-bar fallback at all.

Self-contained: no network, no pytest.
"""

import os
import sys
import tempfile
import types
import datetime as _datetime_mod
from datetime import date, datetime, timedelta
from zoneinfo import ZoneInfo

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pandas as pd
except ImportError:
    print("SKIP : pandas not available")
    sys.exit(77)

for _name in ("yfinance", "numpy", "requests", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

os.environ["FINTERM_DATA_DIR"] = tempfile.mkdtemp(prefix="extcons")

import yfinance_data as yd  # noqa: E402
import extended_sessions as es  # noqa: E402
import extended_rules as xr  # noqa: E402
import exchange_sessions as xs  # noqa: E402

ET = ZoneInfo("America/New_York")
SYM = "MSFT"
FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# ── frames ───────────────────────────────────────────────────────────────────

def minutes(day, hm, n, p0, step):
    h, m = map(int, hm.split(":"))
    t0 = datetime(day.year, day.month, day.day, h, m, tzinfo=ET)
    return [(t0 + timedelta(minutes=i), p0 + step * i) for i in range(n)]


def minute_frame(bars, before):
    """yfinance-shaped 1m frame of the bars strictly before `before` (ET)."""
    bars = [(t, p) for t, p in bars if t < before]
    idx = pd.DatetimeIndex([pd.Timestamp(t).tz_convert("UTC") for t, _ in bars])
    return pd.DataFrame({"Open": [p for _, p in bars], "High": [p + 0.01 for _, p in bars],
                         "Low": [p - 0.01 for _, p in bars], "Close": [p for _, p in bars],
                         "Volume": [0] * len(bars)}, index=idx)


def daily_frame(closes):
    """{date: close} → a daily frame, labels tz-naive dates as yfinance gives them."""
    if not closes:
        return pd.DataFrame()
    days = sorted(closes)
    return pd.DataFrame({"Close": [closes[d] for d in days], "Volume": [1e6] * len(days)},
                        index=pd.DatetimeIndex([pd.Timestamp(d) for d in days]))


def multi(frame):
    return pd.concat([frame], axis=1, keys=[SYM], names=["Ticker", "Price"])


# ── both entry points, one clock, one set of frames ──────────────────────────

def run_both(bars, closes, now):
    """(AFT row, ER answer) for the frames as they stood at `now`."""
    one_min = minute_frame(bars, now)
    daily = daily_frame({d: c for d, c in closes.items() if d <= now.date()})

    class FrozenDatetime(_datetime_mod.datetime):
        @classmethod
        def now(cls, tz=None):
            return now.astimezone(tz) if tz else now.replace(tzinfo=None)

    shim = types.ModuleType("datetime")
    for attr in dir(_datetime_mod):
        if not attr.startswith("__"):
            setattr(shim, attr, getattr(_datetime_mod, attr))
    shim.datetime = FrozenDatetime

    yf = sys.modules["yfinance"]
    saved = {k: getattr(yf, k, None) for k in ("download", "Ticker")}

    def download(symbols, **kw):
        frame = daily if kw.get("interval") == "1d" else one_min
        return multi(frame) if not frame.empty else pd.DataFrame()

    class Ticker:
        def __init__(self, sym):
            self.sym = sym

        def history(self, **kw):
            return daily if kw.get("interval") == "1d" else one_min

    yf.download, yf.Ticker = download, Ticker
    real_dt, real_es_dt, real_xs_dt, real_nasdaq = sys.modules["datetime"], es.datetime, xs.datetime, es._nasdaq
    sys.modules["datetime"] = shim
    es.datetime = xs.datetime = FrozenDatetime
    es._nasdaq = lambda *a, **k: None   # the volume is not under test
    es._daily_cache.clear()
    try:
        aft = yd.get_extended_hours_quotes([SYM])[0]
        er = es.fetch(SYM)
    finally:
        sys.modules["datetime"], es.datetime, es._nasdaq = real_dt, real_es_dt, real_nasdaq
        xs.datetime = real_xs_dt
        for k, v in saved.items():
            if v is None:
                yf.__dict__.pop(k, None)
            else:
                setattr(yf, k, v)
    return aft, er


def ts_date(ts):
    return datetime.fromtimestamp(ts, ET).date().isoformat() if ts is not None else None


def same(a, b):
    return a is not None and b is not None and abs(a - b) < 1e-9


def agree_after_hours(label, aft, er):
    a = er.get("after_hours")
    check(f"{label}: both see an after-hours session", a is not None and aft["post_market"] is not None,
          f"aft={aft['post_market']} er={a}")
    if a is None:
        return
    check(f"{label}: same after-hours price", same(aft["post_market"], a["last"]),
          f"aft={aft['post_market']} er={a['last']}")
    check(f"{label}: same after-hours instant", aft["post_market_ts"] == a["last_ts"],
          f"aft={aft['post_market_ts']} er={a['last_ts']}")
    check(f"{label}: after-hours against the same close", same(aft["regular"], a["ref_close"]),
          f"aft={aft['regular']} er={a['ref_close']}")
    check(f"{label}: ... of the same day", ts_date(aft["regular_ts"]) == a["ref_date"],
          f"aft={ts_date(aft['regular_ts'])} er={a['ref_date']}")


def agree_pre_market(label, aft, er, paired):
    p = er.get("pre_market")
    check(f"{label}: both see the pre-market", p is not None and aft["pre_market"] is not None,
          f"aft={aft['pre_market']} er={p}")
    if p is None:
        return
    check(f"{label}: same pre-market price", same(aft["pre_market"], p["last"]),
          f"aft={aft['pre_market']} er={p['last']}")
    check(f"{label}: same pre-market instant", aft["pre_market_ts"] == p["last_ts"],
          f"aft={aft['pre_market_ts']} er={p['last_ts']}")
    if paired:
        # Before the bell the AFT row's `regular` IS the pre-market's close.
        check(f"{label}: pre-market against the same close", same(aft["regular"], p["ref_close"]),
              f"aft={aft['regular']} er={p['ref_close']}")
        check(f"{label}: ... of the same day", ts_date(aft["regular_ts"]) == p["ref_date"],
              f"aft={ts_date(aft['regular_ts'])} er={p['ref_date']}")
        check(f"{label}: same pre-market percentage", same(aft["ext_change_pct"], p["pct"]),
              f"aft={aft['ext_change_pct']} er={p['pct']}")


# ── an ordinary week: Wed 12, Thu 13, Fri 14 August 2026 ─────────────────────

WED, THU, FRI = date(2026, 8, 12), date(2026, 8, 13), date(2026, 8, 14)
BARS = (minutes(WED, "08:00", 10, 498.0, 0.1) + minutes(WED, "15:50", 10, 499.0, 0.1)    # 15:59 = 499.9
        + minutes(WED, "16:00", 30, 501.0, 0.05)
        + minutes(THU, "08:00", 10, 504.0, 0.1) + minutes(THU, "15:50", 10, 509.0, 0.12)  # 15:59 = 510.08
        + minutes(THU, "16:00", 60, 508.0, -0.01) + minutes(THU, "19:00", 10, 507.0, 0.01)
        + minutes(FRI, "07:00", 30, 512.0, 0.02))
CLOSES = {WED: 500.00, THU: 510.00, FRI: 515.00}

for official in (True, False):
    closes = CLOSES if official else {}
    tag = "official close" if official else "no daily frame"

    # POST: Thursday 18:00. After-hours runs; the reference is Thursday's close.
    aft, er = run_both(BARS, closes, datetime(2026, 8, 13, 18, 0, tzinfo=ET))
    check(f"POST/{tag}: AFT row is in POST", aft["session"] == "POST", aft["session"])
    agree_after_hours(f"POST/{tag}", aft, er)
    # The ER box shows this morning's pre-market against Wednesday's close;
    # the AFT row, whose `regular` is now Thursday's, must not carry it at all.
    check(f"POST/{tag}: the AFT row withholds this morning's pre-market",
          aft["pre_market"] is None and aft["pre_market_ts"] is None, str(aft["pre_market"]))
    check(f"POST/{tag}: the ER box shows it, against Wednesday's close",
          (er.get("pre_market") or {}).get("ref_date") == WED.isoformat())
    # The bell-minute bar is after-hours AND strictly after the close's stamp —
    # ExtendedHoursMath.h's pairing_is_sound rejects ext_ts <= regular_ts.
    first = run_both(BARS, closes, datetime(2026, 8, 13, 16, 0, 30, tzinfo=ET))[0]
    check(f"POST/{tag}: a print in the bell minute pairs, strictly after the close",
          first["post_market"] is not None and first["post_market_ts"] > first["regular_ts"]
          and first["ext_change_pct"] is not None,
          f"post={first['post_market']} ts={first['post_market_ts']} reg_ts={first['regular_ts']}")
    a = er.get("after_hours") or {}
    check(f"POST/{tag}: same after-hours percentage", same(aft["ext_change_pct"], a.get("pct")),
          f"aft={aft['ext_change_pct']} er={a.get('pct')}")
    want_close = 510.00 if official else 509.0 + 0.12 * 9
    check(f"POST/{tag}: the close is {'the official one' if official else 'the 15:59 minute, not the 16:00 bar'}",
          same(aft["regular"], want_close), f"got {aft['regular']}")
    check(f"POST/{tag}: the 16:00 bar is after-hours, so the session has 60+ minutes",
          a.get("minutes", 0) >= 60, str(a.get("minutes")))

    # PRE: Friday 08:00. Today's pre-market against Thursday's close; the
    # after-hours on show is Thursday's, against the same close.
    aft, er = run_both(BARS, closes, datetime(2026, 8, 14, 8, 0, tzinfo=ET))
    check(f"PRE/{tag}: AFT row is in PRE", aft["session"] == "PRE", aft["session"])
    agree_pre_market(f"PRE/{tag}", aft, er, paired=True)
    agree_after_hours(f"PRE/{tag}", aft, er)
    check(f"PRE/{tag}: the pre-market's close is Thursday's",
          ts_date(aft["regular_ts"]) == THU.isoformat(), ts_date(aft["regular_ts"]))

# ── an early close: Black Friday 2026, bell at 13:00 ─────────────────────────

WED2, BF = date(2026, 11, 25), date(2026, 11, 27)    # Thanksgiving between
BF_BARS = (minutes(WED2, "15:50", 10, 99.0, 0.01) + minutes(WED2, "16:00", 30, 99.5, 0.0)
           + minutes(BF, "08:00", 10, 100.0, 0.0) + minutes(BF, "12:50", 10, 101.0, 0.01)
           + minutes(BF, "13:00", 45, 101.5, 0.01))
BF_CLOSES = {WED2: 99.10, BF: 101.10}
aft, er = run_both(BF_BARS, BF_CLOSES, datetime(2026, 11, 27, 14, 0, tzinfo=ET))
check("early close: 14:00 is POST, not REGULAR", aft["session"] == "POST", aft["session"])
agree_after_hours("early close", aft, er)
check("early close: after-hours is the half day's own, against its close",
      (er.get("after_hours") or {}).get("date") == BF.isoformat() and same(aft["regular"], 101.10),
      f"{aft['regular']} {(er.get('after_hours') or {}).get('date')}")
check("early close: the after-hours session is live", (er.get("after_hours") or {}).get("live") is True)

# ── after a holiday: Tuesday 8 September 2026, Labor Day on the Monday ───────

FRI2, TUE2 = date(2026, 9, 4), date(2026, 9, 8)
HOL_BARS = (minutes(FRI2, "15:50", 10, 50.0, 0.01) + minutes(FRI2, "16:00", 20, 50.2, 0.0)
            + minutes(TUE2, "07:00", 30, 51.0, 0.01))
aft, er = run_both(HOL_BARS, {FRI2: 50.05}, datetime(2026, 9, 8, 8, 0, tzinfo=ET))
agree_pre_market("after Labor Day", aft, er, paired=True)
check("after Labor Day: the pre-market's close is Friday's",
      (er.get("pre_market") or {}).get("ref_date") == FRI2.isoformat() and same(aft["regular"], 50.05),
      f"{aft['regular']} {(er.get('pre_market') or {}).get('ref_date')}")

# ── an unscheduled closure: Thursday 9 January 2025 (national day of mourning) ─
# The holiday calendar calls it a trading day. The data does not: no print at
# all that day. It must be neither the AFT row's reference day (its missing
# close would decline every row) nor the pre-market's "session before".

WED3, THU3, FRI3 = date(2025, 1, 8), date(2025, 1, 9), date(2025, 1, 10)
MOURN_BARS = (minutes(WED3, "15:50", 10, 200.0, 0.01) + minutes(WED3, "16:00", 30, 201.0, 0.01)
              + minutes(FRI3, "07:00", 30, 203.0, 0.01))
check("the calendar alone calls 9 Jan 2025 a trading day", xr.is_trading_day(THU3))
aft, er = run_both(MOURN_BARS, {WED3: 200.05}, datetime(2025, 1, 9, 18, 0, tzinfo=ET))
check("closure evening: CLOSED, not POST", aft["session"] == "CLOSED", aft["session"])
check("closure evening: not declined, measured against Wednesday's close",
      aft["declined"] is False and same(aft["regular"], 200.05) and ts_date(aft["regular_ts"]) == WED3.isoformat(),
      f"declined={aft['declined']} regular={aft['regular']}")
agree_after_hours("closure evening", aft, er)
aft, er = run_both(MOURN_BARS, {WED3: 200.05}, datetime(2025, 1, 10, 8, 0, tzinfo=ET))
agree_pre_market("morning after the closure", aft, er, paired=True)
check("morning after the closure: the pre-market's close is Wednesday's",
      (er.get("pre_market") or {}).get("ref_date") == WED3.isoformat(), str(er.get("pre_market")))

# ── the label is per row: the equity calendar is not a future's ──────────────

MLK = datetime(2026, 1, 19, 10, 0, tzinfo=ET)          # NYSE shut, CME open
check("MLK Day 10:00: a US stock's row is CLOSED", yd._ext_session_label(MLK, "MSFT") == "CLOSED")
check("MLK Day 10:00: ES=F keeps the clock rule", yd._ext_session_label(MLK, "ES=F") == "REGULAR")
BF14 = datetime(2026, 11, 27, 14, 0, tzinfo=ET)
check("Black Friday 14:00: a US stock is POST", yd._ext_session_label(BF14, "MSFT") == "POST")
check("Black Friday 14:00: ES=F is not", yd._ext_session_label(BF14, "ES=F") == "REGULAR")

# ── the shared calendar itself ───────────────────────────────────────────────

check("Labor Day is not a trading day", not xr.is_trading_day(date(2026, 9, 7)))
check("a Saturday New Year's Day closes nothing: NYSE trades Friday 31 Dec 2027",
      xr.is_trading_day(date(2027, 12, 31)) and xr.previous_session(date(2028, 1, 3)) == date(2027, 12, 31))
check("a Sunday New Year's Day is observed on the Monday",
      not xr.is_trading_day(date(2023, 1, 2)))
check("the day before Tuesday 8 Sept is Friday 4 Sept", xr.previous_session(TUE2) == FRI2)
check("Thanksgiving skipped", xr.previous_session(BF) == WED2)
check("a holiday is CLOSED, whatever the clock",
      xr.session_label(datetime(2026, 9, 7, 8, 0, tzinfo=ET)) == "CLOSED")
check("last closed day at 02:00 Tuesday after a holiday Monday is Friday",
      xr.last_closed_day(datetime(2026, 9, 8, 2, 0, tzinfo=ET)) == FRI2)
check("DST: the close stamp follows the zone (EDT 19:59:59Z, EST 20:59:59Z)",
      xr.close_instant(THU).astimezone(ZoneInfo("UTC")).strftime("%H:%M:%S") == "19:59:59"
      and xr.close_instant(date(2026, 11, 16)).astimezone(ZoneInfo("UTC")).strftime("%H:%M:%S") == "20:59:59")

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILED: {', '.join(FAILURES)}")
    sys.exit(1)
print("both extended-hours views agree")
