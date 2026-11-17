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


def _session(bars, lo, hi):
    """The most recent ET date with bars in [lo, hi), as (date, frame) or None."""
    t = bars["_t"]
    part = bars[(t >= lo) & (t < hi)]
    part = part[part["Close"].notna()]
    if part.empty:
        return None
    day = part["_d"].iloc[-1]
    return day, part[part["_d"] == day]


def _without_stray_prints(part):
    """Drop minutes whose price sits away from the minutes around them.

    Extended-hours tapes carry trades reported late at another moment's price
    (a prior-reference-price cross prints hours later at the old close) and
    plain bad prints. One of them as the session's high — SPY "ranging" 689 to
    754 around a 740 price — is not a range anybody could have traded. A real
    move drags its neighbours with it; a stray print stands alone."""
    if len(part) < 5:
        return part
    close = part["Close"]
    around = close.rolling(11, center=True, min_periods=3).median()
    keep = ((close / around - 1.0).abs() <= STRAY_PRINT)
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
        "volume": None, "avg_volume": None,
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
    got = _session(bars, REG_CLOSE, POST_CLOSE)
    if got:
        day, part = got
        # Measured against that day's own official close. No such bar (the
        # daily frame lags, or the symbol did not trade the regular session)
        # means no percentage — never some other day's close.
        ref = next(((d, c) for d, c, _ in rows if d == day), None)
        after = _summary(day, part, ref, rows, day == today and REG_CLOSE <= now_t < POST_CLOSE)

    pre = None
    got = _session(bars, PRE_OPEN, REG_OPEN)
    if got:
        day, part = got
        before = [(d, c) for d, c, _ in rows if d < day]
        ref = before[-1] if before else None
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
    and the higher low are the better ones — taken only when Nasdaq states the
    session's date and the result still holds the last price."""
    session["volume"] = volume_for(session, nasdaq)
    if not nasdaq or nasdaq.get("date") is None or nasdaq["date"].isoformat() != session["date"]:
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


def _nasdaq(symbol, market):
    try:
        import ipo_calendar as ic
    except Exception:
        return None
    sym = symbol.replace("-", ".")
    # "stocks" twice: the feed drops a request now and then, and a stock whose
    # first answer was lost would otherwise be asked for as an ETF and given up on.
    for asset in ("stocks", "etf", "stocks"):
        try:
            got = parse_nasdaq(ic._http_json("nasdaq", NASDAQ_URL.format(sym, market, asset), ic.NASDAQ_HEADERS))
        except Exception:
            got = None   # the volume is an extra: never at the cost of the prices
        if got:
            return got
    return None


# ── the volume, kept ──────────────────────────────────────────────────────────
#
# Nasdaq holds a session's summary only until midnight ET, then blanks it until
# the next one opens — so by the morning, last evening's after-hours volume is
# gone from every free source. What was seen while it was there is kept here.
# A symbol nobody looked at before midnight has no volume the next day: a dash.

_KEEP_DAYS = 10


def _store():
    import sqlite3
    from finterm_paths import market_data_path
    con = sqlite3.connect(market_data_path("extended_volume.sqlite"), timeout=5)
    con.execute("CREATE TABLE IF NOT EXISTS volume (symbol TEXT, market TEXT, day TEXT, volume REAL, "
                "PRIMARY KEY (symbol, market, day))")
    return con


def remember_volume(symbol, market, session):
    """Store the session's volume if it has one; otherwise recall a stored one."""
    try:
        con = _store()
        try:
            key = (symbol, market, session["date"])
            if session.get("volume") is not None:
                # A live session's volume only grows; the last reading wins.
                con.execute("INSERT OR REPLACE INTO volume VALUES (?,?,?,?)", key + (session["volume"],))
                con.execute("DELETE FROM volume WHERE day < date(?, ?)", (session["date"], f"-{_KEEP_DAYS} days"))
                con.commit()
            else:
                row = con.execute("SELECT volume FROM volume WHERE symbol=? AND market=? AND day=?", key).fetchone()
                if row:
                    session["volume"] = row[0]
        finally:
            con.close()
    except Exception:
        pass   # the store is a convenience; the answer stands without it


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
    # yfinance answers a refused or rate-limited request with an EMPTY frame,
    # not an exception. A US ticker with no minute bar at all in five sessions
    # — regular hours included — has not been answered; "no extended trades"
    # is a frame that has bars, none of them outside the regular session.
    if bars is None or getattr(bars, "empty", True):
        return {"error": "extended-hours bars unavailable: no bars returned"}
    try:
        daily = t.history(period="2mo", interval="1d", auto_adjust=False)
    except Exception:
        daily = None   # prices still stand; the percentages and the average do not

    after, pre = summarise(bars, daily, now_et)
    for session, market in ((after, "post"), (pre, "pre")):
        if session:
            apply_nasdaq(session, _nasdaq(symbol, market))
            remember_volume(symbol, market, session)
    return dict(empty, after_hours=after, pre_market=pre)
