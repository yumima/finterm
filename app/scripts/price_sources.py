"""Price sources other than Yahoo: daily bars and live quotes from a vendor
the user has a key for. Yahoo stays the default and the fallback.

The app reads every price through the yfinance daemon. This module is what
that daemon asks first when the user picked another source (Settings ›
Data Sources › "Price source", passed in as FINTERM_PRICE_SOURCE) and the
vendor's key is in the keyring (injected as its usual env var):

    tiingo    TIINGO_API_KEY       daily bars (adjusted) + IEX quotes   free tier: both
    polygon   POLYGON_API_KEY      daily bars (adjusted) + snapshot     free tier: bars only
    alpaca    ALPACA_API_KEY_ID +  quotes (free IEX feed: price only)  free with an account
              ALPACA_API_SECRET_KEY
    alpaca_sip  same keys          bars + quotes, consolidated tape     paid plan
    finnhub   FINNHUB_API_KEY      quotes                               free tier: quotes
    fmp       FMP_API_KEY          daily bars (adjusted) + quotes       free tier: both, 250 calls/day

THE CONTRACT — the daemon's own payload, so nothing downstream can tell:
  * a daily bar is stamped at 00:00 America/New_York of its trading date
    (the exchange-midnight rule every bar decode in the app relies on);
  * two price bases, named after what Yahoo returns:
      "total" — adjusted for splits AND dividends (auto_adjust=True: charts,
                technicals, the 52-week band);
      "split" — adjusted for splits only (auto_adjust=False: Yahoo's
                "unadjusted" series is still split-adjusted, and the trailing
                stop compares it with a live price — a truly raw pre-split
                high would read as a −90% drawdown);
    a vendor serves only the bases it really provides;
  * the series reaches the latest session: during or after a trading day a
    series whose last bar is older is refused (end-of-day vendors post the
    day's bar after the close);
  * history as long as asked: "max" always stays on Yahoo, and a series that
    starts well after the requested start (a free tier's history cap) is
    refused rather than served short;
  * volume and high/low are consolidated (all venues): a single-venue feed
    (IEX — Alpaca's free feed, Tiingo's quotes) serves prices only;
  * a quote is the regular-session price, so vendor quotes are used only
    during regular hours (09:30–16:00 ET, weekdays); outside them Yahoo
    answers, with the close and the separate after-hours figure the app
    already pairs correctly;
  * a quote carries price, change, change_percent, previous_close, volume,
    timestamp, and "source" naming the vendor.

Any failure — no key, a free tier without the entitlement (403), a rate limit
(429), an unknown symbol, a malformed answer — raises SourceUnavailable and
the daemon falls back to Yahoo. Nothing is ever half-served: a series comes
from one source.

Response shapes follow each vendor's published API reference. A vendor is
"verified" only once a real key has been run against it; until then the
Settings page says so.
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta, timezone

try:
    from zoneinfo import ZoneInfo
    _NY = ZoneInfo("America/New_York")
except Exception:  # pragma: no cover - zoneinfo ships with 3.9+
    _NY = None

TIMEOUT_S = 15

SOURCES = ("tiingo", "polygon", "alpaca", "alpaca_sip", "finnhub", "fmp")


class SourceUnavailable(Exception):
    """The configured source can't answer this request; use Yahoo."""


# ── configuration ────────────────────────────────────────────────────────────

def _keys(name):
    if name == "tiingo":
        return {"token": os.environ.get("TIINGO_API_KEY", "").strip()}
    if name == "polygon":
        return {"apiKey": os.environ.get("POLYGON_API_KEY", "").strip()}
    if name in ("alpaca", "alpaca_sip"):
        return {"id": os.environ.get("ALPACA_API_KEY_ID", "").strip(),
                "secret": os.environ.get("ALPACA_API_SECRET_KEY", "").strip()}
    if name == "finnhub":
        return {"token": os.environ.get("FINNHUB_API_KEY", "").strip()}
    if name == "fmp":
        return {"apikey": os.environ.get("FMP_API_KEY", "").strip()}
    return {}


def configured():
    """The chosen source when it has its key(s), else None (= Yahoo)."""
    name = os.environ.get("FINTERM_PRICE_SOURCE", "").strip().lower()
    if name not in SOURCES:
        return None
    keys = _keys(name)
    return name if keys and all(keys.values()) else None


