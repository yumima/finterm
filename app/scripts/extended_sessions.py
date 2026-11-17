"""The latest after-hours and pre-market session of one US-listed symbol, each
as its own small summary — for the Equity Research overview.

    {symbol, as_of, after_hours: {...} | None, pre_market: {...} | None}

and each session:

    date        ISO date (ET) of the session
    live        True while that session is still trading
    ref_close   the close the session is measured against
    ref_date    the date of that close
    last        last print, and last_ts the instant it printed (epoch seconds)
    change, pct against ref_close
    high, low   the session's range
    minutes     minutes of the session in which the stock printed at all
    volume      shares traded in the session, or None
    volume_partial  True when `volume` was read mid-session and the session's
                total was never seen (Nasdaq blanks it at midnight ET)
    avg_volume  average full-day volume over the previous 20 sessions, or None

Two sources, because neither has the whole picture:

  * Yahoo's 1-minute bars (prepost) give every extended-hours PRICE, but their
    volume is zero outside the regular session — always, for every symbol.
  * Nasdaq's extended-trading summary gives the session VOLUME (consolidated,
    all venues). Its change figure is not used: Nasdaq measures an after-hours
    print against the PREVIOUS day's close, which on an evening after a -8% day
    reads "-8%" for a stock that has moved +0.4% since the bell.

Each session carries its own reference close, so nothing here depends on what
time it is now — the pairing rules that invert in yfinance_data's
get_extended_hours_quotes (one row, one `regular`) do not arise:

  * after-hours of day D is measured against D's official close;
  * pre-market of day D against the close of the trading day before D.

Daily bar labels are calendar dates and are read as written (tz-naive); minute
bars are real instants and are converted to ET.
"""

import re
from datetime import datetime, time as _time
from zoneinfo import ZoneInfo

ET = ZoneInfo("America/New_York")

PRE_OPEN = _time(4, 0)
REG_OPEN = _time(9, 30)
REG_CLOSE = _time(16, 0)
POST_CLOSE = _time(20, 0)

EARLY_CLOSE = _time(13, 0)
EARLY_POST_CLOSE = _time(17, 0)


