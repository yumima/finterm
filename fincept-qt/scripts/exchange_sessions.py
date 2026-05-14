# Session-aware prior-reference lookup for non-equity instruments.
#
# For US equities the calendar-day boundary matches the regular session
# boundary, so yfinance's daily-bar Close at iloc[-2] is the correct
# denominator for "today's % change". For futures, FX, and crypto the
# trading session straddles the calendar day — using iloc[-2] reaches
# back into the wrong session and produces a "% change" that lumps two
# trading sessions into one (e.g. ES at 22:00 ET Wed compared against
# Tuesday's daily close instead of Wednesday's 16:00 ET settle).
#
# This module:
#   1. Maps a symbol to its exchange's session-end time of day.
#   2. Computes the most recent session-end timestamp <= now.
#   3. Provides a batch helper that returns {symbol: prior_session_close}
#      for a list of non-equity symbols by fetching yfinance intraday
#      bars (one HTTP call) and picking the bar at or just before each
#      symbol's session-end.

from __future__ import annotations

import sys
from datetime import date, datetime, timedelta
from typing import Dict, FrozenSet, List, Optional, Tuple
from zoneinfo import ZoneInfo


def _warn(msg: str) -> None:
    """Tagged stderr log so silent fallbacks aren't invisible in prod."""
    print(f"[exchange_sessions] {msg}", file=sys.stderr)


# Per-product futures session-end (settle) times. Hour/minute, all in
# America/New_York. Unknown roots default to 16:00 ET (CME equity-index
# convention — the most common product). Times reflect each product's
# RTH close as the exchange computes settlement; treating them as the
# "session boundary" gives the right prior-reference for a Globex
# evening session that follows.
_FUTURES_SESSIONS: Dict[str, Tuple[int, int]] = {
    # Equity index — CME
    "ES": (16, 0), "NQ": (16, 0), "YM": (16, 0), "RTY": (16, 0),
    "MES": (16, 0), "MNQ": (16, 0), "MYM": (16, 0), "M2K": (16, 0),
    # Treasury — CBOT
    "ZN": (15, 0), "ZB": (15, 0), "ZF": (15, 0), "ZT": (15, 0),
    "UB": (15, 0), "TN": (15, 0),
    # Energy — NYMEX
    "CL": (14, 30), "NG": (14, 30), "HO": (14, 30), "RB": (14, 30), "BZ": (14, 30),
    # Metals — COMEX
    "GC": (13, 30), "SI": (13, 30), "HG": (13, 30),
    "PL": (13, 30), "PA": (13, 30),
    # Grains / oilseeds — CBOT (13:20 CT == 14:20 ET)
    "ZC": (14, 20), "ZW": (14, 20), "ZS": (14, 20),
    "ZL": (14, 20), "ZM": (14, 20), "ZO": (14, 20), "ZR": (14, 20),
    # Livestock — CME
    "LE": (14, 5), "HE": (14, 5), "GF": (14, 5),
    # FX futures — CME
    "6E": (16, 0), "6J": (16, 0), "6B": (16, 0), "6A": (16, 0),
    "6C": (16, 0), "6S": (16, 0), "6N": (16, 0), "6M": (16, 0), "6R": (16, 0),
    # Crypto futures — CME (track equity-index close)
    "BTC": (16, 0), "ETH": (16, 0), "MBT": (16, 0), "MET": (16, 0),
    # Vol / dollar
    "VX": (16, 15), "DX": (16, 0),
}


def _futures_root(symbol: str) -> str:
    """Strip the '=F' suffix used by yfinance for continuous front-month."""
    return symbol[:-2] if symbol.endswith("=F") else symbol


def _is_crypto_spot(symbol: str) -> bool:
    """True for yfinance crypto-spot pairs: BTC-USD, ETH-USD, etc.

    Tighter than `"-USD" in symbol` (which would match e.g.
    `WEIRD-USD-FOO` or any symbol with `-USD` mid-string). All real
    yfinance crypto pairs end with `-USD`, `-USDT`, `-USDC`, or `-EUR`;
    we accept the USD family as our crypto canonicalisation.
    """
    return (symbol.endswith("-USD")
            or symbol.endswith("-USDT")
            or symbol.endswith("-USDC"))


def is_non_equity(symbol: str) -> bool:
    """True iff the symbol's trading session doesn't match the US-equity
    calendar day. These are the symbols where daily-bar iloc[-2] is the
    wrong denominator and we should use the session-aware path."""
    if symbol.endswith("=F"):
        return True
    if symbol.endswith("=X"):
        return True
    if _is_crypto_spot(symbol):
        return True
    return False


def session_end_local(symbol: str) -> Tuple[int, int, str]:
    """Return (hour, minute, tz_name) for this symbol's session-end clock time."""
    if symbol.endswith("=F"):
        h, m = _FUTURES_SESSIONS.get(_futures_root(symbol), (16, 0))
        return (h, m, "America/New_York")
    if symbol.endswith("=X"):
        return (17, 0, "America/New_York")   # FX rollover at NY 17:00
    if _is_crypto_spot(symbol):
        return (0, 0, "UTC")                  # crypto: midnight UTC
    return (16, 0, "America/New_York")        # equities default


# US-market holiday cache (computed lazily on first lookup, per year).
# Used by previous_session_end_utc to walk back past full closures so the
# returned timestamp lands on a real trading session rather than a holiday
# Monday with no bars. yfinance's intraday bar mask already degrades
# gracefully (no bars on the holiday → next-earlier bar is picked), but
# the timestamp itself is referenced in extended-hours logic for
# same-day filtering and we want it correct there too.
_HOLIDAYS_CACHE: Dict[int, FrozenSet[date]] = {}


