#!/usr/bin/env python3
"""The EXT% source and the ER Extended Hours box must agree about a session.

yfinance_data.get_extended_hours_quotes (every EXT% surface, and the ER box's
headline price) reads Yahoo's batch quote; extended_sessions.fetch (the ER
box's range, volume and minutes) reads 1-minute prepost bars. Two sources,
one set of rules (extended_rules): here the quote Yahoo would serve for a tape
and the bars of that tape go through both real entry points at a frozen clock,
and each session's print, its instant, its day and the close it is measured
against must come out identical — on an ordinary week, an early close, after a
holiday and around an unscheduled closure.

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


# ── what Yahoo's batch quote says about the same tape ────────────────────────

def _in(t, day, window):
    return t.date() == day and window[0] <= t.time() < window[1]


def quote_at(bars, closes, now, sym=SYM):
    """The v7 quote row Yahoo serves at `now` for these bars: the last regular
    price (the official close once the bell has rung), the close before it,
    and the latest pre-market / after-hours print with its time."""
    seen = [(t, p) for t, p in bars if t < now]
    reg_days = sorted({t.date() for t, _ in seen if _in(t, t.date(), xr.regular_window(t.date()))})
    q = {"symbol": sym, "marketState": "TEST"}
    if reg_days:
        d = reg_days[-1]
        done = d < now.date() or now.time() >= xr.bell(d)
        last_t, last_p = [(t, p) for t, p in seen if _in(t, d, xr.regular_window(d))][-1]
        if done and d in closes:
            q["regularMarketPrice"] = closes[d]
            q["regularMarketTime"] = int(datetime.combine(d, xr.bell(d), tzinfo=ET).timestamp())
        else:
            q["regularMarketPrice"], q["regularMarketTime"] = last_p, int(last_t.timestamp())
        if len(reg_days) > 1 and reg_days[-2] in closes:
            q["regularMarketPreviousClose"] = closes[reg_days[-2]]
    for key, window_of in (("postMarket", xr.post_window), ("preMarket", lambda d: xr.PRE_WINDOW)):
        hits = [(t, p) for t, p in seen if _in(t, t.date(), window_of(t.date()))]
        if hits:
            q[key + "Price"], q[key + "Time"] = hits[-1][1], int(hits[-1][0].timestamp())
    return q


# ── both entry points, one clock, one tape ───────────────────────────────────

def run_both(bars, closes, now):
    """(EXT row, ER answer) for the tape as it stood at `now`.

    The EXT row reads the quote, the ER box reads bars — two sources, one set
    of rules (extended_rules). Two symbols carry the same quote, as a book
    would: the newest regular day across the book is the evidence of an
    unscheduled closure."""
    one_min = minute_frame(bars, now)
    daily = daily_frame({d: c for d, c in closes.items() if d <= now.date()})
    quote = quote_at(bars, closes, now)

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

    class Ticker:
        def __init__(self, sym):
            self.sym = sym

        def history(self, **kw):
            return daily if kw.get("interval") == "1d" else one_min

    def no_download(*a, **kw):
        raise AssertionError("the EXT row of a US stock must not download bars")

    yf.download, yf.Ticker = no_download, Ticker
    real_v7 = yd._v7_quotes
    yd._v7_quotes = lambda symbols, fields, chunk=50: [dict(quote, symbol=s) for s in symbols]
    real_dt, real_es_dt, real_xs_dt, real_nasdaq = sys.modules["datetime"], es.datetime, xs.datetime, es._nasdaq
    sys.modules["datetime"] = shim
    es.datetime = xs.datetime = FrozenDatetime
    es._nasdaq = lambda *a, **k: None   # the volume is not under test
    es._daily_cache.clear()
    try:
        ext = yd.get_extended_hours_quotes([SYM, "AAPL"])[0]
        er = es.fetch(SYM)
    finally:
        sys.modules["datetime"], es.datetime, es._nasdaq = real_dt, real_es_dt, real_nasdaq
        xs.datetime = real_xs_dt
        yd._v7_quotes = real_v7
        for k, v in saved.items():
            if v is None:
                yf.__dict__.pop(k, None)
            else:
                setattr(yf, k, v)
    return ext, er


def same(a, b):
    return a is not None and b is not None and abs(a - b) < 1e-9


def agree(label, name, ext_session, er_session):
    """One session, as the EXT row and the ER box each see it: the same print,
    the same instant, the same close of the same day."""
    q, s = ext_session, er_session
    check(f"{label}: both see the {name}", q is not None and s is not None, f"ext={q} er={s}")
    if q is None or s is None:
        return
    check(f"{label}: same {name} price", same(q["price"], s["last"]), f"ext={q['price']} er={s['last']}")
    check(f"{label}: same {name} instant", q["ts"] == s["last_ts"], f"ext={q['ts']} er={s['last_ts']}")
    check(f"{label}: same {name} day", q["day"] == s["date"], f"ext={q['day']} er={s['date']}")
    check(f"{label}: {name} against the same close", same(q["ref"], s["ref_close"]),
          f"ext={q['ref']} er={s['ref_close']}")
    check(f"{label}: ... of the same day", q["ref_date"] == s["ref_date"],
          f"ext={q['ref_date']} er={s['ref_date']}")


# ── an ordinary week: Wed 12, Thu 13, Fri 14 August 2026 ─────────────────────

WED, THU, FRI = date(2026, 8, 12), date(2026, 8, 13), date(2026, 8, 14)
BARS = (minutes(WED, "08:00", 10, 498.0, 0.1) + minutes(WED, "15:50", 10, 499.0, 0.1)    # 15:59 = 499.9
        + minutes(WED, "16:00", 30, 501.0, 0.05)
        + minutes(THU, "08:00", 10, 504.0, 0.1) + minutes(THU, "15:50", 10, 509.0, 0.12)  # 15:59 = 510.08
        + minutes(THU, "16:00", 60, 508.0, -0.01) + minutes(THU, "19:00", 10, 507.0, 0.01)
        + minutes(FRI, "07:00", 30, 512.0, 0.02))
CLOSES = {WED: 500.00, THU: 510.00, FRI: 515.00}

# POST: Thursday 18:00. After-hours against Thursday's close; this morning's
# pre-market is still on show — against WEDNESDAY's close, in both.
ext, er = run_both(BARS, CLOSES, datetime(2026, 8, 13, 18, 0, tzinfo=ET))
check("POST: the EXT row is in POST", ext["session"] == "POST", ext["session"])
agree("POST", "after-hours", ext["post"], er.get("after_hours"))
agree("POST", "pre-market", ext["pre"], er.get("pre_market"))
check("POST: this morning's pre-market is measured against Wednesday's close",
      (ext["pre"] or {}).get("ref_date") == WED.isoformat(), str(ext["pre"]))
check("POST: the last close is Thursday's official one",
      same(ext["regular"], 510.00) and ext["regular_ts"] == int(xr.close_instant(THU).timestamp()), str(ext))
# The bell-minute bar is after-hours AND strictly after the close's stamp —
# ExtendedHoursMath.h's reading_from_json rejects ts <= ref_ts.
first = run_both(BARS, CLOSES, datetime(2026, 8, 13, 16, 0, 30, tzinfo=ET))[0]
check("POST: a print in the bell minute pairs, strictly after the close",
      first["post"] is not None and first["post"]["ts"] > first["post"]["ref_ts"], str(first["post"]))
check("POST: the 16:00 bar is after-hours, so the session has 60+ minutes",
      (er.get("after_hours") or {}).get("minutes", 0) >= 60, str((er.get("after_hours") or {}).get("minutes")))

# PRE: Friday 08:00. Today's pre-market and Thursday's after-hours, both
# against Thursday's close.
ext, er = run_both(BARS, CLOSES, datetime(2026, 8, 14, 8, 0, tzinfo=ET))
check("PRE: the EXT row is in PRE", ext["session"] == "PRE", ext["session"])
agree("PRE", "pre-market", ext["pre"], er.get("pre_market"))
agree("PRE", "after-hours", ext["post"], er.get("after_hours"))
check("PRE: the pre-market's close is Thursday's", (ext["pre"] or {}).get("ref_date") == THU.isoformat())

# ── an early close: Black Friday 2026, bell at 13:00 ─────────────────────────

WED2, BF = date(2026, 11, 25), date(2026, 11, 27)    # Thanksgiving between
BF_BARS = (minutes(WED2, "15:50", 10, 99.0, 0.01) + minutes(WED2, "16:00", 30, 99.5, 0.0)
           + minutes(BF, "08:00", 10, 100.0, 0.0) + minutes(BF, "12:50", 10, 101.0, 0.01)
           + minutes(BF, "13:00", 45, 101.5, 0.01))
BF_CLOSES = {WED2: 99.10, BF: 101.10}
ext, er = run_both(BF_BARS, BF_CLOSES, datetime(2026, 11, 27, 14, 0, tzinfo=ET))
check("early close: 14:00 is POST, not REGULAR", ext["session"] == "POST", ext["session"])
agree("early close", "after-hours", ext["post"], er.get("after_hours"))
agree("early close", "pre-market", ext["pre"], er.get("pre_market"))
check("early close: after-hours is the half day's own, against its close",
      (ext["post"] or {}).get("day") == BF.isoformat() and same(ext["regular"], 101.10), str(ext["post"]))
check("early close: the after-hours session is live", (er.get("after_hours") or {}).get("live") is True)

# ── after a holiday: Tuesday 8 September 2026, Labor Day on the Monday ───────

FRI2, TUE2 = date(2026, 9, 4), date(2026, 9, 8)
HOL_BARS = (minutes(FRI2, "15:50", 10, 50.0, 0.01) + minutes(FRI2, "16:00", 20, 50.2, 0.0)
            + minutes(TUE2, "07:00", 30, 51.0, 0.01))
ext, er = run_both(HOL_BARS, {FRI2: 50.05}, datetime(2026, 9, 8, 8, 0, tzinfo=ET))
agree("after Labor Day", "pre-market", ext["pre"], er.get("pre_market"))
check("after Labor Day: the pre-market's close is Friday's",
      (ext["pre"] or {}).get("ref_date") == FRI2.isoformat() and same(ext["regular"], 50.05), str(ext))

# ── an unscheduled closure: Thursday 9 January 2025 (national day of mourning) ─
# The holiday calendar calls it a trading day. The data does not: no print at
# all that day. It must be neither the EXT row's reference day (its missing
# close would decline every row) nor the pre-market's "session before".

WED3, THU3, FRI3 = date(2025, 1, 8), date(2025, 1, 9), date(2025, 1, 10)
MOURN_BARS = (minutes(WED3, "15:50", 10, 200.0, 0.01) + minutes(WED3, "16:00", 30, 201.0, 0.01)
              + minutes(FRI3, "07:00", 30, 203.0, 0.01))
check("the calendar alone calls 9 Jan 2025 a trading day", xr.is_trading_day(THU3))
ext, er = run_both(MOURN_BARS, {WED3: 200.05}, datetime(2025, 1, 9, 18, 0, tzinfo=ET))
check("closure evening: CLOSED, not POST", ext["session"] == "CLOSED", ext["session"])
check("closure evening: not declined, measured against Wednesday's close",
      ext["declined"] is False and same(ext["regular"], 200.05), f"declined={ext['declined']} regular={ext['regular']}")
agree("closure evening", "after-hours", ext["post"], er.get("after_hours"))
ext, er = run_both(MOURN_BARS, {WED3: 200.05}, datetime(2025, 1, 10, 8, 0, tzinfo=ET))
check("morning after the closure: not declined", ext["declined"] is False, str(ext))
agree("morning after the closure", "pre-market", ext["pre"], er.get("pre_market"))
check("morning after the closure: the pre-market's close is Wednesday's",
      (er.get("pre_market") or {}).get("ref_date") == WED3.isoformat(), str(er.get("pre_market")))

# ── the label is per row: the equity calendar is not a future's ──────────────

MLK = datetime(2026, 1, 19, 10, 0, tzinfo=ET)          # NYSE shut, CME open
check("MLK Day 10:00: a US stock's row is CLOSED", xr.session_label(MLK) == "CLOSED")
check("MLK Day 10:00: ES=F keeps the clock rule", yd._ext_session_label(MLK) == "REGULAR")
BF14 = datetime(2026, 11, 27, 14, 0, tzinfo=ET)
check("Black Friday 14:00: a US stock is POST", xr.session_label(BF14) == "POST")
check("Black Friday 14:00: ES=F is not", yd._ext_session_label(BF14) == "REGULAR")

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
