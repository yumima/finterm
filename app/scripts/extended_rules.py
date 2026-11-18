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
  * which days were sessions: the holiday calendar, overruled by the data when
    the market closed unscheduled (9 Jan 2025) — see is_session;
  * the close an extended print belongs to: after-hours of day D -> D's close;
    pre-market of D -> the close of the session before D;
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

# Loudly: without the holiday calendar every holiday would read as a session
# and its missing close would decline every row.
from exchange_sessions import is_us_market_holiday

ET = ZoneInfo("America/New_York")

PRE_OPEN = time(4, 0)
REG_OPEN = time(9, 30)
REG_CLOSE = time(16, 0)
POST_CLOSE = time(20, 0)

EARLY_CLOSE = time(13, 0)
EARLY_POST_CLOSE = time(17, 0)

PRE_WINDOW = (PRE_OPEN, REG_OPEN)

# A day with no print at all this long after the open was not a session.
_NO_SESSION_AFTER = timedelta(minutes=30)


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
    """A scheduled session: a weekday that is not a market holiday."""
    return day.weekday() < 5 and not is_us_market_holiday(day)


def is_session(day, seen=None, now_et=None):
    """Whether `day` was (or is) a session.

    The holiday calendar knows only scheduled closures. `seen` — the ET dates
    on which the minute data has any print — overrules it for the rest: a
    trading day with no print at all, inside the span the data covers, was
    closed (9 Jan 2025, a national day of mourning); so was today, once it is
    half an hour past the open without one. Before the data's first day, or
    without `seen`, the calendar stands."""
    if not is_trading_day(day):
        return False
    if not seen or day in seen:
        return True
    if min(seen) < day < max(seen):
        return False
    if now_et is not None and day == now_et.date() and day > max(seen):
        return now_et < datetime.combine(day, REG_OPEN, tzinfo=ET) + _NO_SESSION_AFTER
    return True


def previous_session(day, seen=None):
    """The session before `day` (which need not be one itself)."""
    d = day - timedelta(days=1)
    for _ in range(14):
        if is_session(d, seen):
            return d
        d -= timedelta(days=1)
    return d


def bell(day):
    return EARLY_CLOSE if is_early_close(day) else REG_CLOSE


def regular_window(day):
    return (REG_OPEN, bell(day))


def post_window(day):
    return (EARLY_CLOSE, EARLY_POST_CLOSE) if is_early_close(day) else (REG_CLOSE, POST_CLOSE)


def close_instant(day):
    """When `day`'s official close is stamped: one second before the bell.

    The close is struck AT the bell, and the bar Yahoo stamps with the bell
    minute holds prints made after it. Stamping both with the same second would
    break the strict order every consumer checks (ExtendedHoursMath.h's
    pairing_is_sound rejects ext_ts <= regular_ts) and drop a move printed in
    that first minute."""
    return datetime.combine(day, bell(day), tzinfo=ET) - timedelta(seconds=1)


def last_closed_day(now_et, seen=None):
    """The session whose close is the most recent one at `now_et`: today once
    its bell has rung, else the session before."""
    today = now_et.date()
    if now_et.time() >= bell(today) and is_session(today, seen, now_et):
        return today
    return previous_session(today, seen)


def session_label(now_et, seen=None):
    """PRE / REGULAR / POST / CLOSED at `now_et`. PRE and REGULAR are exactly
    the stretch in which last_closed_day() is an EARLIER day — the consumers
    pair today's pre-market with the reference only then."""
    today = now_et.date()
    if not is_session(today, seen, now_et):
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


def et_days(index):
    """The ET dates of a minute index (naive = UTC), as a set."""
    if index is None or len(index) == 0:
        return set()
    if getattr(index, "tz", None) is None:
        index = index.tz_localize("UTC")
    return set(index.tz_convert(ET).normalize().unique().date)


class Minutes:
    """A 1-minute frame, read by window. Each window is a slice found by
    binary search on the time index — the frame is neither copied nor given
    per-row columns, since this runs per symbol on every AFT poll."""

    def __init__(self, bars):
        idx = bars.index
        if getattr(idx, "tz", None) is None:
            bars = bars.set_axis(idx.tz_localize("UTC"))
        if not bars.index.is_monotonic_increasing:
            bars = bars.sort_index()
        self.bars = bars
        self._days = None

    @classmethod
    def of(cls, bars):
        """None when there is no frame or no Close column to read."""
        if bars is None or getattr(bars, "empty", True) or "Close" not in bars.columns:
            return None
        return cls(bars)

    def days(self):
        """ET dates with a row of any kind."""
        if self._days is None:
            self._days = et_days(self.bars.index)
        return self._days

    def window(self, day, window):
        """`day`'s priced rows inside `window` = (start, end), half-open, or None."""
        import pandas as pd
        lo = pd.Timestamp(datetime.combine(day, window[0], tzinfo=ET))
        hi = pd.Timestamp(datetime.combine(day, window[1], tzinfo=ET))
        idx = self.bars.index
        part = self.bars.iloc[idx.searchsorted(lo, "left"):idx.searchsorted(hi, "left")]
        if part.empty:
            return None
        c = pd.to_numeric(part["Close"], errors="coerce")
        part = part[c.notna() & (c > 0) & (c < float("inf"))]
        return None if part.empty else part


def latest_window(m, window):
    """(day, rows) for the most recent ET date with priced rows inside its
    window — `window` is (start, end) or a function of the day — or None."""
    if m is None:
        return None
    for day in sorted(m.days(), reverse=True):
        part = m.window(day, window(day) if callable(window) else window)
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
    """{date: (official close, volume or None)} from a daily frame, labels
    read as the calendar dates they are."""
    if daily is None or getattr(daily, "empty", True) or "Close" not in daily.columns:
        return {}
    idx = daily.index
    if getattr(idx, "tz", None) is not None:
        idx = idx.tz_localize(None)
    vols = daily["Volume"] if "Volume" in daily.columns else [None] * len(idx)
    out = {}
    for d, c, v in zip(idx.date, daily["Close"], vols):
        c = price(c)
        if c is not None:
            v = price(v)
            out[d] = (c, v)
    return out


def reference_close(m, day, officials):
    """(close, epoch seconds) of `day`'s regular session, or None. This is
    also the close day `day`'s after-hours is measured against.

    The official close when the daily frame has that day (see close_instant);
    else the last regular-session minute of that same day, which misses only
    the closing auction. A day without either has no close: never substitute
    another day's."""
    got = (officials or {}).get(day)
    if got is not None:
        return got[0], int(close_instant(day).timestamp())
    return last_print(m.window(day, regular_window(day))) if m is not None else None


def close_for_pre(m, day, officials, seen=None):
    """(ref_day, (close, ts) or None): the close day `day`'s pre-market is
    measured against — the session before's."""
    prev = previous_session(day, seen)
    return prev, reference_close(m, prev, officials)


def latest_close(m, officials, upto_day):
    """(day, (close, ts)) for the most recent day <= upto_day with a close, or
    None. Used only to report a reference that is NOT the one wanted."""
    ref = reference_close(m, upto_day, officials)
    if ref is not None:
        return upto_day, ref   # the usual case: no need to list the frame's days
    days = set(officials or {})
    if m is not None:
        days |= m.days()
    for d in sorted((d for d in days if d <= upto_day), reverse=True):
        ref = reference_close(m, d, officials)
        if ref is not None:
            return d, ref
    return None
