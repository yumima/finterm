"""The rules of a US stock's extended-hours sessions, in ONE place.

Two computations read Yahoo's 1-minute prepost bars for these sessions and
present them differently:

  * yfinance_data.get_extended_hours_quotes — the AFT% column: one row per
    symbol, the latest extended print against the last completed close;
  * extended_sessions.summarise — the ER Overview box: the latest after-hours
    and pre-market session, each against its own close, with range and volume.

They are different views, but they must not disagree about the facts, so both
take them from here:

  * the windows (ET): pre-market 04:00-09:30, regular 09:30-bell, after-hours
    bell-20:00; the bell is 16:00, or 13:00 on an early close (after-hours then
    runs 13:00-17:00). Half-open: the bar stamped 16:00 is after-hours — Yahoo's
    regular minutes end at 15:59, and the 16:00 bar's close is an extended print
    (it carries the closing auction's VOLUME, not its price);
  * the close an extended print belongs to: after-hours of day D -> D's close;
    pre-market of D -> the close of the trading day before D;
  * the close itself: the official daily close (the 1-minute series never
    contains the closing auction), else the last regular-session minute of that
    same day — never another day's;
  * a price is a finite positive Close; a row without one is not a print.

Daily bar labels are calendar dates and are read as written (tz-naive); minute
bars are real instants and are converted to ET.
"""

import math
import re
from datetime import datetime, time, timedelta
from zoneinfo import ZoneInfo

ET = ZoneInfo("America/New_York")

PRE_OPEN = time(4, 0)
REG_OPEN = time(9, 30)
REG_CLOSE = time(16, 0)
POST_CLOSE = time(20, 0)

EARLY_CLOSE = time(13, 0)
EARLY_POST_CLOSE = time(17, 0)


# ── the calendar ─────────────────────────────────────────────────────────────

def is_early_close(day):
    """The same three occasions as is_early_close() in
    src/services/calendar/MarketCalendar.h — change them together.

    NYSE/Nasdaq 13:00 closes: the day after Thanksgiving, Christmas Eve and
    3 July, when they are trading days. After-hours then runs 13:00-17:00."""
    if day.weekday() >= 5:
        return False
    if day.month == 11 and day.weekday() == 4:
        first = day.replace(day=1)
        fourth_thursday = 1 + (3 - first.weekday()) % 7 + 21
        return day.day == fourth_thursday + 1
    if (day.month, day.day) == (12, 24):
        return day.weekday() != 4   # a Friday 24th is the observed holiday itself
    if (day.month, day.day) == (7, 3):
        return day.weekday() != 4   # likewise when the 4th falls on a Saturday
    return False


def is_trading_day(day):
    if day.weekday() >= 5:
        return False
    try:
        from exchange_sessions import is_us_market_holiday
    except Exception:
        return True
    return not is_us_market_holiday(day)


def previous_trading_day(day):
    """The trading day before `day` (which need not be one itself)."""
    d = day - timedelta(days=1)
    for _ in range(14):
        if is_trading_day(d):
            return d
        d -= timedelta(days=1)
    return d


def bell(day):
    return EARLY_CLOSE if is_early_close(day) else REG_CLOSE


def pre_window(day):
    return (PRE_OPEN, REG_OPEN)


def regular_window(day):
    return (REG_OPEN, bell(day))


def post_window(day):
    return (EARLY_CLOSE, EARLY_POST_CLOSE) if is_early_close(day) else (REG_CLOSE, POST_CLOSE)


def bell_instant(day):
    return datetime.combine(day, bell(day), tzinfo=ET)


def last_closed_day(now_et):
    """The trading day whose close is the most recent one at `now_et`: today
    once its bell has rung, else the trading day before."""
    today = now_et.date()
    if is_trading_day(today) and now_et.time() >= bell(today):
        return today
    return previous_trading_day(today)


def session_label(now_et):
    """PRE / REGULAR / POST / CLOSED at `now_et`. PRE and REGULAR are exactly
    the stretch in which last_closed_day() is an EARLIER day — the consumers
    pair today's pre-market with the reference only then."""
    today = now_et.date()
    if not is_trading_day(today):
        return "CLOSED"
    t = now_et.time()
    if t < PRE_OPEN:
        return "CLOSED"
    if t < REG_OPEN:
        return "PRE"
    if t < bell(today):
        return "REGULAR"
    if t < post_window(today)[1]:
        return "POST"
    return "CLOSED"