def alpaca_feed(name=None):
    """'iex' — the free feed, one venue — or 'sip', the consolidated tape on
    Alpaca's paid plan (chosen as the "alpaca_sip" source)."""
    name = name or os.environ.get("FINTERM_PRICE_SOURCE", "").strip().lower()
    return "sip" if name == "alpaca_sip" else "iex"


def capabilities(name):
    """What a source can serve: {'bars': {bases}, 'quotes': bool}."""
    return {
        "tiingo": {"bars": {"total", "split"}, "quotes": True},
        "polygon": {"bars": {"split"}, "quotes": True},
        # The free IEX feed sees ~2-3% of the tape: its volume and high/low
        # are not the stock's. Bars only from the consolidated (paid) feed.
        "alpaca": {"bars": set(), "quotes": True},
        "alpaca_sip": {"bars": {"total", "split"}, "quotes": True},
        "finnhub": {"bars": set(), "quotes": True},
        "fmp": {"bars": {"total"}, "quotes": True},
    }.get(name, {"bars": set(), "quotes": False})


# ── shared helpers ───────────────────────────────────────────────────────────

def _user_agent():
    try:
        from finterm_identity import project_user_agent
        return project_user_agent()
    except Exception:
        return "finterm"


def _get(url, params=None, headers=None):
    if params:
        url = url + ("&" if "?" in url else "?") + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": _user_agent(), **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        # 401/403: no key or not entitled on this plan; 429: rate limit.
        raise SourceUnavailable(f"HTTP {e.code}") from None
    except (urllib.error.URLError, TimeoutError, ValueError) as e:
        raise SourceUnavailable(type(e).__name__) from None


def ny_midnight_epoch(d):
    """The app's bar stamp: 00:00 America/New_York of trading date `d`."""
    if _NY is None:  # pragma: no cover
        raise SourceUnavailable("no tz database")
    return int(datetime(d.year, d.month, d.day, tzinfo=_NY).timestamp())


def vendor_symbol(sym, name):
    """Yahoo's notation → the vendor's: share classes are BRK-B at Yahoo,
    Tiingo and FMP, BRK.B at Polygon, Alpaca and Finnhub."""
    s = sym.upper()
    return s.replace("-", ".") if name in ("polygon", "alpaca", "finnhub") else s


def is_us_stock(sym):
    """Yahoo-only notation is not sent to a vendor: indices (^GSPC), futures
    (ES=F), FX (EURUSD=X), crypto (BTC-USD) and non-US listings (VOD.L)."""
    s = (sym or "").upper()
    if not s or "^" in s or "=" in s or s.endswith("-USD") or "." in s:
        return False
    return True


def _now_ny():
    return datetime.now(_NY)


def regular_session_open(now=None):
    now = now or _now_ny()
    if now.weekday() >= 5:
        return False
    minutes = now.hour * 60 + now.minute
    return 9 * 60 + 30 <= minutes < 16 * 60


def latest_session_date(now=None):
    """The most recent weekday whose regular session has begun — the date a
    complete-to-now series must reach (a market holiday makes it a date with
    no bar, and the request then falls back to Yahoo, which is right too)."""
    now = now or _now_ny()
    d = now.date()
    if now.weekday() >= 5 or now.hour * 60 + now.minute < 9 * 60 + 30:
        d -= timedelta(days=1)
    while d.weekday() >= 5:
        d -= timedelta(days=1)
    return d


def _num(v):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if f == f else None  # NaN → None


def _bar(d, o, h, l, c, v):
    o, h, l, c = _num(o), _num(h), _num(l), _num(c)
    if None in (o, h, l, c) or c <= 0:
        return None
    vol = _num(v)
    return {"timestamp": ny_midnight_epoch(d), "open": round(o, 4), "high": round(h, 4),
            "low": round(l, 4), "close": round(c, 4), "volume": int(vol) if vol is not None else 0}


def _iso_date(s):
    return date.fromisoformat(str(s)[:10])


def parse_ts(s):
    """RFC 3339 with any fraction length ("…59.123456789Z", "…38.18-05:00")
    → aware datetime. fromisoformat takes at most 6 fractional digits."""
    s = str(s).strip().replace("Z", "+00:00")
    tz = ""
    for sep in ("+", "-"):
        i = s.rfind(sep)
        if i > 10:  # past the date's own dashes
            s, tz = s[:i], s[i:]
            break
    if "." in s:
        head, frac = s.split(".", 1)
        s = head + "." + (frac + "000000")[:6]
    return datetime.fromisoformat(s + (tz or "+00:00"))


