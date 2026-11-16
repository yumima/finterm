"""Futures forward curve — the pure part (no network, no pandas).

Yahoo serves every listed delivery month of a US future as its own symbol,
ROOT<month code><yy>.<exchange> (CLF27.NYM, ESH27.CME), each with its own
last price, previous close, volume, open interest and expiry. A curve is
those rows side by side.

What is NOT trusted is the continuous ROOT=F row: Yahoo assembles it from two
contracts around a roll (seen 2026-11-16: KC=F carried March's price and open
interest under December's name and expiry; HE=F the reverse). Only its
`underlyingSymbol` is read, to learn which month is the front and which
exchange suffix the root lives under.
"""

from datetime import date, datetime, timezone

MONTH_CODES = "FGHJKMNQUVXZ"

V7_FIELDS = ("regularMarketPrice,regularMarketPreviousClose,regularMarketTime,"
             "regularMarketVolume,openInterest,expireDate,bid,ask,underlyingSymbol,currency")

from datetime import time as _time, timedelta
from zoneinfo import ZoneInfo

_ET = ZoneInfo("America/New_York")


def trading_day(ts):
    """The CME trading day a print belongs to: its New York date, a session
    opening at 18:00 belonging to the next day, the weekend to Monday."""
    t = datetime.fromtimestamp(ts, tz=timezone.utc).astimezone(_ET)
    d = t.date()
    if t.time() >= _time(18, 0):
        d += timedelta(days=1)
    while d.weekday() >= 5:
        d += timedelta(days=1)
    return d


def previous_trading_day(d):
    d -= timedelta(days=1)
    while d.weekday() >= 5:
        d -= timedelta(days=1)
    return d


def is_stale(ts, newest_ts):
    """A back month is shown but flagged when its last print is older than the
    session before the curve's newest one — its "last" is then an old trade,
    not where that month is worth now. Measured against the curve's own newest
    print, never the wall clock: over a weekend nothing has printed since
    Friday, and that makes nothing stale. (Exchange holidays are not modelled;
    after one a thin month reads stale a session early.)"""
    if not ts or not newest_ts:
        return True
    return trading_day(ts) < previous_trading_day(trading_day(newest_ts))


def split_contract(symbol):
    """'CLF27.NYM' -> ('CL', 2027, 1, 'NYM'); None when it is not a contract month."""
    if not isinstance(symbol, str) or "." not in symbol:
        return None
    body, _, exch = symbol.rpartition(".")
    if len(body) < 4 or not exch.isalpha():
        return None
    yy, code, root = body[-2:], body[-3], body[:-3]
    if not yy.isdigit() or code not in MONTH_CODES or not root:
        return None
    return root, 2000 + int(yy), MONTH_CODES.index(code) + 1, exch


def candidate_symbols(root, exchange, today, months=30):
    """Every calendar month from `today`'s on, as contract symbols. Months the
    exchange does not list simply do not come back from the quote request."""
    y, m = today.year, today.month
    out = []
    for _ in range(max(1, int(months))):
        out.append(f"{root}{MONTH_CODES[m - 1]}{y % 100:02d}.{exchange}")
        m += 1
        if m > 12:
            m, y = 1, y + 1
    return out


def _num(v):
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) and v == v else None


def _pos(v):
    v = _num(v)
    return v if v is not None and v > 0 else None


def contract_row(quote, newest_ts):
    """One curve row from a batch-quote row, or None when it has no price.

    Price and previous close come from ONE snapshot, so the change is
    self-consistent. Absent fields stay None — a contract with no reported
    open interest is not a contract with zero open interest.
    """
    parts = split_contract((quote or {}).get("symbol"))
    price = _pos((quote or {}).get("regularMarketPrice"))
    if parts is None or price is None:
        return None
    _, year, month, _ = parts
    prev = _pos(quote.get("regularMarketPreviousClose"))
    ts = _pos(quote.get("regularMarketTime"))
    exp = _pos(quote.get("expireDate"))
    vol, oi = _num(quote.get("regularMarketVolume")), _num(quote.get("openInterest"))
    return {
        "symbol": quote["symbol"],
        "month": f"{year:04d}-{month:02d}",
        "price": price,
        "prev_close": prev,
        "change": price - prev if prev is not None else None,
        "pct": (price - prev) / prev * 100.0 if prev is not None else None,
        "volume": vol if vol is not None and vol >= 0 else None,
        "oi": oi if oi is not None and oi >= 0 else None,
        # Yahoo stamps the expiry at UTC midnight of the last trading day.
        "expiry": datetime.fromtimestamp(exp, tz=timezone.utc).date().isoformat() if exp else None,
        "bid": _pos(quote.get("bid")),
        "ask": _pos(quote.get("ask")),
        "ts": int(ts) if ts else None,
        "stale": is_stale(ts, newest_ts),
    }


def shape_curve(root, quotes, front_symbol, now_ts, today=None):
    """Curve payload from the batch-quote rows of one root's contract months.

    Expired months that Yahoo still serves are dropped (their expiry is before
    `today`). `front` is the month the continuous series currently follows.
    """
    today = today or date.today()
    rows = []
    currency = ""
    newest = max((_pos(q.get("regularMarketTime")) or 0 for q in quotes or []
                  if (split_contract(q.get("symbol")) or [None])[0] == root), default=0)
    for q in quotes or []:
        parts = split_contract(q.get("symbol"))
        if parts is None or parts[0] != root:
            continue
        row = contract_row(q, newest)
        if row is None:
            continue
        if row["expiry"] and row["expiry"] < today.isoformat():
            continue
        currency = currency or (q.get("currency") or "")
        rows.append(row)
    rows.sort(key=lambda r: r["month"])
    return {"root": root, "front": front_symbol or "", "currency": currency,
            "as_of": int(now_ts), "contracts": rows}


def curve_on_date(closes_by_symbol, ref_date):
    """{symbol: close} for the session `ref_date` (ISO) — only contracts that
    actually traded that session. A month with no bar that day is left out
    rather than filled from a neighbouring day."""
    out = {}
    for sym, bars in (closes_by_symbol or {}).items():
        v = (bars or {}).get(ref_date)
        if isinstance(v, (int, float)) and v == v and v > 0:
            out[sym] = float(v)
    return out