def has_us_extended_session(symbol):
    """Only plain US tickers trade these sessions: no exchange suffix, index,
    future, FX pair or crypto."""
    s = str(symbol or "").strip().upper()
    if not s or s.startswith("^") or "=" in s:
        return False
    if s.endswith("-USD") or s.endswith("-USDT"):
        return False
    # A share class is BRK-B on Yahoo and BRK.B on Nasdaq and at most brokers.
    # A dot is otherwise an exchange suffix (.L, .TO, .HK) — no exchange uses
    # .A, .B or .C, so those three read as classes.
    return re.fullmatch(r"[A-Z]{1,5}(?:-[A-Z]{1,2}|\.[ABC])?", s) is not None


# ── the bars ─────────────────────────────────────────────────────────────────

def price(v):
    """A finite positive price, or None."""
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) and f > 0 else None


def minute_frame(bars):
    """The priced rows of a 1-minute frame, index in UTC, with their ET date
    (`_d`) and time (`_t`) alongside. None when there is nothing priced."""
    if bars is None or getattr(bars, "empty", True) or "Close" not in bars.columns:
        return None
    idx = bars.index
    if getattr(idx, "tz", None) is None:
        idx = idx.tz_localize("UTC")
    import pandas as pd
    m = bars.copy()
    m.index = idx
    c = pd.to_numeric(m["Close"], errors="coerce")
    m = m[c.notna() & (c > 0) & (c < float("inf"))]
    if m.empty:
        return None
    et_idx = m.index.tz_convert(ET)
    m["_d"] = et_idx.date
    m["_t"] = et_idx.time
    return m


def window_bars(m, day, window):
    """`day`'s rows inside `window` = (start, end), half-open."""
    if m is None:
        return None
    lo, hi = window
    part = m[(m["_d"] == day) & (m["_t"] >= lo) & (m["_t"] < hi)]
    return None if part.empty else part


def latest_window(m, window_fn):
    """(day, rows) for the most recent ET date with rows inside its own
    `window_fn(day)`, or None."""
    if m is None:
        return None
    for day in sorted(set(m["_d"]), reverse=True):
        part = window_bars(m, day, window_fn(day))
        if part is not None:
            return day, part
    return None


def last_print(part):
    """(price, epoch seconds) of the last row, or None."""
    if part is None or part.empty:
        return None
    return float(part["Close"].iloc[-1]), int(part.index[-1].timestamp())


# ── the close a print is measured against ────────────────────────────────────

def official_closes(daily):
    """{date: official close} from a daily frame, labels read as written."""
    if daily is None or getattr(daily, "empty", True) or "Close" not in daily.columns:
        return {}
    idx = daily.index
    if getattr(idx, "tz", None) is not None:
        idx = idx.tz_localize(None)
    out = {}
    for d, c in zip(idx.date, daily["Close"]):
        c = price(c)
        if c is not None:
            out[d] = c
    return out


def reference_close(m, day, officials):
    """(close, epoch seconds) of `day`'s regular session, or None.

    The official close when the daily frame has that day (stamped at the
    bell); else the last regular-session minute of that same day, which misses
    only the closing auction. A day without either has no close: never
    substitute another day's."""
    c = (officials or {}).get(day)
    if c is not None:
        return c, int(bell_instant(day).timestamp())
    return last_print(window_bars(m, day, regular_window(day)))


def close_for_post(m, day, officials):
    """The close day `day`'s after-hours is measured against: its own."""
    return reference_close(m, day, officials)


def close_for_pre(m, day, officials):
    """(ref_day, (close, ts)) day `day`'s pre-market is measured against: the
    trading day before's. ref_day is returned even when there is no close."""
    prev = previous_trading_day(day)
    return prev, reference_close(m, prev, officials)


def latest_close(m, officials, upto_day):
    """(day, (close, ts)) for the most recent day <= upto_day with a close, or
    None. Used only to report a reference that is NOT the one wanted."""
    days = set(officials or {})
    if m is not None:
        days |= set(m["_d"])
    for d in sorted((d for d in days if d <= upto_day), reverse=True):
        ref = reference_close(m, d, officials)
        if ref is not None:
            return d, ref
    return None