def is_early_close(day):
    """NYSE/Nasdaq 13:00 closes: the day after Thanksgiving, Christmas Eve and
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


def post_window(day):
    return (EARLY_CLOSE, EARLY_POST_CLOSE) if is_early_close(day) else (REG_CLOSE, POST_CLOSE)


AVG_VOLUME_DAYS = 20
# Nasdaq's range and Yahoo's must describe the same session before Nasdaq's
# volume is put beside Yahoo's prices. Half a percent absorbs odd lots and the
# venues Yahoo does not carry; a different DAY's range misses by far more.
RANGE_TOLERANCE = 0.005
# How far one minute may sit from the median of the minutes around it before it
# is a stray print rather than a move.
STRAY_PRINT = 0.02

_MONTHS = {m: i + 1 for i, m in enumerate(
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"])}


def has_us_extended_session(symbol):
    """Only plain US tickers trade these sessions (and only they are on
    Nasdaq's feed): no exchange suffix, index, future, FX pair or crypto."""
    s = str(symbol or "").strip().upper()
    if not s or s.startswith("^") or "=" in s:
        return False
    if s.endswith("-USD") or s.endswith("-USDT"):
        return False
    # Yahoo writes a share class with a hyphen (BRK-B); a dot is always an
    # exchange suffix (.L, .TO, .HK), one letter or not.
    return re.fullmatch(r"[A-Z]{1,5}(?:-[A-Z]{1,2})?", s) is not None


def _num(v):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if f == f and f not in (float("inf"), float("-inf")) else None


def _daily_rows(daily):
    """[(date, close, volume)] oldest first, from a daily frame, dates as written."""
    if daily is None or getattr(daily, "empty", True) or "Close" not in daily.columns:
        return []
    idx = daily.index
    if getattr(idx, "tz", None) is not None:
        idx = idx.tz_localize(None)
    out = []
    has_vol = "Volume" in daily.columns
    for d, c, v in zip(idx.date, daily["Close"], daily["Volume"] if has_vol else [None] * len(idx)):
        c = _num(c)
        if c is not None and c > 0:
            out.append((d, c, _num(v)))
    out.sort(key=lambda r: r[0])
    return out


def _session(bars, window):
    """The most recent ET date with bars inside its own window, as (date,
    frame) or None. `window(day)` gives that day's (start, end)."""
    bars = bars[bars["Close"].notna()]
    for day in sorted(set(bars["_d"]), reverse=True):
        lo, hi = window(day)
        part = bars[(bars["_d"] == day) & (bars["_t"] >= lo) & (bars["_t"] < hi)]
        if not part.empty:
            return day, part
    return None


def _without_stray_prints(part):
    """Drop minutes whose price stands apart from the minutes on BOTH sides.

    Extended-hours tapes carry trades reported late at another moment's price
    (a prior-reference-price cross prints hours later at the old close) and
    plain bad prints. One of them as the session's high — SPY "ranging" 689 to
    754 around a 740 price — is not a range anybody could have traded.

    A stray print comes back: the minutes after it are where the minutes
    before it were. A move does not — after an earnings jump the following
    minutes agree with it, so it is kept from its first bar. The newest bar
    has nothing after it and cannot be judged; it is kept, because holding
    back a real jump for minutes is worse than showing one bad print for one."""
    if len(part) < 5:
        return part
    close = part["Close"]
    before = close.shift(1).rolling(5, min_periods=1).median()
    after = close[::-1].shift(1).rolling(5, min_periods=1).median()[::-1]
    stray = ((close / before - 1.0).abs() > STRAY_PRINT) & ((close / after - 1.0).abs() > STRAY_PRINT)
    keep = ~stray.fillna(False)
    return part[keep] if keep.any() else part


def _summary(day, part, ref, daily_rows, live):
    part = _without_stray_prints(part)
    last = _num(part["Close"].iloc[-1])
    if last is None:
        return None
    # A bar's own high/low can be a stray print inside an otherwise sound
    # minute, so each is held to the same distance from that minute's close.
    close = part["Close"]
    high = _num(part["High"].clip(upper=close * (1 + STRAY_PRINT)).max()) if "High" in part.columns else None
    low = _num(part["Low"].clip(lower=close * (1 - STRAY_PRINT)).min()) if "Low" in part.columns else None
    out = {
        "date": day.isoformat(),
        "live": bool(live),
        "ref_close": None, "ref_date": None,
        "last": last,
        "last_ts": int(part.index[-1].timestamp()),
        "change": None, "pct": None,
        "high": high, "low": low,
        "minutes": int(len(part)),
        "volume": None, "volume_partial": False, "avg_volume": None,
    }
    if ref is not None:
        ref_date, ref_close = ref
        out["ref_close"] = ref_close
        out["ref_date"] = ref_date.isoformat()
        out["change"] = last - ref_close
        out["pct"] = (last - ref_close) / ref_close * 100.0
    # Full-day volume of the sessions BEFORE this one: the yardstick is fixed
    # for the session rather than growing with today's own running total.
    vols = [v for d, _, v in daily_rows if d < day and v is not None and v > 0][-AVG_VOLUME_DAYS:]
    if len(vols) >= 5:
        out["avg_volume"] = sum(vols) / len(vols)
    return out


def summarise(bars, daily, now_et):
    """(after_hours, pre_market) from a 1-minute prepost frame and a daily frame.

    Pure: no network, no clock but `now_et`."""
    if bars is None or getattr(bars, "empty", True) or "Close" not in bars.columns:
        return None, None
    idx = bars.index
    if idx.tz is None:
        idx = idx.tz_localize("UTC")
    bars = bars.copy()
    bars.index = idx
    et_idx = idx.tz_convert(ET)
    bars["_d"] = et_idx.date
    bars["_t"] = et_idx.time
    rows = _daily_rows(daily)
    today = now_et.date()
    now_t = now_et.time()

    after = None
    got = _session(bars, post_window)
    if got:
        day, part = got
        # Measured against that day's own official close. No such bar (the
        # daily frame lags, or the symbol did not trade the regular session)
        # means no percentage — never some other day's close.
        ref = next(((d, c) for d, c, _ in rows if d == day), None)
        lo, hi = post_window(day)
        after = _summary(day, part, ref, rows, day == today and lo <= now_t < hi)

    pre = None
    got = _session(bars, lambda _day: (PRE_OPEN, REG_OPEN))
    if got:
        day, part = got
        before = [(d, c) for d, c, _ in rows if d < day]
        ref = before[-1] if before else None
        # It has to be the close of the trading day just before. The minute
        # bars know which day that was; a daily frame that has not caught up
        # would hand over an older close and fold a whole day's move into
        # "pre-market".
        earlier = [d for d in set(bars["_d"]) if d < day]
        if ref is not None and earlier and ref[0] != max(earlier):
            ref = None
        pre = _summary(day, part, ref, rows, day == today and PRE_OPEN <= now_t < REG_OPEN)
    return after, pre


# ── Nasdaq: the session's volume ──────────────────────────────────────────────

def _money(s):
    m = re.search(r"-?\d[\d,]*\.?\d*", str(s or "").replace("$", ""))
    return _num(m.group(0).replace(",", "")) if m else None


def _updated_on(doc_data):
    """The ET date in Nasdaq's 'Data last updated Nov 16, 2026 08:00 PM ET.'"""
    for line in (doc_data.get("lastUpdateInfo") or []):
        m = re.search(r"last updated\s+([A-Z][a-z]{2})[a-z]*\.?\s+(\d{1,2}),\s+(\d{4})", str(line))
        if m and m.group(1) in _MONTHS:
            try:
                return datetime(int(m.group(3)), _MONTHS[m.group(1)], int(m.group(2))).date()
            except ValueError:
                return None
    return None


def parse_nasdaq(doc):
    """{date, volume, high, low} from an extended-trading response, or None."""
    data = (doc or {}).get("data") if isinstance(doc, dict) else None
    if not isinstance(data, dict):
        return None
    rows = ((data.get("infoTable") or {}).get("rows")) or []
    if not rows or not isinstance(rows[0], dict):
        return None
    r = rows[0]
    vol = _money(r.get("volume"))
    if vol is None or vol < 0:
        return None
    return {"date": _updated_on(data), "volume": vol,
            "high": _money(r.get("highPrice")), "low": _money(r.get("lowPrice"))}


def volume_for(session, nasdaq):
    """Nasdaq's volume, if it demonstrably belongs to `session`; else None.

    The feed answers for "the latest session it has", which is not always the
    one Yahoo's bars describe (it rolls at its own time, and a thin stock can
    skip a day). A stated date settles it. When the date cannot be read, the
    two ranges must agree instead — a volume printed beside another session's
    prices is worse than a dash."""
    if not session or not nasdaq:
        return None
    if nasdaq.get("date") is not None:
        return nasdaq["volume"] if nasdaq["date"].isoformat() == session["date"] else None
    for key in ("high", "low"):
        a, b = session.get(key), nasdaq.get(key)
        if a is None or b is None or a <= 0:
            return None
        if abs(a - b) / a > RANGE_TOLERANCE:
            return None
    return nasdaq["volume"]


def apply_nasdaq(session, nasdaq):
    """Put Nasdaq's volume on `session`, and tighten its range with Nasdaq's.

    Both tapes carry stray prints, and not the same ones. A stray print can
    only WIDEN a range, so of two readings of the same session the lower high
    and the higher low are the better ones — taken only for a finished session,
    when Nasdaq states its date and the result still holds the last price."""
    session["volume"] = volume_for(session, nasdaq)
    if not nasdaq or nasdaq.get("date") is None or nasdaq["date"].isoformat() != session["date"]:
        return
    # Only for a finished session. While it trades, Nasdaq's summary runs
    # behind the bars, and its "lower high" is simply a high it has not seen yet.
    if session.get("live"):
        return
    high, low = session.get("high"), session.get("low")
    nh, nl = nasdaq.get("high"), nasdaq.get("low")
    if high is not None and nh is not None and nh > 0:
        high = min(high, nh)
    if low is not None and nl is not None and nl > 0:
        low = max(low, nl)
    if high is not None and low is not None and low <= session["last"] <= high:
        session["high"], session["low"] = high, low


NASDAQ_URL = "https://api.nasdaq.com/api/quote/{}/extended-trading?markettype={}&assetclass={}"


NASDAQ_TIMEOUT = 6       # seconds per request: the volume is an extra
_asset_class = {}        # symbol -> "stocks" | "etf", once Nasdaq has answered for it


def _nasdaq(symbol, market, deadline=None):
    """Nasdaq's summary of that session, or None. Bounded: the caller's
    answer (Yahoo's prices) is already in hand and must not wait on this."""
    import time as _clock
    try:
        import requests
        import ipo_calendar as ic
    except Exception:
        return None
    sym = symbol.replace("-", ".")
    for asset in ([_asset_class[symbol]] if symbol in _asset_class else ["stocks", "etf"]):
        if deadline is not None and _clock.monotonic() > deadline:
            return None
        try:
            r = requests.get(NASDAQ_URL.format(sym, market, asset), headers=ic.NASDAQ_HEADERS,
                             timeout=NASDAQ_TIMEOUT)
            r.raise_for_status()
            got = parse_nasdaq(r.json())
        except Exception:
            got = None
        if got:
            _asset_class[symbol] = asset
            return got
    return None


# ── the volume, kept ──────────────────────────────────────────────────────────
#
# Nasdaq holds a session's summary only until midnight ET, then blanks it until
# the next one opens — so by the morning, last evening's after-hours volume is
# gone from every free source. What was seen while it was there is kept here,
# marked final when it was read after the session ended and partial when the
# session was still trading. A symbol nobody looked at before midnight has no
# volume the next day: a dash.

_KEEP_DAYS = 10
SETTLE_MINUTES = 15      # after a session ends, before Nasdaq's total is taken as final
NASDAQ_BUDGET = 14       # seconds per fetch for all Nasdaq requests together
_REASK_SECONDS = 15 * 60
_asked = {}   # (symbol, market, day) -> when a finished session was last asked about in vain


def _store():
    import sqlite3
    from finterm_paths import market_data_path
    con = sqlite3.connect(market_data_path("extended_volume.sqlite"), timeout=5)
    con.execute("CREATE TABLE IF NOT EXISTS session_volume (symbol TEXT, market TEXT, day TEXT, "
                "volume REAL, high REAL, low REAL, final INTEGER, PRIMARY KEY (symbol, market, day))")
    return con


def recall(symbol, market, day):
    """{volume, high, low, final} stored for that session, or None."""
    try:
        con = _store()
        try:
            row = con.execute("SELECT volume, high, low, final FROM session_volume "
                              "WHERE symbol=? AND market=? AND day=?", (symbol, market, day)).fetchone()
        finally:
            con.close()
    except Exception:
        return None   # the store is a convenience; the answer stands without it
    return {"volume": row[0], "high": row[1], "low": row[2], "final": bool(row[3])} if row else None


def remember(symbol, market, day, nasdaq, final):
    try:
        con = _store()
        try:
            # A final reading is never replaced by a partial one.
            con.execute("""INSERT INTO session_volume VALUES (?,?,?,?,?,?,?)
                           ON CONFLICT(symbol, market, day) DO UPDATE SET
                             volume=excluded.volume, high=excluded.high, low=excluded.low, final=excluded.final
                           WHERE excluded.final >= session_volume.final""",
                        (symbol, market, day, nasdaq["volume"], nasdaq.get("high"), nasdaq.get("low"), int(final)))
            con.execute("DELETE FROM session_volume WHERE day < date(?, ?)", (day, f"-{_KEEP_DAYS} days"))
            con.commit()
        finally:
            con.close()
    except Exception:
        pass


def add_volume(symbol, market, session, ask=None, now=None, settled=None):
    """Put the session's volume (and Nasdaq's range) on it, asking Nasdaq only
    when the answer can still be new.

    `settled`: the session ended long enough ago for Nasdaq's summary to hold
    its total. Its summary runs behind the tape, so a reading in the first
    minutes after the session ends is still a partial one."""
    import time as _clock
    ask = ask or _nasdaq
    now = _clock.time() if now is None else now
    settled = (not session["live"]) if settled is None else (settled and not session["live"])
    day, key = session["date"], (symbol, market, session["date"])
    stored = recall(symbol, market, day)
    if settled and stored and stored["final"]:
        # Finished and its total already seen: nothing left to learn.
        apply_nasdaq(session, dict(stored, date=datetime.strptime(day, "%Y-%m-%d").date()))
        return
    asked = False
    if settled and now - _asked.get(key, 0) < _REASK_SECONDS:
        nasdaq = None   # asked a moment ago; a finished session's answer does not appear by itself
    else:
        nasdaq, asked = ask(symbol, market), True
    apply_nasdaq(session, nasdaq)
    if session["volume"] is not None:
        remember(symbol, market, day, nasdaq, final=settled)
        _asked.pop(key, None)
        return
    if asked and settled:
        _asked[key] = now   # from the asking, not from each skipped look: the wait must end
    if stored:
        # A reading from while it traded, and the total never seen: shown as a floor.
        session["volume"] = stored["volume"]
        session["volume_partial"] = True


def fetch(symbol):
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no symbol given"}
    now_et = datetime.now(ET)
    empty = {"symbol": symbol, "as_of": int(now_et.timestamp()), "after_hours": None, "pre_market": None}
    if not has_us_extended_session(symbol):
        return dict(empty, unsupported=True)

    import yfinance as yf
    t = yf.Ticker(symbol)
    try:
        bars = t.history(period="5d", interval="1m", prepost=True, auto_adjust=False)
    except Exception as e:
        # A refused request is not "no extended trading".
        return {"error": f"extended-hours bars unavailable: {type(e).__name__}: {str(e)[:120]}"}
    try:
        daily = t.history(period="2mo", interval="1d", auto_adjust=False)
    except Exception:
        daily = None   # prices still stand; the percentages and the average do not

    if bars is None or getattr(bars, "empty", True):
        # yfinance answers a refused or rate-limited request with an EMPTY
        # frame, not an exception. With the daily bars in hand the symbol
        # exists, and no minute bar in five sessions is most likely one that
        # does not trade intraday (a mutual fund, a halted listing) — but it
        # can also be a refusal of this one call. So: "no extended trades",
        # which the panel looks at again in ten minutes, never "unsupported",
        # which it stops asking about. Without the daily bars, no answer at all.
        if daily is not None and not getattr(daily, "empty", True):
            return empty
        return {"error": "extended-hours bars unavailable: no bars returned"}

    after, pre = summarise(bars, daily, now_et)
    import time as _clock
    from datetime import timedelta
    deadline = _clock.monotonic() + NASDAQ_BUDGET
    for session, market in ((after, "post"), (pre, "pre")):
        if session:
            day = datetime.strptime(session["date"], "%Y-%m-%d").date()
            end = post_window(day)[1] if market == "post" else REG_OPEN
            ended = datetime.combine(day, end, tzinfo=ET)
            add_volume(symbol, market, session,
                       ask=lambda s_, m_: _nasdaq(s_, m_, deadline),
                       settled=now_et >= ended + timedelta(minutes=SETTLE_MINUTES))
    return dict(empty, after_hours=after, pre_market=pre)