def resample(bars, interval):
    """Daily bars → weekly (stamped at the week's first session) or monthly."""
    if interval == "1d":
        return bars
    out, key = [], None
    for b in bars:
        d = datetime.fromtimestamp(b["timestamp"], _NY).date()
        k = (d.isocalendar()[0], d.isocalendar()[1]) if interval == "1wk" else (d.year, d.month)
        if k != key:
            out.append(dict(b))
            key = k
        else:
            cur = out[-1]
            cur["high"] = max(cur["high"], b["high"])
            cur["low"] = min(cur["low"], b["low"])
            cur["close"] = b["close"]
            cur["volume"] += b["volume"]
    return out


def period_to_range(period, today=None):
    """'6mo' / '1y' / 'ytd' / 'max' / 'range:A:B' → (start, end) dates."""
    today = today or date.today()
    if isinstance(period, str) and period.startswith("range:"):
        _, a, b = (period.split(":") + ["", ""])[:3]
        return _iso_date(a), _iso_date(b)
    # "max" is not here: free tiers cap history, and a capped series served
    # as "max" would look complete.
    days = {"1d": 5, "5d": 10, "1mo": 31, "3mo": 92, "6mo": 183, "1y": 366, "2y": 731,
            "5y": 1827, "10y": 3653}
    if period == "ytd":
        return date(today.year, 1, 1), today
    if period not in days:
        raise SourceUnavailable(f"period {period!r}")
    return today - timedelta(days=days[period]), today


# ── bars ─────────────────────────────────────────────────────────────────────

def _bars_tiingo(sym, start, end, basis):
    rows = _get(f"https://api.tiingo.com/tiingo/daily/{urllib.parse.quote(vendor_symbol(sym, 'tiingo').lower())}/prices",
                {"startDate": start.isoformat(), "endDate": end.isoformat(), **_keys("tiingo")})
    if not isinstance(rows, list):
        raise SourceUnavailable("unexpected answer")
    if basis == "total":
        return [b for b in (_bar(_iso_date(r["date"]), r.get("adjOpen"), r.get("adjHigh"), r.get("adjLow"),
                                  r.get("adjClose"), r.get("adjVolume")) for r in rows) if b]
    # Split-only: Tiingo gives raw prices and each day's splitFactor. Divide
    # every bar by the product of the splits that came AFTER it (and scale
    # volume up), which is what Yahoo's unadjusted series shows.
    out, factor = [], 1.0
    for r in reversed(rows):
        f = factor
        b = _bar(_iso_date(r["date"]), _scale(r.get("open"), f), _scale(r.get("high"), f),
                 _scale(r.get("low"), f), _scale(r.get("close"), f),
                 (_num(r.get("volume")) or 0) * f)
        if b:
            out.append(b)
        sf = _num(r.get("splitFactor")) or 1.0
        if sf > 0:
            factor *= sf  # a split on this day applies to every earlier bar
    return list(reversed(out))


def _scale(v, factor):
    n = _num(v)
    return n / factor if n is not None and factor else n


def _bars_polygon(sym, start, end, basis):
    # Polygon's adjusted=true covers splits only — exactly Yahoo's
    # "unadjusted" basis — and never dividends, so it can't serve "total".
    if basis != "split":
        raise SourceUnavailable("no dividend adjustment")
    data = _get(f"https://api.polygon.io/v2/aggs/ticker/{urllib.parse.quote(vendor_symbol(sym, 'polygon'))}/range/1/day/"
                f"{start.isoformat()}/{end.isoformat()}",
                {"adjusted": "true", "sort": "asc", "limit": 50000, **_keys("polygon")})
    rows = data.get("results") if isinstance(data, dict) else None
    if rows is None:
        raise SourceUnavailable(str((data or {}).get("status", "no results")))
    # `t` is the bar's start in Unix ms — midnight New York for a daily bar.
    return [b for b in (_bar(datetime.fromtimestamp(r["t"] / 1000, _NY).date(), r.get("o"), r.get("h"),
                              r.get("l"), r.get("c"), r.get("v")) for r in rows) if b]


def _alpaca_headers():
    k = _keys("alpaca")
    return {"APCA-API-KEY-ID": k["id"], "APCA-API-SECRET-KEY": k["secret"]}