def _us_market_holidays(year: int) -> FrozenSet[date]:
    cached = _HOLIDAYS_CACHE.get(year)
    if cached is not None:
        return cached
    try:
        from pandas.tseries.holiday import (
            GoodFriday,
            USFederalHolidayCalendar,
        )

        class _USMarketCal(USFederalHolidayCalendar):
            # USFederalHolidayCalendar omits Good Friday but US equity
            # markets and CME's equity-index products do close that day.
            rules = USFederalHolidayCalendar.rules + [GoodFriday]

        hols = _USMarketCal().holidays(
            start=f"{year}-01-01", end=f"{year}-12-31"
        )
        result = frozenset(h.date() for h in hols)
    except Exception as e:
        # pandas missing or holiday lookup blew up. Fall back to "no
        # holidays" — the bar-mask fallback in callers covers us. Warn
        # so the silent degradation is visible in stderr / logs.
        _warn(f"holiday calendar unavailable ({e!r}); skipping holiday walk-back")
        result = frozenset()
    _HOLIDAYS_CACHE[year] = result
    return result


def previous_session_end_utc(symbol: str,
                              now_utc: Optional[datetime] = None) -> datetime:
    """Most recent session-end timestamp (UTC, tz-aware) at or before `now`.

    Weekend session-ends are skipped for non-crypto symbols: if today's
    candidate lands on Sat/Sun the function walks back until weekday.

    Examples (assuming a Wednesday 22:14 ET):
      ES=F  → Wed 16:00 ET   (today's settle, already happened)
    Wednesday 12:00 ET (during RTH, before 16:00 settle):
      ES=F  → Tue 16:00 ET   (today's not yet; use yesterday's)
    Monday 10:00 ET:
      ES=F  → Fri 16:00 ET   (weekend skip)
    """
    if now_utc is None:
        now_utc = datetime.now(ZoneInfo("UTC"))
    h, m, tz_name = session_end_local(symbol)
    tz = ZoneInfo(tz_name)
    now_local = now_utc.astimezone(tz)

    candidate = now_local.replace(hour=h, minute=m, second=0, microsecond=0)
    if candidate > now_local:
        candidate -= timedelta(days=1)

    if tz_name != "UTC":  # weekend + US-holiday skip for non-crypto
        # Bound the walk so a pathological calendar can't loop forever
        # (e.g. an empty holiday set across a known-closed week still
        # terminates via the weekday check; this is belt-and-braces).
        max_steps = 14
        while max_steps > 0:
            if candidate.weekday() >= 5:  # Sat=5, Sun=6
                candidate -= timedelta(days=1)
                max_steps -= 1
                continue
            if candidate.date() in _us_market_holidays(candidate.year):
                candidate -= timedelta(days=1)
                max_steps -= 1
                continue
            break

    return candidate.astimezone(ZoneInfo("UTC"))


def batch_prior_references(symbols: List[str],
                           now_utc: Optional[datetime] = None
                           ) -> Dict[str, float]:
    """For each non-equity symbol in `symbols`, return {symbol: prior_close}.

    Uses ONE yfinance intraday batch download (period=5d, interval=1m)
    and picks the Close of the bar at or just before each symbol's
    most-recent session-end timestamp. Symbols yfinance returns no usable
    data for, or whose intraday timeline doesn't reach back to the
    session-end, are omitted — callers should fall back to their
    existing daily-bar value for those.

    Why interval="1m": yfinance only publishes the current day's bars
    eagerly at 1-minute granularity. At 5m / 15m / 60m granularity, today's
    bars don't appear until well after the session closes. For "today's
    settle just happened" lookups (e.g. ES=F at 22:00 ET, needing Wed
    16:00 ET settle), only 1m gives us a real intraday bar at the target
    timestamp.

    Why period="5d": "1d" returns empty for ES=F (yfinance bug); "2d" works
    intraday but a Monday-morning lookup needs Friday's settle three
    calendar days back, so "5d" is the safe minimum that covers a typical
    weekend (longer 3-day weekends and full holiday weeks still work).
    """
    targets = [s for s in symbols if is_non_equity(s)]
    if not targets:
        return {}

    try:
        import io
        import contextlib
        import yfinance as _yf
        import pandas as _pd
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            data = _yf.download(
                targets, period="5d", interval="1m",
                prepost=False, group_by="ticker", progress=False,
                threads=True, auto_adjust=True,
            )
    except Exception as e:
        _warn(f"batch intraday download failed: {e!r}")
        return {}

    if data is None or getattr(data, "empty", True):
        _warn(f"batch intraday returned empty for {len(targets)} symbols")
        return {}

    out: Dict[str, float] = {}
    for sym in targets:
        try:
            if isinstance(data.columns, _pd.MultiIndex):
                level0 = data.columns.get_level_values(0).unique().tolist()
                level1 = data.columns.get_level_values(1).unique().tolist()
                if sym in level0:
                    hist = data[sym]
                elif sym in level1:
                    hist = data.xs(sym, axis=1, level=1)
                else:
                    continue
            else:
                hist = data
            hist = hist.dropna(how="all")
            if hist.empty:
                continue

            idx = hist.index
            if idx.tz is None:
                idx = idx.tz_localize("UTC")
            else:
                idx = idx.tz_convert("UTC")

            target_ts = previous_session_end_utc(sym, now_utc)
            mask = idx <= target_ts
            if not bool(mask.any()):
                continue
            close = hist["Close"][mask].iloc[-1]
            if _pd.isna(close):
                continue
            out[sym] = float(close)
        except Exception as e:
            _warn(f"prior-reference lookup failed for {sym}: {e!r}")
            continue
    return out