def _bars_alpaca(sym, start, end, basis):
    out, token = [], None
    while True:
        # Alpaca reads a bare date as 00:00Z, before the day's bar (stamped
        # 04:00-05:00Z): the exclusive end is the day after.
        params = {"timeframe": "1Day", "start": start.isoformat(), "end": (end + timedelta(days=1)).isoformat(),
                  "adjustment": "all" if basis == "total" else "split", "feed": alpaca_feed(), "limit": 10000}
        if token:
            params["page_token"] = token
        data = _get(f"https://data.alpaca.markets/v2/stocks/{urllib.parse.quote(vendor_symbol(sym, 'alpaca'))}/bars",
                    params, _alpaca_headers())
        for r in (data or {}).get("bars") or []:
            b = _bar(parse_ts(r["t"]).astimezone(_NY).date(),
                     r.get("o"), r.get("h"), r.get("l"), r.get("c"), r.get("v"))
            if b:
                out.append(b)
        token = (data or {}).get("next_page_token")
        if not token:
            return out


def _bars_fmp(sym, start, end, basis):
    if basis != "total":
        raise SourceUnavailable("basis")
    rows = _get("https://financialmodelingprep.com/stable/historical-price-eod/dividend-adjusted",
                {"symbol": vendor_symbol(sym, "fmp"), "from": start.isoformat(), "to": end.isoformat(),
                 **_keys("fmp")})
    if not isinstance(rows, list):
        raise SourceUnavailable("unexpected answer")
    bars = [b for b in (_bar(_iso_date(r["date"]), r.get("adjOpen"), r.get("adjHigh"), r.get("adjLow"),
                              r.get("adjClose"), r.get("volume")) for r in rows) if b]
    return sorted(bars, key=lambda b: b["timestamp"])  # FMP answers newest first


_BARS = {"tiingo": _bars_tiingo, "polygon": _bars_polygon, "alpaca": _bars_alpaca, "alpaca_sip": _bars_alpaca,
         "fmp": _bars_fmp}


def daily_bars(symbol, start, end, interval="1d", adjusted=True, source=None, now=None):
    """Bars from the configured source in the daemon's payload shape, or
    SourceUnavailable. Daily/weekly/monthly only. `adjusted` follows Yahoo's
    auto_adjust: True = splits and dividends, False = splits only."""
    name = source or configured()
    if not name or name not in _BARS:
        raise SourceUnavailable("no source")
    if interval not in ("1d", "1wk", "1mo"):
        raise SourceUnavailable("intraday")
    if not is_us_stock(symbol):
        raise SourceUnavailable("not a US stock symbol")
    basis = "total" if adjusted else "split"
    if basis not in capabilities(name)["bars"]:
        raise SourceUnavailable("basis")
    bars = _BARS[name](symbol, start, end, basis)
    if not bars:
        raise SourceUnavailable("no bars")  # unknown symbol or empty window: let Yahoo try
    first = datetime.fromtimestamp(bars[0]["timestamp"], _NY).date()
    last = datetime.fromtimestamp(bars[-1]["timestamp"], _NY).date()
    # A free tier's history cap shows as a series starting long after the
    # start asked for: serve the full history from Yahoo instead.
    if (first - start).days > 10:
        raise SourceUnavailable(f"history starts {first}, asked {start}")
    # Complete to now: an end-of-day vendor has no bar yet for a session in
    # progress (or just closed), and a chart ending yesterday would disagree
    # with the live quote all day.
    wanted = min(end, latest_session_date(now))
    if last < wanted:
        raise SourceUnavailable(f"series ends {last}, latest session {wanted}")
    return resample(bars, interval)


# ── quotes ───────────────────────────────────────────────────────────────────

def _quote(sym, price, prev, volume=None, ts=None, src=""):
    price, prev = _num(price), _num(prev)
    if price is None or price <= 0:
        return None
    change = price - prev if prev else None
    return {"symbol": sym, "price": round(price, 4),
            "change": round(change, 4) if change is not None else None,
            "change_percent": round(change / prev * 100, 2) if change is not None else None,
            "previous_close": round(prev, 4) if prev else None,
            "volume": int(_num(volume)) if _num(volume) is not None else None,
            "high": None, "low": None, "open": None,
            "timestamp": int(ts or datetime.now(timezone.utc).timestamp()), "exchange": "", "source": src}


def _quotes_tiingo(syms):
    rows = _get("https://api.tiingo.com/iex/", {"tickers": ",".join(vendor_symbol(s, "tiingo").lower() for s in syms),
                                                 **_keys("tiingo")})
    out = {}
    for r in rows if isinstance(rows, list) else []:
        sym = str(r.get("ticker", "")).upper()
        ts = r.get("timestamp")
        epoch = parse_ts(ts).timestamp() if ts else None
        # IEX volume is one venue's: left out rather than shown as the stock's.
        q = _quote(sym, r.get("tngoLast") or r.get("last"), r.get("prevClose"), None, epoch, "tiingo")
        if q:
            out[sym] = q
    return out


def _quotes_polygon(syms):
    data = _get("https://api.polygon.io/v2/snapshot/locale/us/markets/stocks/tickers",
                {"tickers": ",".join(vendor_symbol(s, "polygon") for s in syms), **_keys("polygon")})
    out = {}
    for r in (data or {}).get("tickers") or []:
        sym = str(r.get("ticker", "")).replace(".", "-")
        last = (r.get("lastTrade") or {}).get("p") or (r.get("day") or {}).get("c")
        q = _quote(sym, last, (r.get("prevDay") or {}).get("c"), (r.get("day") or {}).get("v"),
                   (r.get("updated") or 0) / 1e9 or None, "polygon")
        if q:
            out[sym] = q
    return out


def _quotes_alpaca(syms):
    data = _get("https://data.alpaca.markets/v2/stocks/snapshots",
                {"symbols": ",".join(vendor_symbol(s, "alpaca") for s in syms), "feed": alpaca_feed()},
                _alpaca_headers())
    out = {}
    consolidated = alpaca_feed() == "sip"
    for vsym, r in (data or {}).items():
        if not isinstance(r, dict):
            continue
        sym = vsym.replace(".", "-")
        trade = r.get("latestTrade") or {}
        ts = trade.get("t")
        epoch = parse_ts(ts).timestamp() if ts else None
        q = _quote(sym, trade.get("p"), (r.get("prevDailyBar") or {}).get("c"),
                   (r.get("dailyBar") or {}).get("v") if consolidated else None, epoch, "alpaca")
        if q:
            out[sym] = q
    return out


FINNHUB_MAX_SYMBOLS = 30  # one call each; the free tier allows 60 a minute


def _quotes_finnhub(syms):
    out = {}
    for sym in syms[:FINNHUB_MAX_SYMBOLS]:
        try:
            r = _get("https://finnhub.io/api/v1/quote", {"symbol": vendor_symbol(sym, "finnhub"), **_keys("finnhub")})
        except SourceUnavailable as e:
            if "429" in str(e):
                break  # rate-limited: keep what we have, Yahoo prices the rest
            continue  # this symbol only
        q = _quote(sym.upper(), (r or {}).get("c"), (r or {}).get("pc"), None, (r or {}).get("t"), "finnhub")
        if q:
            out[sym.upper()] = q
    return out


def _quotes_fmp(syms):
    rows = _get("https://financialmodelingprep.com/stable/batch-quote",
                {"symbols": ",".join(vendor_symbol(s, "fmp") for s in syms), **_keys("fmp")})
    out = {}
    for r in rows if isinstance(rows, list) else []:
        sym = str(r.get("symbol", "")).upper()
        q = _quote(sym, r.get("price"), r.get("previousClose"), r.get("volume"), r.get("timestamp"), "fmp")
        if q:
            out[sym] = q
    return out


_QUOTES = {"tiingo": _quotes_tiingo, "polygon": _quotes_polygon, "alpaca": _quotes_alpaca,
           "alpaca_sip": _quotes_alpaca, "finnhub": _quotes_finnhub, "fmp": _quotes_fmp}


def quotes(symbols, source=None, now=None):
    """{SYMBOL: quote} for the symbols the source could price; the caller
    asks Yahoo for the rest. SourceUnavailable when it priced none."""
    name = source or configured()
    if not name or name not in _QUOTES:
        raise SourceUnavailable("no source")
    # A vendor's last trade outside the regular session is an extended-hours
    # print: the app shows that separately (AFT%), from Yahoo.
    if not regular_session_open(now):
        raise SourceUnavailable("outside regular hours")
    stocks = [s for s in symbols if is_us_stock(s)]
    if not stocks:
        raise SourceUnavailable("no stock symbols")
    got = _QUOTES[name](stocks)
    if not got:
        raise SourceUnavailable("no quotes")
    return got
