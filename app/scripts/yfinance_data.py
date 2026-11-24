"""
YFinance Data Fetcher
Fetches real-time stock quotes and historical data using yfinance
Returns JSON output for Qt/C++ integration
"""

import sys
import json
import math
import os
import re
import threading
import time
import heapq
import concurrent.futures
import yfinance as yf
import pandas as pd
from datetime import date, datetime, timedelta

# ── yfinance timezone-cache hardening ─────────────────────────────────────────
# yfinance caches each ticker's exchange timezone in an on-disk SQLite file
# (~/.cache/py-yfinance/tkr-tz.db via peewee). Resolving the timezone is a hard
# prerequisite for EVERY price/history download, and yfinance treats the cache
# as mandatory once its folder exists: when the DB file itself can't be opened
# it raises peewee.OperationalError('unable to open database file') straight out
# of Ticker.history() (yfinance/base.py:_get_ticker_tz), which the daemon turns
# into an empty result. In a long-lived daemon this surfaces after many hours /
# overnight — under memory pressure, or if anything removes or locks the db file
# — and then EVERY symbol reports "possibly delisted; no price data" until the
# process is restarted (portfolio perf chart goes flat, equity-research overview
# empties, sparklines blank, …).
#
# The on-disk cache is only a latency optimisation. Replace it with a process-
# local in-memory cache: timezones are still fetched live (real data) and reused
# for the daemon's lifetime, but there is no SQLite file left to fail, so the
# whole failure class disappears. A restart simply re-warms the in-memory map.
# Dict get/set/pop are atomic under the GIL, so it is safe for the daemon's
# worker threads to share one instance without locking.
class _InMemoryTzCache:
    """Drop-in for yfinance's _TzCache with no on-disk SQLite dependency."""
    def __init__(self):
        self._tz = {}
    def lookup(self, tkr):
        return self._tz.get(tkr)
    def store(self, tkr, tz):
        if tz is None:
            self._tz.pop(tkr, None)
        else:
            self._tz[tkr] = tz
    @property
    def tz_db(self):
        return None

def _install_inmemory_tz_cache():
    try:
        from yfinance import cache as _yf_cache
        _yf_cache._TzCacheManager._tz_cache = _InMemoryTzCache()
    except Exception:
        # Best-effort: if yfinance internals move in a future version, fall back
        # to the stock on-disk cache rather than failing to start the daemon.
        pass

_install_inmemory_tz_cache()

# ── yfinance download() serialisation ─────────────────────────────────────────
# yfinance.download() is NOT thread-safe. It parks every ticker's frame in
# MODULE-GLOBAL dicts (yfinance.shared._DFS / _ERRORS / _TRACEBACKS / _ISINS),
# clears them at the top of each call, and at the bottom rebuilds the result
# with `pd.concat(_DFS.values(), keys=_DFS.keys())`. This daemon dispatches
# actions from a 6-thread pool, so two downloads overlap routinely — the
# 20-second DataHub `batch_all` tick alone lands on top of anything else in
# flight. Three things then go wrong, in increasing order of nastiness:
#
#   1. `for tkr in shared._DFS.keys()` blows up with "dictionary changed size
#      during iteration" and the whole action fails. (Seen in production.)
#   2. `while len(_DFS) < len(tickers)` is satisfied by the OTHER call's
#      tickers, so a download returns before its own symbols have arrived.
#   3. `.values()` and `.keys()` are two separate reads of a dict that is being
#      mutated between them, so the concat can label one symbol's bars with
#      another symbol's ticker. That one is SILENT: the portfolio AFT column
#      renders a foreign stock's after-hours move on a row that says MSFT.
#
# Serialising download() against itself fixes all three — but it is not the
# only writer. Ticker.history() sets shared._DFS / _ERRORS on its FAILURE paths
# (scrapers/history.py: missing timezone, invalid period, dead symbol), and a
# download polls `len(_DFS)` and iterates `_DFS.keys()` for its whole duration.
# One unrelated history() call failing at the wrong moment is enough to break a
# download that has nothing to do with it — and `_retry_one` in the
# extended-hours path calls history() for precisely the symbols that just
# failed, so those two collide by construction.
#
# A plain lock around history() would DEADLOCK: download() fetches by calling
# Ticker.history() on its own worker threads (multi._download_one), which would
# then block on the lock their parent is holding. So the gate is a
# readers-writer one — any number of history() calls run together, a download()
# excludes them all — and download's own workers are exempt, identified by
# wrapping multi._download_one to record the thread it runs on.
#
# Every wait is bounded. If a future yfinance stops routing its fetches through
# _download_one, the exemption stops working and the gate must degrade to
# today's unsynchronised behaviour rather than hang the daemon.
_DOWNLOAD_LOCK = threading.RLock()
_GATE_WAIT_SEC = 15.0

_gate_cv = threading.Condition()
_gate_download_running = False
_gate_history_inflight = 0
_gate_download_threads = set()   # thread ids currently inside multi._download_one


def _gate_enter_download():
    """Wait for in-flight standalone history() calls to drain, then close the gate."""
    global _gate_download_running
    with _gate_cv:
        _gate_download_running = True
        deadline = time.monotonic() + _GATE_WAIT_SEC
        while _gate_history_inflight:
            if not _gate_cv.wait(timeout=max(0.0, deadline - time.monotonic())):
                break   # bounded: proceed unsynchronised rather than stall


def _gate_leave_download():
    global _gate_download_running
    with _gate_cv:
        _gate_download_running = False
        _gate_cv.notify_all()


def _gate_enter_history():
    """Yield to a download in progress, then register as in-flight."""
    global _gate_history_inflight
    with _gate_cv:
        deadline = time.monotonic() + _GATE_WAIT_SEC
        while _gate_download_running:
            if not _gate_cv.wait(timeout=max(0.0, deadline - time.monotonic())):
                break
        _gate_history_inflight += 1


def _gate_leave_history():
    global _gate_history_inflight
    with _gate_cv:
        _gate_history_inflight -= 1
        if not _gate_history_inflight:
            _gate_cv.notify_all()


def _install_download_serializer():
    """Gate yfinance's shared-state writers, process-wide.

    Patched at the modules yfinance re-exports from, so every caller is
    covered — including helper modules (exchange_sessions) that import
    yfinance themselves and would otherwise race the daemon's own downloads.

    The history() wrapper is installed ONLY if _download_one could be wrapped
    too: without the worker exemption it would make every download wait out the
    gate timeout, which is worse than the race it prevents.
    """
    try:
        import functools
        import yfinance as _y
        from yfinance import multi as _multi
        # history() lives on TickerBase, not Ticker — patch it there so every
        # subclass (Ticker, and whatever Tickers builds) goes through the gate.
        from yfinance import base as _base

        orig_one = _multi._download_one
        if not getattr(orig_one, "_finterm_gated", False):
            @functools.wraps(orig_one)
            def _download_one_marked(*args, **kwargs):
                tid = threading.get_ident()
                _gate_download_threads.add(tid)
                try:
                    return orig_one(*args, **kwargs)
                finally:
                    _gate_download_threads.discard(tid)

            _download_one_marked._finterm_gated = True
            _multi._download_one = _download_one_marked

        orig_hist = _base.TickerBase.history
        if not getattr(orig_hist, "_finterm_gated", False):
            @functools.wraps(orig_hist)
            def _history_gated(self, *args, **kwargs):
                if threading.get_ident() in _gate_download_threads:
                    return orig_hist(self, *args, **kwargs)   # download's own worker
                _gate_enter_history()
                try:
                    return orig_hist(self, *args, **kwargs)
                finally:
                    _gate_leave_history()

            _history_gated._finterm_gated = True
            _base.TickerBase.history = _history_gated

        orig = _multi.download
        if getattr(orig, "_finterm_serialized", False):
            return

        from yfinance.data import YfData as _YfData

        @functools.wraps(orig)
        def _serialized(*args, **kwargs):
            # Hand download the LIVE session. Given none, it builds a fresh
            # curl session per call and installs it into the YfData singleton
            # (multi.py `YfData(session=session)`), which drops the cookie the
            # cached crumb was minted for: every .info after a batch quote
            # then 401'd "Invalid Crumb" (caught by the [yf-auth] probe,
            # 2026-10-20). _install_session_pin is the backstop.
            kwargs.setdefault("session", _YfData()._session)
            with _DOWNLOAD_LOCK:
                _gate_enter_download()
                try:
                    return orig(*args, **kwargs)
                finally:
                    _gate_leave_download()

        _serialized._finterm_serialized = True
        _multi.download = _serialized
        _y.download = _serialized
    except Exception:
        # Best-effort, same as the tz cache above: a yfinance layout change
        # must not stop the daemon from starting.
        pass


_install_download_serializer()

# ── yfinance crumb/session state ──────────────────────────────────────────────
# HTTP 401 "Invalid Crumb" means the request's crumb does not match its cookie
# (verified 2026-10-19: another session's crumb, a garbage crumb and no crumb
# all return exactly this; new sessions on the same IP do NOT invalidate an
# old one). What changed the cookie under the crumb was yf.download() swapping
# the singleton's session (caught by _install_auth_probe 2026-10-20) — fixed
# in _install_download_serializer, backstopped by _install_session_pin; the
# probe stays to catch any other cause.
#
# NOTE _reset_yfinance_session does NOT reset the crumb: it lives on the
# yfinance.data.YfData singleton, which reload(yf) leaves in place (verified).
# And it rarely runs at all — yfinance logs a 401 and returns None rather than
# raising, so _daemon_dispatch_with_crumb_retry never sees an exception.

_CRUMB_ERROR_PATTERNS = ("invalid crumb", "unauthorized", "401")

def _is_crumb_error(exc_or_msg: str) -> bool:
    s = str(exc_or_msg).lower()
    return any(p in s for p in _CRUMB_ERROR_PATTERNS)

def _reset_yfinance_session() -> None:
    """Best-effort crumb reset — tries several yfinance internals in order."""
    try:
        # yfinance 0.2.x: clear the module-level requests-cache session
        import importlib
        importlib.reload(yf)
    except Exception:
        pass
    # reload() re-executes yfinance/__init__.py, which rebinds the names we
    # patched at import time. Both installers are idempotent, so just re-run
    # them: without this a single crumb rotation would silently drop the
    # download lock for the rest of the daemon's life.
    _install_download_serializer()
    _install_inmemory_tz_cache()

# ── Auth probe + per-thread HTTP outcome ─────────────────────────────────────
# Every Yahoo request yfinance makes passes through YfData._make_request. The
# wrapper records each response's status for the calling thread (so a lookup
# can tell a transient refusal from an answer) and, on a 401 or on a response
# that changes the session's cookie, writes one line of evidence to stderr
# (the app log): what the request sent vs what the session holds now. Hashes
# only — never a cookie or crumb value.

_http_trace = threading.local()
# Statuses that say nothing about the symbol: auth, rate limit, server trouble.
_TRANSIENT_HTTP = {401, 403, 408, 429, 500, 502, 503, 504}
_NO_ANSWER = -1   # a request that raised before any response (recorded in order)


def _trace_reset():
    _http_trace.statuses = []
    _http_trace.net_error = False


def _trace_transient():
    """True when a request on this thread since _trace_reset was refused or
    never answered — the lookup's outcome is not a fact about the symbol."""
    st = getattr(_http_trace, "statuses", [])
    return getattr(_http_trace, "net_error", False) or any(c in _TRANSIENT_HTTP for c in st)


def _download_refusals(symbols, dl_errors):
    """{caller's spelling: error} for the tickers a yf.download was refused
    for, read from its shared._ERRORS snapshot. Keyed by EVERY spelling a
    caller asked with (yfinance uppercases its keys; a merged request can
    carry "brk-b" and "BRK-B"); only tickers THIS download asked for (a
    concurrent standalone history() can write into the shared dict).
    ("no timezone found" is NOT read as a refusal: yfinance says the same for
    a ticker Yahoo doesn't know, and a chunk of delisted tickers would then
    "refuse" — and pause every background refresh — on every ask.)"""
    asked = {}
    for sym in symbols:
        asked.setdefault(sym.upper(), []).append(sym)
    mine = {str(k).upper(): str(e) for k, e in dl_errors.items() if str(k).upper() in asked}
    refused = {}
    for k, e in mine.items():
        if _is_refusal_text(e):
            for spelling in asked[k]:
                refused[spelling] = e
    return refused


def _is_refusal_text(msg):
    """An error message that says Yahoo refused or never answered (as opposed
    to "no such ticker" / "no data for this period")."""
    m = msg.lower()
    if any(k in m for k in ("ratelimit", "too many requests", "unauthorized", "invalid crumb",
                            "timed out", "timeout", "connection", "could not resolve",
                            "failed to connect", "couldn't connect", "server error",
                            "service unavailable", "bad gateway", "gateway time", "forbidden")):
        return True
    # Status codes as whole words only — a timestamp or ticker can contain 401.
    return re.search(r"\b(?:http(?: error)? ?)(401|403|429|5\d\d)\b", m) is not None


def _trace_last_refused():
    """True when the LAST data request on this thread was refused or
    unanswered — the one an empty answer came from. An earlier 401 or network
    error that yfinance recovered from does not make a real empty answer a
    refusal. (Crumb/cookie fetches use the session directly and are not
    traced, so they cannot mask the data request.)"""
    st = getattr(_http_trace, "statuses", [])
    return bool(st) and (st[-1] in _TRANSIENT_HTTP or st[-1] == _NO_ANSWER)


def _trace_refusal_text():
    """What the last request on this thread got: 'HTTP 429', 'no answer' or
    'refused' — the one the failure came from, not a status yfinance had
    already recovered from."""
    st = getattr(_http_trace, "statuses", [])
    if st and st[-1] in _TRANSIENT_HTTP:
        return f"HTTP {st[-1]}"
    return "no answer" if st and st[-1] == _NO_ANSWER else "refused"


def _h(v):
    if not v:
        return "-"
    import hashlib
    return hashlib.sha1(str(v).encode()).hexdigest()[:8]


def _session_a3(yd):
    try:
        for c in yd._session.cookies.jar:
            if c.name == "A3":
                return c.value
    except Exception:
        pass
    return None


def _install_auth_probe():
    try:
        from yfinance.data import YfData
    except Exception:
        return
    orig = YfData._make_request
    if getattr(orig, "_finterm_probe", False):
        return

    # *args/**kwargs: the signature differs across yfinance versions (1.x
    # passes data= from post()).
    def _make_request(self, url, request_method, *args, **kwargs):
        def traced(**kw):
            crumb_sent = (kw.get("params") or {}).get("crumb")
            cookie_sent = _session_a3(self)
            strategy = self._cookie_strategy
            r = request_method(**kw)
            cookie_now = _session_a3(self)
            if r.status_code == 401 or cookie_now != cookie_sent:
                try:
                    set_cookie = "A3" if "A3=" in (r.headers.get("set-cookie") or "") else "-"
                except Exception:
                    set_cookie = "?"
                print(f"[yf-auth] {r.status_code} {url.split('?')[0].rsplit('/', 3)[-2]} "
                      f"thread={threading.current_thread().name} strategy={strategy}->{self._cookie_strategy} "
                      f"crumb sent={_h(crumb_sent)} now={_h(self._crumb)} "
                      f"cookie sent={_h(cookie_sent)} now={_h(cookie_now)} set-cookie={set_cookie}",
                      file=sys.stderr, flush=True)
            return r
        try:
            r = orig(self, url, traced, *args, **kwargs)
        except Exception as e:
            _http_trace.net_error = True
            st = getattr(_http_trace, "statuses", None)
            if st is not None:
                # In order, so "the last request" can see it. yfinance raises
                # YFRateLimitError for a 429 before we see the response.
                st.append(429 if "RateLimit" in type(e).__name__ else _NO_ANSWER)
            raise
        # Only the request's FINAL status counts: yfinance retries any >= 400
        # once with the other cookie strategy, and a 401 the retry recovered
        # is an answer.
        st = getattr(_http_trace, "statuses", None)
        if st is not None:
            st.append(r.status_code)
        return r

    _make_request._finterm_probe = True
    YfData._make_request = _make_request

    # cache_get is lru_cache'd by URL for the process's life, and keeps
    # whatever came back — a 401 included. .info's key-statistics request goes
    # through it with a URL that changes once a day, so one refused request
    # failed every later .info for that symbol until midnight or a restart
    # (verified 2026-10-19). Replaced by the same bounded LRU that simply
    # never STORES a refusal (clearing the lru on one would drop every good
    # entry, re-fetching them exactly while Yahoo is refusing).
    if not getattr(YfData.cache_get, "_finterm_probe", False):
        import collections
        store = collections.OrderedDict()
        store_lock = threading.Lock()

        # An entry lives 60 seconds. yfinance's own lru had no clock at all,
        # and neither did this: the earnings calendar is fetched through a URL
        # that never changes, so a reported EPS or a moved report date stayed
        # frozen for as long as the daemon ran, and statements (a URL that
        # changes once a UTC day) hid a new quarter until midnight. The TTLs
        # in _CACHE_TTL are the freshness contract; this layer only spares
        # yfinance repeating one request inside a single call.
        YF_GET_TTL_S = 60.0

        def cache_get(self, url, params=None, timeout=30):
            key = (url, json.dumps(params, sort_keys=True, default=str))
            now = time.monotonic()
            with store_lock:
                hit = store.get(key)
                if hit is not None:
                    if now - hit[0] < YF_GET_TTL_S:
                        store.move_to_end(key)
                        return hit[1]
                    del store[key]
            r = self.get(url, params=params, timeout=timeout)
            if getattr(r, "status_code", 200) not in _TRANSIENT_HTTP:
                with store_lock:
                    store[key] = (now, r)
                    while len(store) > 64:  # yfinance's own lru maxsize
                        store.popitem(last=False)
            return r

        cache_get._finterm_probe = True
        YfData.cache_get = cache_get


_install_auth_probe()


def _install_session_pin():
    """Keep YfData's first session for the process's life.

    A crumb is valid only with the cookie it was minted against, and
    _set_session swaps the cookie jar while keeping the crumb — so ANY
    replacement turns the next crumbed request into a 401. yf.download() was
    the one caller doing it (fixed at the source in the download serializer);
    this drops whatever else tries, and says so each time, because the daemon
    never passes a session of its own and a drop now means a new cause."""
    try:
        from yfinance.data import YfData
    except Exception:
        return
    orig = YfData._set_session
    if getattr(orig, "_finterm_pinned", False):
        return

    def _set_session(self, session):
        live = getattr(self, "_session", None)
        if live is None:
            return orig(self, session)
        if session is not None and session is not live:
            print(f"[yf-auth] replacement session ignored (crumb belongs to the live one) "
                  f"thread={threading.current_thread().name}", file=sys.stderr, flush=True)

    _set_session.__wrapped__ = orig
    _set_session._finterm_pinned = True
    YfData._set_session = _set_session


_install_session_pin()


_NASDAQ_INFO_URL = "https://api.nasdaq.com/api/quote/{}/info?assetclass={}"
_US_TICKER = re.compile(r"^[A-Z]{1,5}(-[A-Z]{1,2})?$")
_book_asset = {}   # symbol -> "stocks" | "etf": the class Nasdaq answered for it


def _nasdaq_money(v):
    """Nasdaq's "$352.85" / "352.85" / "N/A" -> 352.85 or 0 (no quote)."""
    try:
        x = float(str(v).replace("$", "").replace(",", "").strip())
    except (TypeError, ValueError):
        return 0.0
    return x if x > 0 else 0.0


def get_orderbook(symbol):
    """The best bid and ask (+ sizes) for one US-listed symbol, from Nasdaq's
    real-time quote — the one free source whose book is current.

    Not Yahoo: its `bid`/`ask` (quoteSummary `info` and the v7 quote alike) sit
    frozen for hours on Nasdaq-listed names and need not bracket the price at
    all (2026-11-18 12:10 ET: AAPL 351.02/355.98 unchanged across samples with
    the last at 352.4; NVDA 151.00/160.00 with the last at 151.8), while
    Nasdaq's answered AAPL 352.85/352.88 at a 352.85 last. fast_info carries
    none at all.

    Zeros mean no book to show — a listing Nasdaq does not quote (foreign,
    futures, FX, crypto), a refusal, or a side with nothing on it; consumers
    show a dash, never $0.00."""
    empty = {"symbol": symbol or "", "bid": 0, "ask": 0, "bid_size": 0, "ask_size": 0}
    sym = (symbol or "").strip().upper()
    if not _US_TICKER.match(sym):
        return empty
    import requests
    import ipo_calendar as ic   # NASDAQ_HEADERS: the browser-like headers Nasdaq's API wants
    for asset in ([_book_asset[sym]] if sym in _book_asset else ["stocks", "etf"]):
        try:
            r = requests.get(_NASDAQ_INFO_URL.format(sym.replace("-", "."), asset),
                             headers=ic.NASDAQ_HEADERS, timeout=(3, 6))
            r.raise_for_status()
            data = (r.json() or {}).get("data") or {}
        except Exception:
            continue
        p = data.get("primaryData") or {}
        bid, ask = _nasdaq_money(p.get("bidPrice")), _nasdaq_money(p.get("askPrice"))
        if not (bid or ask):
            continue
        _book_asset[sym] = asset
        # A crossed book is not a quote to show.
        if bid and ask and bid > ask:
            return empty
        return {
            "symbol": symbol,
            "bid": _round_price(bid) if bid else 0,
            "ask": _round_price(ask) if ask else 0,
            "bid_size": _nasdaq_money(p.get("bidSize")),
            "ask_size": _nasdaq_money(p.get("askSize")),
            "timestamp": int(datetime.now().timestamp()),
        }
    return empty

def get_quote(symbol):
    """Fetch real-time quote for a single symbol.

    Prefers ``ticker.fast_info`` (lightweight modern endpoint, ~100-200ms)
    over the heavy ``ticker.info`` + ``ticker.history(1d)`` combination
    (~500ms-1s with a full Yahoo summary-page scrape). The fast path covers
    all fields the C++ side parses (price, change, OHLC, volume, exchange).

    Falls back to the legacy ``.info``/``.history`` path when:
      • fast_info raises (some yfinance versions / asset classes don't
        populate it: certain crypto, FX, mutual funds);
      • fast_info returns no price (delisted / data hole).

    Wall-time savings matter most for the 20s refresh tick — historically
    every refresh did a full info scrape just to compute the price delta.
    """
    try:
        fast = _quote_via_fast_info(symbol)
        if fast is not None:
            return fast
    except Exception:
        # fall through to heavy path
        pass
    return _quote_via_full_info(symbol)


def _price_dp(value):
    """Decimal places that preserve a price's information.

    Two decimals is right for equities and destroys anything quoted below
    ~1.00 — and that is not a rare corner: FX pairs (JPYUSD=X ~ 0.0064 →
    0.01, a 56% error; KRWUSD=X ~ 0.00072 → 0.0, which downstream reads as
    "no rate" and silently drops the conversion), sub-cent crypto, penny
    stocks, and every change and spread derived from them.

    Scale precision to the magnitude rather than guessing from the ticker:
    roughly six significant figures below 100, and the two decimals
    everything already expects for prices from 100 up.

    1-100 needs five decimals too: EURUSD=X at 1.105021 rounded to 2dp is
    1.11 — the Markets WORLD board showed EUR/USD 1.1100 and, against an
    unrounded reference, +0.42% on a day it fell 0.29% (2026-11-14). GBP/USD,
    USD/CNH and every other pair between 1 and 100 had the same error.
    """
    try:
        v = abs(float(value))
    except (TypeError, ValueError):
        return 2
    if v >= 100.0 or v == 0.0:
        return 2
    if v >= 1.0:
        return 5
    if v >= 0.01:
        return 6
    return 10


def _round_price(value, symbol=None):
    """Round a price without destroying it. `symbol` is accepted so call
    sites read naturally; the precision follows the value, not the ticker."""
    try:
        return round(float(value), _price_dp(value))
    except (TypeError, ValueError):
        return value


def _pct_to_ratio(value):
    """A Yahoo percentage as a plain ratio, or None when absent.

    Yahoo is inconsistent about which fields carry a percentage and which
    carry a fraction, and it is not inferable from the value — so each field
    is converted explicitly at its own call site rather than by a heuristic.
    Verified against cached data before applying: dividendYield and
    debtToEquity are percentages; margins, ROE, ROA, growth, insider and
    institutional holdings and short-percent are already fractions and must
    NOT be run through this.
    """
    if value is None:
        return None
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    import math as _m
    if not _m.isfinite(f):
        return None
    return f / 100.0


def _dividend_yield_fraction(value):
    """Yahoo's dividendYield as a FRACTION, which is this app's contract.

    Yahoo changed this field to a PERCENTAGE in early 2025 (0.73 means 0.73%),
    and yfinance passes quoteSummary through untouched — verified against both
    pinned versions, 0.2.66 and 1.3.0, neither of which mentions the field.
    Every C++ consumer still multiplies by 100 to display it (fmt_pct, the
    portfolio heatmap, the peers table, the knowledge panels), so the raw value
    rendered as 100x its true size: MSFT showed a 73.00% dividend yield.

    Checked against 231 cached info payloads — AAPL 0.33, MSFT 0.73, AVGO 0.70,
    HD 2.75, LMT 2.53, TXT 0.08 — each matching that ticker's real percentage
    yield to two decimals. There is no reading of those numbers as fractions.

    Converted here, at the one boundary the value enters, rather than at the
    eight display sites that would each have to remember.
    """
    if value is None:
        return None
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    import math as _m
    if not _m.isfinite(f):
        return None
    return f / 100.0


def _has_prices(*values):
    """True only if every value is a real number — present and finite.

    Deliberately NOT a positivity test. Zero and negative prints are real
    market data: ^IRX (13-week bill) closed at 0.000 for long stretches of
    2020-2021 and CL=F settled at -37.63 on 2020-04-20, and both are on this
    app's own watchlists. Dropping those bars would delete history rather
    than repair it.

    What this rejects is the row yfinance emits for a session it has no
    prices for yet — typically today's bar before the feed fills it in, all
    four OHLC fields NaN. Volume is usually present on that row, so it looks
    like a bar and isn't one.

    Such a row must never leave the daemon. _sanitize_for_json turns each NaN
    into JSON null (it has to — bare NaN is not valid JSON and Qt rejects the
    whole document), and on the C++ side QJsonValue::toDouble() reads null
    back as 0.0 without complaint. "No price" then silently becomes "priced
    at zero": one such bar drags a chart's y-range low to 0, and a log axis
    spanning 1e-9…hi flattens every real candle into the top 1% of the plot.
    Dropping the row here is the only place that fixes every consumer at
    once — charts, technicals and TALIpp each read this payload by a
    different route.
    """
    import math as _m
    for v in values:
        if v is None:
            return False
        try:
            f = float(v)
        except (TypeError, ValueError):
            return False
        if not _m.isfinite(f):
            return False
    return True


def _quote_via_fast_info(symbol):
    """Quote via ticker.fast_info. Returns None if fast_info doesn't have a
    usable price (caller should fall back). Raises on yfinance errors."""
    ticker = yf.Ticker(symbol)
    fi = ticker.fast_info  # AttributeError if not supported (very old yfinance)

    # last_price falls back to regularMarketPrice internally; if both are
    # missing we don't have a quote.
    last_price = getattr(fi, "last_price", None)
    if last_price is None or (isinstance(last_price, float) and last_price != last_price):
        return None

    current = float(last_price)
    # PREVIOUS CLOSE — one definition across the whole app: the official
    # REGULAR-MARKET previous close, which is what every other terminal
    # quotes and what a user cross-checking against Bloomberg or Yahoo will
    # see.
    #
    # fast_info.previous_close is NOT that: yfinance derives it from a
    # 1-week/1-hour PREPOST series grouped by date, so for any name with
    # active extended-hours trading it is the prior day's last after-hours
    # print. Change and change% computed from it silently disagree with
    # every other source, and with this app's own dashboard.
    # NaN is a real return value here, not a defensive hypothetical:
    # regular_market_previous_close is Close.iloc[-2] of a keepna=True history,
    # so a retained null row (holiday, halt, thin listing) yields NaN — and
    # `NaN is None` is False, so an `is None` check would sail past it and
    # leave change/change% as NaN. Those serialize to null and arrive in C++ as
    # 0.0, i.e. a real price beside a confidently flat "0.00 / 0.00%".
    # (The old previous_close path came from a history() without keepna, which
    # dropped null rows, so this failure mode is new with the switch.)
    def _first_real(*names):
        for n in names:
            v = getattr(fi, n, None)
            if v is None:
                continue
            try:
                v = float(v)
            except (TypeError, ValueError):
                continue
            # isfinite, not `> 0`. A previous close can legitimately be
            # NEGATIVE: WTI (CL=F, quoted on the dashboard) settled at -$37.63
            # on 2020-04-20. Rejecting it would fall through to prev_close =
            # current and print a flat 0.00 / 0.00% beside a real price —
            # reintroducing the exact bug this guard exists to prevent, on the
            # one day of the decade anyone was staring at the tape. isfinite
            # also rejects ±inf, which `> 0` happily accepts.
            if math.isfinite(v) and v != 0:
                return v
        return None

    prev_close_raw = _first_real("regular_market_previous_close", "previous_close")
    # No real previous close → change/change_percent are unknown (None), never
    # a fabricated flat 0.00 / 0.00% computed against the current price.
    prev_close = prev_close_raw
    change = (current - prev_close) if prev_close is not None else None
    pct = (change / prev_close * 100.0) if prev_close else None

    def _f(name):
        v = getattr(fi, name, None)
        if v is None: return None
        try:
            v = float(v)
            return _round_price(v)
        except (TypeError, ValueError):
            return None

    def _i(name):
        v = getattr(fi, name, None)
        if v is None: return None
        try: return int(v)
        except (TypeError, ValueError): return None

    return {
        "symbol":          symbol,
        "price":           _round_price(current, symbol),
        "change":          _round_price(change, symbol) if change is not None else None,
        "change_percent":  round(pct, 2) if pct is not None else None,
        "volume":          _i("last_volume"),
        "high":            _f("day_high"),
        "low":             _f("day_low"),
        "open":            _f("open"),
        "previous_close":  _round_price(prev_close, symbol) if prev_close is not None else None,
        "timestamp":       int(datetime.now().timestamp()),
        "exchange":        getattr(fi, "exchange", "") or "",
    }


def _quote_via_full_info(symbol):
    """Legacy quote path — used as a fallback when fast_info isn't usable."""
    try:
        import io, contextlib
        ticker = yf.Ticker(symbol)
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            info = ticker.info
            hist = ticker.history(period="1d")

        if hist.empty:
            return {"error": "No data available", "symbol": symbol}

        current_price = hist['Close'].iloc[-1]
        previous_close = info.get('previousClose')
        try:
            previous_close = float(previous_close) if previous_close is not None else None
        except (TypeError, ValueError):
            previous_close = None
        if previous_close is not None and (not math.isfinite(previous_close) or previous_close == 0):
            previous_close = None
        # Missing previous close → change unknown (None), not 0 vs current.
        change = (current_price - previous_close) if previous_close is not None else None
        change_percent = (change / previous_close) * 100 if previous_close else None

        quote_data = {
            "symbol": symbol,
            "price": _round_price(current_price),
            "change": _round_price(change) if change is not None else None,
            "change_percent": round(float(change_percent), 2) if change_percent is not None else None,
            "volume": int(hist['Volume'].iloc[-1]) if not hist['Volume'].empty and not pd.isna(hist['Volume'].iloc[-1]) else None,
            "high": _round_price(hist['High'].iloc[-1]) if not hist['High'].empty else None,
            "low": _round_price(hist['Low'].iloc[-1]) if not hist['Low'].empty else None,
            "open": _round_price(hist['Open'].iloc[-1]) if not hist['Open'].empty else None,
            "previous_close": _round_price(previous_close) if previous_close is not None else None,
            "timestamp": int(datetime.now().timestamp()),
            "exchange": info.get('exchange', '')
        }

        return quote_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

def get_historical(symbol, start_date, end_date, interval='1d'):
    """Fetch historical data for a symbol with specified interval

    Args:
        symbol: Stock symbol (e.g., 'AAPL')
        start_date: Start date (YYYY-MM-DD)
        end_date: End date (YYYY-MM-DD)
        interval: Data interval - valid values: 1m, 2m, 5m, 15m, 30m, 60m, 90m, 1h, 1d, 5d, 1wk, 1mo, 3mo
                  Note: Intraday data (< 1d) is only available for last 60 days
    """
    vendor = _vendor_bars(symbol, lambda: (date.fromisoformat(start_date),
                                           date.fromisoformat(end_date) - timedelta(days=1)), interval, True)
    if vendor is not None:
        return [{"symbol": symbol, **b, "adj_close": b["close"]} for b in vendor]
    try:
        import io, contextlib
        ticker = yf.Ticker(symbol)
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            hist = ticker.history(start=start_date, end=end_date, interval=interval)

        if hist.empty:
            return []

        historical_data = []
        for index, row in hist.iterrows():
            # A bar with no prices is not a bar — see _has_prices.
            if not _has_prices(row['Open'], row['High'], row['Low'], row['Close']):
                continue
            historical_data.append({
                "symbol": symbol,
                "timestamp": int(index.timestamp()),
                "open": _round_price(row['Open']),
                "high": _round_price(row['High']),
                "low": _round_price(row['Low']),
                "close": _round_price(row['Close']),
                # See get_historical_period: int() raises on NaN and the
                # function-level except would blank the entire series.
                "volume": int(row['Volume']) if _has_prices(row['Volume']) else 0,
                # Same value as "close" — kept for payload compatibility.
                # yfinance already applied any adjustment upstream; there is
                # no separate raw/adjusted pair here, and the name implying
                # one has misled every reader of this payload.
                "adj_close": _round_price(row['Close'])
            })

        return historical_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

def batch_closes(symbols, start_date, end_date):
    """Daily close history for many tickers over [start_date, end_date] in ONE
    download. Returns {"closes": {ticker: [["YYYY-MM-DD", close], ...]}} with real
    auto-adjusted closes only — tickers/days with no data are omitted (never
    fabricated). Callers: disclosed congressional trades valued at the real
    close on their transaction date, and 13F books marked to market against the
    quarter end they describe.
    """
    try:
        syms = [s for s in (symbols or []) if s]
        if not syms:
            return {"closes": {}}
        import io, contextlib
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            data = yf.download(syms, start=start_date, end=end_date, interval="1d",
                               group_by="ticker", progress=False, threads=True,
                               auto_adjust=True)

        def _closes_for(s):
            # Layouts vary (ticker on column level 0 or 1, or no ticker level at
            # all). The single-ticker case is NOT flat on current yfinance: with
            # group_by="ticker" one symbol still comes back as (Ticker, Price)
            # columns, so data["Close"] raised and every one-symbol request —
            # the forward returns on a stock's own Form 4 rows — came back
            # empty. Look for the ticker on either level first, whatever the
            # count, and fall back to the flat frame only when there is one.
            try:
                if isinstance(data.columns, pd.MultiIndex):
                    if s in data.columns.get_level_values(0):
                        return data[s]["Close"]
                    if s in data.columns.get_level_values(1):
                        return data.xs(s, axis=1, level=1)["Close"]
                    if "Close" in data.columns.get_level_values(0):
                        return data["Close"]
                    return None
                return data["Close"]
            except Exception:
                return None

        out = {}
        for s in syms:
            closes = _closes_for(s)
            if closes is None:
                continue
            # A one-ticker download still comes back with (Price, Ticker)
            # column levels on current yfinance, so data["Close"] is a
            # one-column frame rather than a series; iterating it yields the
            # column name, not the dates, and every row was silently dropped.
            if isinstance(closes, pd.DataFrame):
                if closes.shape[1] == 0:
                    continue
                closes = closes.iloc[:, 0]
            series = []
            for idx, val in closes.items():
                try:
                    if val is None or pd.isna(val):
                        continue
                    # Date as a plain YYYY-MM-DD string — avoids epoch/timezone
                    # round-trip ambiguity (an off-by-one date in negative-UTC TZs).
                    series.append([pd.Timestamp(idx).strftime("%Y-%m-%d"), round(float(val), 4)])
                except (TypeError, ValueError):
                    continue
            if series:
                out[s] = series
        return {"closes": out}
    except Exception as e:
        return {"error": str(e), "closes": {}}

def get_historical_price(symbol, target_date):
    """Fetch closing price for a specific date

    Args:
        symbol: Stock symbol (e.g., 'AAPL')
        target_date: Target date (YYYY-MM-DD)

    Returns:
        dict with price info or error
    """
    try:
        from datetime import timedelta

        # Parse target date
        target = datetime.strptime(target_date, '%Y-%m-%d')

        # Fetch a range around the target date (to handle weekends/holidays)
        start = target - timedelta(days=5)
        end = target + timedelta(days=1)

        ticker = yf.Ticker(symbol)
        hist = ticker.history(start=start.strftime('%Y-%m-%d'), end=end.strftime('%Y-%m-%d'), interval='1d')

        if hist.empty:
            return {"found": False, "error": "No data available for this date range", "symbol": symbol}

        # Find the closest date <= target_date
        closest_date = None
        closest_price = None

        for index, row in hist.iterrows():
            idx_date = index.to_pydatetime().replace(tzinfo=None)
            # Skip price-less rows (see _has_prices) — taking one would report
            # a confident price of 0 for the date rather than "no data".
            if idx_date.date() <= target.date() and _has_prices(row['Close']):
                closest_date = idx_date
                closest_price = _round_price(row['Close'])

        if closest_price is None:
            # If no date before or on target, take the first PRICED row.
            for index, row in hist.iterrows():
                if _has_prices(row['Close']):
                    closest_date = index.to_pydatetime()
                    closest_price = _round_price(row['Close'])
                    break

        if closest_price is None:
            return {"found": False, "error": "No priced bar in this date range",
                    "symbol": symbol}

        return {
            "found": True,
            "symbol": symbol,
            "price": closest_price,
            "date": closest_date.strftime('%Y-%m-%d'),
            "requested_date": target_date
        }
    except Exception as e:
        return {"found": False, "error": str(e), "symbol": symbol}

def get_info(symbol):
    """Fetch company info for a symbol.

    A lookup Yahoo refused or did not answer — any request inside it, since
    .info makes two and returns whatever the other one got — comes back as
    {"error", "transient": true}: its blanks are not facts about the symbol
    (a 401 on the profile request alone used to read as "no sector")."""
    _trace_reset()
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.info
        if _trace_transient():
            return {"error": "Yahoo refused or did not answer part of the lookup",
                    "transient": True, "symbol": symbol}

        # Class-share dot notation (BRK.B, BF.B) returns a degraded info dict
        # from Yahoo — the financialData module comes back empty, so analyst
        # targets, the recommendation and every margin are missing while the
        # symbol still "works". get_historical_period has had this fallback
        # since the same problem hit charts; info never got it, which is why
        # BRK.B showed no analyst targets and BRK-B showed a full set.
        #
        # Narrow trigger, on the SUFFIX. An absent currentPrice is the normal
        # state for a fund or an index rather than a symptom, and quoteType
        # alone does not separate the cases that matter: BP.L, VOD.L and Tokyo
        # .T listings are all quoteType == "EQUITY", so gating on that admits
        # exactly the foreign symbols _yf_ticker_with_fallback would mangle
        # (its regex rewrites any single trailing capital, so BP.L -> BP-L,
        # despite a docstring claiming exchange suffixes are left alone).
        #
        # US class shares in practice use A-D — BRK.A/B, BF.A/B, LEN.B,
        # GEF.B, HEI.A, CWEN.A, PBR.A — while the single-letter exchange
        # suffixes that collide are L, T, F and V. Restricting to A-D keeps
        # the foreign listings out and costs nothing: a class share outside
        # that range would simply not get the retry.
        qt = (info.get("quoteType") or "").upper()
        if qt in ("", "EQUITY") and re.search(r"\.[A-D]$", symbol) \
                and not info.get("currentPrice") and not info.get("targetMeanPrice"):
            # Its own try: yfinance raises for an unknown ticker, and the
            # fabricated alt symbol often IS unknown. Sharing the outer try
            # would throw away the good primary payload we already hold and
            # blank the whole overview.
            try:
                ticker_alt, alt = _yf_ticker_with_fallback(symbol)
                if alt != symbol:
                    info_alt = ticker_alt.info
                    # Adopt only if it is the SAME COMPANY. The rewrite is a
                    # guess — BP.L -> BP-L is a real, unrelated listing — and
                    # the result is cached under the symbol the user asked
                    # for, so a mismatch would silently serve one company's
                    # name, sector, targets and margins under another's
                    # ticker. Better a thin profile than a wrong one.
                    def _name(d):
                        return (d.get("longName") or d.get("shortName") or "").strip().lower()
                    # Yahoo often knows NOTHING under the dot form (BRK.B
                    # returns no name at all, so the comparison could never
                    # pass): with no primary name there is nothing to
                    # contradict, and for an A-D class share the dash form is
                    # Yahoo's spelling of the same security, not a guess.
                    same = _name(info_alt) and (_name(info_alt) == _name(info) or not _name(info))
                    if same and (info_alt.get("currentPrice") or
                                 info_alt.get("targetMeanPrice")):
                        info = info_alt
            except Exception:
                # Swallowed, like every other best-effort lookup in this file.
                # stdout is the daemon's JSON frame channel, so there is no
                # printing here; the primary payload is already in `info` and
                # is what the caller gets.
                pass

        # Extract comprehensive information - many more fields available from yfinance
        info_data = {
            "symbol": symbol,
            # EQUITY / ETF / MUTUALFUND / INDEX / CRYPTOCURRENCY. The panels
            # need this to say WHY a figure is missing: an ETF has no analyst
            # target because none exists, a stock has none because the fetch
            # came back short. Rendered identically, those are very different
            # facts for someone setting a price target.
            "quote_type": info.get('quoteType'),
            "company_name": info.get('longName', info.get('shortName', 'N/A')),
            "sector": info.get('sector', 'N/A'),
            "industry": info.get('industry', 'N/A'),
            "market_cap": info.get('marketCap'),
            "pe_ratio": info.get('trailingPE'),
            "forward_pe": info.get('forwardPE'),
            # Yahoo reports this as a percentage; the app's contract is a
            # fraction. See _dividend_yield_fraction.
            "dividend_yield": _dividend_yield_fraction(info.get('dividendYield')),
            "beta": info.get('beta'),
            "fifty_two_week_high": info.get('fiftyTwoWeekHigh'),
            "fifty_two_week_low": info.get('fiftyTwoWeekLow'),
            "average_volume": info.get('averageVolume'),
            "description": info.get('longBusinessSummary', 'N/A'),
            "website": info.get('website', 'N/A'),
            "country": info.get('country', 'N/A'),
            # NO default: an invented "USD" is indistinguishable from a real
            # one, and the portfolio FX layer caches this for 30 days — a guess
            # silently sums a CAD holding into a USD total. Empty = unknown.
            "currency": info.get('currency') or '',
            "exchange": info.get('exchange', 'N/A'),
            "employees": info.get('fullTimeEmployees'),
            # Officer roster — CEO/CFO/etc. Each entry has {name, title, age,
            # totalPay, ...}. We pass through name+title only; the detail rail
            # turns these into a "LEADERSHIP" section.
            "officers": [
                {"name": o.get("name", ""), "title": o.get("title", "")}
                for o in (info.get("companyOfficers") or [])
                if o.get("name")
            ],
            # Additional comprehensive metrics
            # `or`, not a get-default: currentPrice comes from the
            # financialData module and is absent for funds, indices and many
            # non-US listings, where regularMarketPrice is the real quote. The
            # overview uses this field to decide whether a profile came back
            # incomplete, so a missing one there reads as a fetch failure.
            "current_price": info.get('currentPrice') or info.get('regularMarketPrice'),
            "target_high_price": info.get('targetHighPrice'),
            "target_low_price": info.get('targetLowPrice'),
            "target_mean_price": info.get('targetMeanPrice'),
            "recommendation_mean": info.get('recommendationMean'),
            "recommendation_key": info.get('recommendationKey'),
            "number_of_analyst_opinions": info.get('numberOfAnalystOpinions'),
            "total_cash": info.get('totalCash'),
            "total_debt": info.get('totalDebt'),
            "total_revenue": info.get('totalRevenue'),
            "revenue_per_share": info.get('revenuePerShare'),
            "return_on_assets": info.get('returnOnAssets'),
            "return_on_equity": info.get('returnOnEquity'),
            "gross_profits": info.get('grossProfits'),
            "free_cashflow": info.get('freeCashflow'),
            "operating_cashflow": info.get('operatingCashflow'),
            "earnings_growth": info.get('earningsGrowth'),
            "revenue_growth": info.get('revenueGrowth'),
            "gross_margins": info.get('grossMargins'),
            "operating_margins": info.get('operatingMargins'),
            "ebitda_margins": info.get('ebitdaMargins'),
            "profit_margins": info.get('profitMargins'),
            "book_value": info.get('bookValue'),
            "price_to_book": info.get('priceToBook'),
            "enterprise_value": info.get('enterpriseValue'),
            "enterprise_to_revenue": info.get('enterpriseToRevenue'),
            "enterprise_to_ebitda": info.get('enterpriseToEbitda'),
            "shares_outstanding": info.get('sharesOutstanding'),
            # Every share class (Strategy has A and B): the count a market cap
            # of the whole company needs. None when Yahoo has none.
            "implied_shares_outstanding": info.get('impliedSharesOutstanding'),
            "float_shares": info.get('floatShares'),
            "held_percent_insiders": info.get('heldPercentInsiders'),
            "held_percent_institutions": info.get('heldPercentInstitutions'),
            "short_ratio": info.get('shortRatio'),
            "short_percent_of_float": info.get('shortPercentOfFloat'),
            "peg_ratio": info.get('trailingPegRatio'),
            "total_assets": info.get('totalAssets'),
            "total_liabilities": info.get('totalLiab', info.get('totalLiabilities')),
            "book_value_total": (info.get('bookValue') or 0) * (info.get('sharesOutstanding') or 0) if info.get('bookValue') and info.get('sharesOutstanding') else None,
            "timestamp": int(datetime.now().timestamp())
        }

        return info_data
    except Exception as e:
        if _trace_transient():
            return {"error": f"Yahoo refused or did not answer ({e})", "transient": True, "symbol": symbol}
        return {"error": str(e), "symbol": symbol}

def get_batch_info(symbols):
    """Fetch a *minimal* company profile (sector / industry / longName /
    market cap / website / country / employees) for many symbols in parallel.

    Each yf.Ticker(sym).info is a separate web call — sequentially this
    bottlenecks badly for the IPO Watch use case (50–100 priced tickers).
    A ThreadPoolExecutor with 8 workers brings ~80 lookups from ~40s to ~6s.
    Returns a flat list keyed on symbol; failures are recorded inline so the
    caller can render "—" without re-trying."""
    if not symbols:
        return []
    from concurrent.futures import ThreadPoolExecutor, as_completed

    def _one(sym):
        # Runs on its own pool thread, so the per-thread trace is this
        # lookup's alone. A refused row is an error (consumers skip it and
        # it is not cached), never a profile with a blank sector.
        _trace_reset()
        try:
            info = yf.Ticker(sym).info or {}
            if _trace_transient():
                return {"symbol": sym, "error": "Yahoo refused or did not answer", "transient": True}
            # yfinance swallows quoteSummary's 404 for an unknown ticker and
            # returns an empty .info: with no name from any request and that
            # 404 on record, Yahoo has said the ticker is unknown.
            if not (info.get("longName") or info.get("shortName") or info.get("quoteType")) and \
                    404 in getattr(_http_trace, "statuses", []):
                return {"symbol": sym, "error": "unknown to Yahoo (404)", "not_found": True}
            return {
                "symbol": sym,
                "long_name": info.get("longName") or info.get("shortName") or "",
                "sector":    info.get("sector") or "",
                "industry":  info.get("industry") or "",
                "market_cap": info.get("marketCap"),
                "website":   info.get("website") or "",
                "country":   info.get("country") or "",
                "employees": info.get("fullTimeEmployees"),
            }
        except Exception as e:
            if _trace_transient():
                return {"symbol": sym, "error": f"Yahoo refused or did not answer ({e})", "transient": True}
            # Only a 404 on the request it failed on is Yahoo saying the
            # ticker is unknown — the one failure a caller may remember as
            # "no profile". A parse error or a yfinance bug is neither.
            st = getattr(_http_trace, "statuses", [])
            not_found = bool(st) and st[-1] == 404
            return {"symbol": sym, "error": str(e), **({"not_found": True} if not_found else {})}

    out = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        futures = {ex.submit(_one, s): s for s in symbols}
        for f in as_completed(futures):
            try:
                out.append(f.result())
            except Exception as e:
                # Keep the ticker: a row without one can't be matched, and
                # the caller would settle it as answered.
                out.append({"symbol": futures[f], "error": str(e),
                            "transient": _is_refusal_text(str(e))})
    return out


def get_financials(symbol):
    """Financial statements for a symbol, on a STATED basis.

    `Ticker.financials` is ANNUAL. The consumer documented this stream as
    quarterly and set its cache TTL on that belief, so the app presented
    fiscal-year figures as the latest quarter — a whole different number
    with the same label.

    Both are returned now, each named for what it is, plus `basis` so a
    reader cannot mistake one for the other. The annual frames stay under
    the original keys so existing consumers keep working unchanged.
    """
    try:
        ticker = yf.Ticker(symbol)

        # ANNUAL frames (what this function has always returned).
        income_stmt = ticker.financials
        balance_sheet = ticker.balance_sheet
        cash_flow = ticker.cashflow

        # QUARTERLY income statement — the basis for the TTM figures below,
        # which is what an analyst actually quotes ("TTM revenue"), and what
        # a fiscal-year number cannot give you mid-year.
        #
        # Each frame is wrapped separately: a shared except would discard two
        # good frames because the third failed. Only the income statement is
        # fetched — balance-sheet and cash-flow quarterlies had no consumer
        # and cost ~0.7s per cache miss for data nothing read.
        try:
            q_income = ticker.quarterly_financials
        except Exception:
            q_income = None

        def dataframe_to_dict(df):
            """Convert pandas DataFrame to dict with cleaned values"""
            if df is None or df.empty:
                return {}
            result = {}
            for col in df.columns:
                result[str(col)] = {}
                for idx in df.index:
                    value = df.loc[idx, col]
                    if pd.notna(value):
                        result[str(col)][str(idx)] = float(value) if isinstance(value, (int, float)) else str(value)
            return result

        # TRAILING TWELVE MONTHS: sum the four most recent quarters.
        #
        # Only FLOW items (revenue, income) may be summed — a balance-sheet
        # stock like Total Assets is a point-in-time value and summing four
        # of them is meaningless, which is why nothing here touches them.
        # Requires four full quarters; with fewer, a "TTM" would silently be
        # a 6- or 9-month figure wearing an annual label.
        ttm = {}
        try:
            if q_income is not None and not q_income.empty and len(q_income.columns) >= 4:
                cols = list(q_income.columns)[:4]  # yfinance orders newest-first
                for out_key, aliases in (
                    ("revenue", ("Total Revenue", "Revenue")),
                    ("net_income", ("Net Income", "Net Income Common Stockholders")),
                    ("operating_income", ("Operating Income", "Operating Profit")),
                    ("ebitda", ("EBITDA", "Normalized EBITDA")),
                ):
                    for alias in aliases:
                        if alias in q_income.index:
                            vals = [q_income.loc[alias, c] for c in cols]
                            if all(pd.notna(v) for v in vals):
                                ttm[out_key] = float(sum(float(v) for v in vals))
                            break
                if ttm:
                    ttm["quarters"] = [str(c)[:10] for c in cols]
        except Exception as e:
            ttm = {"error": str(e)}

        financials_data = {
            "symbol": symbol,
            # basis names what the primary keys are, so nothing downstream
            # has to infer it (and get it wrong).
            "basis": "annual",
            "income_statement": dataframe_to_dict(income_stmt),
            "balance_sheet": dataframe_to_dict(balance_sheet),
            "cash_flow": dataframe_to_dict(cash_flow),
            "quarterly_income_statement": dataframe_to_dict(q_income),
            "ttm": ttm,
            "timestamp": int(datetime.now().timestamp())
        }

        # Operating income as the company FILED it, where Yahoo's adjusted
        # figure differs, and equity where Yahoo shows a zero for a filed
        # number (sec_statement_overlay.py — measured, and narrow on purpose).
        # Its own try: a failure here must never cost the statements.
        try:
            import sec_statement_overlay
            sec_statement_overlay.overlay(symbol, financials_data)
        except Exception as e:
            financials_data["sec_overlay"] = {"status": "error", "reason": str(e)[:160]}

        return financials_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}


def analyst_action_when(ts):
    """(New York date, published after the close?) of an analyst action.

    Yahoo stamps these as instants, which pandas hands back as naive UTC. The
    day an investor means is the exchange's: a note stamped 00:30 UTC was
    published the evening before in New York. The ONE decode of these stamps —
    the Report's rating changes and the Overview's target ages both use it.
    """
    t = pd.Timestamp(ts)
    t = (t.tz_localize("UTC") if t.tzinfo is None else t).tz_convert("America/New_York")
    return t.date(), (t.hour, t.minute) >= (16, 0)


def latest_firm_targets(rows, today, max_age_days=400):
    """Each firm's most recent price target, newest first.

    `rows` are (date, firm, target, prior, target_action, grade[, after_close]),
    any order. The consensus "mean target" is an average of these, and each
    has its own date: a target set before a stock halved is still in the mean.
    A row with no positive target is not a target; a firm silent for over a
    year has dropped out of what anyone would call current coverage.

    `set_against` is the last session whose close the analyst had seen: the
    day itself for a note after the close, the session before otherwise — a
    pre-market cut is published BEFORE that day's fall, not after it.
    """
    newest = {}
    for row in rows:
        d, firm, target, prior, action, grade = row[:6]
        after_close = bool(row[6]) if len(row) > 6 else False
        try:
            target = float(target)
        except (TypeError, ValueError):
            continue
        if not firm or not (target > 0) or d is None or d > today or (today - d) > timedelta(days=max_age_days):
            continue
        if firm not in newest or d > newest[firm]["_d"]:
            try:
                prior_v = float(prior)
            except (TypeError, ValueError):
                prior_v = None
            newest[firm] = {"_d": d, "firm": str(firm), "date": d.isoformat(), "target": target,
                            "prior": prior_v if prior_v and prior_v > 0 else None,
                            "action": str(action or ""), "grade": str(grade or ""),
                            "set_against": (d if after_close else d - timedelta(days=1)).isoformat()}
    out = sorted(newest.values(), key=lambda r: r["_d"], reverse=True)
    for r in out:
        del r["_d"]
    return out


def close_on_or_before(closes_by_date, iso_date):
    """(date, close) of the last session on or before `iso_date`, or None."""
    days = [d for d, v in (closes_by_date or {}).items() if d <= iso_date and isinstance(v, float) and v == v and v > 0]
    if not days:
        return None
    d = max(days)
    return d, closes_by_date[d]


def get_analyst_targets(symbol):
    """Every covering firm's latest price target with the day it was set —
    what the consensus mean hides. {symbol, firms: [...], as_of} or {error}."""
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no symbol given"}
    _trace_reset()
    ticker = yf.Ticker(symbol)
    ud = None
    try:
        ud = ticker.upgrades_downgrades
    except Exception as e:
        # yfinance raises when Yahoo lists no history at all — a fund, an
        # uncovered stock. That is an answer; anything else is a failure.
        if "No upgrade/downgrade history" not in str(e):
            return {"error": f"Yahoo refused or did not answer ({str(e)[:120]})",
                    "transient": _is_refusal_text(str(e)) or _trace_transient(), "symbol": symbol}
    # yfinance swallows a refused request and hands back an empty frame:
    # the trace is what tells a refusal from "nobody covers this".
    if _trace_transient():
        return {"error": "Yahoo refused or did not answer the analyst lookup", "transient": True, "symbol": symbol}
    rows = []
    if ud is not None and not ud.empty and "currentPriceTarget" in ud.columns:
        for ts, r in ud.iterrows():
            d, after_close = analyst_action_when(ts)
            rows.append((d, r.get("Firm"), r.get("currentPriceTarget"), r.get("priorPriceTarget"),
                         r.get("priceTargetAction"), r.get("ToGrade"), after_close))
    today = pd.Timestamp.now(tz="America/New_York").date()
    firms = latest_firm_targets(rows, today)
    # `listed` tells "nobody publishes targets" (a cacheable answer) from a
    # payload with nothing in it.
    out = {"symbol": symbol, "firms": firms, "listed": len(firms), "as_of": int(datetime.now().timestamp())}
    # The close the newest target was published against: unadjusted, like the
    # targets themselves. Left out when it cannot be read — never guessed —
    # and the miss is named, so the answer is asked for again, not kept.
    if firms:
        try:
            against = firms[0]["set_against"]
            start = (date.fromisoformat(against) - timedelta(days=10)).isoformat()
            closes = ticker.history(start=start, auto_adjust=False)["Close"].dropna()
            # Only sessions that have closed: today's bar is still forming.
            by_day = {ts.date().isoformat(): float(v) for ts, v in closes.items() if ts.date() < today}
            ref = close_on_or_before(by_day, against)
            if ref:
                out["ref_date"], out["ref_close"] = ref
            elif _trace_transient():
                out["ref_error"] = "Yahoo refused or did not answer the price lookup"
        except Exception as e:
            out["ref_error"] = str(e)[:120]
    return out


def get_industry_peers(symbol, count=5):
    """The companies nearest `symbol` in size within its Yahoo industry — a
    peer group for a ticker nobody has hand-picked one for.

    Yahoo ranks an industry's companies by market weight; the neighbours on
    either side of the symbol are taken, so a mid-cap is compared with
    mid-caps rather than with the industry's giant. Returns {symbol, industry,
    peers: [...]}, or {error} when Yahoo names no industry (a fund, an index).
    """
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no symbol given"}
    try:
        info = yf.Ticker(symbol).get_info() or {}
        key = info.get("industryKey")
        if not key:
            return {"error": "Yahoo lists no industry for this symbol", "symbol": symbol}
        top = yf.Industry(key).top_companies
    except Exception as e:
        return {"error": f"Yahoo refused or did not answer ({str(e)[:120]})",
                "transient": _is_refusal_text(str(e)), "symbol": symbol}
    names = [str(s).upper() for s in (list(top.index) if top is not None else [])]
    peers = nearest_in_ranking(names, symbol, int(count))
    if not peers:
        return {"error": "Yahoo lists no other companies in this industry", "symbol": symbol}
    return {"symbol": symbol, "industry": str(info.get("industry") or key), "peers": peers}


def nearest_in_ranking(names, symbol, count):
    """`count` neighbours of `symbol` in a ranked list, taken alternately from
    just above and just below it; the top of the list when it is not ranked."""
    others = [n for n in names if n != symbol]
    if symbol not in names:
        return others[:count]
    i = names.index(symbol)
    above, below = names[:i][::-1], names[i + 1:]
    out = []
    while len(out) < count and (above or below):
        if above:
            out.append(above.pop(0))
        if below and len(out) < count:
            out.append(below.pop(0))
    return out


# ── Report extras (ER "Report" tab) ────────────────────────────────────────
_REPORT_QUARTER_LINES = {
    # out key: (statement, aliases)
    "revenue": ("income", ("Total Revenue", "Operating Revenue")),
    "gross_profit": ("income", ("Gross Profit",)),
    "operating_income": ("income", ("Operating Income",)),
    "net_income": ("income", ("Net Income", "Net Income Common Stockholders")),
    "eps": ("income", ("Diluted EPS", "Basic EPS")),
    "rd": ("income", ("Research And Development",)),
    "ocf": ("cash", ("Operating Cash Flow",)),
    "capex": ("cash", ("Capital Expenditure",)),
    "fcf": ("cash", ("Free Cash Flow",)),
    "buybacks": ("cash", ("Repurchase Of Capital Stock",)),
    "dividends": ("cash", ("Cash Dividends Paid", "Common Stock Dividend Paid")),
}


def _report_quarters(q_income, q_cash, limit=8):
    """Quarterly rows, newest first. A line the company did not report for a
    quarter is None — never 0 (a bank has no gross profit; that is not a
    gross profit of zero)."""
    frames = {"income": q_income, "cash": q_cash}
    periods = []
    for df in frames.values():
        if df is not None and not getattr(df, "empty", True):
            periods += [c for c in df.columns if c not in periods]
    periods = sorted(periods, reverse=True)[:limit]
    rows = []
    for col in periods:
        row = {"period": str(col)[:10]}
        for key, (stmt, aliases) in _REPORT_QUARTER_LINES.items():
            df = frames[stmt]
            val = None
            if df is not None and not getattr(df, "empty", True) and col in df.columns:
                for alias in aliases:
                    if alias in df.index:
                        v = df.loc[alias, col]
                        if pd.notna(v):
                            val = float(v)
                            break  # only a reported value ends the search
            row[key] = val
        if any(row[k] is not None for k in _REPORT_QUARTER_LINES):
            rows.append(row)
    return rows


def get_report_extras(symbol):
    """What the Report tab needs beyond the other tabs' data: the last eight
    quarters (income and cash-flow lines), recent analyst rating changes and
    the recommendation distribution. Each part fails on its own
    (`<part>_error`), so one refusal does not cost the others."""
    symbol = str(symbol or "").strip().upper()
    if not symbol:
        return {"error": "no symbol given"}
    out = {"symbol": symbol, "as_of": int(datetime.now().timestamp())}
    try:
        ticker = yf.Ticker(symbol)
    except Exception as e:
        return {"error": str(e)[:160], "transient": _is_refusal_text(str(e)), "symbol": symbol}

    # The currency the STATEMENTS are in — not always the quote's (TSM trades
    # in dollars and reports in Taiwan dollars). Absent stays absent.
    try:
        fc = (ticker.get_info() or {}).get("financialCurrency")
        out["financial_currency"] = str(fc) if fc else None
    except Exception:
        out["financial_currency"] = None

    try:
        try:
            q_cash = ticker.quarterly_cashflow
        except Exception:
            q_cash = None
        out["quarters"] = _report_quarters(ticker.quarterly_financials, q_cash)
    except Exception as e:
        out["quarters"] = []
        out["quarters_error"] = str(e)[:160]
        out["quarters_transient"] = _is_refusal_text(str(e))

    try:
        ud = ticker.upgrades_downgrades
        actions = []
        if ud is not None and not ud.empty:
            cutoff = pd.Timestamp.now() - pd.Timedelta(days=180)
            for ts, r in ud.sort_index(ascending=False).iterrows():
                when = pd.Timestamp(ts).tz_localize(None) if pd.Timestamp(ts).tzinfo else pd.Timestamp(ts)
                if when < cutoff:
                    break
                action_day = analyst_action_when(ts)[0]
                # A reiterated rating (a target change) is not a rating
                # change. Dropped HERE, before the list is cut: a heavily
                # covered stock has dozens a month, and they would crowd the
                # six months' real upgrades and downgrades out of the limit.
                if str(r.get("Action") or "") in ("main", "reit"):
                    continue
                actions.append({
                    "date": action_day.isoformat(),
                    "firm": str(r.get("Firm") or ""),
                    "to_grade": str(r.get("ToGrade") or ""),
                    "from_grade": str(r.get("FromGrade") or ""),
                    "action": str(r.get("Action") or ""),
                })
        out["actions"] = actions[:25]
    except Exception as e:
        out["actions"] = []
        out["actions_error"] = str(e)[:160]

    try:
        rec = ticker.recommendations
        trend = []
        if rec is not None and not rec.empty:
            for _, r in rec.iterrows():
                row = {"period": str(r.get("period") or "")}
                for src, key in (("strongBuy", "strong_buy"), ("buy", "buy"), ("hold", "hold"),
                                 ("sell", "sell"), ("strongSell", "strong_sell")):
                    v = r.get(src)
                    row[key] = int(v) if pd.notna(v) else None
                trend.append(row)
        out["rec_trend"] = trend
    except Exception as e:
        out["rec_trend"] = []
        out["rec_trend_error"] = str(e)[:160]

    if not out["quarters"] and not out["actions"] and not out["rec_trend"]:
        why = out.get("quarters_error") or "Yahoo returned nothing for this symbol"
        return {"error": why, "transient": bool(out.get("quarters_transient")), "symbol": symbol}
    return out


def get_ipo_extras(symbol):
    """Single round-trip aggregator powering the IPO Watch detail rail's
    NEWS / FINANCIALS / HOLDERS tabs. Each sub-fetch is wrapped in its own
    try/except so a partial failure (e.g. yfinance returns no institutional
    holders for a young IPO) still returns the other fields.

    Output keys (all optional, omitted if yfinance returns nothing):
      quarterly_revenue:   [{"date": "YYYY-MM-DD", "value": float}]   up to 8 quarters
      quarterly_net_income: same shape
      institutional_holders: [{"holder","pct","shares","value","date"}]  top 8
      major_holders: [{"label","value"}]  the breakdown table
      news: [{"title","publisher","link","ts"}]  up to 10 most-recent
    """
    out = {"symbol": symbol}
    try:
        t = yf.Ticker(symbol)
    except Exception as e:
        return {"symbol": symbol, "error": f"ticker init failed: {e}"}

    def _series(df_row):
        """Convert a single financials row to a [{date,value}] list sorted by date asc."""
        items = []
        for k, v in df_row.items():
            try:
                if v != v: continue  # NaN
                items.append({"date": str(k)[:10], "value": float(v)})
            except Exception:
                pass
        items.sort(key=lambda r: r["date"])
        return items[-8:]

    # ── Quarterly financials (REVENUE chart) ───────────────────────────────
    try:
        qf = t.quarterly_income_stmt
        if qf is not None and not qf.empty:
            if "Total Revenue" in qf.index:
                out["quarterly_revenue"] = _series(qf.loc["Total Revenue"])
            # Try several aliases — yfinance row labels drift across versions.
            for label in ("Net Income", "Net Income Common Stockholders",
                          "Net Income From Continuing Operation Net Minority Interest"):
                if label in qf.index:
                    out["quarterly_net_income"] = _series(qf.loc[label])
                    break
    except Exception as e:
        out["financials_error"] = str(e)

    # ── Annual financials (FINANCIALS tab YoY column) ──────────────────────
    # `t.financials` returns annual data — the same source EquityResearch's
    # financials tab uses for its "YoY" labels. Up to 4 years per yfinance.
    try:
        af = t.financials
        if af is not None and not af.empty:
            if "Total Revenue" in af.index:
                out["annual_revenue"] = _series(af.loc["Total Revenue"])
            for label in ("Net Income", "Net Income Common Stockholders",
                          "Net Income From Continuing Operation Net Minority Interest"):
                if label in af.index:
                    out["annual_net_income"] = _series(af.loc[label])
                    break
            if "Gross Profit" in af.index:
                out["annual_gross_profit"] = _series(af.loc["Gross Profit"])
            if "Operating Income" in af.index:
                out["annual_operating_income"] = _series(af.loc["Operating Income"])
                # The same line as the Financials tab shows it: the filed
                # figure where Yahoo's adjusted one differs (all years or none).
                try:
                    import sec_statement_overlay as _sso
                    _facts, _ = _sso.facts_for(symbol)
                    if _facts is not None:
                        _filed = _sso.filed_annual_operating_income(_facts, out["annual_operating_income"])
                        if _filed is not None:
                            out["annual_operating_income"] = _filed
                except Exception:
                    pass
    except Exception as e:
        out["annual_financials_error"] = str(e)

    # ── Institutional holders (top 8) ──────────────────────────────────────
    try:
        ih = t.institutional_holders
        if ih is not None and not ih.empty:
            rows = []
            for _, r in ih.head(8).iterrows():
                rows.append({
                    "holder": str(r.get("Holder", "")),
                    "pct":    float(r.get("pctHeld") or 0),
                    "shares": int(r.get("Shares") or 0),
                    "value":  float(r.get("Value") or 0),
                    "date":   str(r.get("Date Reported", ""))[:10],
                })
            out["institutional_holders"] = rows
    except Exception as e:
        out["holders_error"] = str(e)

    # ── Major-holder breakdown (insider % / institution % / count) ────────
    try:
        mh = t.major_holders
        if mh is not None and not mh.empty:
            rows = []
            # major_holders is a DataFrame with index = breakdown label,
            # single 'Value' column.
            for label, val in zip(mh.index, mh.iloc[:, 0].tolist()):
                try:
                    rows.append({"label": str(label), "value": float(val)})
                except Exception:
                    pass
            out["major_holders"] = rows
    except Exception as e:
        out["major_error"] = str(e)

    # ── News (up to 10 most recent headlines) ─────────────────────────────
    try:
        # Same two failure modes as get_news(): [] or a raise ("YAHOO! FINANCE
        # IS CURRENTLY DOWN"). Either way fall back to Search, mapped to the
        # row shape below (link/ts instead of url/published_date).
        primary_err = None
        try:
            items = t.news or []
        except Exception as e:
            primary_err, items = e, []
        # Search items are already the flat shape parsed below, with the epoch
        # intact (the C++ side decodes an integer ts to local time).
        if not any(((n.get("content") if isinstance(n.get("content"), dict) else n) or {}).get("title")
                   for n in items):
            items = _search_news_items(symbol, 10)
            if not items and primary_err is not None:
                raise primary_err  # → news_error below: the outage, not "no news"
        news_rows = []
        for n in items[:10]:
            # yfinance 0.2+ wraps the article in a "content" dict; older
            # versions return the fields at top level. Handle both.
            content = n.get("content") if isinstance(n.get("content"), dict) else n
            title = content.get("title") or ""
            if not title:
                continue
            link = ""
            if isinstance(content.get("canonicalUrl"), dict):
                link = content["canonicalUrl"].get("url", "")
            link = link or content.get("link", "") or content.get("url", "")
            publisher = ""
            if isinstance(content.get("provider"), dict):
                publisher = content["provider"].get("displayName", "")
            publisher = publisher or content.get("publisher", "")
            ts = content.get("pubDate") or content.get("providerPublishTime") or ""
            news_rows.append({
                "title": title,
                "link": link,
                "publisher": publisher,
                "ts": str(ts),
            })
        if news_rows:
            out["news"] = news_rows
    except Exception as e:
        out["news_error"] = str(e)

    return out


# ── S-1 section slicing (module level so it is unit-testable) ────────────────
# Kept out of parse_s1_funding's closure deliberately: every guard below was
# added in response to a real filing defeating the previous version, and a
# closure over a network fetch cannot be tested. See
# tests/scripts/test_s1_sections.py.

SECTION_BOUNDARY = re.compile(
    r"\n\s*(item\s+\d+|exhibits\s+and\s+financial|signatures|"
    r"undertakings|index\s+to\s+(?:financial|exhibits)|"
    r"principal\s+(?:stock|share)holders|use\s+of\s+proceeds|underwriting|"
    r"recent\s+sales\s+of\s+unregistered\s+securities|"
    r"plan\s+of\s+distribution|risk\s+factors|"
    r"description\s+of\s+capital\s+stock|"
    r"selected\s+(?:consolidated\s+)?financial\s+data|"
    r"summary\s+(?:consolidated\s+)?financial\s+data|"
    r"management's\s+discussion)",
    re.IGNORECASE,
)
def _s1_looks_like_toc(chunk):
    """True when a chunk is a table-of-contents fragment, not prose.

    A ToC is a run of short lines, most ending in a page number. Real
    section prose is long sentences that rarely end in a bare integer.
    """
    lines = [ln.strip() for ln in chunk.split("\n") if ln.strip()]
    if len(lines) < 3:
        return False
    page_numbered = sum(1 for ln in lines if re.search(r"\s\d{1,4}$", ln))
    short = sum(1 for ln in lines if len(ln) < 80)
    return page_numbered >= max(2, len(lines) // 3) and short > len(lines) * 0.7

def s1_extract_section(text, pattern, cap=12000, lookahead_skip=200):
    """Slice the BODY of a section, not its table-of-contents entry.

    Every heading we search for appears at least twice in a real S-1:
    once in the table of contents (with a page number) and once as the
    actual section. re.search returns the FIRST match, which is almost
    always the ToC line — and because SECTION_BOUNDARY also matches the
    NEXT ToC line, the "section" came back as a two-line ToC fragment.
    The UI then presented that fragment under the heading "Verbatim
    excerpts below are authoritative".

    So: consider every occurrence, discard the ones that look like a
    ToC, and keep the longest remaining body. A ToC entry yields a few
    hundred characters before it hits the next listed heading; the real
    section yields thousands. Longest-wins therefore picks the body even
    when the ToC filter misses, and the filter catches the case where a
    document has only a ToC entry (no body) so we return nothing rather
    than a page-number list.
    """
    candidates = []
    for mm in re.finditer(pattern, text, re.IGNORECASE):
        # The match must look like a HEADING, not a passing mention.
        # Without this the slicer happily anchored on prose like "…the
        # use of proceeds not held in the trust account…" and, because
        # a long paragraph follows such a mention, longest-wins then
        # PREFERRED it over the real section. A heading sits alone on
        # its short line, optionally behind an "Item 15." marker.
        ls = text.rfind("\n", 0, mm.start()) + 1
        le = text.find("\n", mm.end())
        line = text[ls: le if le >= 0 else len(text)].strip()
        prefix = text[ls: mm.start()].strip()
        # The line must be essentially JUST the heading. Allowing extra
        # words lets a cross-reference — see "Underwriting" for a
        # description of… — pass as a section start.
        # A trailing page number is the signature of a ToC entry:
        # "Recent Sales of Unregistered Securities 104". Such a line is
        # otherwise indistinguishable from a heading — short, no
        # prefix — and its slice runs on through the rest of the ToC
        # into the following prose, so it WINS on length. Reject it by
        # the one thing only a ToC line has.
        if re.search(r"\s\d{1,4}$", line):
            continue
        # A cross-reference split by HTML flattening puts the heading word
        # alone on a line and CONTINUES the sentence on the next one, which
        # therefore opens with closing punctuation:
        #     ...as described under
        #     Risk Factors
        #     ." Should one or more of these risks materialize...
        # A real section never continues a quotation. Verified against a live
        # 927k-char S-1/A where this exact shape was being returned as the
        # RISK FACTORS body.
        nl = text.find("\n", mm.end())
        nxt_line = ""
        while nl >= 0:
            e = text.find("\n", nl + 1)
            cand = text[nl + 1: e if e >= 0 else len(text)].strip()
            if cand:
                nxt_line = cand
                break
            if e < 0:
                break
            nl = e
        # A cross-reference continues its sentence on the next line. But a
        # heading's own terminal period frequently lands in its own text node
        # (EDGAR wraps the heading in <b>…</b> and leaves the "." outside), so
        # a next line of just "." is a heading ARTEFACT, not a continuation —
        # rejecting it silently lost the Recent Sales section on a real filing,
        # and the miss is cached permanently.
        if len(nxt_line) > 1 and nxt_line[:1] in {".", ",", ";", ")", "\u201d",
                                                  "\u2019", '"', "'"}:
            continue

        # A ToC entry's page number is usually on its OWN line, because EDGAR
        # renders the contents as a two-column <table> and get_text() emits
        # each cell separately. Checking only for a trailing number on the
        # heading line missed every real filing: measured across 6 live S-1s,
        # UNDERWRITING's ToC entry won on length in 5 of 5 (its ToC neighbours
        # — LEGAL MATTERS, EXPERTS — are not boundary tokens, so its slice ran
        # the full 12,000-char cap and beat the real 2,238-char section).
        if re.fullmatch(r"[A-Z]?-?\d{1,4}", nxt_line):
            continue


        # The line must be essentially JUST the heading — at most a trailing
        # period or colon. A looser bound let "Underwriting Agreement" (a
        # financial-statement note in the F-pages) stand in for the
        # UNDERWRITING section.
        # Only what FOLLOWS the heading counts. Subtracting lengths from the
        # whole line also charged the candidate for whitespace BEFORE the match
        # and for non-breaking spaces inside the prefix — so
        # "ITEM 15.\xa0\xa0RECENT SALES OF UNREGISTERED SECURITIES." scored a
        # phantom trailing of 3 with nothing after it at all, and a <= 2 bound
        # discarded the single most common Item-15 shape. Those misses are
        # cached as "section not found", i.e. permanent.
        trailing = len(text[mm.end(): le if le >= 0 else len(text)].strip())
        heading_like = (
            len(line) <= 90
            and trailing <= 2
            and (not prefix or re.fullmatch(r"(item|part)\s*[\dIVX]+[A-Z]?\s*[.:\-]?",
                                            prefix, re.IGNORECASE))
        )
        if not heading_like:
            continue
        chunk = text[mm.start(): mm.start() + cap]
        # Find the NEXT boundary heading after the current one (skip the
        # current heading itself by skipping the first `lookahead_skip`
        # chars).
        nxt = SECTION_BOUNDARY.search(chunk[lookahead_skip:])
        if nxt:
            chunk = chunk[: nxt.start() + lookahead_skip]
        chunk = chunk.strip()
        if _s1_looks_like_toc(chunk):
            continue
        candidates.append(chunk)

    # Longest wins, now that the two shapes which used to beat the real section
    # are excluded up front: ToC lines by their trailing page number, and
    # cross-references by the punctuation that continues their sentence.
    #
    # "Last substantial" was tried instead and is worse — on a real filing it
    # picked a forward-looking-statements fragment over a 40,000-char RISK
    # FACTORS body, and an "Underwriting Agreement" note in the F-pages over
    # the UNDERWRITING section. A section body is simply far longer than
    # anything that merely mentions it.
    return max(candidates, key=len) if candidates else ""


def parse_s1_funding(url):
    """Download an S-1 (or S-1/A) document from SEC EDGAR and extract the
    research-relevant sections — the free public surrogate for Crunchbase /
    NPM's Financing History + Cap Table + Use-of-Proceeds panels.

    Sections extracted (all optional, omitted if not found):
      section_text:           "Recent Sales of Unregistered Securities"
                              (private-placement rounds with dates + amounts)
      principal_stockholders: "Principal Stockholders" / "Principal and
                              Selling Stockholders" (5%+ holders table)
      use_of_proceeds:        "Use of Proceeds" (how IPO funds will be spent)
      underwriters:           "Underwriting" / "Plan of Distribution"
                              (bookrunner roster + allocation)
      risk_factors:           list of top-level risk-factor headings

      rounds: best-effort {date, amount, context} hints regex-extracted
              from section_text. Noisy by design — the verbatim text is
              authoritative; rounds is a quick scan aid.

    On hard failure (HTTP error / Recent-Sales section absent) returns
    {"url": url, "error": <reason>}. On partial failure (e.g. no Principal
    Stockholders section) the corresponding key is just omitted.
    """
    if not url:
        return {"error": "no url"}
    try:
        import re
        import sec_http  # the one SEC client: contact identity, shared pacing
        from bs4 import BeautifulSoup
        import warnings
        try:
            from bs4 import XMLParsedAsHTMLWarning
            warnings.filterwarnings("ignore", category=XMLParsedAsHTMLWarning)
        except Exception:
            pass

        # The user's contact goes to SEC only: any other host is not fetched.
        if not sec_http.is_sec_host(url):
            return {"url": url, "error": "not an SEC EDGAR document"}
        r, status = sec_http.get(url, timeout=30)
        if r is None:
            return {"url": url, "error": f"HTTP {status}" if status else "SEC did not answer"}

        # Strip HTML, normalize whitespace. S-1s use heavy table markup so
        # get_text() leaves a lot of whitespace; collapse runs.
        soup = BeautifulSoup(r.text, "html.parser")
        text = soup.get_text("\n")
        # Pre-clean whitespace so all subsequent regex searches and slicing
        # operate on the same canonical form. Done once instead of per-section.
        text = re.sub(r"\n{2,}", "\n\n", text)
        text = re.sub(r"[ \t]+", " ", text)

        # ── Section slicer ─────────────────────────────────────────────────
        # Each S-1 section we care about can be found by a heading regex; we
        # slice from the heading to the next major-section heading (or a fixed
        # cap, whichever comes first). Returns "" if the heading isn't found.
        # Python re alternation is leftmost (not longest) — the more specific
        # patterns must therefore appear in the alternation BEFORE the
        # shorter prefixes they share. We dropped the bare "recent sales"
        # alternative because the specific "recent sales of unregistered
        # securities" pattern already covers our use; the bare form would
        # have matched first if added back.
        extract_section = lambda pattern, cap=12000, lookahead_skip=200: \
            s1_extract_section(text, pattern, cap, lookahead_skip)

        # ── Recent Sales (the original section — primary output) ───────────
        section = extract_section(
            r"recent\s+sales\s+of\s+unregistered\s+securities", cap=12000)
        if not section:
            # No Recent Sales = treat as hard miss so the UI shows the
            # informative "no S-1 funding section" state and we cache the
            # negative result instead of re-parsing on every click.
            return {"url": url, "error": "section not found in this filing"}

        # ── Principal Stockholders (5% holders — closest free analog to a
        #    cap table) ──
        principal = extract_section(
            r"principal\s+(?:and\s+selling\s+)?(?:stock|share)holders?", cap=10000)

        # ── Use of Proceeds ──
        # Use a short lookahead_skip — "Use of Proceeds" sections can be a
        # ~2-sentence summary, and a larger skip overshoots past the body so
        # the boundary search runs against the next section's content.
        proceeds = extract_section(r"use\s+of\s+proceeds", cap=4000, lookahead_skip=80)

        # ── Underwriters (Underwriting / Plan of Distribution) ──
        underwriters = extract_section(
            r"(?:underwriting|plan\s+of\s+distribution)", cap=6000)

        # ── Selected Financial Data — multi-year revenue / net income table.
        # This is the section where pre-IPO companies historically disclose
        # 3-5 years of audited annual financials. For freshly-listed deals
        # yfinance also has this, but for upcoming/filed companies the S-1 is
        # the only free public source. We return the verbatim text; future
        # work can layer structured (year, revenue, net_income) extraction.
        selected_financials = extract_section(
            r"(?:selected\s+consolidated\s+financial\s+data|"
            r"summary\s+consolidated\s+financial\s+data|"
            r"selected\s+financial\s+data|"
            r"summary\s+financial\s+data)", cap=8000)

        # ── Risk Factors — extract just the top-level risk headings ────────
        # The full Risk Factors section is huge (often 50+ pages of text).
        # What's useful in a research rail is the list of top risks; each is
        # usually a bolded sentence/heading followed by a paragraph. Heuristic:
        # short non-empty lines under the Risk Factors heading that end with a
        # period and aren't part of a sentence. Capped at 15.
        risk_factors = []
        risk_section = extract_section(r"risk\s+factors", cap=40000)
        if risk_section:
            # The header line itself plus typical preamble; skip to the body.
            body = risk_section.split("\n", 8)[-1]
            for line in body.split("\n"):
                s = line.strip()
                # Filter: 20-180 chars (heading-length), ends in a period or
                # question mark, capitalized first letter, no inline tab/colon
                # noise (skip table rows).
                if not (20 <= len(s) <= 180): continue
                if not s[0].isupper(): continue
                if not (s.endswith(".") or s.endswith("?")): continue
                if "$" in s or "%" in s: continue       # skip stat lines
                if s.count(",") > 4: continue           # skip list-of-things rows
                # Skip lines that are clearly sentences in a paragraph (start
                # with "The ", "We ", "Our " followed by lowercase verb).
                low = s.lower()
                if any(low.startswith(p) for p in ("the company ", "we have ", "we may ",
                                                    "our ", "in addition", "as a result")):
                    continue
                risk_factors.append(s)
                if len(risk_factors) >= 15:
                    break

        # ── Best-effort round extraction from Recent Sales ─────────────────
        rounds = []
        money_re = re.compile(r"\$\s?[\d,]+(?:\.\d+)?\s?(?:million|billion|M|B)?",
                              re.IGNORECASE)
        date_re  = re.compile(
            r"(?:January|February|March|April|May|June|July|August|"
            r"September|October|November|December)\s+\d{1,2},?\s+\d{4}|"
            r"(?:Q[1-4]\s+\d{4})|(?:in\s+\d{4})", re.IGNORECASE)
        # Amounts that are definitionally NOT the size of a round. Par value and
        # exercise/conversion prices sit in exactly the same sentences as real
        # issuances ("...at a purchase price of $0.003 per share, par value
        # $0.0001..."), so without this the extractor reported a company's par
        # value as a funding round. Per-share prices are excluded the same way:
        # a round is an aggregate, and "$X per share" is a unit price.
        NOT_A_ROUND = re.compile(
            r"par\s+value|exercise\s+price|conversion\s+price|"
            r"per\s+share|per\s+unit|per\s+warrant|price\s+of\s*$",
            re.IGNORECASE)
        seen_rounds = set()
        for mm in money_re.finditer(section):
            if len(rounds) >= 20: break
            lo = max(0, mm.start() - 250)
            hi = min(len(section), mm.end() + 100)
            ctx = section[lo:hi].strip()
            dm = date_re.search(ctx)
            if not dm: continue
            # Look immediately around THIS amount, and stop at the neighbouring
            # amount in each direction. Without the stop, a real aggregate is
            # vetoed by its own restatement: "for an aggregate of $25,000 or
            # $0.003 per share" put "per share" inside $25,000's window, so the
            # genuine round was dropped and only the unit price survived.
            after = section[mm.end(): mm.end() + 24]
            cut = after.find("$")
            if cut >= 0:
                after = after[:cut]
            before = section[max(0, mm.start() - 45): mm.start()]
            cut = before.rfind("$")
            if cut >= 0:
                before = before[cut + 1:]
            if NOT_A_ROUND.search(before) or NOT_A_ROUND.search(after):
                continue
            key = (dm.group(0).strip().lower(), mm.group(0).strip().lower())
            if key in seen_rounds:
                continue   # the same issuance restated in the same section
            seen_rounds.add(key)
            rounds.append({
                "date":    dm.group(0).strip(),
                "amount":  mm.group(0).strip(),
                "context": re.sub(r"\s+", " ", ctx)[:280],
            })

        return {
            "url": url,
            "section_text":           section,
            "principal_stockholders": principal,
            "use_of_proceeds":        proceeds,
            "underwriters":           underwriters,
            "selected_financials":    selected_financials,
            "risk_factors":           risk_factors,
            "rounds":                 rounds,
        }
    except Exception as e:
        return {"url": url, "error": str(e)}


def get_top_movers(count=10):
    """Real day's top gainers / losers via yfinance's predefined screeners.
    Replaces the previous TopMoversWidget behavior of ranking a hardcoded
    12-ticker watchlist — which produced misleading "biggest gainer" rows
    like PLTR +0.19% when the actual day's leader was something off-list.

    Returns both sides in one call: {gainers: [...], losers: [...]}.
    Each entry has the fields the C++ QuoteData struct already consumes,
    so the widget can render the result without an extra fetch.
    """
    def _normalize(qs):
        out = []
        for q in qs:
            sym = q.get("symbol") or ""
            if not sym: continue
            out.append({
                "symbol":     sym,
                "name":       q.get("shortName") or q.get("longName") or "",
                "price":      float(q.get("regularMarketPrice") or 0),
                "change":     float(q.get("regularMarketChange") or 0),
                "change_pct": float(q.get("regularMarketChangePercent") or 0),
                "high":       float(q.get("regularMarketDayHigh") or 0),
                "low":        float(q.get("regularMarketDayLow") or 0),
                "volume":     float(q.get("regularMarketVolume") or 0),
            })
        return out
    try:
        out = {}
        for direction, scr in (("gainers", "day_gainers"), ("losers", "day_losers")):
            try:
                r = yf.screen(scr, count=count)
                out[direction] = _normalize(r.get("quotes", []))
            except Exception as e:
                out[direction + "_error"] = str(e)
        return out
    except Exception as e:
        return {"error": str(e)}


def _ticker_frame(data, symbol):
    """One symbol's columns out of a yf.download frame, whichever way the
    MultiIndex is ordered; None when the symbol is absent."""
    if data is None or data.empty:
        return None
    if not isinstance(data.columns, pd.MultiIndex):
        return data
    if symbol in data.columns.get_level_values(0):
        return data[symbol]
    if symbol in data.columns.get_level_values(1):
        return data.xs(symbol, axis=1, level=1)
    return None


def _quote_from_intraday(symbol, daily, intra):
    """Quote for a symbol whose newest daily bar has no Close: the price is
    the session's last real 1-minute close, the prior close is the previous
    daily bar's. None — never a guess — when the intraday bars are missing or
    belong to a different session than the Close-less daily row."""
    f = _ticker_frame(intra, symbol)
    if f is None or "Close" not in f:
        return None
    closes = pd.to_numeric(f["Close"], errors="coerce").dropna()
    if closes.empty:
        return None
    if closes.index[-1].date() != daily.index[-1].date():
        return None
    price = float(closes.iloc[-1])
    prev = float(daily["Close"].iloc[-2])
    if not (math.isfinite(price) and math.isfinite(prev) and prev > 0):
        return None
    row = daily.iloc[-1]

    def _f(v):
        return float(v) if v is not None and not pd.isna(v) else None

    chg = price - prev
    hi, lo, op, vol = _f(row.get("High")), _f(row.get("Low")), _f(row.get("Open")), _f(row.get("Volume"))
    return {
        "symbol": symbol,
        "price": _round_price(price, symbol),
        "change": _round_price(chg, symbol),
        "change_percent": round(chg / prev * 100, 2),
        "volume": int(vol) if vol is not None else None,
        "high": _round_price(hi) if hi is not None else None,
        "low": _round_price(lo) if lo is not None else None,
        "open": _round_price(op) if op is not None else None,
        "previous_close": _round_price(prev, symbol),
        "timestamp": int(datetime.now().timestamp()),
        "exchange": "",
    }


# ── Another price source (price_sources.py) ──────────────────────────────────
# When the user picked a vendor and its key is set, daily/weekly/monthly bars
# and stock quotes come from it; Yahoo answers everything else and whatever the
# vendor can't (index/FX/crypto symbols, intraday, a refused or rate-limited
# request). One series always comes from one source.

def _vendor_bars(symbol, start_end, interval, adjusted):
    try:
        import price_sources as ps
        if not ps.configured():
            return None
        start, end = start_end()
        bars = ps.daily_bars(symbol, start, end, interval, adjusted)
        return bars or None
    except Exception as e:
        print(f"[price_source] bars {symbol}: {type(e).__name__}: {e} — using Yahoo", file=sys.stderr)
        return None


def get_batch_quotes(symbols):
    """Quotes for `symbols`: from the user's price source where it can, the
    rest from Yahoo, in the caller's order."""
    vendor = {}
    try:
        import price_sources as ps
        if ps.configured():
            vendor = ps.quotes(list(symbols))
    except Exception as e:
        print(f"[price_source] quotes: {type(e).__name__}: {e} — using Yahoo", file=sys.stderr)
        vendor = {}
    if not vendor:
        return _yahoo_batch_quotes(symbols)
    rest = [s for s in symbols if s.upper() not in vendor]
    yahoo = _yahoo_batch_quotes(rest) if rest else []
    if isinstance(yahoo, dict):  # Yahoo refused the rest: keep what the vendor priced
        yahoo = [{"symbol": s, **yahoo} for s in rest]
    by_sym = {q.get("symbol", "").upper(): q for q in yahoo if isinstance(q, dict)}
    by_sym.update(vendor)
    return [by_sym[s.upper()] for s in symbols if s.upper() in by_sym]


def _yahoo_batch_quotes(symbols):
    """Fetch quotes for multiple symbols at once using yfinance batch download"""
    try:
        import io, contextlib
        # Suppress any stdout noise from yfinance (progress bars, deprecation notices)
        # that would corrupt the JSON output parsed by the host
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            # Use 5d period to guarantee at least 2 trading days for futures/commodities
            # auto_adjust=False: a QUOTE is what the instrument actually
            # trades at, not a total-return series. With adjustment on, the
            # prior close below is pushed DOWN by any dividend since, so on an
            # ex-dividend morning the dashboard reported a change larger than
            # the real price move by the dividend amount — and disagreed with
            # the ER header on the same symbol.
            #
            # (Adjusted series remain correct for performance questions:
            # charts, technicals and the 52-week band all use auto_adjust=True
            # via get_historical_period.)
            # Held across the download AND the read of its per-ticker errors
            # (module globals the next download clears); the lock is
            # re-entrant, so the serializer's own acquire nests.
            with _DOWNLOAD_LOCK:
                data = yf.download(symbols, period="5d", group_by='ticker', progress=False, threads=True, auto_adjust=False)
                dl_errors = dict(getattr(getattr(yf, "shared", None), "_ERRORS", {}) or {})

        # A refused ticker (429, 401, 5xx, no answer) is recorded only in
        # shared._ERRORS. All refused: a failed reply. Some refused: an error
        # row each (callers skip it; a pacing caller sees the refusal) —
        # never "no quote" for a ticker Yahoo didn't answer about.
        refused = _download_refusals(symbols, dl_errors)
        if data is None or data.empty:
            if refused:
                return {"error": f"Yahoo refused the quote download ({len(refused)} symbols)",
                        "transient": True}
            return []

        results = []
        # Symbols whose newest bar exists but has no Close. Yahoo does this to
        # the current session's daily bar for a while after the close (Open/
        # High/Low/Volume filled, Close NaN). Skipping them emptied the whole
        # refresh and every holding fell back to yesterday's cached print.
        no_close = {}   # symbol -> its daily frame (newest row Close-less)
        for symbol in symbols:
            try:
                if not isinstance(data.columns, pd.MultiIndex):
                    # Flat columns (rare): use directly
                    hist = data
                else:
                    # MultiIndex columns — detect (Ticker, Price) vs (Price, Ticker)
                    level0 = data.columns.get_level_values(0).unique().tolist()
                    level1 = data.columns.get_level_values(1).unique().tolist()
                    if symbol in level0:
                        # (Ticker, Price) ordering
                        hist = data[symbol]
                    elif symbol in level1:
                        # (Price, Ticker) ordering
                        hist = data.xs(symbol, axis=1, level=1)
                    else:
                        continue

                if hist.empty or hist.dropna(how='all').empty:
                    continue

                hist = hist.dropna(how='all')
                raw_price = hist['Close'].iloc[-1]
                if pd.isna(raw_price):
                    # Never substitute the previous bar's close: that is
                    # yesterday's price presented as today's. The session's
                    # last real trade comes from the intraday bars (below);
                    # the rest of the row — prior close, open/high/low,
                    # volume — is real and kept.
                    if len(hist) >= 2:
                        no_close[symbol] = hist
                    continue
                current_price = float(raw_price)
                # Previous trading day's RAW close — the same definition the
                # ER header uses (regular-market previous close), so one
                # symbol shows one change% wherever it appears.
                # With period="5d" we always have >= 2 rows for normally-traded instruments.
                # No prior bar (or NaN) → previous close unknown; change and
                # change_percent are None rather than a fabricated 0 / 0.00%.
                raw_prev = hist['Close'].iloc[-2] if len(hist) >= 2 else None
                previous_close = float(raw_prev) if raw_prev is not None and not pd.isna(raw_prev) else None
                change = (current_price - previous_close) if previous_close is not None else None
                change_percent = (change / previous_close) * 100 if previous_close else None

                # Bid/ask intentionally omitted from the batch hot path.
                # yfinance's fast_info hits a separate Yahoo endpoint per
                # symbol — calling it inside the per-symbol loop would balloon
                # a 50-holding refresh from ~2s to 20-40s and trip rate limits.
                # Order-book data is now served by the dedicated
                # `quote_orderbook` action, called only when a panel actually
                # needs bid/ask (e.g. when the user focuses a single ticker
                # in the portfolio perf chart). See get_orderbook() below.

                results.append({
                    "symbol": symbol,
                    "price": _round_price(current_price, symbol),
                    "change": _round_price(change, symbol) if change is not None else None,
                    "change_percent": round(change_percent, 2) if change_percent is not None else None,
                    "volume": int(hist['Volume'].iloc[-1]) if not pd.isna(hist['Volume'].iloc[-1]) else None,
                    "high": _round_price(hist['High'].iloc[-1]) if not pd.isna(hist['High'].iloc[-1]) else None,
                    "low": _round_price(hist['Low'].iloc[-1]) if not pd.isna(hist['Low'].iloc[-1]) else None,
                    "open": _round_price(hist['Open'].iloc[-1]) if not pd.isna(hist['Open'].iloc[-1]) else None,
                    "previous_close": _round_price(previous_close, symbol) if previous_close is not None else None,
                    "timestamp": int(datetime.now().timestamp()),
                    "exchange": ""
                })
            except Exception:
                continue

        if no_close:
            # ONE extra batched request for all of them — never one per
            # symbol, which turns a 50-holding refresh into tens of seconds.
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    intra = yf.download(list(no_close), period="1d", interval="1m",
                                        group_by="ticker", progress=False, threads=True,
                                        auto_adjust=False, prepost=False)
            except Exception:
                intra = None
            for symbol, hist in no_close.items():
                try:
                    q = _quote_from_intraday(symbol, hist, intra)
                except Exception:
                    q = None
                if q is not None:
                    results.append(q)

        # Session-aware fix-up for non-equity instruments (futures, FX, crypto
        # spot). The daily-bar iloc[-2] above gives the wrong denominator for
        # these: their trading session straddles the calendar day so "today's
        # row" can be a live overnight session compared against day-before-
        # yesterday's daily close. Replace previous_close / change / change_percent
        # with values computed against the bar at the symbol's most-recent
        # exchange session-end. See exchange_sessions.py for the registry.
        try:
            from exchange_sessions import batch_prior_references, is_non_equity
            prior_refs = batch_prior_references([r["symbol"] for r in results])
            for r in results:
                sym = r["symbol"]
                if not is_non_equity(sym) or sym not in prior_refs:
                    continue
                prior = prior_refs[sym]
                if prior <= 0:
                    continue
                current = r["price"]
                chg = current - prior
                r["change"] = _round_price(chg)
                r["change_percent"] = round((chg / prior) * 100, 2)
                r["previous_close"] = _round_price(prior)
        except Exception as e:
            # Helper unavailable or failed — fall through with the
            # daily-bar values rather than break the whole batch. Log
            # so the silent fallback is visible.
            import sys
            print(f"[yfinance_data] session-aware fixup failed: {e!r}",
                  file=sys.stderr)

        have = {r.get("symbol") for r in results}
        for sym, why in refused.items():
            if sym not in have:
                results.append({"symbol": sym, "error": f"Yahoo refused: {why[:120]}", "transient": True})
        return results
    except Exception as e:
        # Fallback: fetch one by one
        results = []
        for symbol in symbols:
            quote = get_quote(symbol)
            if quote and "error" not in quote:
                results.append(quote)
        return results

def get_batch_sparklines(symbols, period="5d", interval="1h"):
    """Fetch close price series for multiple symbols — used for blotter sparklines.
    Returns: {"AAPL": [170.1, 171.3, ...], "MSFT": [...], ...}
    """
    import re, io, contextlib

    # Normalize class-share dot notation → hyphen (BRK.B → BRK-B) so yfinance
    # batch download finds the symbol.  Keep a reverse map so results are
    # keyed by the original symbol the caller passed in.
    # Every original spelling per normalized ticker: a merged request can
    # carry "BRK.B" from one caller and "BRK-B" from another.
    norm_to_orig = {}
    download_syms = []
    for s in symbols:
        n = re.sub(r'\.([A-Z])$', r'-\1', s)
        if n not in norm_to_orig:
            download_syms.append(n)
        norm_to_orig.setdefault(n, []).append(s)

    try:
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf), _DOWNLOAD_LOCK:   # re-entrant; see get_batch_quotes
            data = yf.download(download_syms, period=period, interval=interval,
                               group_by='ticker', progress=False, threads=True, auto_adjust=True)
            dl_errors = dict(getattr(getattr(yf, "shared", None), "_ERRORS", {}) or {})

        refused_syms = _download_refusals(download_syms, dl_errors)
        if data is None or data.empty:
            # A refused download is not "no data" (see get_batch_quotes).
            if refused_syms:
                return {"error": "Yahoo refused the sparkline download", "transient": True}
            return {"error": "No data"}

        result = {}
        for norm_sym in download_syms:
            orig_syms = norm_to_orig.get(norm_sym, [norm_sym])
            try:
                if not isinstance(data.columns, pd.MultiIndex):
                    hist = data
                else:
                    level0 = data.columns.get_level_values(0).unique().tolist()
                    level1 = data.columns.get_level_values(1).unique().tolist()
                    if norm_sym in level0:
                        hist = data[norm_sym]
                    elif norm_sym in level1:
                        hist = data.xs(norm_sym, axis=1, level=1)
                    else:
                        continue

                closes = hist['Close'].dropna()
                if closes.empty:
                    continue
                series = [round(float(v), 4) for v in closes.tolist()]
                for orig_sym in orig_syms:
                    result[orig_sym] = series
            except Exception:
                continue

        if refused_syms:
            # Some tickers refused: the rest is kept, but marked so the result
            # isn't cached and the worker backs off (_is_refusal_result).
            result["_transient"] = True
        return result
    except Exception as e:
        return {"error": str(e)}


def get_batch_all(payload):
    """Unified batch fetcher — returns quotes + sparklines + histories in a single call.

    Called by MarketDataService::refresh() so one DataHub refresh tick spawns a single
    Python process rather than three (quote + sparkline + history). Shares the same
    underlying yf.download paths as the individual endpoints — no behaviour change
    per family, just fewer process spawns.

    payload = {
      "quotes":     [sym, ...],                         # optional
      "sparklines": [sym, ...],                         # optional, 5d/1h
      "histories":  [{"symbol","period","interval"}, ...],  # optional
    }

    Returns:
      {
        "quotes":     [<quote dicts from get_batch_quotes>, ...],
        "sparklines": {sym: [close, ...], ...},
        "histories":  [{"symbol","period","interval","points":[...]}],
      }
    Any family absent from `payload` is omitted from the result.
    """
    out = {}

    quote_syms = payload.get("quotes") or []
    if quote_syms:
        q = get_batch_quotes(quote_syms)
        if isinstance(q, dict):   # refused: a list for the C++ parser, and a
            out["quotes"] = []    # "<section>_error" so the result isn't cached
            out["quotes_error"] = q.get("error", "quotes failed")
            if q.get("transient"):
                out["quotes_transient"] = True
        else:
            out["quotes"] = q   # refused rows carry "transient" (not cached: _is_cacheable_result)

    spark_syms = payload.get("sparklines") or []
    if spark_syms:
        sp = get_batch_sparklines(spark_syms)
        if isinstance(sp, dict) and sp.pop("_transient", False):   # partly refused
            out["sparklines_transient"] = True
        if isinstance(sp, dict) and sp.get("transient"):   # refused: not cached, worker backs off
            out["sparklines"] = {}
            out["sparklines_error"] = sp.get("error", "sparklines refused")
            out["sparklines_transient"] = True
        elif isinstance(sp, dict) and isinstance(sp.get("error"), str):
            out["sparklines"] = {}   # plain "No data": an answer, cacheable as before
        else:
            out["sparklines"] = sp

    hist_reqs = payload.get("histories") or []
    if hist_reqs:
        histories = []
        for req in hist_reqs:
            try:
                sym = req.get("symbol")
                period = req.get("period", "6mo")
                interval = req.get("interval", "1d")
                if not sym:
                    continue
                points = get_historical_period(sym, period, interval)
                histories.append({
                    "symbol": sym,
                    "period": period,
                    "interval": interval,
                    "points": points if isinstance(points, list) else [],
                    # A refusal is not an empty series: MarketDataService
                    # skips an entry carrying "error" (it used to publish []).
                    **({"error": points.get("error"), "transient": bool(points.get("transient"))}
                       if isinstance(points, dict) else {}),
                })
            except Exception as e:
                histories.append({
                    "symbol": req.get("symbol", ""),
                    "period": req.get("period", ""),
                    "interval": req.get("interval", ""),
                    "points": [],
                    "error": str(e),
                })
        out["histories"] = histories

    return out


def get_financial_ratios(symbol):
    """Get financial ratios formatted for peer comparison"""
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.info

        if not info or 'symbol' not in info:
            return {"error": f"No data found for symbol: {symbol}"}

        # NO `, 0` defaults below. Two separate reasons the old ones were
        # wrong, and neither is theoretical:
        #
        #   * A default only fires for a MISSING key. Yahoo far more often
        #     sends the key with a None value — trailingPegRatio was null in
        #     421 of the cached peer rows — so the default never applied and
        #     the null flowed on anyway.
        #   * Where it DID apply it destroyed the distinction the consumer
        #     needs: a company with no trailing earnings and one trading at
        #     exactly 0.0x both arrived as 0, and the peers table had no way
        #     to show one as "—" and the other as a figure. Its own comment
        #     said so: "the peer feed uses 0.0 as its no-data sentinel (no
        #     NaN/optional is available at this layer)". This is that layer.
        #
        # Absent now travels as null, which the C++ side reads as NaN.
        #
        # freeCashflow / sharesOutstanding are guarded rather than defaulted:
        # None / n raises TypeError, and the function-level except would turn
        # that into {"error": ...} — one absent field blanking the whole
        # ratios payload for a symbol whose other fifteen were fine.
        free_cashflow = info.get("freeCashflow")
        shares_outstanding = info.get("sharesOutstanding")
        fcf_per_share = (free_cashflow / shares_outstanding
                         if _has_prices(free_cashflow) and _has_prices(shares_outstanding)
                         and shares_outstanding
                         else None)

        ratios = {
            "symbol": symbol,
            "peRatio": info.get("trailingPE"),
            "forwardPE": info.get("forwardPE"),
            "priceToBook": info.get("priceToBook"),
            "priceToSales": info.get("priceToSalesTrailing12Months"),
            "pegRatio": info.get("trailingPegRatio"),
            # Yahoo reports debtToEquity as a PERCENTAGE — MSFT 29.12, MS
            # 517.28 — while every consumer treats it as a ratio: the peers
            # table grades it on a 0.5/2.0 scale, so 21 of 25 cached peers
            # rendered red including MSFT at a conservative 0.29x, and IPO
            # Watch and the report builder print it raw as "Debt / Equity
            # 29.12". Converted here, at the one boundary it enters, exactly
            # as dividendYield is. Same evidence: 21 of 25 values exceed 3,
            # which is impossible for a ratio across that many large caps.
            "debtToEquity": _pct_to_ratio(info.get("debtToEquity")),
            "returnOnEquity": info.get("returnOnEquity"),
            "returnOnAssets": info.get("returnOnAssets"),
            "profitMargin": info.get("profitMargins"),
            "operatingMargin": info.get("operatingMargins"),
            "grossMargin": info.get("grossMargins"),
            "currentRatio": info.get("currentRatio"),
            "quickRatio": info.get("quickRatio"),
            # Percentage from Yahoo -> fraction, as in the "info" action.
            "dividendYield": _dividend_yield_fraction(info.get("dividendYield")),
            "revenuePerShare": info.get("revenuePerShare"),
            "bookValuePerShare": info.get("bookValue"),
            "freeCashFlowPerShare": fcf_per_share,
            # The peers table has had PRICE, REV GRWTH and BETA columns since
            # it was written, and this producer never sent the fields — so
            # parse_peers had nothing to assign and all three rendered empty
            # for every row. They cost nothing here: `info` is already fetched.
            # currentPrice comes from Yahoo's financialData module, which is
            # absent for ETFs, indices and many non-US listings — so it falls
            # back to regularMarketPrice, or the new PRICE column would stay
            # empty for exactly those peer rows.
            # `or`, not a get-default: a default only fires for a MISSING key,
            # and Yahoo sends currentPrice present-but-null for halted and
            # delisted names — the very rows the fallback exists for. (This is
            # the same trap the comment above warns about; I walked into it.)
            "price": info.get("currentPrice") or info.get("regularMarketPrice"),
            "beta": info.get("beta"),
            "marketCap": info.get("marketCap"),
            "revenueGrowth": info.get("revenueGrowth"),
            "earningsGrowth": info.get("earningsGrowth"),
            "name": info.get("longName") or info.get("shortName"),
            "sector": info.get("sector"),
        }

        return ratios
    except Exception as e:
        return {"error": str(e)}

def get_multiple_ratios(symbols):
    """Get financial ratios for multiple symbols"""
    results = []
    for symbol in symbols:
        ratios = get_financial_ratios(symbol)
        if ratios and "error" not in ratios:
            results.append(ratios)
    return results

# Yahoo quoteType → CommandBar asset-type slug. Used to (a) filter when a caller
# asks for a specific category and (b) label results. Slugs match CommandBar's
# tabs (build_asset_types). dr/bond/economic have no distinct Yahoo quoteType
# (ADRs come back as EQUITY), so those tabs can't be served from search.
_YAHOO_TO_UI_TYPE = {
    "EQUITY": "stock",
    "ETF": "fund",
    "MUTUALFUND": "fund",
    "INDEX": "index",
    "CRYPTOCURRENCY": "crypto",
    "CURRENCY": "forex",
    "FUTURE": "futures",
}


# Indian listings never surface from search (user policy: no India sources).
# Yahoo marks them by suffix (.NS NSE, .BO BSE), exchange code or market.
# agents/rdagents/mcp_server.py symbol_search inlines the same test.
_INDIA_SUFFIXES = (".NS", ".BO")
_INDIA_EXCHANGES = {"NSI", "BSE", "BOM", "NSE"}


def _is_indian_listing(symbol, exchange="", market=""):
    return ((symbol or "").upper().endswith(_INDIA_SUFFIXES)
            or (exchange or "").upper() in _INDIA_EXCHANGES
            or (market or "").lower() == "in_market")


def search_symbols(query, limit=20, asset_type=""):
    """
    Search for stock symbols using yfinance's search API.
    Fast — uses Yahoo Finance's search endpoint instead of brute-force .info calls.

    asset_type (stock/fund/index/crypto) filters by Yahoo's quoteType; empty
    means "any". Results carry the canonical Yahoo `symbol` (foreign listings
    already include their exchange suffix, e.g. "SAP.DE"), a friendly
    exchange display name, and the UI asset-type slug.
    """
    asset_type = (asset_type or "").strip().lower()
    try:
        from urllib.request import urlopen, Request
        from urllib.parse import quote as url_quote
        import ssl

        results = []
        search_failed, search_transient = None, False
        query_str = query.strip()
        if not query_str:
            return {"results": [], "query": query, "count": 0}

        # Use Yahoo Finance search/autocomplete API (fast, no auth needed)
        url = f"https://query2.finance.yahoo.com/v1/finance/search?q={url_quote(query_str)}&quotesCount={min(limit * 2, 50)}&newsCount=0&enableFuzzyQuery=false&quotesQueryId=tss_match_phrase_query"
        ctx = ssl.create_default_context()
        req = Request(url, headers={"User-Agent": "Mozilla/5.0"})

        try:
            with urlopen(req, timeout=5, context=ctx) as response:
                data = json.loads(response.read().decode())
                quotes = data.get("quotes", [])

                for q in quotes:
                    symbol = q.get("symbol", "")
                    if not symbol or _is_indian_listing(symbol, q.get("exchange"), q.get("market")):
                        continue
                    yahoo_type = (q.get("quoteType") or "").upper()
                    ui_type = _YAHOO_TO_UI_TYPE.get(yahoo_type)
                    if asset_type:
                        # Caller wants one category — drop anything that doesn't match.
                        if ui_type != asset_type:
                            continue
                        result_type = ui_type
                    else:
                        # "Any" caller (CLI, screener, portfolio add) keeps every
                        # quoteType; label with the slug when known, else the raw type.
                        result_type = ui_type or yahoo_type.lower()
                    results.append({
                        "symbol": symbol,
                        "name": q.get("longname") or q.get("shortname", ""),
                        # Friendly display name ("NASDAQ") over the raw code ("NMS").
                        "exchange": q.get("exchDisp") or q.get("exchange", ""),
                        "country": q.get("market", ""),
                        "type": result_type,
                        "currency": q.get("currency") or "",
                        "sector": "",
                        "industry": q.get("industry", "")
                    })
                    if len(results) >= limit:
                        break
        except Exception as search_err:
            # Classified by type: a refusal or no answer is "failed — try
            # again" (transient); a 4xx for an odd query is no matches; a bug,
            # a bad body or a certificate error is a failed search a retry
            # won't fix (not transient).
            from urllib.error import HTTPError as _HTTPError, URLError as _URLError
            if isinstance(search_err, _HTTPError):
                if search_err.code in _TRANSIENT_HTTP:
                    search_failed, search_transient = search_err, True
            elif isinstance(search_err, (_URLError, TimeoutError, ConnectionError, OSError,
                                         __import__("http.client").client.HTTPException)) and \
                    "certificate" not in str(search_err).lower() and \
                    "ssl" not in type(search_err).__name__.lower():
                search_failed, search_transient = search_err, True
            elif isinstance(search_err, ValueError) and not isinstance(search_err, UnicodeDecodeError) \
                    and "certificate" not in str(search_err).lower() and "ssl" not in type(search_err).__name__.lower():
                # A 200 that isn't JSON — Yahoo's throttling / consent
                # interstitial — is a refusal, not a bug in the query.
                search_failed, search_transient = search_err, True
            else:
                search_failed, search_transient = search_err, False
            # Fallback: try the exact symbol as a yfinance Ticker
            query_upper = query_str.upper()
            for candidate in [query_upper]:
                if _is_indian_listing(candidate):
                    break
                try:
                    ticker = yf.Ticker(candidate)
                    info = ticker.info
                    if _is_indian_listing(candidate, info.get("exchange"), info.get("market")):
                        break
                    if info.get("longName") or info.get("shortName"):
                        yahoo_type = (info.get("quoteType") or "").upper()
                        ui_type = _YAHOO_TO_UI_TYPE.get(yahoo_type)
                        if asset_type and ui_type != asset_type:
                            break  # exact symbol isn't the requested category
                        results.append({
                            "symbol": candidate,
                            "name": info.get("longName", info.get("shortName", "")),
                            "exchange": info.get("exchange", ""),
                            "type": ui_type or yahoo_type.lower(),
                            "currency": info.get("currency") or "",
                            "sector": info.get("sector", ""),
                            "industry": info.get("industry", "")
                        })
                        break
                except Exception:
                    continue

        # The search endpoint failed (a 429, a timeout) and the exact-symbol
        # fallback found nothing: that is a failed search, not "no matches" —
        # the box must not read as "no such ticker".
        if search_failed is not None and not results:
            return {"error": f"search failed: {search_failed}", "transient": search_transient,
                    "query": query, "results": []}
        return {"results": results, "query": query, "count": len(results)}

    except Exception as e:
        return {"error": str(e), "query": query, "results": []}

def get_earnings_dates(symbol, lookback_days=1825, lookahead_days=180):
    """Return past + upcoming earnings dates for a symbol.

    yfinance's `ticker.earnings_dates` returns a DataFrame indexed by date
    with EPS estimate / actual / surprise columns. We keep the last 5 years
    (covers the ER 5Y chart) and any upcoming dates within 180 days so the
    chart can flag the next earnings event.

    Returns:
        {"symbol": "...", "dates": [
            {"timestamp": <unix sec>,
             "eps_estimate": <float|None>,
             "eps_actual":   <float|None>,
             "surprise_pct": <float|None>}, ...]}

    A failure carries "transient" when Yahoo refused or did not answer — a
    429 is not "this ticker has no earnings".
    """
    from datetime import datetime, timedelta, timezone
    _trace_reset()
    try:
        ticker = yf.Ticker(symbol)
        df = ticker.earnings_dates
        if df is None or getattr(df, "empty", True):
            # yfinance swallows a refused request into None / an empty frame;
            # that is not "this ticker has no earnings".
            if _trace_last_refused():
                return {"error": f"Yahoo refused or did not answer ({_trace_refusal_text()})",
                        "symbol": symbol, "dates": [], "transient": True}
            return {"symbol": symbol, "dates": []}
        now = datetime.now(timezone.utc)
        cutoff_back = now - timedelta(days=lookback_days)
        cutoff_fwd  = now + timedelta(days=lookahead_days)
        dates = []
        for idx, row in df.iterrows():
            # yfinance returns tz-aware timestamps; normalize.
            ts = pd.Timestamp(idx)
            if ts.tzinfo is None:
                ts = ts.tz_localize("UTC")
            dt = ts.to_pydatetime()
            if dt < cutoff_back or dt > cutoff_fwd:
                continue

            def _f(name):
                if name not in df.columns:
                    return None
                v = row[name]
                try:
                    if pd.isna(v):
                        return None
                except Exception:
                    pass
                try:
                    return float(v)
                except (TypeError, ValueError):
                    return None

            dates.append({
                "timestamp":    int(dt.timestamp()),
                "eps_estimate": _f("EPS Estimate"),
                "eps_actual":   _f("Reported EPS"),
                "surprise_pct": _f("Surprise(%)"),
            })
        # Sort oldest → newest (chart layer can pick a slice).
        dates.sort(key=lambda d: d["timestamp"])
        return {"symbol": symbol, "dates": dates}
    except Exception as e:
        return {"error": str(e), "symbol": symbol, "dates": [],
                # The request the failure came from — a 401 yfinance already
                # recovered from does not make an ETF's "no earnings" a refusal.
                "transient": _trace_last_refused() or "RateLimit" in type(e).__name__}


# ── Earnings analysis (ER "Earnings" tab) ────────────────────────────────────
# One round trip that assembles everything a pre-earnings read needs: the
# realised track record (surprises + how the stock actually traded on the
# print), where consensus sits right now and which way it is being revised,
# and the forward estimates. The C++ side turns this into the buy/hold/sell
# scorecard; Python stays a pure data layer and computes no verdict.

_EST_PERIOD_LABELS = {
    "0q": "CURRENT QTR",
    "+1q": "NEXT QTR",
    "0y": "CURRENT YEAR",
    "+1y": "NEXT YEAR",
    "-1q": "PRIOR QTR",
    "LTG": "LONG-TERM",
}


def _df_to_period_rows(df, fields):
    """[{period, label, <fields>}] from an estimates DataFrame indexed by period.

    `fields` maps output key → DataFrame column. Missing columns and NaNs come
    back as None so the UI can tell "no data" from a real zero.
    """
    rows = []
    if df is None or getattr(df, "empty", True):
        return rows
    for period, row in df.iterrows():
        out = {"period": str(period), "label": _EST_PERIOD_LABELS.get(str(period), str(period))}
        for key, col in fields.items():
            v = None
            if col in df.columns:
                raw = row[col]
                try:
                    if not pd.isna(raw):
                        v = float(raw)
                except (TypeError, ValueError):
                    v = None
            out[key] = v
        rows.append(out)
    return rows


def _eps_change_pct(value, base):
    """Percent change in EPS against `base`, or None when it can't be stated.

    Half a cent is the floor, matching EarningsMath::sequential_pct on the C++
    side: below that the percentage is dominated by rounding, and dividing by
    it produces a number the UI would render as if it meant something. The
    denominator is |base| so a loss-making base keeps the sign meaningful — a
    $0.25 print after a -$0.75 quarter is an improvement, not a decline.
    """
    if value is None or base is None or abs(base) < 0.005:
        return None
    return (value - base) / abs(base) * 100.0


# The timezones yfinance stamps US equity bars with. Only for these do we know
# the session's closing time well enough to judge a same-day bar by the clock.
_US_EASTERN_TZ_NAMES = frozenset(("America/New_York", "US/Eastern", "EST5EDT"))


def _session_is_final(idx, i):
    """Has the session at row `i` of a daily index actually closed?

    yfinance hands back a bar for the CURRENT session the moment it opens,
    with a running close that keeps moving all day. Reading that as a settled
    close turns a half-formed session into a "1-day earnings reaction" — and
    the signal ledger writes the number it settles on down PERMANENTLY, since
    resolve() only ever touches rows that are still unresolved. A print on
    Monday night would have its whole recorded outcome fixed by whatever the
    tape happened to say at 10:05 on Tuesday.

    Two independent tests, either of which is sufficient:

      * a LATER bar exists in the series — then this one is closed, whatever
        the clock or the exchange calendar says. Exact, and it is what makes
        holidays and half-days a non-question.
      * the bar is dated before today, or it is today and the ET clock is past
        16:20. This one applies ONLY to bars stamped on US Eastern, because
        16:00 is the only close this codebase knows — exchange_sessions is
        US-centric by construction. Applying it to a bar stamped elsewhere
        fails in the UNSAFE direction: XETRA and Euronext close at 17:30
        local, so a 16:20 cutoff would call a German reaction bar final more
        than an hour before its session ended. Elsewhere the same-day case
        waits for midnight in the venue's own timezone instead — a few hours
        of extra caution on a number that is written down once, and unlike
        refusing outright it still settles a bar that is merely stale (thin
        trading, an old fetch) rather than live.

    The 20-minute margin is not cosmetic either. For the first few minutes
    after the bell the last daily close can still be the 15:59 print rather
    than the official closing auction — the same ~0.05-0.1pp gap the
    extended-hours work already had to correct for — and a ledger row settled
    at 16:01 keeps whichever one it happened to see. Twenty minutes of extra
    caution costs a label; getting it wrong costs a permanent record.
    """
    try:
        if i < len(idx) - 1:
            return True
        ts = pd.Timestamp(idx[i])
        if ts.tz is None:
            # No zone on the stamp, so any "now" we build is the HOST's clock,
            # which says nothing about the venue. A naive Tokyo bar for a
            # session still trading reads as "tomorrow" to a host west of it —
            # and calling that final writes a running close into a record that
            # resolve() never revisits. Wait for a later bar instead.
            return False
        now = pd.Timestamp.now(tz=ts.tz)
        if ts.date() != now.date():
            return True     # not the venue's current session, on its own calendar
        # Same day. Only for US Eastern do we know the close well enough to
        # say. Elsewhere, wait for midnight in the venue's own timezone — a
        # few hours of extra caution on a number that is written down once.
        if str(ts.tz) not in _US_EASTERN_TZ_NAMES:
            return False
        return (now.hour, now.minute) >= (16, 20)
    except Exception:
        # Unreadable stamp: treat as unfinished. Withholding a settled number
        # for one refresh is recoverable; freezing a live one is not.
        return False


def _earnings_price_reaction(hist, event_ts):
    """Realised price action around one earnings print.

    `event_ts` is the tz-aware announcement timestamp from
    ticker.earnings_dates — Yahoo carries the time of day, which is what tells
    us whether the print lands after the close (16:00+, the common case) or
    before the open. That decides which session is "before" and which is
    "after"; getting it wrong assigns the move to the wrong day and inverts
    roughly half the reactions.

    Returns (reaction_pct, runup_pct, runup_20d_pct, price_before,
    price_after, reaction_live_pct) — any element None when the surrounding
    sessions aren't in `hist`.

    `reaction_pct` and `price_after` are populated ONLY once the session after
    the print has closed. While it is still open the same number is returned
    as `reaction_live_pct` instead, which nothing scores, correlates or
    settles — the same separation `move_since_last_pct` already uses for the
    trailing row, and for the same reason: a live number must never enter the
    record as a finished observation.
    """
    if hist is None or getattr(hist, "empty", True):
        return None, None, None, None, None, None
    try:
        closes = hist["Close"]
        idx = hist.index
        ts = pd.Timestamp(event_ts)
        if ts.tzinfo is None:
            ts = ts.tz_localize("UTC")
        ts = ts.tz_convert(idx.tz) if idx.tz is not None else ts.tz_localize(None)

        event_day = ts.normalize()
        # Sessions strictly before the event day, and on/after it.
        days = idx.normalize()
        before_pos = [i for i, d in enumerate(days) if d < event_day]
        on_after_pos = [i for i, d in enumerate(days) if d >= event_day]
        if not before_pos or not on_after_pos:
            return None, None, None, None, None, None

        prev_i = before_pos[-1]
        same_i = on_after_pos[0] if days[on_after_pos[0]] == event_day else None

        # After the close (16:00+, or a date-only midnight stamp) → the
        # reaction is the next session. Anything earlier — before the open,
        # or the rare intraday print — → the reaction is the event day
        # itself, since that session already trades on the news.
        after_close = ts.hour >= 16 or (ts.hour == 0 and ts.minute == 0)
        if after_close and same_i is not None:
            i_before, i_after = same_i, same_i + 1
        elif after_close:
            i_before, i_after = prev_i, prev_i + 1
        else:
            i_before = prev_i
            i_after = same_i if same_i is not None else prev_i + 1
        if i_after >= len(closes) or i_before < 0:
            return None, None, None, None, None, None

        p_before = float(closes.iloc[i_before])
        p_after = float(closes.iloc[i_after])
        reaction = ((p_after - p_before) / p_before * 100.0) if p_before else None
        # The reaction session may still be open. Hand the number over in the
        # live slot until it closes; see _session_is_final.
        reaction_live = None
        if not _session_is_final(idx, i_after):
            reaction_live = reaction
            reaction = None
            p_after = None

        # Five-session run-up into the print — the "is the move already priced
        # in" half of the question.
        # Run-ups into the print. 5 sessions is the headline "already moved?"
        # number; 20 sessions feeds the crowding leg of the historical
        # reconstruction (the live scorer measures crowding over 20 sessions —
        # feeding it the 5-session number instead quietly rescaled that leg
        # by ~2x). One implementation so the two can never diverge.
        def _runup_from(n):
            j = i_before - n
            if j < 0:
                return None
            base = float(closes.iloc[j])
            if not base:
                return None
            return (p_before - base) / base * 100.0

        runup = _runup_from(5)
        runup20 = _runup_from(20)
        return reaction, runup, runup20, p_before, p_after, reaction_live
    except Exception:
        return None, None, None, None, None, None


_INDEX_CLOSES = {"ts": 0.0, "closes": None}
_INDEX_CLOSES_TTL_SEC = 900


def _index_closes(symbol="SPY"):
    """Daily closes for the benchmark, memoised for 15 minutes.

    Used only to strip the market's own move out of a stock's pre-earnings
    run-up: a name "up 8% into the print" in a week the index rose 7% has not
    actually been bid up. Every earnings_analysis call would otherwise
    re-download the same series, and the daemon is long-lived enough that one
    memo serves a whole session.

    Returns None on any failure — the caller degrades to the absolute run-up
    rather than losing the entire response over a benchmark it can live
    without. Two threads racing here at worst duplicates one download; the
    dict write itself is atomic under CPython.
    """
    now = time.time()
    cached = _INDEX_CLOSES["closes"]
    if cached is not None and now - _INDEX_CLOSES["ts"] < _INDEX_CLOSES_TTL_SEC:
        return cached
    try:
        h = yf.Ticker(symbol).history(period="1y", interval="1d", auto_adjust=True)
        closes = h["Close"] if h is not None and not getattr(h, "empty", True) else None
    except Exception:
        closes = None
    if closes is not None:
        # Only a successful fetch refreshes the stamp; a failure leaves the
        # previous (stale but real) series in place rather than caching None.
        _INDEX_CLOSES["ts"] = now
        _INDEX_CLOSES["closes"] = closes
    return closes


def _pct_back(closes, back):
    """Percent change over the last `back` sessions, or None."""
    try:
        if closes is None or len(closes) <= back:
            return None
        last = float(closes.iloc[-1])
        ref = float(closes.iloc[-1 - back])
        if not ref:
            return None
        return (last - ref) / ref * 100.0
    except Exception:
        return None


def _announce_et(ts_epoch):
    """ET timestamp for an announcement epoch, robust to date-only stamps.

    Real Yahoo earnings rows carry genuine ET times of day. Calendar-fallback
    rows are bare dates that arrive as MIDNIGHT UTC — converting those to ET
    lands 19:00/20:00 the PREVIOUS day and shifts the calendar date back one,
    which re-admitted the announcement-day expiry the implied-move AMC guard
    exists to skip. A midnight-UTC stamp is a date, not a time: pin it to
    midnight ET of the same calendar date (downstream treats midnight as AMC,
    the convention for unknown times).
    """
    ts = pd.Timestamp(ts_epoch, unit="s", tz="UTC")
    if ts.hour == 0 and ts.minute == 0:
        return pd.Timestamp(ts.date(), tz="America/New_York")
    return ts.tz_convert("America/New_York")


def _pct_back_calendar(closes, days):
    """Percent change over the last `days` CALENDAR days, or None.

    For any window that is raced against a Yahoo "NdaysAgo" estimate field
    (eps_trend d30/d60/d90 are calendar-dated). Using `_pct_back(closes, 90)`
    there compared 90 SESSIONS (~126 calendar days) of price against 90
    calendar days of estimate drift — the price side accrued ~40% more
    elapsed time, which dragged the expectations gap negative on every
    steadily-rising name.
    """
    try:
        if closes is None or len(closes) < 2:
            return None
        cutoff = closes.index[-1] - pd.Timedelta(days=days)
        older = closes[closes.index <= cutoff]
        if getattr(older, "empty", True):
            return None
        ref = float(older.iloc[-1])
        last = float(closes.iloc[-1])
        if not ref:
            return None
        return (last - ref) / ref * 100.0
    except Exception:
        return None


def _pre_event_vol(hist, event_ts, win=20):
    """Stdev of daily returns over the `win` sessions BEFORE an event, in %.

    The magnitude of an earnings reaction is the one genuinely forecastable
    part of it, and a name's own earnings history supplies only eight to
    sixteen observations, months apart. This supplies twenty daily returns from
    the weeks immediately before the print — the regime it will actually land
    in. Measured out of sample across 181 names and 1463 prints it correlates
    +0.52 with the realised move size, against +0.43 for the trailing average
    of past earnings moves, and blending the two beats either alone.

    None when there aren't enough sessions; the consumer falls back to history.
    """
    if hist is None or getattr(hist, "empty", True):
        return None
    try:
        closes = hist["Close"]
        idx = hist.index
        ts = pd.Timestamp(event_ts)
        if ts.tzinfo is None:
            ts = ts.tz_localize("UTC")
        ts = ts.tz_convert(idx.tz) if idx.tz is not None else ts.tz_localize(None)
        before = [i for i, d in enumerate(idx.normalize()) if d < ts.normalize()]
        if len(before) < win + 2:
            return None
        window = closes.iloc[before[-(win + 1)]:before[-1] + 1]
        rets = window.pct_change().dropna()
        if len(rets) < win // 2:
            return None
        return float(rets.std() * 100.0)
    except Exception:
        return None


_IMPLIED_MAX_GAP_DAYS = 10

def _implied_earnings_move(ticker, earnings_ts, hist):
    """What the option market is pricing for the coming print.

    The number every professional earnings screen leads with, and the one this
    panel had no equivalent of. It is also the only genuinely forward-looking
    input available here: everything else on the tab is derived from what the
    company has already done.

    Two things make a naive straddle quote wrong, and both are handled.

    First, the expiry has to sit just after the print. An ATM straddle prices
    every session up to expiry, so reading the next available expiry gives a
    number dominated by calendar time rather than the event — AAPL's nearest
    post-earnings expiry is 102 days out and quotes 11.3%, which says nothing
    about its earnings move. Anything more than `_IMPLIED_MAX_GAP_DAYS` past
    the print is refused rather than reported misleadingly.

    Second, even a tight expiry covers the jump *plus* the ordinary days
    between now and it. Variance adds, so the event component is the
    straddle's implied move with the ordinary drift taken out in quadrature:

        event = sqrt(max(total^2 - (daily_vol * sqrt(sessions))^2, 0))

    `event_move_pct` is the figure the terminal displays, and `total_move_pct`
    travels with it for context only. Showing the raw straddle instead reported
    ORCL as "±12.1%" eleven sessions before its print — beside a SINGLE-SESSION
    expected move, which is the comparison the panel invites — overstating what
    the market was pricing for the report by more than 3x.

    The subtraction is only reported when it is well conditioned; see the
    threshold below. That is what keeps this from trading an overstatement for
    an understatement, since the ordinary leg is a realised-volatility proxy
    for an implied quantity.

    Returns None whenever any leg is missing — a wrong implied move is worse
    than none, because it is the number a reader would trust most.
    """
    import datetime as _dt
    if not earnings_ts:
        return None
    try:
        expiries = ticker.options or ()
    except Exception:
        return None
    if not expiries:
        return None

    # The announcement date must be read in ET, not UTC: a 20:00+ ET print is
    # already "tomorrow" in UTC, which shifted earn_day a day late and let the
    # gap guard measure from the wrong side. _announce_et also pins date-only
    # (midnight-UTC) stamps to the intended calendar date.
    earn_dt = _announce_et(earnings_ts)
    earn_day = earn_dt.date()
    # An AMC print (16:00+ ET) lands AFTER that day's options have expired —
    # an expiry ON the announcement date contains none of the event variance,
    # and pricing the "implied move" off it reported ordinary drift as the
    # market's earnings estimate. Date-only stamps (midnight) are treated as
    # AMC, the same convention _earnings_price_reaction uses.
    after_close = earn_dt.hour >= 16 or (earn_dt.hour == 0 and earn_dt.minute == 0)
    chosen = None
    for e in expiries:
        try:
            d = _dt.date.fromisoformat(e)
        except ValueError:
            continue
        if (d > earn_day) if after_close else (d >= earn_day):
            chosen = (e, d)
            break
    if chosen is None:
        return None
    expiry, expiry_day = chosen
    gap = (expiry_day - earn_day).days
    if gap > _IMPLIED_MAX_GAP_DAYS:
        return None

    try:
        chain = ticker.option_chain(expiry)
        calls, puts = chain.calls, chain.puts
        spot = float(ticker.fast_info["last_price"])
    except Exception:
        return None
    if calls is None or puts is None or calls.empty or puts.empty or not spot:
        return None

    def _atm_mid(df):
        row = df.iloc[(df["strike"] - spot).abs().argsort()[:1]]
        bid = float(row["bid"].iloc[0] or 0.0)
        ask = float(row["ask"].iloc[0] or 0.0)
        last = float(row["lastPrice"].iloc[0] or 0.0)
        # Mid when there is a two-sided market; last only as a fallback, since
        # a stale print on an illiquid strike can be far from anything tradeable.
        return ((bid + ask) / 2.0 if bid > 0 and ask > 0 else last), float(row["strike"].iloc[0])

    try:
        call_px, strike = _atm_mid(calls)
        put_px, _ = _atm_mid(puts)
    except Exception:
        return None
    straddle = call_px + put_px
    if straddle <= 0:
        return None

    total_pct = straddle / spot * 100.0

    # Ordinary (non-event) vol over the same window, from realised daily moves.
    event_pct = None
    # ET, not the host's local date: the daemon can run west of UTC, where
    # `date.today()` is a day behind the market it is pricing.
    today_et = pd.Timestamp.now(tz="America/New_York").date()
    sessions = max(1, int(round((expiry_day - today_et).days * 5.0 / 7.0)))
    try:
        if hist is not None and not getattr(hist, "empty", True):
            daily = hist["Close"].pct_change().dropna().tail(60)
            if len(daily) >= 20:
                # Median absolute move, rescaled, rather than the standard
                # deviation. A 60-session window contains the *previous*
                # earnings jump, and a stdev that includes it overstates the
                # ordinary day badly enough to swallow the whole straddle —
                # NVDA's event component came out at exactly 0.0% that way.
                # The median is unmoved by one jump in sixty sessions.
                mad = float(daily.abs().median() * 100.0) * 1.4826
                ordinary = mad * (sessions ** 0.5)
                ev = (max(total_pct ** 2 - ordinary ** 2, 0.0)) ** 0.5
                # Report the split only when the subtraction is well
                # conditioned. event = sqrt(total^2 - ordinary^2) is a
                # difference of squares, so as `ordinary` approaches `total`
                # the answer becomes a small residual between two large,
                # uncertain numbers — and `ordinary` is a REALISED-volatility
                # proxy for an IMPLIED quantity, so it carries real error.
                #
                # Requiring the event to keep at least half the straddle (a
                # quarter of the variance) bounds that leverage: below it, a
                # 25% miss on the ordinary leg can move the answer by more
                # than the answer itself. The old floor was 0.15, which
                # accepted subtracting 98% of the variance and reported ORCL
                # at 3.4% off a 12.1% straddle eleven sessions before the
                # print — a number with almost no signal left in it.
                #
                # What this means in practice: the figure appears in the last
                # sessions before a report, which is both when it is most
                # decision-relevant and when the straddle genuinely is mostly
                # the event. Further out the caller shows nothing and says why.
                if 0.5 * total_pct < ev < 0.98 * total_pct:
                    event_pct = ev
    except Exception:
        event_pct = None

    return {
        "expiry": expiry,
        "days_after_print": gap,
        "strike": strike,
        "spot": round(spot, 4),
        "straddle": round(straddle, 4),
        "total_move_pct": round(total_pct, 2),
        "event_move_pct": round(event_pct, 2) if event_pct is not None else None,
    }


def _surprise_basis_suspect(est, act, sur):
    """Is this "surprise" an accounting artefact rather than a beat?

    Yahoo's `Reported EPS` is the as-reported GAAP figure; its `EPS Estimate`
    is the street's adjusted, non-GAAP consensus. For a company with no
    material one-offs the two are close enough to compare — Apple's last four
    quarters read +6.7%, +3.5%, +6.3%, +4.5%, all plausible. For a company
    carrying a large equity portfolio or a one-time charge they are not on the
    same basis at all, and the subtraction produces a number that describes an
    accounting event rather than an operational result:

        GOOG 2026-07-22   est 2.91  ->  act 9.11   =  +213%
        META 2025-10-29   est 6.71  ->  act 1.05   =   -84%   (tax charge)

    Measured across 3,857 reported quarters from 166 large caps, the surprise
    figure does relate to the next session's move — Spearman +0.114, p ~1e-12
    — but that relationship vanishes in the tail: above 100% it is +0.077 at
    p = 0.32, indistinguishable from noise, which is exactly what a
    basis mismatch predicts. 4.3% of quarters sit above that line.

    So 100% is where the flag falls. Quarters above it are still shown, since
    something real did happen in them, but they are marked and the scorer drops
    them from the beat-rate and consistency legs rather than letting one
    accounting quarter set a company's whole track record.
    """
    try:
        if sur is None or act is None or est is None:
            return False
        return abs(float(sur)) > 100.0
    except (TypeError, ValueError):
        return False


def get_earnings_analysis(symbol, quarters=12):
    """Past / current / forward earnings picture for one symbol.

    Shape (every numeric may be None = no data):
      {"symbol", "currency", "as_of",
       "next": {timestamp, eps_avg, eps_low, eps_high, analysts, rev_avg,
                rev_low, rev_high, year_ago_eps, year_ago_rev, eps_growth,
                rev_growth},
       "history": [{timestamp, eps_estimate, eps_actual, surprise_pct,
                    eps_qoq_pct, eps_yoy_pct, reaction_pct, runup_pct,
                    price_before, price_after}],
       "estimates": [{period, label, eps_avg, eps_low, eps_high, analysts,
                      eps_growth, year_ago_eps, rev_avg, rev_growth,
                      year_ago_rev}],
       "trend":     [{period, label, current, d7, d30, d60, d90}],
       "revisions": [{period, label, up_7d, up_30d, down_7d, down_30d}],
       "growth":    [{period, label, stock, index}],
       "valuation": {price, trailing_eps, forward_eps, trailing_pe, forward_pe,
                     target_mean, target_high, target_low, recommendation,
                     recommendation_mean, analyst_count}}
    """
    from datetime import datetime, timezone
    if not symbol:
        return {"error": "symbol required"}
    try:
        ticker = yf.Ticker(symbol)
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

    out = {"symbol": symbol, "as_of": int(time.time()), "currency": ""}

    # ── info: valuation context (cheap; usually already warm) ────────────────
    info = {}
    try:
        info = ticker.info or {}
    except Exception:
        info = {}

    def _inf(key):
        v = info.get(key)
        try:
            if v is None or pd.isna(v):
                return None
        except (TypeError, ValueError):
            pass
        try:
            return float(v)
        except (TypeError, ValueError):
            return None

    out["currency"] = info.get("currency") or ""
    out["valuation"] = {
        "price":               _inf("currentPrice") or _inf("regularMarketPrice"),
        "trailing_eps":        _inf("trailingEps"),
        "forward_eps":         _inf("forwardEps"),
        "trailing_pe":         _inf("trailingPE"),
        "forward_pe":          _inf("forwardPE"),
        "target_mean":         _inf("targetMeanPrice"),
        "target_high":         _inf("targetHighPrice"),
        "target_low":          _inf("targetLowPrice"),
        "recommendation":      info.get("recommendationKey") or "",
        "recommendation_mean": _inf("recommendationMean"),
        "analyst_count":       _inf("numberOfAnalystOpinions"),
        "earnings_growth":     _inf("earningsQuarterlyGrowth"),
        "revenue_growth":      _inf("revenueGrowth"),
    }

    # ── forward estimates / revision trend ───────────────────────────────────
    try:
        out["estimates"] = _df_to_period_rows(
            ticker.earnings_estimate,
            {"eps_avg": "avg", "eps_low": "low", "eps_high": "high",
             "analysts": "numberOfAnalysts", "year_ago_eps": "yearAgoEps",
             "eps_growth": "growth"})
    except Exception:
        out["estimates"] = []
    try:
        rev_rows = _df_to_period_rows(
            ticker.revenue_estimate,
            {"rev_avg": "avg", "rev_low": "low", "rev_high": "high",
             "rev_analysts": "numberOfAnalysts", "year_ago_rev": "yearAgoRevenue",
             "rev_growth": "growth"})
        by_period = {r["period"]: r for r in out["estimates"]}
        for r in rev_rows:
            tgt = by_period.get(r["period"])
            if tgt is None:
                out["estimates"].append(r)
            else:
                tgt.update({k: v for k, v in r.items() if k not in ("period", "label")})
    except Exception:
        pass
    try:
        out["trend"] = _df_to_period_rows(
            ticker.eps_trend,
            {"current": "current", "d7": "7daysAgo", "d30": "30daysAgo",
             "d60": "60daysAgo", "d90": "90daysAgo"})
    except Exception:
        out["trend"] = []
    try:
        out["revisions"] = _df_to_period_rows(
            ticker.eps_revisions,
            {"up_7d": "upLast7days", "up_30d": "upLast30days",
             "down_7d": "downLast7Days", "down_30d": "downLast30days"})
    except Exception:
        out["revisions"] = []
    try:
        out["growth"] = _df_to_period_rows(
            ticker.growth_estimates, {"stock": "stockTrend", "index": "indexTrend"})
    except Exception:
        out["growth"] = []

    # ── history + upcoming date, with realised price reactions ───────────────
    hist = None
    try:
        # 3y of daily closes covers the 12 quarters of reactions below with
        # room for the 5-session run-up window on the oldest one.
        hist = ticker.history(period="3y", interval="1d", auto_adjust=True)
    except Exception:
        hist = None

    history = []
    upcoming = None
    try:
        df = ticker.earnings_dates
    except Exception:
        df = None
    if df is not None and not getattr(df, "empty", True):
        now = pd.Timestamp.now(tz=df.index.tz) if df.index.tz is not None else pd.Timestamp.now()

        def _cell(row, name):
            if name not in df.columns:
                return None
            v = row[name]
            try:
                if pd.isna(v):
                    return None
            except (TypeError, ValueError):
                pass
            try:
                return float(v)
            except (TypeError, ValueError):
                return None

        for idx, row in df.iterrows():
            ts = pd.Timestamp(idx)
            est = _cell(row, "EPS Estimate")
            act = _cell(row, "Reported EPS")
            sur = _cell(row, "Surprise(%)")
            if act is None and ts >= now:
                # Nearest future date wins — yfinance can list more than one.
                if upcoming is None or ts < upcoming["_ts"]:
                    upcoming = {"_ts": ts, "timestamp": int(ts.timestamp()), "eps_avg": est}
                continue
            if act is None:
                # Past date with no reported figure: Yahoo sometimes lags a day
                # or two. Keep it (the UI shows "pending") but don't score it.
                pass
            reaction, runup, runup20, p_before, p_after, reaction_live = \
                _earnings_price_reaction(hist, ts)
            history.append({
                "timestamp":    int(ts.timestamp()),
                "pre_vol_pct":  _pre_event_vol(hist, ts),
                "eps_estimate": est,
                "eps_actual":   act,
                "surprise_pct": sur,
                "surprise_suspect": _surprise_basis_suspect(est, act, sur),
                "reaction_pct": reaction,
                # Set instead of reaction_pct while the reaction session is
                # still trading. Displayed, never scored or settled.
                "reaction_live_pct": reaction_live,
                "runup_pct":    runup,
                "runup_20d_pct": runup20,
                "price_before": p_before,
                "price_after":  p_after,
            })
        # Sequential / year-ago EPS change, computed on the FULL series before
        # truncation so the oldest kept quarter still has its year-ago
        # reference. QoQ is what a reader eyeballs first but it carries the
        # company's seasonality (a retailer's March quarter is "down" against
        # December every single year); YoY is the seasonality-free version.
        # Both are reported and neither is scored — the UI shows each one's
        # measured correlation with the next-day move instead.
        history.sort(key=lambda d: d["timestamp"])
        # Yahoo lists the same print twice on report day — once with the
        # reported figure and once still pending. Keep one row per calendar
        # date, preferring the one that carries an actual, or the sequential
        # chain below counts the quarter twice and computes a 0% QoQ against
        # its own duplicate. The date must be taken in ET, not UTC: a 20:00+
        # ET print lands on the next UTC day, which let a 16:00 placeholder
        # and its 20:05 reported duplicate land on "different" days and both
        # survive.
        by_date = {}
        for row in history:
            day = _announce_et(row["timestamp"]).date()
            keep = by_date.get(day)
            if keep is None or (keep.get("eps_actual") is None and row.get("eps_actual") is not None):
                by_date[day] = row
        history = sorted(by_date.values(), key=lambda d: d["timestamp"])

        for row in history:
            row["is_estimate"] = False
            row["eps_qoq_pct"] = None
            row["eps_yoy_pct"] = None
        # Chain over the REPORTED quarters, not over every row: a quarter
        # Yahoo hasn't filled in yet would otherwise break the link and drop
        # QoQ from the quarter after it, which is the one a reader is most
        # likely to be looking at.
        reported = [r for r in history if r.get("eps_actual") is not None]
        for j, row in enumerate(reported):
            act = row["eps_actual"]
            row["eps_qoq_pct"] = _eps_change_pct(
                act, reported[j - 1]["eps_actual"] if j >= 1 else None)
            row["eps_yoy_pct"] = _eps_change_pct(
                act, reported[j - 4]["eps_actual"] if j >= 4 else None)
        history.reverse()
        history = history[:max(1, int(quarters))]

    out["history"] = history

    # Run-up *into the current setup* — the "is the move already priced in?"
    # half of the pre-earnings question. Same close-to-close basis as the
    # per-event runup above so the two are directly comparable.
    #
    # The 60/90-day legs are here for the expectations gap: the scorer races
    # the price's move over a window against the move in the consensus EPS
    # number over the SAME window, which is how it can tell "the numbers went
    # up" from "the multiple went up". Relative versions strip the index out,
    # so a stock that merely rode the market isn't read as crowded.
    recent = {"runup_5d": None, "runup_20d": None, "runup_90d": None,
              "rel_runup_20d": None, "pct_from_52w_high": None,
              "pre_vol_pct": None}
    if hist is not None and not getattr(hist, "empty", True):
        try:
            closes = hist["Close"]
            # 5d/20d are SESSION windows (matched against the per-event runup
            # and the crowding calibration, both session-based). 60d/90d are
            # CALENDAR windows because they are raced against eps_trend's
            # calendar-dated d60/d90 estimate fields — see _pct_back_calendar.
            for key, back in (("runup_5d", 5), ("runup_20d", 20)):
                recent[key] = _pct_back(closes, back)
            recent["runup_90d"] = _pct_back_calendar(closes, 90)
            # Distance from the 52-week high: 0 means sitting on it. A stock at
            # its high into a print carries a higher bar than the same stock
            # 30% off it, whatever the fundamentals say.
            window = closes.iloc[-252:] if len(closes) > 252 else closes
            high = float(window.max())
            last = float(closes.iloc[-1])
            if high:
                recent["pct_from_52w_high"] = (last - high) / high * 100.0
        except Exception:
            pass
    # Current regime, for the coming print — same measure the history carries.
    if hist is not None and not getattr(hist, "empty", True):
        try:
            recent["pre_vol_pct"] = _pre_event_vol(
                hist, pd.Timestamp(hist.index[-1]) + pd.Timedelta(days=1))
        except Exception:
            pass
    idx_closes = _index_closes()
    if idx_closes is not None:
        # The relative leg strips the index over the SAME window basis as its
        # own leg — 20d is sessions. Only the crowding leg reads a relative
        # run-up; the expectations gap deliberately races the ABSOLUTE price
        # move against the estimate, because a re-rating raises the bar
        # whether it came from the sector, the market or the name.
        own = recent.get("runup_20d")
        idx = _pct_back(idx_closes, 20)
        if own is not None and idx is not None:
            recent["rel_runup_20d"] = own - idx
    out["recent"] = recent

    # ── next report: merge the calendar's consensus onto the date ────────────
    nxt = {"timestamp": None, "eps_avg": None, "eps_low": None, "eps_high": None,
           "analysts": None, "rev_avg": None, "rev_low": None, "rev_high": None,
           "year_ago_eps": None, "year_ago_rev": None, "eps_growth": None,
           "rev_growth": None, "is_estimated": True}
    if upcoming:
        nxt["timestamp"] = upcoming["timestamp"]
        nxt["eps_avg"] = upcoming["eps_avg"]
    try:
        cal = ticker.calendar or {}
    except Exception:
        cal = {}
    if isinstance(cal, dict):
        dates = cal.get("Earnings Date") or []
        if not isinstance(dates, (list, tuple)):
            dates = [dates]
        if dates and nxt["timestamp"] is None:
            try:
                # A bare DATE, not an instant: pin it to midnight ET of that
                # date — the convention _announce_et and the app's card both
                # read as "date only, treated as after close". Stamped at UTC
                # midnight it decoded as 19:00 ET the DAY BEFORE: on NVDA's
                # print day (17 Nov) the card read "Mon 16 Nov — date has
                # passed", and the links header named the wrong day.
                # Take the calendar DATE first: converting a tz-aware UTC
                # midnight to ET would land on the day before again.
                d0 = pd.Timestamp(pd.Timestamp(dates[0]).date()).tz_localize("America/New_York")
                nxt["timestamp"] = int(d0.timestamp())
            except Exception:
                pass
        # A single calendar date means Yahoo has a confirmed day; a range means
        # it is still an estimate. Surface that so the countdown can say so.
        nxt["is_estimated"] = len(dates) != 1

        def _cal(key):
            v = cal.get(key)
            try:
                return float(v) if v is not None else None
            except (TypeError, ValueError):
                return None

        nxt["eps_avg"] = nxt["eps_avg"] if nxt["eps_avg"] is not None else _cal("Earnings Average")
        nxt["eps_low"] = _cal("Earnings Low")
        nxt["eps_high"] = _cal("Earnings High")
        nxt["rev_avg"] = _cal("Revenue Average")
        nxt["rev_low"] = _cal("Revenue Low")
        nxt["rev_high"] = _cal("Revenue High")

    # The "0q" estimate row is the same quarter the next report covers — it
    # carries the analyst count and the year-ago comparables the calendar lacks.
    cur_q = next((r for r in out.get("estimates", []) if r.get("period") == "0q"), None)
    if cur_q:
        for src, dst in (("eps_avg", "eps_avg"), ("eps_low", "eps_low"), ("eps_high", "eps_high"),
                         ("analysts", "analysts"), ("year_ago_eps", "year_ago_eps"),
                         ("eps_growth", "eps_growth"), ("rev_avg", "rev_avg"),
                         ("year_ago_rev", "year_ago_rev"), ("rev_growth", "rev_growth")):
            if nxt.get(dst) is None:
                nxt[dst] = cur_q.get(src)
    # The market's own forecast of the coming move, alongside the history.
    nxt["implied"] = _implied_earnings_move(ticker, nxt.get("timestamp"), hist)
    out["next"] = nxt

    # ── The coming quarter, as a projection ──────────────────────────────────
    # Viewed a week before the print there is no actual yet, but the consensus
    # is published — so carry the same row shape with the estimate standing in,
    # flagged `is_estimate` so the UI can colour it as provisional and the
    # scorer can refuse to count it. Its QoQ/YoY are the estimate measured
    # against real prior actuals, which is the "what are analysts expecting
    # versus what the company just did" number the calendar widget shows.
    # `history` is newest-first from here on.
    reported_desc = [r for r in history if r.get("eps_actual") is not None]
    newest_actual_ts = reported_desc[0]["timestamp"] if reported_desc else 0

    # Where the price sits NOW relative to where the last print left it. This
    # is what carries the curve up to the present instead of stopping at the
    # last earnings reaction. Deliberately its own field, NOT reaction_pct:
    # it is not a print reaction, and letting it into that series would enter
    # a live number as a completed observation in the correlations.
    move_since_last = None
    last_close = None
    if hist is not None and not getattr(hist, "empty", True):
        try:
            last_close = float(hist["Close"].iloc[-1])
        except Exception:
            last_close = None
    if last_close:
        # Base on the last print whose reaction has actually completed, not
        # merely the last one reported. A company that reported after today's
        # close has an actual but no post-print close yet (META, 29 Jul) — its
        # reaction session hasn't happened, so measuring "since the last print"
        # from it would divide by a price that doesn't exist.
        # `history` is newest-first by this point, so a plain scan finds the
        # most recent completed reaction.
        base = next((r.get("price_after") for r in history if r.get("price_after")), None)
        if base:
            move_since_last = (last_close - base) / base * 100.0
    # Trailing column: the bar is the next quarter's consensus (when Yahoo has
    # a genuinely un-reported one), the point is where the price is right now.
    # It exists if either half does, so the curve always reaches the present
    # even when the calendar has no forward date.
    #
    # "Genuinely un-reported" matters: Yahoo's calendar keeps pointing at a
    # print for a day or two after the numbers land (META's calendar said
    # 28 Jul while earnings_dates already carried the 29 Jul actual), and
    # projecting there would show one quarter twice — once as a forecast of
    # something that has already happened.
    has_forward_estimate = (nxt.get("timestamp") is not None
                            and nxt.get("eps_avg") is not None
                            and nxt["timestamp"] > newest_actual_ts)
    if has_forward_estimate or move_since_last is not None:
        prev_act = reported_desc[0].get("eps_actual") if reported_desc else None
        year_ago_act = reported_desc[3].get("eps_actual") if len(reported_desc) >= 4 else None
        est = nxt["eps_avg"] if has_forward_estimate else None
        projected = {
            "timestamp":    nxt["timestamp"] if has_forward_estimate else int(time.time()),
            "eps_estimate": est,
            "eps_actual":   None,
            "surprise_pct": None,
            "eps_qoq_pct":  _eps_change_pct(est, prev_act),
            "eps_yoy_pct":  _eps_change_pct(est, year_ago_act),
            # No print reaction by definition — `move_since_last_pct` is the
            # live stand-in and is kept in its own field so it can never be
            # counted as a completed observation.
            "reaction_pct": None,
            "reaction_live_pct": None,
            "move_since_last_pct": move_since_last,
            "price_now":    last_close,
            "runup_pct":    recent.get("runup_5d"),
            "pre_vol_pct":  recent.get("pre_vol_pct"),
            "price_before": None,
            "price_after":  None,
            "is_estimate":  True,
            "surprise_suspect": False,
            "has_forward_estimate": has_forward_estimate,
        }
        # The projection supersedes any still-pending row for the same date
        # (report filed, figures not yet published by Yahoo). ET dates, like
        # the history dedup above — comparing UTC dates here while the dedup
        # compares ET let a 20:00+ ET pending row escape the filter and the
        # upcoming quarter render twice.
        proj_day = _announce_et(projected["timestamp"]).date()
        kept = [r for r in history
                if r.get("eps_actual") is not None
                or _announce_et(r["timestamp"]).date() != proj_day]
        out["history"] = [projected] + kept

    return out


def _search_news_items(symbol, count):
    """Raw yf.Search news items: flat title/link/publisher/providerPublishTime
    (unix seconds — an EVENT instant, kept as the integer so consumers decode it
    to local time), relatedTickers. The one place Search news is fetched."""
    return yf.Search(symbol, news_count=count).news or []


def _news_via_search(symbol, count):
    """Fallback news source: yfinance's Search endpoint.

    Ticker.news returns [] on yfinance 0.2.66 (Yahoo retired the endpoint it
    scrapes), which silently blanked every news consumer. Search still serves
    news, in the OLD flat shape (title/link/publisher/providerPublishTime) and
    without summaries. providerPublishTime is an EVENT instant (unix seconds);
    it is emitted as ISO-8601 UTC so the C++ side's ISO parse keeps working.
    """
    import re
    from datetime import datetime, timezone
    items = _search_news_items(symbol, count)
    articles = []
    for it in items:
        title = it.get("title", "")
        if not title:
            continue
        ts = it.get("providerPublishTime")
        pub_date = (datetime.fromtimestamp(ts, tz=timezone.utc).isoformat()
                    if isinstance(ts, (int, float)) and ts > 0 else "")
        # Same cleaning guarantee as the primary path below: a summary can
        # carry markup, and this text flows into LLM prompts and tooltips.
        summary = re.sub(r'<[^>]+>', '', it.get("summary", "") or "")
        articles.append({
            "title": title,
            "description": summary,
            "url": it.get("link", "") or "",
            "publisher": (it.get("publisher", "") or ""),
            "published_date": pub_date,
        })
    return articles


_OPT_MONEYNESS_BAND = 0.30   # strikes within ±30% of spot
_OPT_MAX_EXPIRIES = 12


def get_options_surface(symbol, max_expiries=_OPT_MAX_EXPIRIES):
    """Implied-volatility surface for one listed stock, from Yahoo's chain.

    Every point is a contract Yahoo quoted, with Yahoo's own implied
    volatility. Nothing is interpolated, smoothed or filled: a strike an
    expiry does not list is absent, and the caller draws it as a hole.

    The surface uses the out-of-the-money side at each strike — puts below
    spot, calls at or above — the standard construction, because the
    in-the-money side is dominated by intrinsic value and its IV is noise.

    A contract is kept only with a two-sided market (bid > 0 and ask > 0).
    Yahoo reports an IV for no-bid contracts too, but it is a solver artefact
    (often 1e-5 or a round placeholder), and a surface drawn through those
    points shows spikes nobody can trade. How many were dropped travels with
    the result so the screen can say so.
    """
    import datetime as _dt
    sym = (symbol or "").strip().upper()
    if not sym:
        return {"error": "no symbol"}
    t = yf.Ticker(sym)
    try:
        expiries = list(t.options or ())
    except Exception as e:
        return {"error": f"option expiries unavailable: {e}"}
    out = {"symbol": sym, "source": "Yahoo Finance (delayed)",
           "as_of": datetime.now().isoformat(timespec="seconds"),
           "expiries_listed": len(expiries), "points": [], "atm": []}
    if not expiries:
        # yfinance also returns an empty list when the expiry request itself
        # failed (network, crumb), so this cannot be stated as a fact about
        # the stock. Saying so — and the "_error" key keeping it out of the
        # daemon cache — stops a transient blip from reading as "AAPL has no
        # options" for five minutes.
        out["reason"] = ("Yahoo returned no option expiries — the stock may have "
                         "no listed options, or the request failed (REFRESH to retry)")
        out["listing_error"] = "empty expiry list"
        return out
    try:
        spot = float(t.fast_info["last_price"])
    except Exception:
        spot = float("nan")
    if not (spot > 0 and math.isfinite(spot)):
        return {"error": "no spot price — cannot place strikes against the money"}
    out["spot"] = spot

    # Listed options settle on the ET calendar; the host's local date is a
    # day off for part of every evening west of New York (or morning east).
    today = pd.Timestamp.now(tz="America/New_York").date()
    usable = []
    for e in expiries:
        try:
            d = _dt.date.fromisoformat(e)
        except ValueError:
            continue
        dte = (d - today).days
        # Same-day expiries carry almost no time value; their IV is unstable
        # enough to dominate the colour scale of the whole surface.
        if dte >= 1:
            usable.append((e, dte))
    # Weeklies crowd the front of the list, so the first N expiries would
    # all sit inside a few months. Sample N evenly across the listed range
    # (always keeping the nearest and the farthest) for a real term axis.
    n_max = max(2, int(max_expiries))
    if len(usable) > n_max:
        idx = sorted({round(i * (len(usable) - 1) / (n_max - 1)) for i in range(n_max)})
        usable = [usable[i] for i in idx]

    lo, hi = spot * (1 - _OPT_MONEYNESS_BAND), spot * (1 + _OPT_MONEYNESS_BAND)

    def _fetch(item):
        e, dte = item
        chain = t.option_chain(e)
        return e, dte, chain.calls, chain.puts

    def _num_col(df, name):
        if name not in df.columns:
            return pd.Series(float("nan"), index=df.index)
        return pd.to_numeric(df[name], errors="coerce")

    dropped = 0   # OTM, in-band contracts left out for lacking a two-sided market or an IV
    errors = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        results = list(ex.map(lambda it: _safe(_fetch, it), usable))
    for item, res in zip(usable, results):
        if isinstance(res, Exception):
            errors.append(f"{item[0]}: {res}")
            continue
        e, dte, calls, puts = res
        per_strike = {}   # strike -> {"call": iv, "put": iv} (two-sided only)
        for kind, df in (("call", calls), ("put", puts)):
            if df is None or df.empty:
                continue
            k = _num_col(df, "strike")
            bid, ask = _num_col(df, "bid"), _num_col(df, "ask")
            iv, oi = _num_col(df, "impliedVolatility"), _num_col(df, "openInterest")
            in_band = k.between(lo, hi)
            quoted = (bid > 0) & (ask > 0) & (iv > 0) & iv.notna()
            otm = (k < spot) if kind == "put" else (k >= spot)
            dropped += int((in_band & otm & ~quoted).sum())
            for kk, vv in zip(k[in_band & quoted], iv[in_band & quoted]):
                per_strike.setdefault(float(kk), {})[kind] = float(vv)
            keep = in_band & quoted & otm
            for kk, b, a, vv, o in zip(k[keep], bid[keep], ask[keep], iv[keep], oi[keep]):
                out["points"].append({
                    "expiry": e, "dte": dte, "strike": float(kk), "type": kind,
                    "iv": float(vv), "bid": float(b), "ask": float(a),
                    "oi": int(o) if pd.notna(o) else None,
                })
        if per_strike:
            # ATM: the listed strike nearest spot; average call and put there
            # when both are quoted (put-call parity makes them estimates of
            # the same number), else whichever side has a market.
            k_atm = min(per_strike, key=lambda k: abs(k - spot))
            ivs = list(per_strike[k_atm].values())
            out["atm"].append({"expiry": e, "dte": dte, "strike": k_atm,
                               "iv": sum(ivs) / len(ivs)})
    out["expiries_used"] = len(usable) - len(errors)
    out["dropped_one_sided"] = dropped
    if errors:
        # "*_error" keeps a partially failed chain out of the daemon cache,
        # so the missing expiries are retried on the next load.
        out["chain_error"] = "; ".join(errors)
    if not out["points"] and errors and len(errors) == len(usable):
        return {"error": "option chains unavailable: " + errors[0]}
    if not out["points"]:
        out["reason"] = "no two-sided quotes within ±30% of spot"
    return out


def _safe(fn, *a):
    try:
        return fn(*a)
    except Exception as e:  # surfaced per expiry, never swallowed silently
        return e


def get_news(symbol, count=20):
    """Fetch news articles for a symbol using yfinance"""
    try:
        import re
        ticker = yf.Ticker(symbol)
        # Ticker.news has two failure modes and BOTH must reach the Search
        # fallback: [] (yfinance 0.2.66, endpoint retired) and a raise —
        # 2026-11 Yahoo answers it with "*** YAHOO! FINANCE IS CURRENTLY
        # DOWN! ***" (YFDataException) while Search keeps serving news.
        primary_err = None
        try:
            raw_news = ticker.news or []
        except Exception as e:
            primary_err, raw_news = e, []

        articles = []
        for item in raw_news[:count]:
            content = item.get("content", {})
            title = content.get("title", "")
            if not title:
                continue
            # Clean HTML from summary
            summary = content.get("summary", "")
            summary = re.sub(r'<[^>]+>', '', summary)
            pub_date = content.get("pubDate", "")
            provider = content.get("provider", {})
            publisher = provider.get("displayName", "")
            # Get URL
            url = ""
            click_url = content.get("clickThroughUrl", {})
            if click_url:
                url = click_url.get("url", "")
            if not url:
                canonical = content.get("canonicalUrl", {})
                if canonical:
                    url = canonical.get("url", "")

            articles.append({
                "title": title,
                "description": summary,
                "url": url,
                "publisher": publisher,
                "published_date": pub_date
            })

        # The gate is "nothing PARSED", not "nothing returned": a reshaped
        # Ticker.news (every item missing content.title) blanks the feed too.
        if not articles:
            try:
                articles = _news_via_search(symbol, count)
            except Exception as e:
                if primary_err is None:
                    raise
                return {"error": f"{primary_err}; search fallback: {e}", "symbol": symbol, "articles": []}
            if not articles and primary_err is not None:
                # Empty fallback after a refused primary is the outage, not "no news".
                return {"error": str(primary_err), "symbol": symbol, "articles": []}

        return {"articles": articles, "symbol": symbol}
    except Exception as e:
        return {"error": str(e), "symbol": symbol, "articles": []}

def get_ownership_extras(symbol):
    """Institutional ownership and short interest for the OWNERSHIP screen.

    The EDGAR half of that screen (Form 4, 13D/G) comes from
    sec_ownership_data.py; this supplies the two things EDGAR cannot give a
    per-ticker view of cheaply: who the large institutional holders are (13F is
    filed by the manager, not the issuer, so a per-issuer answer would mean
    scanning every manager's filing) and the exchange short-interest figures.

    Every field is omitted when yfinance does not report it. Nothing is
    defaulted to zero — "no short interest reported" and "short interest is
    zero" are different facts and the UI has to be able to tell them apart.
    """
    out = {"symbol": symbol}
    try:
        t = yf.Ticker(symbol)
    except Exception as e:
        return {"symbol": symbol, "error": f"ticker init failed: {e}"}

    # ── Institutional holders ──────────────────────────────────────────────
    try:
        ih = t.institutional_holders
        if ih is not None and not ih.empty:
            rows = []
            for _, r in ih.head(15).iterrows():
                row = {"holder": str(r.get("Holder", ""))}
                for key, field in (("pctHeld", "pct"), ("Shares", "shares"),
                                   ("Value", "value")):
                    v = r.get(key)
                    if v is not None and v == v:  # not NaN
                        row[field] = float(v)
                d = str(r.get("Date Reported", ""))[:10]
                if d:
                    row["date"] = d
                rows.append(row)
            out["institutional_holders"] = rows
    except Exception as e:
        out["holders_error"] = str(e)

    # ── Major-holder breakdown ─────────────────────────────────────────────
    try:
        mh = t.major_holders
        if mh is not None and not mh.empty:
            rows = []
            for label, val in zip(mh.index, mh.iloc[:, 0].tolist()):
                try:
                    rows.append({"label": str(label), "value": float(val)})
                except Exception:
                    pass
            if rows:
                out["major_holders"] = rows
    except Exception as e:
        out["major_error"] = str(e)

    # ── Short interest + float ─────────────────────────────────────────────
    try:
        info = t.info or {}
        fields = {
            "shares_short": "sharesShort",
            "shares_short_prior": "sharesShortPriorMonth",
            "short_ratio": "shortRatio",
            "short_pct_float": "shortPercentOfFloat",
            "short_pct_out": "sharesPercentSharesOut",
            "float_shares": "floatShares",
            "shares_outstanding": "sharesOutstanding",
            "held_pct_insiders": "heldPercentInsiders",
            "held_pct_institutions": "heldPercentInstitutions",
        }
        short = {}
        for out_key, info_key in fields.items():
            v = info.get(info_key)
            if v is not None:
                try:
                    short[out_key] = float(v)
                except (TypeError, ValueError):
                    pass
        # The as-of date matters: short interest is a twice-monthly settlement
        # snapshot, so a figure with no date attached cannot be aged by the UI.
        for out_key, info_key in (("date_short_interest", "dateShortInterest"),
                                  ("date_short_prior", "sharesShortPreviousMonthDate")):
            v = info.get(info_key)
            if v:
                short[out_key] = v
        if short:
            out["short_interest"] = short
    except Exception as e:
        out["short_error"] = str(e)

    return out


def _candidate_yf_symbols(symbol):
    """Generate yfinance ticker candidates for a portfolio symbol.

    Heuristic — try the symbol as-given first, then common non-US exchange
    suffixes. We can't ask the portfolio for currency here, so we just iterate
    until something returns data. Order matters: most likely first.
    """
    s = symbol.strip().upper()
    if not s:
        return []
    # Already has an exchange suffix or is an index/crypto/forex pair → trust it.
    if '.' in s or s.startswith('^') or '-' in s or '=' in s:
        return [s]
    # Bare ticker — try US first, then Toronto, LSE, ASX, Hong Kong.
    return [s, f"{s}.TO", f"{s}.L", f"{s}.AX", f"{s}.HK"]


def _resolve_for_history(symbols, period, auto_adjust=True):
    """Resolve every input symbol to a yfinance ticker that returns data
    in the given period, AND return the close-price DataFrame for each.

    Returns: dict[input_symbol] -> (resolved_symbol, closes_series).
    Symbols that fail every candidate are omitted.

    We do this in two passes:
      1. Bulk download all bare symbols + obvious-suffix variants we need to
         cover. yf.download is one HTTP fan-out so this is cheap.
      2. For each input, pick the first candidate that yielded a non-empty
         close series.
    """
    import io, contextlib, logging
    candidates_per_symbol = {s: _candidate_yf_symbols(s) for s in symbols}
    all_candidates = sorted({c for variants in candidates_per_symbol.values() for c in variants})

    if not all_candidates:
        return {}

    # yfinance prints "Failed download" / HTTP error lines straight to stdout
    # AND uses the logging module. Silence both — we report misses via the
    # `missing` key in the returned JSON, and noise on stdout corrupts the
    # JSON the C++ caller is trying to parse.
    yf_logger = logging.getLogger("yfinance")
    prev_level = yf_logger.level
    yf_logger.setLevel(logging.CRITICAL)
    _buf_out = io.StringIO()
    _buf_err = io.StringIO()
    try:
        with contextlib.redirect_stdout(_buf_out), contextlib.redirect_stderr(_buf_err):
            data = yf.download(
                tickers=all_candidates,
                period=period,
                interval="1d",
                group_by="ticker",
                auto_adjust=auto_adjust,
                # Carries the "Stock Splits" column the repair reads. Without it
                # the only way to learn a symbol's splits is a per-symbol
                # Ticker fetch, which is both an ungated concurrent download and
                # the opposite of what this bulk path exists to do.
                actions=True,
                progress=False,
                threads=True,
            )
    finally:
        yf_logger.setLevel(prev_level)

    if data is None or data.empty:
        return {}

    single = len(all_candidates) == 1

    def closes_for(ticker):
        try:
            # .copy() is load-bearing, not defensive: data[ticker] is a slice
            # view of the bulk frame, and the repair's writes into a view are
            # the SettingWithCopy case pandas explicitly does not promise to
            # honour. Copying also keeps one symbol's repair from reaching into
            # the shared frame the other symbols are still read from.
            # group_by="ticker" returns MultiIndex columns even for a single
            # ticker (multi_level_index defaults True), so the old `data`
            # shortcut for the one-candidate case made frame["Close"] a KeyError
            # that closes_for swallowed into "missing". A portfolio whose only
            # holding is a suffixed or hyphenated symbol — BTC-USD, RY.TO,
            # ^GSPC, none of which generate extra suffix candidates — got no NAV
            # backfill at all. Both shapes are handled rather than pinning
            # multi_level_index, which older yfinance does not accept.
            frame = (data[ticker] if isinstance(data.columns, pd.MultiIndex)
                     else data).copy()
            # Splits and dividends are INDEPENDENT axes, and only one of them is
            # this caller's to decline. Bloomberg splits the same decision three
            # ways — CapChg for splits, spin-offs and stock dividends,
            # CshAdjNormal for regular cash dividends, CshAdjAbnormal for
            # specials — so a request can carry raw, dividend-inclusive prices
            # while still being on one consistent share basis. auto_adjust is a
            # single boolean and cannot express that.
            #
            # auto_adjust gates CshAdjNormal only: yfinance applies CapChg
            # unconditionally (NVDA's 10:1 comes back at ~118 on 2024-06-05 with
            # auto_adjust=False, not ~1180). These callers therefore already
            # receive split-adjusted prices, and the repair restores that basis
            # when the vendor fails to deliver it — it is not introducing an
            # adjustment they opted out of.
            #
            # NOT claimed here: that this makes portfolio NAV correct across a
            # split. PortfolioLedger replays a SPLIT row as quantity *= ratio,
            # so a pre-split date holds the PRE-split share count while the
            # price for that date is already POST-split — the backfill is out by
            # the ratio for any holding whose splits are entered as SPLIT rows,
            # which is exactly what get_portfolio_closes_history's docstring
            # asks users to do. Separate, pre-existing defect in the
            # ledger/price pairing; this repair neither causes nor cures it.
            frame = _repair_unadjusted_splits(frame, ticker)
            return frame["Close"].dropna()
        except Exception:
            return None

    resolved = {}
    for s, variants in candidates_per_symbol.items():
        for v in variants:
            ser = closes_for(v)
            if ser is not None and not ser.empty:
                resolved[s] = (v, ser)
                break
    return resolved


def get_portfolio_closes_history(symbols, period='1y'):
    """Per-symbol daily RAW close history for portfolio NAV backfill.

    Returns {"closes": {input_symbol: [[YYYY-MM-DD, close], ...]},
             "resolved": {sym: yf_sym}, "missing": [sym, ...]}.

    Each input symbol is resolved via _resolve_for_history, which tries common
    exchange suffixes (.TO, .L, .AX, .HK) when the bare symbol
    returns no data.

    Closes are UNADJUSTED (auto_adjust=False), deliberately. The C++ caller
    values each date with the quantities the transaction log says were held
    ON THAT DATE, so prices must be what the shares actually traded at then.
    Adjusted closes fold splits and dividends into past prices — the right
    companion for a today's-quantities back-projection, which is exactly the
    model this replaced (it manufactured a phantom NAV cliff at every date
    where the share count has since changed). Splits belong in the replay as
    SPLIT transactions; dividends are cash the holdings-only NAV never held.

    Dates ship as plain YYYY-MM-DD strings: an epoch round-trip renders daily
    bars a day early in negative-UTC timezones.
    """
    try:
        syms = [s for s in (symbols or []) if s]
        if not syms:
            return {"closes": {}, "resolved": {}, "missing": []}

        resolved = _resolve_for_history(syms, period, auto_adjust=False)
        out = {}
        for s, (_yf_sym, closes) in resolved.items():
            series = []
            for idx, val in closes.items():
                try:
                    if val is None or pd.isna(val):
                        continue
                    series.append([pd.Timestamp(idx).strftime("%Y-%m-%d"), round(float(val), 4)])
                except (TypeError, ValueError):
                    continue
            if series:
                out[s] = series
        if not out:
            return {"closes": {}, "resolved": {}, "missing": syms,
                    "error": "no historical data for any symbol"}
        return {
            "closes": out,
            "resolved": {s: yf for s, (yf, _) in resolved.items()},
            "missing": [s for s in syms if s not in out],
        }
    except Exception as e:
        return {"error": str(e), "closes": {}}


def _yf_ticker_with_fallback(symbol):
    """Return (ticker, canonical_symbol).

    yfinance requires a hyphen for US class-share suffixes (BRK-B, BF-B,
    MOH-A, …) but brokerage exports often use a dot (BRK.B).  When a
    Ticker is constructed with the dot form it may return empty history for
    periods longer than 1d even though the quote endpoint still works.
    Return the hyphen variant as a fallback only when the suffix is a single
    uppercase letter — exchange-qualified symbols like SAP.DE / BP.L are
    left unchanged.
    """
    import re
    alt = re.sub(r'\.([A-Z])$', r'-\1', symbol)
    return yf.Ticker(alt), alt


# What separates "the vendor did not apply this split" from "the price really
# moved"? Both leave a level shift, so ONE observation — a close-to-close ratio —
# cannot tell them apart, and any fixed cutoff on it is a guess about how far a
# real move can go. That guess is what these constants used to be, and it was
# wrong in both directions: at a 25% window a correctly-adjusted 3:2 on an 11%
# down day looked exactly like an unapplied one, so the cutoffs were pushed out
# to 1.8/0.55 — which still left a live false-positive window (a real -35% to
# -52% bar at the forward cutoff, +58% to +114% at the reverse one) and made
# every 3:2 and 5:4 permanently unrepairable.
#
# The evidence that actually decides it: a split is a PURE RESCALING. Open, High,
# Low and Close are all multiplied by the same number, so the four ratios across
# the boundary agree with each other. A real move is not a rescaling — it changes
# the shape of the bar, and the four ratios scatter. Measured on live data:
#
#     APH's unapplied 2:1   1.888 1.906 1.905 1.925   dispersion  1.95%
#     six applied splits    mean ratio 0.94 - 1.02    dispersion  1.0 - 4.0%
#     genuine large moves   (ORCL, DELL, MRVL, IONQ, SMCI, CVNA)  2.8 - 35.6%
#
# So the test is now two-sided: the four ratios must agree with EACH OTHER (this
# is a rescaling) and their mean must agree with k (it is THIS rescaling). The
# applied case fails the second — its mean sits at 1.0, not k.
#
# The second guess removed is "how far from 1 must k be", and this one was
# MEASURED rather than picked. Over 18,468 boundaries across 50 symbols (20
# deliberately volatile small caps, 30 large caps, two years daily), every real
# move that was ALSO rescaling-shaped was tabulated:
#
#     max span of a rescaling-shaped real move   1.2801  (BBAI 2025-03-07)
#     next largest                               1.268   (PLTR), 1.252 (INTC)
#     p99 / p99.9 in units of the series' own SD  4.0 / 8.5
#
# So 1.35 is not a taste: it sits above everything the market actually produced,
# with headroom. It is also why a 5:4 is genuinely undecidable — real moves reach
# 1.28, so that ratio is inside the noise — while a 3:2 is now repairable, which
# the old fixed 1.8 cutoff refused.
#
# A pure SD multiple cannot do this job alone: the tail reaches 16 SD (CSCO at
# span 1.158), because a placid mega-cap's earnings gap is enormous relative to
# its own volatility while being a small move in absolute terms. So the floor is
# the MAX of the two — an absolute bound that kills the low-volatility tail, and
# an SD-relative bound that tightens automatically for the volatile names, which
# are exactly the ones that reverse-split. At an absolute floor of 1.30 or above
# the measured false-positive count is zero at every multiple tried (4x-10x); 8x
# is taken for defence in depth against series more volatile than this sample,
# and it degrades safely — a too-high floor declines to repair, it never
# mis-repairs.
_SPLIT_TOLERANCE = 0.15        # |mean OHLC ratio - k| <= this * k
_SPLIT_DISPERSION_MAX = 0.06   # (max-min)/mean across the four ratios
_SPLIT_VOL_MULTIPLE = 8.0      # k must beat this many SDs of the series' own 1-bar change
_SPLIT_MIN_SEPARATION = 1.35   # absolute floor: above the largest measured real move (1.2801)


def _bar_change_sd_pct(hist):
    """Typical 1-bar move for THIS series, as a fraction, outliers trimmed.

    Median across the OHLC columns per bar (one bad column cannot dominate),
    then an interquartile trim so the very events being hunted — splits, gaps,
    100x errors — do not inflate the notion of "normal" and hide themselves.
    """
    cols = [c for c in ("Open", "High", "Low", "Close") if c in hist.columns]
    if len(cols) < 2 or len(hist) < 30:
        return None
    import statistics
    ratios = []
    prev = None
    for _, row in hist[cols].iterrows():
        cur = [float(row[c]) for c in cols]
        if any(v != v or v <= 0 for v in cur):
            prev = None if any(v != v for v in cur) else prev
            continue
        if prev is not None:
            ratios.append(statistics.median([c1 / c0 for c1, c0 in zip(cur, prev)]))
        prev = cur
    if len(ratios) < 30:
        return None
    ratios.sort()
    n = len(ratios)
    q1, q3 = ratios[n // 4], ratios[(3 * n) // 4]
    iqr = q3 - q1
    lo, hi = q1 - 1.5 * iqr, q3 + 1.5 * iqr
    kept = [r for r in ratios if lo <= r <= hi]
    if len(kept) < 20:
        return None
    mean = statistics.fmean(kept)
    if mean <= 0:
        return None
    return statistics.pstdev(kept) / mean


def _straddles_split(hist, boundary, k, tol):
    """True when the bar AT `boundary` itself contains the split.

    A daily bar is entirely on one side of a split. A weekly one need not be:
    Yahoo builds the week from daily prints without re-basing them, so the bar
    covering a mid-week split opens pre-split and closes post-split. APH's split
    week is the last bar of a 10y weekly frame and opens near 158 against a close
    of 82.33 — its four column ratios scatter 64%, and a shape test reads that
    (correctly) as "not a rescaling" and declines, leaving all 520 earlier bars
    on the wrong basis to save one bar that is corrupt either way.

    The bar's own open-to-close ratio matching k is the signature, and it says
    precisely which fields can still be trusted: Close is on the new basis, the
    previous bar's Close on the old one, and everything in between is mixed.
    Comparing those two Closes is not weaker evidence — it is the only evidence
    the bar has left, and the volatility and separation floors still gate it.
    """
    try:
        row = hist.iloc[boundary]
        o, c = float(row["Open"]), float(row["Close"])
    except (KeyError, TypeError, ValueError):
        return False
    if not (o > 0 and c > 0):
        return False
    return abs(o / c - k) <= tol * k


def _boundary_ratios(hist, boundary, cols):
    """The per-column ratios across `boundary`, bridging price-less bars.

    Bridging is what makes this fire where yfinance's own repair does not: it
    compares strictly ADJACENT bars, so when Yahoo leaves the split-date bar
    price-less — as it did for APH — the one step carrying the evidence becomes
    NaN and is coerced to "no change" (scrapers/history.py, `_1d_change_minx[f_na] = 1.0`),
    and the cliff is invisible to it.
    """
    before = hist.iloc[:boundary].dropna(subset=cols)
    after = hist.iloc[boundary:].dropna(subset=cols)
    if before.empty or after.empty:
        return None
    pb, pa = before.iloc[-1], after.iloc[0]
    out = []
    for c in cols:
        lo, hi = float(pb[c]), float(pa[c])
        if not (lo > 0 and hi > 0):
            return None
        out.append(lo / hi)
    return out


def _repair_unadjusted_splits(hist, symbol=""):
    """Back-adjust a history whose splits the vendor announced but never applied.

    On the session a split takes effect Yahoo flips the quote to the new basis
    before it rewrites the historical bars, so the frame reports the split in its
    actions column while every pre-split bar is still on the old one. The series
    then carries a one-bar cliff of exactly the split ratio, and nothing
    downstream can tell it from a crash: APH's 2-for-1 on 2026-09-03 put the
    price 43% under all six moving averages, drove CCI to -539 and Aroon-down to
    100, and the Technicals verdict came back SELL on a stock trading near its
    highs. The 52-week low, the chart and every moving-average distance were
    wrong the same way, because all of them read this one series.

    Three independent things must hold before anything is rewritten. See the
    constants above for why each one is evidence rather than a tuned guess:

      1. the four OHLC ratios across the boundary agree with each other
         — this is a rescaling, not a move;
      2. their mean agrees with the announced ratio — it is THIS rescaling,
         which is what separates an unapplied split (mean = k) from one the
         vendor already applied (mean = 1);
      3. the ratio is large relative to what this particular series does in a
         normal bar.

    yfinance ships its own repair (`history(repair=True)`) and it does NOT catch
    this case: asked for APH it logs "No bad splits detected", because it
    compares strictly adjacent bars and Yahoo left the split-date bar price-less,
    so the decisive step became NaN and was coerced to "no change". It also costs
    a 1h and a 15m refetch per symbol to reconstruct the missing bars (2.3x wall
    time on APH) and, having declined the split, returns three reconstructed bars
    still on the OLD basis — a worse series than the one it started with. Its
    discriminator is nevertheless the better idea, and points 1 and 3 above are
    taken from it.

    Splits are read from the frame's own "Stock Splits" column rather than from
    Ticker.splits: a fresh Ticker's .splits triggers its own history fetch, which
    would put one ungated network call per symbol inside a path whose entire
    purpose is a single bulk download, and concurrent yfinance downloads return
    each other's tickers. Reading the column also means the split timestamp IS an
    index value, so there is no timezone to reconcile and no way for the splits to
    describe a different series than the bars.

    The boundary is positional, which is what makes one rule cover every
    interval. A weekly bar is stamped at the start of its week and carries the
    split on that same bar, so searchsorted lands on the first bar of the new
    basis for daily and weekly alike; comparing timestamps against the split
    instant instead put all 105 of APH's weekly bars on the "before" side and
    skipped the repair entirely while the 47% cliff stayed in the weekly rating
    (the 1W toggle is a live path). The j-1 candidate is defence for any feed that
    stamps the action on the preceding bar — the tests above decide which
    boundary is real, so an extra candidate cannot pick a wrong one.

    Volume moves the other way, as yfinance's own adjustment does; MFI, OBV, CMF
    and VWAP all read it.

    Fail-soft by construction: callers wrap this in `except Exception` and turn a
    raise into an error result, which would blank the chart, the technicals AND
    the 52-week band. A repair that cannot run must leave the series alone, not
    delete it.
    """
    try:
        if hist is None or hist.empty or "Close" not in hist.columns:
            return hist
        actions = hist.get("Stock Splits")
        if actions is None:
            return hist
        announced = actions[actions.notna() & (actions != 0)]
        if announced.empty:
            return hist
        ohlc = [c for c in ("Open", "High", "Low", "Close") if c in hist.columns]
        price_cols = [c for c in ("Open", "High", "Low", "Close", "Adj Close")
                      if c in hist.columns]
        if len(ohlc) < 2 or not price_cols:
            return hist

        idx = hist.index
        sd_pct = _bar_change_sd_pct(hist)
        for split_ts, ratio in announced.items():
            try:
                k = float(ratio)
            except (TypeError, ValueError):
                continue
            if not k > 0:
                continue
            k_span = max(k, 1.0 / k)
            # How far from 1 does a ratio have to be before it cannot be a normal
            # bar for THIS series? A constant cannot answer that; the series can.
            floor = _SPLIT_MIN_SEPARATION
            if sd_pct is not None:
                floor = max(floor, 1.0 + _SPLIT_VOL_MULTIPLE * sd_pct)
            if k_span < floor:
                continue

            j = int(idx.searchsorted(split_ts))
            for boundary in (j, j - 1):
                if boundary <= 0 or boundary >= len(idx):
                    continue
                straddle = _straddles_split(hist, boundary, k, _SPLIT_TOLERANCE)
                cols_used = ["Close"] if straddle else ohlc
                rs = _boundary_ratios(hist, boundary, cols_used)
                if not rs:
                    continue
                mean_r = sum(rs) / len(rs)
                if mean_r <= 0:
                    continue
                # (1) a rescaling multiplies every column by the SAME number.
                # Skipped for a straddling bar, whose columns sit on two
                # different bases by construction — see _straddles_split.
                if not straddle and (max(rs) - min(rs)) / mean_r > _SPLIT_DISPERSION_MAX:
                    continue
                # (2) and by THIS number — an applied split sits at 1, not k.
                if abs(mean_r - k) > _SPLIT_TOLERANCE * k:
                    continue

                # A boolean mask indexes .loc BY POSITION, which is the only form
                # that survives a duplicated timestamp. Selecting with labels
                # matches EVERY occurrence of a repeated stamp, so a bar after the
                # boundary sharing a label with one before it was divided by k
                # too — a correctly-basised bar silently halved. yfinance does
                # emit duplicate stamps (synthetic action rows, DST collisions).
                sel = [i < boundary for i in range(len(hist))]
                for col in price_cols:
                    hist.loc[sel, col] = hist.loc[sel, col] / k
                # Per-share cash amounts sit on the same basis as the prices: a
                # dividend declared before a 2:1, quoted post-split, is half the
                # figure. Rescaled so the frame handed back is self-consistent.
                for col in ("Dividends", "Capital Gains"):
                    if col in hist.columns:
                        hist.loc[sel, col] = hist.loc[sel, col] / k
                if "Volume" in hist.columns:
                    # int64 in, float out: assigning floats into an int column is
                    # a FutureWarning in pandas 2 and raises in pandas 3, and a
                    # raise here costs the whole series (see docstring).
                    hist["Volume"] = hist["Volume"].astype("float64")
                    hist.loc[sel, "Volume"] = hist.loc[sel, "Volume"] * k
                import sys
                print("[yfinance_data] split-repair %s: vendor left its %g:1 on %s "
                      "unadjusted (%s ratio mean %.3f, spread %.1f%%); "
                      "back-adjusted %d bars%s"
                      % (symbol or "?", k,
                         getattr(split_ts, "date", lambda: split_ts)(),
                         "close-only" if straddle else "OHLC",
                         mean_r, 100.0 * (max(rs) - min(rs)) / mean_r, boundary,
                         "; bar %d straddles the split and stays mixed" % boundary
                         if straddle else ""),
                      file=sys.stderr)
                break
        return hist
    except Exception as e:
        import sys
        print(f"[yfinance_data] split-repair skipped for {symbol}: {e!r}", file=sys.stderr)
        return hist


def get_historical_period(symbol, period='6mo', interval='1d', auto_adjust=True):
    """Fetch historical data using a period string ('1mo', '6mo', '1y', '5y')
    or a custom date range encoded as 'range:YYYY-MM-DD:YYYY-MM-DD'. The
    range form drives yfinance's start/end parameters so the C++ side can
    pass any user-chosen window through the same daemon action."""
    _trace_reset()

    def _window():
        import price_sources as ps
        start, end = ps.period_to_range(period)
        # yfinance's range end is exclusive; the vendors' are inclusive.
        if isinstance(period, str) and period.startswith("range:"):
            end = end - timedelta(days=1)
        return start, end

    vendor = _vendor_bars(symbol, _window, interval, auto_adjust)
    if vendor is not None:
        return vendor
    try:
        import io, contextlib
        ticker = yf.Ticker(symbol)
        _buf = io.StringIO()

        # Detect the range: prefix and route to start/end. Period strings
        # like '1mo'/'1y' fall through to the legacy period= path so existing
        # callers and the QueryStore cache keys keep working.
        is_range = isinstance(period, str) and period.startswith("range:")
        range_start = range_end = None
        if is_range:
            parts = period.split(":")
            if len(parts) >= 3 and parts[1] and parts[2]:
                range_start, range_end = parts[1], parts[2]
            else:
                is_range = False  # malformed — fall back to default period

        with contextlib.redirect_stdout(_buf):
            if is_range:
                hist = ticker.history(start=range_start, end=range_end, interval=interval, auto_adjust=auto_adjust)
            else:
                hist = ticker.history(period=period, interval=interval, auto_adjust=auto_adjust)

        # BRK.B-style dot notation returns empty history for periods > 1d
        # even though the 1d quote endpoint succeeds.  Retry with the
        # canonical hyphen form (BRK-B) before giving up.
        # The primary request's refusal is remembered: the dash-form retry
        # answering empty must not turn a refused BRK.B into "no bars".
        primary_refused = hist.empty and _trace_last_refused()
        primary_why = _trace_refusal_text() if primary_refused else None
        if hist.empty:
            ticker_alt, _ = _yf_ticker_with_fallback(symbol)
            if ticker_alt.ticker != symbol:
                with contextlib.redirect_stdout(_buf):
                    if is_range:
                        hist = ticker_alt.history(start=range_start, end=range_end,
                                                  interval=interval, auto_adjust=auto_adjust)
                    else:
                        hist = ticker_alt.history(period=period, interval=interval,
                                                  auto_adjust=auto_adjust)

        if hist.empty:
            # yfinance turns a refused request (401 crumb, a 429 on the
            # timezone probe) into an empty frame plus a log line. That is
            # not "no bars for this period" — judged by the request the empty
            # answer came from, not one yfinance already recovered from.
            if primary_refused or _trace_last_refused():
                why = primary_why or _trace_refusal_text()
                return {"error": f"Yahoo refused or did not answer ({why}) — try again",
                        "transient": True, "symbol": symbol}
            return []

        # A split Yahoo has announced but not yet applied to the bars would
        # otherwise reach the chart, the technicals and the 52-week band as a
        # ~50% crash. Repaired here, at the one chokepoint all three share.
        hist = _repair_unadjusted_splits(hist, symbol)

        # PRICE BASIS is the CALLER's decision, defaulting to total-return
        # (auto_adjust=True) and pinned rather than inherited from a library
        # default that could change under us.
        #
        # Charts, technicals and the 52-week band want adjusted: they ask
        # "how did this investment perform", and a dividend is performance.
        # A trailing stop asks a different question — "how far below its
        # actual high is it trading now" — and must compare like with like
        # against a raw live price, so the portfolio's peak-high fetch passes
        # auto_adjust=False. Mixing the two silently misstated every derived
        # distance, and the direction of the error grew with holding period
        # and yield.
        historical_data = []
        for index, row in hist.iterrows():
            # A bar with no prices is not a bar — see _has_prices.
            if not _has_prices(row['Open'], row['High'], row['Low'], row['Close']):
                continue
            historical_data.append({
                "timestamp": int(index.timestamp()),
                "open": _round_price(row['Open']),
                "high": _round_price(row['High']),
                "low": _round_price(row['Low']),
                "close": _round_price(row['Close']),
                # int() is the only conversion in this row that RAISES on NaN
                # rather than passing a bad value along, and the caller's
                # except-Exception would turn that into {"error": ...} — one
                # bad volume cell blanking the whole multi-year series, and
                # with it the chart, technicals and TALIpp. A priced bar with
                # an unknown volume is still a priced bar.
                "volume": int(row['Volume']) if _has_prices(row['Volume']) else 0
            })

        return historical_data
    except Exception as e:
        # YFRateLimitError lands here: Yahoo's 429 on the chart request. A
        # 401 often surfaces as a yfinance TypeError on the error body — the
        # status is appended so the log says what it really was.
        refused = _trace_last_refused()
        return {"error": f"{type(e).__name__}: {e}" + (f" [{_trace_refusal_text()}]" if refused else ""),
                "symbol": symbol, "transient": refused or "RateLimit" in type(e).__name__}

def main(args=None):
    # Support both worker pool (args parameter) and subprocess/CLI (sys.argv)
    if args is None:
        args = sys.argv[1:]

    if len(args) < 1:
        return json.dumps({"error": "Usage: python yfinance_data.py <command> <args>"})

    command = args[0]

    if command == "quote":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py quote <symbol>"}
        else:
            symbol = args[1]
            result = get_quote(symbol)

    elif command == "batch_quotes":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py batch_quotes <symbol1> <symbol2> ..."}
        else:
            symbols = args[1:]
            result = get_batch_quotes(symbols)

    elif command == "historical":
        if len(args) < 4:
            result = {"error": "Usage: python yfinance_data.py historical <symbol> <start_date> <end_date> [interval]"}
        else:
            symbol = args[1]
            start_date = args[2]
            end_date = args[3]
            interval = args[4] if len(args) > 4 else '1d'
            result = get_historical(symbol, start_date, end_date, interval)

    elif command == "info":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py info <symbol>"}
        else:
            symbol = args[1]
            result = get_info(symbol)

    elif command == "financials":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py financials <symbol>"}
        else:
            symbol = args[1]
            result = get_financials(symbol)

    elif command == "financial_ratios":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py financial_ratios <symbol>"}
        else:
            symbol = args[1]
            result = get_financial_ratios(symbol)

    elif command == "multiple_ratios":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py multiple_ratios <symbol1,symbol2,...>"}
        else:
            symbols = args[1].split(",")
            result = get_multiple_ratios(symbols)

    elif command == "search":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py search <query> [limit]"}
        else:
            query = args[1]
            limit = int(args[2]) if len(args) > 2 else 50
            result = search_symbols(query, limit)

    elif command == "news":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py news <symbol> [count]"}
        else:
            symbol = args[1]
            count = int(args[2]) if len(args) > 2 else 20
            result = get_news(symbol, count)

    elif command == "portfolio_closes_history":
        # Args layout: <period> <sym1> <sym2> ...
        if len(args) < 3:
            result = {"error": "Usage: yfinance_data.py portfolio_closes_history <period> <sym1> ..."}
        else:
            result = get_portfolio_closes_history(args[2:], args[1])

    elif command == "historical_period":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py historical_period <symbol> [period] [interval]"}
        else:
            symbol = args[1]
            period = args[2] if len(args) > 2 else '6mo'
            interval = args[3] if len(args) > 3 else '1d'
            result = get_historical_period(symbol, period, interval)

    elif command == "batch_sparklines":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py batch_sparklines <sym1> <sym2> ..."}
        else:
            symbols = args[1:]
            result = get_batch_sparklines(symbols)

    elif command == "batch_all":
        # Unified hub-refresh endpoint: payload is a single JSON blob on argv[1]
        # describing quotes + sparklines
        # + histories to fetch. One process, one yfinance warm-up, three family
        # results. See MarketDataService::refresh().
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py batch_all <json_payload>"}
        else:
            try:
                result = get_batch_all(json.loads(args[1]))
            except json.JSONDecodeError as e:
                result = {"error": f"batch_all: invalid JSON payload: {e}"}

    elif command == "historical_price":
        # Get closing price for a specific date
        if len(args) < 3:
            result = {"error": "Usage: python yfinance_data.py historical_price <symbol> <date>"}
        else:
            symbol = args[1]
            target_date = args[2]  # Format: YYYY-MM-DD
            result = get_historical_price(symbol, target_date)

    else:
        result = {"error": f"Unknown command: {command}"}

    # Return JSON for worker pool, print for subprocess/CLI
    # IMPORTANT: Do NOT use indent=2 here. The host subprocess parser
    # looks for the last line starting with '{' or '[' to extract JSON.
    # Pretty-printed JSON puts '{' alone on the first line, breaking parsing.
    output = json.dumps(result)
    print(output)
    return output

# ── Daemon mode ──────────────────────────────────────────────────────────────
#
# Long-lived worker invoked by PythonWorker on the C++ side. Reads length-prefixed
# JSON request frames from stdin, writes length-prefixed JSON response frames to
# stdout. Keeps yfinance + pandas loaded so MarketDataService refreshes don't pay
# the 2–3s import cost on every spawn.
#
# Frame format (stdin + stdout, network byte order):
#   [4 bytes big-endian uint32 length N] [N bytes UTF-8 JSON]
#
# Request JSON:  {"id": <int>, "action": "batch_all"|"batch_quotes"|..., "payload": {...}}
# Response JSON: {"id": <int>, "ok": true, "result": <any>}  or
#                {"id": <int>, "ok": false, "error": "<msg>"}
#
# Special action "shutdown" causes a clean exit. EOF on stdin also exits.

def _daemon_read_frame(stream):
    """Read one length-prefixed frame. Returns bytes payload, or None on EOF."""
    header = b""
    while len(header) < 4:
        chunk = stream.read(4 - len(header))
        if not chunk:
            return None
        header += chunk
    n = int.from_bytes(header, byteorder="big", signed=False)
    if n == 0 or n > 64 * 1024 * 1024:  # sanity: cap frames at 64 MB
        return None
    buf = b""
    while len(buf) < n:
        chunk = stream.read(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def _daemon_write_frame(stream, data_bytes):
    """Write one length-prefixed frame. `data_bytes` must be bytes."""
    n = len(data_bytes)
    stream.write(n.to_bytes(4, byteorder="big", signed=False))
    stream.write(data_bytes)
    stream.flush()


def _sanitize_for_json(obj):
    """Replace NaN / +Inf / -Inf with None recursively.

    Python's `json.dumps` emits these as bare `NaN` / `Infinity` tokens by
    default (allow_nan=True) — that is invalid per the JSON spec and Qt's
    QJsonDocument::fromJson rejects the whole frame with "illegal number".
    We sanitize on the daemon side so every action's result is safe to
    transmit, regardless of how careful the implementation was about
    pandas/numpy NaN handling.
    """
    import math as _m
    if isinstance(obj, float):
        if _m.isnan(obj) or _m.isinf(obj):
            return None
        return obj
    if isinstance(obj, dict):
        return {k: _sanitize_for_json(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_sanitize_for_json(v) for v in obj]
    return obj


# ── Response cache (per-action TTL) ───────────────────────────────────────────
# Yahoo Finance rate-limits aggressively (HTTP 429), and the SAME read is often
# requested many times in a short window: the 20s quote-refresh tick racing a
# manual action, several dashboard panels asking for the same symbol, or an AI
# agent calling get_quote / get_info / get_financials on one ticker in sequence.
# yfinance already bounds each HTTP call (10s download / 30s info) and uses
# curl_cffi browser impersonation, so the residual pain is duplicate round-trips
# — each one re-pays the latency AND adds to the rate-limit pressure that makes
# the NEXT call fail. Caching successful results for a short, per-action TTL
# collapses those duplicates: fewer 429s, near-instant repeats, and the daemon's
# worker pool stops getting tied up re-fetching data it just had.
#
# Invisible by construction: the TTLs are shorter than (or comparable to) each
# consumer's own refresh cadence, so a caller never sees data older than it
# would have tolerated anyway. Errors are never cached, so a transient failure
# self-heals on the next request rather than sticking for the whole TTL.
_CACHE_TTL = {
    # live-ish prices — short (the 20s refresh tick still always gets fresh data).
    # NB: quote_orderbook is intentionally NOT cached — it's the live bid/ask
    # spread fetched on focus-mode enter, where freshness matters and the
    # duplicate-call pressure the cache targets doesn't apply; caching it would
    # also pin a transient all-zeros (swallowed 429) for the TTL.
    "quote": 8, "batch_quotes": 8, "batch_all": 8,
    "extended_hours": 8,
    # two history calls and two Nasdaq requests; the panel asks once a minute
    "extended_sessions": 45,
    # index_futures: the futures bars it reads are ~10 min delayed, and a TTL
    # just under the consumers' 60 s poll makes every tile and chart dialog
    # share one download a minute instead of one each.
    "index_futures": 55, "batch_sparklines": 30, "top_movers": 30,
    # one batch-quote request per curve; the back months print rarely, and the
    # past sessions' settles behind the comparison curves do not change at all.
    "futures_curve": 20, "futures_curve_history": 1800, "futures_spread_history": 900,
    # slow-moving fundamentals / metadata — long (change at most daily)
    # Kept well inside the app's own cache of the same data (the reader sees
    # the sum of both): info is 10 min in the app, so 4 min here.
    "info": 240, "batch_info": 600, "financials": 600, "financial_ratios": 600,
    # quarterly statements and rating changes — move a few times a quarter
    "report_extras": 600,
    # an industry's membership and ranking move over months
    "industry_peers": 3600,
    # a target changes when an analyst publishes — minutes matter on those days
    "analyst_targets": 300,  # the Overview asks every 10 minutes
    "multiple_ratios": 600, "ipo_extras": 600,
    # earnings_dates carries the reported figure for the quarter that just
    # printed, which Yahoo fills in within hours of the call — the tile is
    # read on exactly those days, so it is not a 10-minute-stale field.
    "earnings_dates": 300,
    # earnings_analysis pulls estimates + 3y of daily bars — expensive, but it
    # must stay UNDER the app's own 180 s limit for it (EquityResearchService
    # kEarningsAnalysisTtlSec): at 900 here, four of every five of the app's
    # refreshes were handed the same copy back and the tab ran 15+ minutes old.
    "earnings_analysis": 150,
    # historical / news / search — medium
    "historical_period": 120, "news": 120, "search": 300,
    # a full chain is one request per expiry; quotes are delayed anyway
    "options_surface": 300,
    # batched close history over a date range — long: past daily closes don't
    # change, so a 1h TTL avoids re-downloading wide ranges. No C++ caller
    # since Power Trader was removed; kept as generic multi-symbol history.
    "batch_closes": 3600,   # keyed on a payload carrying end=today, so a stale hit is not possible
    # per-symbol raw closes for portfolio NAV backfill — same rationale.
    "portfolio_closes_history": 3600,
    # static SEC filing — very long
    # institutional holders move quarterly and short interest twice monthly.
    # Ten minutes, not an hour: the app keeps its own one-hour copy above this
    # (OwnershipService), and two stacked hours showed data two hours old.
    "ownership_extras": 600,
    "parse_s1": 3600,
    # one print's call webcast (IR site) and release (EDGAR). The app's
    # release-time schedule bypasses this with `fresh` when it matters.
    "earnings_links": 300,
}
_CACHE_MAX = 512  # hard cap on entries; evict soonest-to-expire beyond this

_cache_lock = threading.Lock()
_cache = {}  # key -> (expires_at_monotonic, result)


def _cache_key(action, payload):
    """Stable key over (action, payload). default=str tolerates odd value types
    (e.g. tuples, numpy scalars) that would otherwise break json.dumps."""
    try:
        return action + "|" + json.dumps(payload, sort_keys=True, default=str)
    except Exception:
        return action + "|" + repr(payload)


def _cache_get(key):
    now = time.monotonic()
    with _cache_lock:
        ent = _cache.get(key)
        if ent is None:
            return None
        if ent[0] <= now:           # expired — drop it
            _cache.pop(key, None)
            return None
        return ent[1]


def _cache_put(key, ttl, result):
    with _cache_lock:
        _cache[key] = (time.monotonic() + ttl, result)
        if len(_cache) > _CACHE_MAX:
            # Evict the soonest-to-expire entries. heapq.nsmallest is O(n) for a
            # small overflow vs a full O(n log n) sort — this runs under the lock
            # on the hot dispatch path once the cache saturates, so keep it cheap.
            overflow = len(_cache) - _CACHE_MAX
            for k in heapq.nsmallest(overflow, _cache, key=lambda k: _cache[k][0]):
                _cache.pop(k, None)


def _is_cacheable_result(result):
    """Decide whether a dispatch result is a real success worth caching.

    We must NOT cache transient failures, or a rate-limited blip sticks for the
    whole TTL instead of self-healing on the next request. Beyond the standard
    {"error": ...} shape, daemon actions signal trouble three other ways:
      • partial-failure dicts carry a "<section>_error" key (ipo_extras's
        holders_error / financials_error, top_movers's gainers_error /
        losers_error) — reject the whole result so the failed part is retried;
      • no-data / all-failed actions return an empty list or empty dict
        (e.g. multiple_ratios filters out every errored symbol → []);
      • a bare {"symbol": X} with no data keys is an action that caught every
        sub-fetch — a failure in disguise.
    Substantive lists (get_batch_info records "—" inline by design, so a list
    with some inline-error rows is still worth caching) and dicts carrying real
    data are cacheable.
    """
    if result is None:
        return False
    if isinstance(result, list):
        # a row Yahoo refused must be asked again, not served for the TTL
        if any(isinstance(r, dict) and r.get("transient") for r in result):
            return False
        return len(result) > 0
    if isinstance(result, dict):
        if not result:
            return False
        for k, v in result.items():
            if k in ("error", "fatal") or (isinstance(k, str) and k.endswith("_error")):
                return False
            if isinstance(k, str) and k.endswith("_transient") and v:   # a refused part
                return False
            # A section list carrying a refused row (batch_all's "quotes"):
            # the row must be asked again, not served for the TTL.
            if isinstance(v, list) and any(isinstance(r, dict) and r.get("transient") for r in v):
                return False
        if set(result.keys()) <= {"symbol"}:
            return False
        return True
    return True


# ── Freshness around a report ────────────────────────────────────────────────
# Within a few days either side of a print, an earnings payload is the fastest
# moving data this daemon serves: the consensus is still being revised, the
# reported figure lands, and the price reaction only becomes computable once
# the following session closes. A quarter-hour cache is right for the eleven
# weeks in between and badly wrong for those few days — it is exactly when
# someone is looking, and exactly when they would be shown yesterday's answer.
_EARNINGS_HOT_WINDOW_SEC = 3 * 86400
_EARNINGS_HOT_TTL_SEC = 120


def _effective_ttl(action, result, default_ttl):
    """Cache lifetime for one result — shortened near a report."""
    if action == "earnings_links" and isinstance(result, dict):
        # An IR site that timed out is worth asking again soon. Print-day
        # freshness is the app's job: its refresh schedule sends `fresh`.
        return 45 if result.get("ir_error") else default_ttl
    if action != "earnings_analysis" or not isinstance(result, dict):
        return default_ttl
    try:
        now = time.time()
        stamps = []
        nxt = result.get("next") or {}
        if nxt.get("timestamp"):
            stamps.append(float(nxt["timestamp"]))
        # Newest REPORTED quarter. The projected row is skipped: when there is
        # no scheduled date it is stamped with the current time, which would
        # make every symbol look permanently hot.
        for row in result.get("history") or []:
            if row.get("is_estimate"):
                continue
            if row.get("timestamp"):
                stamps.append(float(row["timestamp"]))
            break
        for ts in stamps:
            if abs(now - ts) <= _EARNINGS_HOT_WINDOW_SEC:
                return _EARNINGS_HOT_TTL_SEC
    except Exception:
        pass
    return default_ttl


# ── Single-flight ────────────────────────────────────────────────────────────
# One in-flight upstream call per (action, payload). Without this, N concurrent
# misses of the SAME key each hit Yahoo: the cache is checked before the call
# and populated after, so every one of them sees a miss. Six network workers
# asking for the same symbol at once is not hypothetical — a portfolio refresh,
# a chart repaint and a technicals recompute routinely coincide on one ticker,
# and the duplicate load is what pushes the account into 429 rate limiting,
# which the user experiences as missing data rather than as slowness.
#
# Waiters block on an Event and then re-read the cache, so they get the
# leader's result without issuing a call of their own.
_inflight_lock = threading.Lock()
_inflight = {}  # key -> _Flight

# How long a follower waits on the leader before taking over. Deliberately
# NOT generous: the C++ client abandons a network action at 10s, so a longer
# wait just holds a worker for a request nobody is still listening for.
_INFLIGHT_WAIT_SEC = 15


class _Flight:
    """One in-flight upstream call, and the outcome it will publish.

    The result is handed to followers DIRECTLY rather than left for them to
    re-read from the cache. That distinction is the whole point: errors are
    deliberately never cached (a rate-limit blip must not stick), so a
    cache-only handoff collapses nothing in exactly the situation this
    exists for — every follower would find a miss and make its own call,
    having first waited out the leader's latency.
    """

    __slots__ = ("event", "result", "error", "done")

    def __init__(self):
        self.event = threading.Event()
        self.result = None
        self.error = None
        self.done = False


def _exception_reply(req_id, exc):
    """A failed reply for an action that RAISED. The body says whether it was
    Yahoo refusing (worth a retry) or a bug / unknown action (not) — without
    one, the C++ side reads any failure as "no answer, try again"."""
    msg = f"{type(exc).__name__}: {exc}"
    # By type: a programming error is not worth a retry; anything else
    # raised (network, curl, HTTP, rate limit) is treated as no answer —
    # what a body-less failure always meant before.
    # A 401 often surfaces as a yfinance TypeError on the error body: the
    # request the failure came from decides first.
    transient = _trace_last_refused() or not isinstance(
        exc, (KeyError, IndexError, TypeError, AttributeError, NameError,
              AssertionError, NotImplementedError, ZeroDivisionError))
    return {"id": req_id, "ok": False, "error": str(exc), "refused": transient,
            "result": {"error": msg, "transient": transient}}


def _reply_for(req_id, result):
    """The reply frame for an action's result. An action that failed RETURNS
    {"error": ...} (the CLI path prints it); sent as ok:true, every C++ caller
    had to remember result.contains("error") — the ones that didn't read a
    Yahoo 429 as an empty answer ("No data available for this period" on the
    Markets chart). A top-level error is a failed reply; the body still rides
    along so a caller can read "transient". Partial failures use
    "<section>_error" keys and stay ok."""
    refused = _is_refusal_result(result)
    if isinstance(result, dict) and result.get("error"):
        return {"id": req_id, "ok": False, "error": str(result["error"]), "refused": refused,
                "result": _sanitize_for_json(result)}
    return {"id": req_id, "ok": True, "refused": refused, "result": _sanitize_for_json(result)}


def _is_refusal_result(result):
    """Did Yahoo refuse (all or part of) this result? The one rule, next to
    _is_cacheable_result, that the worker's background back-off reads from
    the frame's "refused" flag: a top-level transient error, a refused
    section ("<section>_transient"), or refused rows ({"transient": true})
    in a list. Plain "no data" is not a refusal."""
    def rows_refused(v):
        return isinstance(v, list) and any(isinstance(r, dict) and r.get("transient") for r in v)
    if rows_refused(result):
        return True
    if isinstance(result, dict):
        if result.get("transient"):
            return True
        for k, v in result.items():
            if (isinstance(k, str) and k.endswith("_transient") and v) or rows_refused(v):
                return True
    return False


def _daemon_dispatch(action, payload):
    """Run one action and return the raw result object (not wrapped).

    Read-only actions are served from a short-TTL in-process cache (see
    _CACHE_TTL) so duplicate requests don't re-hit Yahoo, and identical
    concurrent misses are collapsed to a single upstream call (single-flight).
    The upstream call happens OUTSIDE the cache lock, so DIFFERENT keys still
    run in parallel.

    On the first HTTP 401 / Invalid-Crumb error the yfinance session is reset
    and the action is retried once so callers don't see a cascade of failures
    every time Yahoo Finance rotates its auth crumb mid-session.
    """
    # `fresh`: the caller knows the cached answer is out of date (an earnings
    # release just landed, the user pressed refresh). Skip the READ, still
    # share an in-flight call and still WRITE the result — under the same key
    # as an ordinary request, so the next ordinary reader gets the new answer.
    fresh = isinstance(payload, dict) and bool(payload.get("fresh"))
    if fresh:
        payload = {k: v for k, v in payload.items() if k != "fresh"}
    ttl = _CACHE_TTL.get(action)
    if not ttl:
        # Uncached actions are pass-through: no key, nothing to share.
        return _daemon_dispatch_with_crumb_retry(action, payload)

    key = _cache_key(action, payload)
    hit = None if fresh else _cache_get(key)
    if hit is not None:
        return hit

    while True:
        with _inflight_lock:
            flight = _inflight.get(key)
            if flight is None:
                # Re-check under the lock before committing to a call: a
                # leader can finish between the read above and this point,
                # and a burst of workers dispatched from one recv loop hits
                # that window routinely.
                hit = None if fresh else _cache_get(key)
                if hit is not None:
                    return hit
                flight = _Flight()
                _inflight[key] = flight
                is_leader = True
            else:
                is_leader = False

        if is_leader:
            try:
                result = _daemon_dispatch_with_crumb_retry(action, payload)
            except BaseException as exc:
                flight.error = exc
                flight.done = True
                raise
            else:
                flight.result = result
                flight.done = True
                if _is_cacheable_result(result):
                    _cache_put(key, _effective_ttl(action, result, ttl), result)
                return result
            finally:
                # Release waiters on every path, including failure: the whole
                # point is that a failed call is shared, not repeated.
                with _inflight_lock:
                    if _inflight.get(key) is flight:
                        del _inflight[key]
                flight.event.set()

        # Follower: take the leader's outcome, success or failure.
        if flight.event.wait(timeout=_INFLIGHT_WAIT_SEC) and flight.done:
            if flight.error is not None:
                raise flight.error
            return flight.result

        # The leader is hung (no timeout exists on yfinance's socket path).
        # Evict it — identity-checked so a newer leader is never disturbed —
        # and loop to take over. Leaving the entry would park every later
        # request on this key for the full wait, forever.
        with _inflight_lock:
            if _inflight.get(key) is flight:
                del _inflight[key]


def _daemon_dispatch_with_crumb_retry(action, payload):
    try:
        result = _daemon_dispatch_inner(action, payload)
    except Exception as e:
        if _is_crumb_error(e):
            _reset_yfinance_session()
            return _daemon_dispatch_inner(action, payload)
        raise
    # A RETURNED failure (most actions; sent as ok:false by _reply_for) is not
    # retried here: per the NOTE above, a session reset does not reset the
    # crumb, so a retry would only repeat the refused call.
    return result


def _daemon_dispatch_inner(action, payload):
    """Actual dispatch — called by _daemon_dispatch (with crumb-retry wrapper)."""
    if action == "batch_all":
        return get_batch_all(payload or {})
    if action == "batch_quotes":
        syms = (payload or {}).get("symbols") or []
        return get_batch_quotes(syms)
    if action == "batch_sparklines":
        syms = (payload or {}).get("symbols") or []
        return get_batch_sparklines(syms)
    if action == "historical_period":
        p = payload or {}
        # A failed fetch comes back {"error", "transient"} and goes out as a
        # failed reply (see _reply_for).
        return get_historical_period(
            p.get("symbol"), p.get("period", "6mo"), p.get("interval", "1d"),
            # Callers that compare against a RAW live price (the portfolio's
            # trailing-stop peak) pass auto_adjust=false explicitly.
            bool(p.get("auto_adjust", True)))
    if action == "batch_closes":
        p = payload or {}
        return batch_closes(p.get("symbols") or [], p.get("start"), p.get("end"))
    if action == "quote":
        return get_quote((payload or {}).get("symbol"))
    if action == "quote_orderbook":
        return get_orderbook((payload or {}).get("symbol"))
    if action == "info":
        return get_info((payload or {}).get("symbol"))
    if action == "batch_info":
        # Used by IPO Watch to enrich priced tickers with sector / industry /
        # market cap in one round-trip. ThreadPoolExecutor parallelism inside.
        return get_batch_info((payload or {}).get("symbols") or [])
    if action == "ownership_extras":
        return get_ownership_extras((payload or {}).get("symbol", ""))
    if action == "ipo_extras":
        # IPO Watch detail-rail aggregator: quarterly financials, holders,
        # news. One daemon round-trip on row click.
        return get_ipo_extras((payload or {}).get("symbol"))
    if action == "earnings_release_filed":
        # Polled each minute in a print's release window: one SEC request
        # says whether the earnings 8-K is up, and only then does the app pay
        # for the full analysis + links refresh. Uncached on purpose.
        import earnings_links
        p = payload or {}
        return earnings_links.release_filed(p.get("symbol"), p.get("report_date"))
    if action == "earnings_links":
        # ER Earnings NEXT REPORT header: for the print on report_date, the
        # call's webcast (IR site) and the release (8-K Item 2.02, EX-99.1).
        # Its own action so EDGAR / IR sites never delay earnings_analysis.
        import earnings_links
        p = payload or {}
        return earnings_links.get_earnings_links(p.get("symbol"), p.get("report_date"))
    if action == "parse_s1":
        # Parse an S-1 prospectus into its "Recent Sales of Unregistered
        # Securities" section for the FUNDING tab. URL comes from
        # fetch_sec_filings on the C++ side.
        return parse_s1_funding((payload or {}).get("url"))
    if action == "top_movers":
        # Real day's top gainers + losers via yfinance.screen. Replaces the
        # hardcoded 12-ticker watchlist used by TopMoversWidget.
        return get_top_movers((payload or {}).get("count", 10))
    if action == "earnings_dates":
        # Per-symbol earnings dates (past + upcoming) for the chart's
        # earnings markers. Backed by yfinance's ticker.earnings_dates so
        # we don't pay the Finnhub round-trip when the key isn't set.
        p = payload or {}
        return get_earnings_dates(
            p.get("symbol"),
            p.get("lookback_days", 1825),
            p.get("lookahead_days", 180),
        )
    if action == "earnings_analysis":
        # Full pre-earnings picture for the ER Earnings tab: surprise +
        # price-reaction history, live consensus and its revision trend, and
        # forward estimates. One call so the tab paints in a single hop.
        p = payload or {}
        return get_earnings_analysis(p.get("symbol"), p.get("quarters", 12))
    if action == "news":
        p = payload or {}
        return get_news(p.get("symbol"), p.get("count", 20))
    if action == "options_surface":
        # ER Options tab: IV surface, smile and ATM term structure from
        # Yahoo's delayed chain (one request per expiry).
        p = payload or {}
        try:
            n = int(p.get("max_expiries") or _OPT_MAX_EXPIRIES)
        except (TypeError, ValueError):
            n = _OPT_MAX_EXPIRIES
        return get_options_surface(p.get("symbol"), min(max(n, 2), 24))
    if action == "portfolio_closes_history":
        p = payload or {}
        return get_portfolio_closes_history(p.get("symbols") or [], p.get("period", "1y"))
    if action == "extended_hours":
        p = payload or {}
        symbols = p.get("symbols") or []
        return get_extended_hours_quotes(symbols)
    if action == "extended_sessions":
        # ER Overview: the latest after-hours and pre-market session of one
        # symbol, each against its own close. See extended_sessions.py.
        import extended_sessions
        return extended_sessions.fetch((payload or {}).get("symbol"))
    if action == "index_futures":
        return get_index_futures((payload or {}).get("pairs") or [])
    if action == "analyst_targets":
        return get_analyst_targets((payload or {}).get("symbol"))
    if action == "industry_peers":
        p = payload or {}
        return get_industry_peers(p.get("symbol"), p.get("count", 5))
    if action == "report_extras":
        return get_report_extras((payload or {}).get("symbol"))
    if action == "futures_curve":
        p = payload or {}
        return get_futures_curve(p.get("root"), p.get("exchange"), p.get("months", 30))
    if action == "futures_spread_history":
        p = payload or {}
        return get_futures_spread_history(p.get("near"), p.get("far"), p.get("period", "6mo"))
    if action == "futures_curve_history":
        return get_futures_curve_history((payload or {}).get("symbols") or [])
    if action == "financial_ratios":
        return get_financial_ratios((payload or {}).get("symbol"))
    if action == "compute_technicals":
        p = payload or {}
        return compute_technicals_from_candles(p.get("candles") or [])
    if action == "search":
        p = payload or {}
        return search_symbols(p.get("query", ""), p.get("limit", 20), p.get("type", ""))
    if action == "financials":
        return get_financials((payload or {}).get("symbol"))
    if action == "multiple_ratios":
        p = payload or {}
        syms = p.get("symbols") or []
        # get_multiple_ratios expects a list — accept either list or CSV
        # string for forward compatibility with CLI-style payloads.
        if isinstance(syms, str):
            syms = [s.strip() for s in syms.split(",") if s.strip()]
        return get_multiple_ratios(syms)
    return {"error": f"Unknown action: {action}"}


# Technical indicators — heavy `ta` library import is amortized over the
# lifetime of the daemon (loaded lazily on first request, then reused).
_TECHNICALS_MOD = None
_TECHNICALS_LOCK = threading.Lock()


def _load_technicals_modules():
    """Import the technicals package once; return the bundle. Thread-safe."""
    global _TECHNICALS_MOD
    if _TECHNICALS_MOD is not None:       # fast path: no lock needed after first load
        return _TECHNICALS_MOD
    with _TECHNICALS_LOCK:
        if _TECHNICALS_MOD is not None:   # double-checked: another thread may have loaded
            return _TECHNICALS_MOD
        # All imports and the assignment happen inside the lock so no thread
        # can observe a partially-initialised _TECHNICALS_MOD.
        import os as _os, sys as _sys
        _sd = _os.path.dirname(_os.path.abspath(__file__))
        if _sd not in _sys.path:
            _sys.path.insert(0, _sd)
        from technicals.momentum_indicators import calculate_all_momentum_indicators
        from technicals.volume_indicators import calculate_all_volume_indicators
        from technicals.volatility_indicators import calculate_all_volatility_indicators
        from technicals.trend_indicators import calculate_all_trend_indicators
        from technicals.others_indicators import calculate_all_others_indicators
        _TECHNICALS_MOD = {
            "momentum":   calculate_all_momentum_indicators,
            "volume":     calculate_all_volume_indicators,
            "volatility": calculate_all_volatility_indicators,
            "trend":      calculate_all_trend_indicators,
            "others":     calculate_all_others_indicators,
        }
    return _TECHNICALS_MOD


def compute_technicals_from_candles(candles):
    """
    Compute the full indicator suite from a candle list — the app's one
    technicals compute (the Technicals tab, MCP and the workflow node all
    read it), on the daemon's already-imported pandas + cached `ta` modules.

    candles: list of {timestamp/date, open, high, low, close, volume?}.
    Returns: {success, data: <merged indicator dict>, error?}
    """
    try:
        if not candles:
            return {"success": False, "error": "no candles", "data": []}
        try:
            mods = _load_technicals_modules()
        except Exception as e:
            return {"success": False, "error": f"technicals import failed: {e}", "data": []}

        df = pd.DataFrame(candles)
        # Tolerate either lowercase or capitalized column names.
        rename = {c: c.lower() for c in df.columns if c[0].isupper()}
        if rename:
            df = df.rename(columns=rename)
        for col in ("open", "high", "low", "close"):
            if col not in df.columns:
                return {"success": False, "error": f"missing column: {col}", "data": []}
            df[col] = pd.to_numeric(df[col], errors="coerce")
        if "volume" not in df.columns:
            df["volume"] = 0.0
        else:
            df["volume"] = pd.to_numeric(df["volume"], errors="coerce").fillna(0.0)
        df = df.dropna(subset=["close"]).reset_index(drop=True)

        # Each calculator returns the df with new indicator columns appended.
        # Order: trend, momentum, volatility, volume (only if available),
        # others.
        result = df.copy()
        for stage in ("trend", "momentum", "volatility", "volume", "others"):
            if stage == "volume" and "volume" not in result.columns:
                continue
            try:
                result = mods[stage](result)
            except Exception as e:
                # Don't abort the whole compute on one stage failure — just
                # surface it via a sentinel column the frontend can ignore.
                result[f"_{stage}_error"] = str(e)

        result = result.replace([float("inf"), float("-inf")], None)
        result = result.where(pd.notna(result), None)
        rows = result.to_dict("records")
        return {"success": True, "data": rows}
    except Exception as e:
        return {"success": False, "error": str(e), "data": []}


# Extended-hours (EXT%) rows — one per symbol, each extended session's latest
# print with the close it is measured against:
#
#   {symbol, session, regular, regular_ts, declined, [unavailable],
#    pre: {price, ts, day, ref, ref_ts, ref_date} | None,
#    post: {...} | None, currency, name}
#
# `regular` / `regular_ts` is the most recent completed close. The C++ side
# (ExtendedHoursMath.h) keeps each session separately, adds the overnight
# session from the push stream, and shows the latest print since that close.
#
# Plain US stocks read Yahoo's batch quote (extended_rules.quote_sessions): one
# ~0.1 s request for the whole book that never waits behind the process-wide
# download lock. It used to be a 5-day 1-minute download plus a daily one —
# ~1.2 s on its own, and seconds more queued behind the start-up sparkline and
# quote downloads, so the column always filled last. Futures, FX, crypto and
# non-US listings have no such quote fields and keep the bar/settle path.
#
# Contract: `declined` ONLY for a stale reference close (no number, and a live
# print must not build one); `unavailable` = refused or dropped (ask again); a
# row with neither and no sessions has simply not printed.
_EXT_RETRY_MAX = 15


def get_extended_hours_quotes(symbols):
    if not symbols:
        return []
    # Outside any try: a module that fails to load is a broken install, and []
    # would read as "nothing to report" — raise, the daemon answers ok:false.
    import extended_rules as _xr
    # Imported here, not at module level, so a test can freeze the clock.
    from datetime import datetime
    from zoneinfo import ZoneInfo
    now_et = datetime.now(ZoneInfo("America/New_York"))

    us = [s for s in symbols if _xr.has_us_extended_session(s)]
    other = [s for s in symbols if not _xr.has_us_extended_session(s)]
    rows = {}
    if us:
        rows.update(_us_extended_rows(us, now_et, _xr))
    if other:
        rows.update(_bar_extended_rows(other, now_et, _xr))
    return [rows[s] for s in symbols]


def _us_extended_rows(symbols, now_et, _xr):
    """Plain US stocks, from one batch-quote snapshot."""
    # BRK.B is BRK-B on Yahoo.
    asked = {s: re.sub(r"\.([ABC])$", r"-\1", s.upper()) for s in symbols}
    try:
        quotes = {q.get("symbol"): q for q in _v7_quotes(sorted(set(asked.values())), _xr.QUOTE_FIELDS)}
    except Exception as e:
        # A refusal (429, crumb, timeout) says nothing about the prints: ask again.
        print(f"[extended_hours] batch quote refused: {str(e)[:160]}", file=sys.stderr, flush=True)
        label = _xr.session_label(now_et)
        return {s: _empty_ext_row(s, label, unavailable=True) for s in symbols}
    # The newest regular-session day across the book: evidence that overrules
    # the holiday calendar on an unscheduled closure (extended_rules.is_session).
    # Only the newest — one halted name's older day must not mark the days
    # between as closures. Before today's open, a book whose every name last
    # traded on that day says the sessions since were closed (the morning
    # after 9 Jan 2025): today goes in too, which marks the days between.
    # Never from a single name, whose quote alone may simply be stale: the
    # calendar decides then.
    days = [d for d in (_xr.quote_day(q) for q in quotes.values()) if d is not None]
    seen = None
    if len(days) >= 2:
        seen = {max(days)}
        if now_et.time() < _xr.REG_OPEN and _xr.is_trading_day(now_et.date()):
            seen.add(now_et.date())
    label = _xr.session_label(now_et, seen)
    out = {}
    for s in symbols:
        q = quotes.get(asked[s])
        if q is None:
            # Left out of the answer: a dropped row or an unknown ticker — the
            # batch cannot say which. Asked again (the service backs off), as
            # the bar path re-asks a symbol its bulk frame dropped.
            out[s] = _empty_ext_row(s, label, unavailable=True)
            continue
        out[s] = dict(_xr.quote_sessions(q, now_et, seen), symbol=s, session=label,
                      currency=q.get("currency") or "", name=s, unavailable=False)
    return out


def _bar_extended_rows(symbols, now_et, _xr):
    """Futures, FX, crypto and non-US listings: one batched 1-minute download
    (period=5d, prepost) and the settle-based reference."""
    import io
    import contextlib
    import yfinance as _yf
    import pandas as _pd
    from datetime import datetime
    from zoneinfo import ZoneInfo

    _buf = io.StringIO()
    with contextlib.redirect_stdout(_buf):
        try:
            # auto_adjust=False: these are the prints that happened, compared
            # with each other over five days — back-adjusting the regular bars
            # for a dividend inside the window manufactures a move.
            data = _yf.download(
                symbols, period="5d", interval="1m",
                prepost=True, group_by="ticker", progress=False,
                threads=True, auto_adjust=False,
            )
        except Exception as e:
            # Raise, don't return []: an empty list reads as "market is quiet".
            # The daemon turns an exception into ok=false, which the C++ retry
            # paths are written for.
            raise RuntimeError(f"extended-hours download failed: {e}") from e

    et = ZoneInfo("America/New_York")
    if data is None or getattr(data, "empty", True):
        # Nothing came back at all — a rate-limited burst looks exactly like
        # this. That says nothing about any symbol's prints.
        return {s: _empty_ext_row(s, _ext_session_label(now_et), unavailable=True) for s in symbols}

    today = now_et.date()
    pre_open_utc = _pd.Timestamp(now_et.replace(hour=4, minute=0, second=0, microsecond=0)).tz_convert("UTC")
    open_utc = _pd.Timestamp(now_et.replace(hour=9, minute=30, second=0, microsecond=0)).tz_convert("UTC")

    # Per-symbol "most recent completed session-end" from the exchange_sessions
    # registry — futures: each exchange's settle time, crypto: 00:00 UTC, FX:
    # 17:00 ET, a 16:00-ET listing: the bell.
    try:
        from exchange_sessions import previous_session_end_utc as _prev_end
    except Exception:
        _prev_end = None

    # Official daily closes, for instruments whose session ends at the 16:00
    # ET bell (a Toronto listing): Yahoo's 1-minute series stops at 15:59 and
    # never contains the closing auction. A futures or crypto "daily close" is
    # struck at a different moment than the settle, so those keep the bar.
    def _ends_at_the_bell(sym):
        if _prev_end is None:
            return True
        try:
            end_et = _pd.Timestamp(_prev_end(sym)).tz_convert(et)
            return (end_et.hour, end_et.minute) == (16, 0)
        except Exception:
            return True

    daily = None
    if any(_ends_at_the_bell(s) for s in symbols):
        with contextlib.redirect_stdout(io.StringIO()):
            try:
                daily = _yf.download(
                    symbols, period="5d", interval="1d",
                    prepost=False, group_by="ticker", progress=False,
                    threads=True, auto_adjust=False,
                )
            except Exception:
                daily = None   # the 1-minute frame alone still yields a reference

    _officials_memo = {}

    def _officials(sym):
        """{date: (official close, volume)} for that symbol (labels read as dates)."""
        if sym in _officials_memo:
            return _officials_memo[sym]
        out = {}
        if daily is not None and not getattr(daily, "empty", True):
            try:
                dhist = None
                if isinstance(daily.columns, _pd.MultiIndex):
                    if sym in daily.columns.get_level_values(0):
                        dhist = daily[sym]
                    elif sym in daily.columns.get_level_values(1):
                        dhist = daily.xs(sym, axis=1, level=1)
                elif len(symbols) == 1:
                    dhist = daily
                out = _xr.official_closes(dhist)
            except Exception:
                out = {}
        _officials_memo[sym] = out
        return out

    def _slice(sym):
        """The bulk frame's columns for one symbol, or None when it isn't there."""
        try:
            if isinstance(data.columns, _pd.MultiIndex):
                level0 = data.columns.get_level_values(0).unique().tolist()
                level1 = data.columns.get_level_values(1).unique().tolist()
                if sym in level0:
                    hist = data[sym]
                elif sym in level1:
                    hist = data.xs(sym, axis=1, level=1)
                else:
                    return None
            elif len(symbols) == 1:
                hist = data
            else:
                # Flat columns with several symbols requested: the frame
                # carries ONE ticker and there is no telling which. Handing it
                # to every symbol prints one instrument's move on the whole
                # book; the per-symbol retry below fetches the real thing.
                return None
            hist = hist.dropna(how="all")
            if hist.empty or "Close" not in hist.columns:
                return None
            return hist
        except Exception:
            return None

    def _prints(sym, hist):
        idx = hist.index
        if idx.tz is None:
            idx = idx.tz_localize("UTC")
        # The most recent COMPLETED session end. Past 20:00 or before 04:00 ET
        # "today's" end would be in the future, and a "before it" filter would
        # pick up the previous day's after-hours bars as the close.
        if _prev_end is not None:
            regular_end_utc = _pd.Timestamp(_prev_end(sym))
        else:
            from datetime import timedelta as _td
            t = now_et.replace(hour=16, minute=0, second=0, microsecond=0)
            if t > now_et:
                t -= _td(days=1)
            while t.weekday() >= 5:
                t -= _td(days=1)
            regular_end_utc = _pd.Timestamp(t).tz_convert("UTC")

        # Bar times travel with the prices: a consumer can check a pairing's
        # times, never its prices.
        regular = regular_ts = None
        mask_reg = idx <= regular_end_utc
        if bool(mask_reg.any()):
            v = hist["Close"][mask_reg].iloc[-1]
            if not _pd.isna(v):
                regular = float(v)
                regular_ts = int(idx[mask_reg][-1].timestamp())
        regular_end_et = regular_end_utc.tz_convert(et)
        if (regular_end_et.hour, regular_end_et.minute) == (16, 0):
            official = _officials(sym).get(regular_end_et.date())
            if official:
                regular = official[0]
                regular_ts = int(regular_end_utc.timestamp())

        # After the session end, same ET day only (tomorrow's early bars are
        # not this session's after-hours).
        post = post_ts = None
        mask_post = idx > regular_end_utc
        if bool(mask_post.any()):
            idx_et = idx[mask_post].tz_convert(et)
            same_day = (idx_et.date == regular_end_et.date())
            if same_day.any():
                v = hist["Close"][mask_post][same_day].iloc[-1]
                if not _pd.isna(v):
                    post = float(v)
                    post_ts = int(idx[mask_post][same_day][-1].timestamp())

        # Today's [04:00, 09:30) ET window.
        pre = pre_ts = None
        mask_pre = (idx >= pre_open_utc) & (idx < open_utc)
        if bool(mask_pre.any()):
            idx_et = idx[mask_pre].tz_convert(et)
            same_day = (idx_et.date == today)
            if same_day.any():
                v = hist["Close"][mask_pre][same_day].iloc[-1]
                if not _pd.isna(v):
                    pre = float(v)
                    pre_ts = int(idx[mask_pre][same_day][-1].timestamp())
        return regular, regular_ts, regular_end_et.date(), pre, pre_ts, post, post_ts

    def _row(sym, hist):
        """One symbol's row from its 1-minute frame, or None."""
        try:
            session = _ext_session_label(now_et)
            regular, regular_ts, ref_day, pre, pre_ts, post, post_ts = _prints(sym, hist)
            # A reference from an earlier day (no trade in the last session)
            # turns any print into a multi-day return: no sessions at all.
            declined = (regular_ts is not None and datetime.fromtimestamp(regular_ts, et).date() != ref_day)

            def reading(p, t):
                if declined or p is None or t is None or not regular or (regular_ts and t <= regular_ts):
                    return None
                return {"price": p, "ts": t, "day": datetime.fromtimestamp(t, et).date().isoformat(),
                        "ref": regular, "ref_ts": regular_ts,
                        "ref_date": ref_day.isoformat()}

            return {
                "symbol": sym, "session": session,
                "regular": regular, "regular_ts": regular_ts,
                "declined": bool(declined), "unavailable": False,
                # Today's early window pairs with `regular` only while that is
                # the close before it (PRE / REGULAR); after the settle it
                # would measure the day's move backwards.
                "pre": reading(pre, pre_ts) if session in ("PRE", "REGULAR") else None,
                "post": reading(post, post_ts),
                "currency": "", "name": sym,
            }
        except Exception:
            return None

    rows = {}
    missing = []
    for sym in symbols:
        hist = _slice(sym)
        row = _row(sym, hist) if hist is not None else None
        if row is None:
            missing.append(sym)
            row = _empty_ext_row(sym, _ext_session_label(now_et))
        rows[sym] = row

    # A multi-symbol 1-minute download drops tickers intermittently; a single-
    # ticker request is far more reliable. Small pool on purpose: it is per
    # request, inside a network worker — not a rate-limit generator.
    if missing:
        targets = missing[:_EXT_RETRY_MAX]

        def _retry_one(sym):
            try:
                h = _yf.Ticker(sym).history(period="5d", interval="1m", prepost=True, auto_adjust=False)
            except Exception:
                return sym, "unavailable"
            if h is None or getattr(h, "empty", True) or "Close" not in h.columns:
                return sym, None   # no intraday bars (a fund): an answer
            return sym, _row(sym, h)

        with concurrent.futures.ThreadPoolExecutor(max_workers=min(2, len(targets))) as ex:
            for sym, row in ex.map(_retry_one, targets):
                if row == "unavailable":
                    rows[sym]["unavailable"] = True
                elif row is not None:
                    rows[sym] = row
        for sym in missing[_EXT_RETRY_MAX:]:
            rows[sym]["unavailable"] = True   # never asked: not an answer either
    return rows


# ── Index futures ────────────────────────────────────────────────────────────
#
# The futures quote for a cash index (ES for the S&P, NQ for the Nasdaq, …):
# last price and change against the previous settle, the way every futures
# board shows it. This is also the only honest "after-hours" for an index: a
# cash index stops at the bell, and Yahoo keeps stamping the closing level
# after 16:00 (a ^GSPC "post-market" print equal to the close, a fake +0.00%),
# so indices must never be run through extended_hours.

# Index futures whose individual contracts Yahoo serves as ROOT<month><yy>.<EX>,
# with the Friday of the expiry month the contract settles on (3rd for the CME
# E-minis, 2nd for the Nikkei).
_INDEX_FUTURE_CONTRACTS = {
    "ES": ("CME", 3), "NQ": ("CME", 3), "RTY": ("CME", 3), "YM": ("CBT", 3),
    "NIY": ("CME", 2),
}
_QUARTER_CODES = {3: "H", 6: "M", 9: "U", 12: "Z"}


def _nth_friday(year, month, n):
    from datetime import date as _date, timedelta as _td
    d = _date(year, month, 1)
    d += _td(days=(4 - d.weekday()) % 7)        # first Friday
    return d + _td(weeks=n - 1)


def _index_future_contract(proxy, today):
    """The specific contract to read an index future from, e.g. ES=F -> ESZ26.CME.

    The continuous ROOT=F series switches contract on the roll, and the switch
    shows up as a jump the size of the calendar spread. Measuring the close and
    the latest print on the same series would report that spread as an
    after-hours move on a flat evening. Pinning ONE contract for both prints
    makes the roll impossible to see. The front quarter is used until eight days
    before its expiry (where the volume has moved to the next one), then the
    next quarter. Unknown roots come back unchanged.
    """
    root = proxy[:-2] if proxy.endswith("=F") else proxy
    spec = _INDEX_FUTURE_CONTRACTS.get(root)
    if spec is None:
        return proxy
    from datetime import timedelta as _td
    exch, nth = spec
    y, m = today.year, today.month
    while True:
        qm = ((m - 1) // 3 + 1) * 3                 # this quarter's expiry month
        if today < _nth_friday(y, qm, nth) - _td(days=8):
            return f"{root}{_QUARTER_CODES[qm]}{y % 100:02d}.{exch}"
        m = qm + 1
        if m > 12:
            m, y = 1, y + 1


def _futures_trading_day(t_utc):
    """The CME trading day an instant belongs to (ET date; a session opening at
    18:00 ET belongs to the next day, Sunday evening's to Monday)."""
    from datetime import time as _time, timedelta as _td
    from zoneinfo import ZoneInfo
    t = pd.Timestamp(t_utc).tz_convert(ZoneInfo("America/New_York"))
    d = t.date()
    if t.time() >= _time(18, 0):
        d += _td(days=1)
    while d.weekday() >= 5:
        d += _td(days=1)
    return d


def _futures_is_trading(now_utc):
    """CME Globex equity-index hours: Sun 18:00 – Fri 17:00 ET, halted 17–18 daily.
    Holidays are not modelled; on one the freshness check fails and the row
    shows a dash rather than a stale quote."""
    from datetime import time as _time
    from zoneinfo import ZoneInfo
    t = pd.Timestamp(now_utc).tz_convert(ZoneInfo("America/New_York"))
    wd, tm = t.weekday(), t.time()
    if wd == 5 or (wd == 6 and tm < _time(18, 0)) or (wd == 4 and tm >= _time(17, 0)):
        return False
    return not (_time(17, 0) <= tm < _time(18, 0))


# Longer than any quiet spell a thin overnight contract (NIY, RTY) shows in
# 1-minute bars plus Yahoo's ~10 min delay; shorter than any outage worth
# hiding.
# Plain minutes, not a Timedelta: this module is imported with pandas stubbed
# out by tests that never reach the futures code.
_FUTURES_STALE_MIN = 90


_V7_QUOTE_URL = "https://query1.finance.yahoo.com/v7/finance/quote"


def _v7_quotes(symbols, fields, chunk=50):
    """Yahoo's batch quote endpoint — one request per `chunk` symbols, the one
    Yahoo's own pages poll. Each row's price and previous close are ONE
    snapshot, so a change derived from the pair is always self-consistent.
    Not under _DOWNLOAD_LOCK: the crumb hazard (yf.download swapping YfData's
    session) is closed by the session pin (_install_session_pin) and the
    serializer handing download the live session — holding the lock across a
    network round trip would stall every quote download behind this poll."""
    from yfinance.data import YfData
    rows = []
    for i in range(0, len(symbols), chunk):
        r = YfData().get_raw_json(_V7_QUOTE_URL, timeout=10, params={
            "symbols": ",".join(symbols[i:i + chunk]), "formatted": "false", "fields": fields})
        # A body without a quoteResponse is not "Yahoo knows none of these":
        # raise, so a caller can tell a refusal from an answer.
        resp = r.get("quoteResponse") if isinstance(r, dict) else None
        if not isinstance(resp, dict):
            raise RuntimeError("batch quote: no quoteResponse in the answer")
        rows += resp.get("result") or []
    return rows


def _index_future_row(quote, now_utc):
    """Last price and change-since-previous-close for one futures contract,
    from its batch-quote row.

    The reference is Yahoo's own previous close for the contract
    (regularMarketPreviousClose) from the SAME snapshot as the price — never
    one picked out of daily bars: Yahoo stamps a futures session with the date
    it OPENS, so from the 18:00 ET reopen to midnight the in-progress session
    carries today's date, and a "bar before today's trading day" lookup paired
    the live price with itself (+0.00% every evening). The pair also needs no
    holiday calendar (half-days, Columbus/Veterans Day trading) and is what the
    push feed's live prints are measured against, so they can extend it.

    `session_date` is the CME trading day of the last print — the session the
    previous close comes before. Returns (row, None) or (None, reason).
    """
    if not quote:
        return None, "no quote for the contract"
    price = quote.get("regularMarketPrice")
    ref = quote.get("regularMarketPreviousClose")
    t = quote.get("regularMarketTime")
    if not isinstance(price, (int, float)) or price <= 0:
        return None, "no last price"
    if not isinstance(ref, (int, float)) or ref <= 0:
        return None, "no previous close"
    if not isinstance(t, (int, float)) or t <= 0:
        return None, "no time for the last price"
    last = pd.Timestamp(int(t), unit="s", tz="UTC")
    now_utc = pd.Timestamp(now_utc).tz_convert("UTC")
    trading = _futures_is_trading(now_utc)
    # A contract that stopped printing while Globex is open is a dead feed or a
    # wrong symbol; its last change is not today's.
    if trading and now_utc - last > pd.Timedelta(minutes=_FUTURES_STALE_MIN):
        return None, f"no prints since {last.tz_convert('America/New_York'):%a %H:%M} ET"
    # The reopen: Yahoo's futures prices run ~10 minutes behind, so for a few
    # minutes after 18:00 ET the newest price is still the closed session's
    # while the previous close may already have rolled to that session's own
    # settle — a pair that reads ~0.00%. Wait for the new session's first print.
    if trading and _futures_trading_day(last) < _futures_trading_day(now_utc):
        return None, "waiting for the reopened session's first print"
    price, ref = float(price), float(ref)
    return {
        "price": price,
        "change": price - ref,
        "pct": (price - ref) / ref * 100.0,
        "ref_price": ref,
        "session_date": _futures_trading_day(last).isoformat(),
        "ts": int(t),
    }, None


def get_index_futures(pairs):
    """Futures quotes for cash indices.

    `pairs` is a list of {index, proxy}, proxy being the continuous ROOT=F
    symbol; the quote itself is read from one pinned contract month (see
    _index_future_contract). Every requested index gets a row; `pct` is null
    when there is no honest number, and `reason` says why.
    """
    from zoneinfo import ZoneInfo
    pairs = [p for p in (pairs or []) if p.get("index") and p.get("proxy")]
    if not pairs:
        return []
    now_utc = datetime.now(ZoneInfo("UTC"))
    cme_today = now_utc.astimezone(ZoneInfo("America/Chicago")).date()
    contract = {p["proxy"]: _index_future_contract(p["proxy"], cme_today) for p in pairs}
    symbols = sorted(set(contract.values()))
    # A refused quote (Yahoo 429s per IP) is not a failure of the column:
    # every row still names its contract, which the host streams — each push
    # print carries its own previous close, so FUT% fills from the stream.
    refused = None
    try:
        quotes = {q.get("symbol"): q for q in _v7_quotes(
            symbols, "regularMarketPrice,regularMarketPreviousClose,regularMarketTime")}
    except Exception as e:
        quotes, refused = {}, str(e)[:120]

    rows = []
    for p in pairs:
        fut = contract[p["proxy"]]
        row = {"symbol": p["index"], "proxy": p["proxy"], "contract": fut, "pct": None, "reason": ""}
        try:
            r, why = _index_future_row(quotes.get(fut), now_utc)
            if refused:
                row["reason"] = f"{fut}: quote refused ({refused}) — waiting for the contract's next print"
                row["transient"] = True
            elif r is None:
                row["reason"] = f"{fut}: {why}"
            else:
                row.update(r)
        except Exception as e:
            row["reason"] = str(e)[:120]
        rows.append(row)
    return rows


# ── Futures forward curve ─────────────────────────────────────────────────
# Which exchange suffix a root's contract months live under (CL -> NYM), as
# learned from the continuous row's underlyingSymbol. It never changes.
_FUTURES_EXCHANGE = {}


def get_futures_curve(root, exchange=None, months=30):
    """Every listed delivery month of one futures root: last, previous close,
    volume, open interest, expiry. See futures_curve.py for what is and is not
    trusted. `exchange` is a hint; Yahoo's own answer wins."""
    import time as _time
    from datetime import date as _date
    import futures_curve as fc
    root = str(root or "").strip().upper()
    if not root:
        return {"error": "no futures root given"}
    try:
        front = ""
        head = _v7_quotes([root + "=F"], "underlyingSymbol")
        parts = fc.split_contract((head[0] if head else {}).get("underlyingSymbol"))
        if parts and parts[0] == root:
            front = head[0]["underlyingSymbol"]
            _FUTURES_EXCHANGE[root] = parts[3]
        exch = _FUTURES_EXCHANGE.get(root) or str(exchange or "").strip().upper()
        if not exch:
            return {"error": f"Yahoo lists no contract months for {root}", "root": root}
        quotes = _v7_quotes(fc.candidate_symbols(root, exch, _date.today(), months), fc.V7_FIELDS)
    except Exception as e:
        return {"error": f"Yahoo refused or did not answer ({str(e)[:120]})", "transient": True, "root": root}
    out = fc.shape_curve(root, quotes, front, _time.time())
    if not out["contracts"]:
        return {"error": f"no contract months quoted for {root}", "root": root}
    return out


def get_futures_curve_history(symbols):
    """Where the same contract months settled 5 and 21 sessions ago — the
    curve's shape then, to lay under the curve now. The reference sessions
    are picked off the first symbol's bars (the front month, which trades
    every session); a month with no bar on that exact session is left out."""
    import futures_curve as fc
    symbols = [s for s in (symbols or []) if fc.split_contract(s)][:40]
    if not symbols:
        return {"error": "no contract symbols given"}
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        try:
            data = yf.download(symbols, period="3mo", interval="1d", group_by="ticker",
                               progress=False, threads=True, auto_adjust=False)
        except Exception as e:
            return {"error": f"Yahoo refused or did not answer ({str(e)[:120]})", "transient": True}
    if data is None or getattr(data, "empty", True):
        return {"error": "Yahoo returned no bars for the curve", "transient": True}
    closes = {}
    for sym in symbols:
        try:
            col = data[sym]["Close"].dropna()
        except Exception:
            continue
        # A daily futures bar is stamped with its session's date.
        # Yahoo's bars are float32 widened to float64 (273.6000061…): seven
        # significant figures is all they ever held.
        closes[sym] = {ts.date().isoformat(): float(f"{float(v):.7g}") for ts, v in col.items() if v > 0}
    dates = sorted(closes.get(symbols[0], {}))
    out = {"reference": symbols[0], "curves": []}
    for label, back in (("1W", 5), ("1M", 21)):
        if len(dates) > back:
            ref = dates[-1 - back]
            out["curves"].append({"label": label, "sessions_back": back, "date": ref,
                                  "closes": fc.curve_on_date(closes, ref)})
    return out


def get_futures_spread_history(near, far, period="6mo"):
    """Daily history of a calendar spread: `far` minus `near`, two contract
    months of the same root, on the sessions both traded."""
    import futures_curve as fc
    a, b = fc.split_contract(near), fc.split_contract(far)
    if not a or not b or a[0] != b[0] or near == far:
        return {"error": "a spread needs two different months of one contract"}
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        try:
            data = yf.download([near, far], period=period if period in ("3mo", "6mo", "1y") else "6mo",
                               interval="1d", group_by="ticker", progress=False, threads=True, auto_adjust=False)
        except Exception as e:
            return {"error": f"Yahoo refused or did not answer ({str(e)[:120]})", "transient": True}
    if data is None or getattr(data, "empty", True):
        return {"error": "Yahoo returned no bars for the spread", "transient": True}
    closes = {}
    for sym in (near, far):
        try:
            col = data[sym]["Close"].dropna()
        except Exception:
            col = []
        closes[sym] = {ts.date().isoformat(): float(f"{float(v):.7g}") for ts, v in getattr(col, "items", lambda: [])()
                       if v > 0}
    rows = fc.spread_series(closes[near], closes[far])
    if len(rows) < 2:
        return {"error": "the two months have not traded on enough of the same sessions"}
    return {"near": near, "far": far, "rows": rows}


def _empty_ext_row(sym, session, unavailable=False):
    """A row with nothing in it. `unavailable` = the source refused or dropped
    the request (ask again); without it, the symbol simply has no prints."""
    return {
        "symbol": sym, "session": session,
        "regular": None, "regular_ts": None,
        "pre": None, "post": None,
        "currency": "", "name": sym,
        "declined": False, "unavailable": bool(unavailable),
    }


def _ext_session_label(now_et):
    """The session label (PRE/REGULAR/POST/CLOSED) of a bar-path row (a
    future, FX pair, coin or foreign listing) at `now_et`.

    Not cosmetic: today's pre-market window pairs with `regular` only under
    PRE/REGULAR, so the label must flip exactly when the reference does. The
    plain clock rule the settle-based reference was written against — the US
    equity calendar is not theirs (CME trades MLK Day and a full day after
    Thanksgiving). Plain US stocks take extended_rules.session_label."""
    if now_et.weekday() >= 5:
        return "CLOSED"
    h, m = now_et.hour, now_et.minute
    if h < 4:
        return "CLOSED"
    if (h, m) < (9, 30):
        return "PRE"
    if h < 16:
        return "REGULAR"
    if h < 20:
        return "POST"
    return "CLOSED"


_STREAM_URL = "wss://streamer.finance.yahoo.com/?version=2"
_STREAM_TICK_KEYS = ("id", "price", "time", "change", "change_percent", "day_volume",
                     "day_high", "day_low", "market_hours")


class _QuoteStreamer:
    """Yahoo's push price feed (the protobuf websocket yf.WebSocket wraps),
    forwarded to the C++ host as unsolicited {"event": "ticks"} frames.

    Polling (batch_all) stays the source of truth for every field; the stream
    moves the last price between polls. Yahoo never ticks cash indices — they
    get the fast lane below — nor the continuous futures aliases (GC=F): those
    ride their underlying contract's ticks (GCZ26.CMX), relabelled.

    Our own loop rather than yf.WebSocket.listen(): that one gives up on the
    first error (no reconnect) and the sync client never sends the 15s
    re-subscribe heartbeat the async one does.

    Ticks are batched — the latest per symbol, flushed every FLUSH_S — so a
    busy symbol costs the host at most one update per flush, never one per trade.
    """

    HEARTBEAT_S = 15.0
    FLUSH_S = 0.25
    # Fast lane: cash indices (^GSPC) never tick on the push feed, so they are
    # read from the batch quote endpoint — ONE request for all of them — every
    # FAST_POLL_S while any is in session, FAST_IDLE_S otherwise. Polling is
    # the one thing Yahoo throttles per IP (a 2 s cadence plus a screener scan
    # earned a 429 on EVERY endpoint, 2026-11-12), so the lane is kept to the
    # symbols with no other live source and backs off hard on a refusal.
    FAST_POLL_S = 5.0
    FAST_IDLE_S = 60.0
    FAST_RESEND_S = 300.0  # < the host's kSnapshotPrevMs (10 min)
    FAST_CHUNK = 50
    RATE_LIMIT_BACKOFF_S = (300.0, 900.0)  # first wait after a 429, ceiling
    ALIAS_REFRESH_S = 600.0  # ROOT=F → contract map (it moves on a roll)

    def __init__(self, send):
        self._send = send  # callable(dict) — the daemon's locked frame writer
        self._lock = threading.Lock()
        self._want = set()
        self._pending = {}
        self._started = False
        self._fast_last = {}  # symbol -> ((price, prev_close), sent_at): unchanged quotes aren't re-sent...
        self._fast_wake = threading.Event()  # set_symbols: a newly shown symbol shouldn't wait out an idle sleep
        self._alias = {}  # "GC=F" -> "GCZ26.CMX" (Yahoo's own underlyingSymbol)
        self._alias_at = 0.0
        self._alias_tried = {}  # alias -> monotonic time last asked (answered or not)
        self._limited_until = 0.0  # monotonic: Yahoo 429'd us — nothing wakes the lane before this

    @staticmethod
    def fast_lane(symbol):
        """Symbols polled instead of streamed: cash indices."""
        return symbol.startswith("^")

    @staticmethod
    def is_alias(symbol):
        """Continuous futures (GC=F): streamed through their underlying contract."""
        return symbol.endswith("=F")

    def _stream_set(self, want):
        """What the push feed is subscribed to for `want`: streamable symbols
        as they are, aliases as their mapped contract (unmapped: none yet)."""
        out = set()
        for x in want:
            if self.fast_lane(x):
                continue
            if self.is_alias(x):
                c = self._alias.get(x)
                if c:
                    out.add(c)
            else:
                out.add(x)
        return out

    @staticmethod
    def alias_tick(alias, msg):
        """A contract's push tick relabelled for its ROOT=F alias. Price and
        Yahoo's change come as a pair from the same print, so the host takes
        the previous close as price − change (source "alias")."""
        t = {k: msg[k] for k in _STREAM_TICK_KEYS if k in msg}
        t["id"] = alias
        t["source"] = "alias"
        return t

    def set_symbols(self, symbols):
        from yfinance.live import BaseWebSocket  # raises on a yfinance without the live feed
        with self._lock:
            self._want = {str(x) for x in symbols if x}
            self._pending = {k: v for k, v in self._pending.items() if k in self._want}
            self._fast_last = {k: v for k, v in self._fast_last.items() if k in self._want}
            # Wake for a newly shown symbol — never during a rate-limit backoff
            # (that is what the backoff is for), and not for an alias already
            # asked about this refresh (Yahoo may simply have no underlying).
            now = time.monotonic()
            if now >= self._limited_until and any(
                    (self.fast_lane(x) and x not in self._fast_last)
                    or (self.is_alias(x) and x not in self._alias
                        and now - self._alias_tried.get(x, -1e18) >= self.ALIAS_REFRESH_S)
                    for x in self._want):
                self._fast_wake.set()
            if not self._started:
                self._decode = BaseWebSocket(verbose=False)._decode_message
                self._started = True
                threading.Thread(target=self._conn_loop, name="yf-stream", daemon=True).start()
                threading.Thread(target=self._flush_loop, name="yf-stream-flush", daemon=True).start()
                threading.Thread(target=self._fast_loop, name="yf-fastpoll", daemon=True).start()
        return len(self._want)

    @staticmethod
    def fast_tick(q):
        """A batch-quote row as a tick, or None. `prev_close` travels with the
        price: both come from the same snapshot, so the host derives the change
        from them as a pair — never this price against another source's close."""
        sym = q.get("symbol")
        price = q.get("regularMarketPrice")
        prev = q.get("regularMarketPreviousClose")
        t = q.get("regularMarketTime")
        if not sym or not isinstance(price, (int, float)) or price <= 0:
            return None
        if not isinstance(prev, (int, float)) or prev <= 0:
            return None
        tick = {"id": sym, "price": float(price), "prev_close": float(prev), "source": "quote",
                "market_hours": 1}
        if isinstance(t, (int, float)) and t > 0:
            tick["time"] = str(int(t) * 1000)  # ms, as a string like the push feed's int64s
        vol = q.get("regularMarketVolume")
        if isinstance(vol, (int, float)) and vol > 0:
            tick["day_volume"] = str(int(vol))
        return tick

    def _refresh_aliases(self, want):
        """Map each wanted ROOT=F to Yahoo's underlyingSymbol (every
        ALIAS_REFRESH_S, and at once for a newly shown alias)."""
        aliases = sorted(x for x in want if self.is_alias(x))
        if not aliases:
            return
        now = time.monotonic()
        stale = now - self._alias_at >= self.ALIAS_REFRESH_S
        # Negative cache: an alias Yahoo answered WITHOUT an underlying (or
        # dropped) is not re-asked every loop — only on the next refresh.
        if not stale and all(x in self._alias or now - self._alias_tried.get(x, -1e18) < self.ALIAS_REFRESH_S
                             for x in aliases):
            return
        for x in aliases:
            self._alias_tried[x] = now
        self._alias_at = now  # an attempt, answered or not: the next one waits a full refresh
        rows = _v7_quotes(aliases, "underlyingSymbol", self.FAST_CHUNK)
        fresh = {q.get("symbol"): q.get("underlyingSymbol") for q in rows
                 if q.get("symbol") and q.get("underlyingSymbol")}
        with self._lock:
            self._alias.update(fresh)

    def _fast_loop(self):
        backoff = 0.0
        while True:
            wanted = self._wanted()
            want = sorted(s for s in wanted if self.fast_lane(s))
            if not want and not any(self.is_alias(s) for s in wanted):
                time.sleep(1.0)
                continue
            live = False
            try:
                self._refresh_aliases(wanted)
            except Exception as e:
                # Its own failure: retried on the next refresh (_alias_at). A
                # rate limit holds the whole lane back, like the index poll's.
                if "too many requests" in str(e).lower() or "rate limit" in str(e).lower():
                    self._limited_until = time.monotonic() + self.RATE_LIMIT_BACKOFF_S[0]
                print(f"[yf-fastpoll] futures alias map failed: {e}", file=sys.stderr, flush=True)
            try:
                rows = _v7_quotes(want, "regularMarketPrice,regularMarketPreviousClose,regularMarketTime,"
                                        "regularMarketVolume,marketState", self.FAST_CHUNK) if want else []
                backoff = 0.0
                for q in rows:
                    live = live or q.get("marketState") == "REGULAR"
                    tick = self.fast_tick(q)
                    if tick is None:
                        continue
                    key = (tick["price"], tick["prev_close"])
                    now = time.monotonic()
                    with self._lock:
                        if tick["id"] not in self._want:
                            continue
                        last = self._fast_last.get(tick["id"])
                        # ...except every FAST_RESEND_S: the host keeps a snapshot's
                        # previous close as the baseline only while it is fresh
                        # (kSnapshotPrevMs), and a quiet index must not fall
                        # back to the poll's own baseline (CHG flip).
                        if last and last[0] == key and now - last[1] < self.FAST_RESEND_S:
                            continue
                        self._fast_last[tick["id"]] = (key, now)
                        self._pending[tick["id"]] = tick
            except Exception as e:
                limited = "too many requests" in str(e).lower() or "rate limit" in str(e).lower()
                lo, hi = self.RATE_LIMIT_BACKOFF_S if limited else (self.FAST_POLL_S * 2, 120.0)
                backoff = min(max(backoff * 2, lo), hi)
                if limited:
                    self._limited_until = time.monotonic() + backoff
                print(f"[yf-fastpoll] quote poll failed: {e} — retry in {backoff:.0f}s", file=sys.stderr,
                      flush=True)
            wait = backoff or (self.FAST_POLL_S if live else self.FAST_IDLE_S)
            wait = max(wait, self._limited_until - time.monotonic())
            self._fast_wake.wait(wait)
            self._fast_wake.clear()

    def _wanted(self):
        with self._lock:
            return set(self._want)

    def _conn_loop(self):
        from websockets.sync.client import connect
        backoff = 1.0
        while True:
            if not self._stream_set(self._wanted()):
                time.sleep(1.0)  # nothing to stream: no connection held open
                continue
            try:
                ws = connect(_STREAM_URL, open_timeout=10, close_timeout=2)
            except Exception as e:
                print(f"[yf-stream] connect failed: {e} — retry in {backoff:.0f}s", file=sys.stderr, flush=True)
                time.sleep(backoff)
                backoff = min(backoff * 2, 60.0)
                continue
            opened = time.monotonic()
            sent, last_hb = set(), 0.0
            idle_close = False
            try:
                while True:
                    want = self._stream_set(self._wanted())
                    if not want:
                        idle_close = True  # our own close: nothing on screen
                        break
                    now = time.monotonic()
                    if want != sent or now - last_hb >= self.HEARTBEAT_S:
                        gone = sent - want
                        if gone:
                            ws.send(json.dumps({"unsubscribe": sorted(gone)}))
                        ws.send(json.dumps({"subscribe": sorted(want)}))
                        sent, last_hb = want, now
                    try:
                        raw = ws.recv(timeout=1.0)  # bounded: a changed set is sent within ~1s
                    except TimeoutError:
                        continue
                    try:
                        msg = self._decode(json.loads(raw).get("message", ""))
                    except Exception:
                        continue  # one bad frame is not a dead connection
                    if not isinstance(msg, dict):
                        continue
                    sym = msg.get("id")
                    if sym and "price" in msg:
                        with self._lock:
                            if sym in self._want:
                                self._pending[sym] = {k: msg[k] for k in _STREAM_TICK_KEYS if k in msg}
                            for alias, contract in self._alias.items():
                                if contract == sym and alias in self._want:
                                    self._pending[alias] = self.alias_tick(alias, msg)
            except Exception as e:
                print(f"[yf-stream] connection dropped: {e}", file=sys.stderr, flush=True)
            finally:
                try:
                    ws.close()
                except Exception:
                    pass
            # Back off unless the connection actually held: a server that accepts
            # and then closes at once (rate limit, rejected subscribe) must not be
            # hammered every second. Closing it ourselves (the user left every
            # quote screen) is no failure — sleeping here would leave the next
            # screen without ticks for up to a minute.
            # The escalated backoff itself is kept across an idle close: a server
            # that was refusing us is still refusing when the user comes back.
            if time.monotonic() - opened >= 60.0:
                backoff = 1.0
            elif not idle_close:
                time.sleep(backoff)
                backoff = min(backoff * 2, 60.0)

    def _flush_loop(self):
        while True:
            time.sleep(self.FLUSH_S)
            with self._lock:
                batch, self._pending = self._pending, {}
            if batch:
                try:
                    self._send({"id": 0, "event": "ticks", "ticks": list(batch.values())})
                except Exception:
                    return  # host gone; the daemon is exiting


def run_daemon_socket(socket_path=None, tcp=False):
    """Multi-threaded daemon over a Unix domain socket — or, with `tcp`, over a
    loopback TCP connection.

    TCP is the Windows transport: CPython on Windows has no AF_UNIX, and Qt's
    QLocalSocket there is a named pipe, so the Unix-socket daemon died at
    start-up on every Windows machine and the app showed no data at all. The
    daemon binds 127.0.0.1 on a port the OS picks, reports it in the ready
    frame (a private pipe to the host), and serves only a client that opens
    with the token the host passed in FINTERM_DAEMON_TOKEN — loopback is
    reachable by every local process, a socket file is not.

    Main thread reads request frames from the socket and submits each to a
    thread pool. Worker threads call _daemon_dispatch and write the response
    back (protected by a write lock so frames never interleave).

    Two pools:
      network_pool (6 workers) — yfinance HTTP I/O: quote, info, historical,
                                  batch_all, batch_quotes, news, financials …
      compute_pool (2 workers) — CPU-bound: compute_technicals
    """
    import socket as _sock

    _COMPUTE_ACTIONS = {"compute_technicals"}

    ready = {"ready": True, "pid": os.getpid()}
    token = os.environ.get("FINTERM_DAEMON_TOKEN", "") if tcp else ""
    authed = not tcp
    if tcp:
        if not token:
            return  # no token, no service: never an open port anyone can drive
        srv = _sock.socket(_sock.AF_INET, _sock.SOCK_STREAM)
        srv.bind(("127.0.0.1", 0))
        ready["port"] = srv.getsockname()[1]
    else:
        # Remove stale socket file left by a previous crash
        try:
            os.unlink(socket_path)
        except OSError:
            pass
        srv = _sock.socket(_sock.AF_UNIX, _sock.SOCK_STREAM)
        # SO_REUSEADDR has no effect on AF_UNIX sockets; stale-file removal above
        # is the correct mechanism for Unix domain socket reuse.
        srv.bind(socket_path)
    srv.listen(1)

    # Signal ready to the C++ host via stdout (same framing as run_daemon)
    try:
        ready_bytes = json.dumps(ready).encode("utf-8")
        n = len(ready_bytes)
        sys.stdout.buffer.write(n.to_bytes(4, "big") + ready_bytes)
        sys.stdout.buffer.flush()
    except Exception:
        pass

    # Wait for the C++ host to connect. The 30s timeout prevents the daemon
    # from hanging forever if the host crashes between the ready signal and
    # calling connectToServer().
    srv.settimeout(30.0)
    try:
        conn, _ = srv.accept()
    except _sock.timeout:
        return  # host never connected — exit cleanly; C++ will restart us
    finally:
        srv.settimeout(None)  # restore blocking mode for the connection socket
    write_lock = threading.Lock()

    def _send_response(resp_dict):
        try:
            body = json.dumps(resp_dict, allow_nan=False).encode("utf-8")
        except (ValueError, TypeError) as exc:
            # A bug, not a refusal — said in a body, or the C++ side reads a
            # body-less failure as "no answer, try again".
            body = json.dumps({
                "id": resp_dict.get("id", 0), "ok": False,
                "error": f"non-JSON-safe result: {exc}",
                "result": {"error": f"non-JSON-safe result: {exc}", "transient": False}
            }).encode("utf-8")
        frame = len(body).to_bytes(4, "big") + body
        with write_lock:
            conn.sendall(frame)

    streamer = _QuoteStreamer(_send_response)

    def _handle(req_id, action, payload):
        # Pool threads are reused: this request's trace starts empty, so a
        # failure is never judged by an earlier request's statuses.
        _trace_reset()
        try:
            result = _daemon_dispatch(action, payload)
            _send_response(_reply_for(req_id, result))
        except Exception as exc:
            _send_response(_exception_reply(req_id, exc))

    network_pool = concurrent.futures.ThreadPoolExecutor(
        max_workers=6, thread_name_prefix="yf-net")
    compute_pool = concurrent.futures.ThreadPoolExecutor(
        max_workers=2, thread_name_prefix="yf-compute")

    read_buf = b""
    try:
        while True:
            try:
                chunk = conn.recv(65536)
            except OSError:
                break
            if not chunk:
                break
            read_buf += chunk

            while len(read_buf) >= 4:
                n = int.from_bytes(read_buf[:4], "big")
                if n == 0 or n > 64 * 1024 * 1024:
                    read_buf = b""
                    break
                if len(read_buf) < 4 + n:
                    break
                body = read_buf[4:4 + n]
                read_buf = read_buf[4 + n:]

                try:
                    req = json.loads(body.decode("utf-8"))
                except Exception as exc:
                    _send_response({"id": 0, "ok": False,
                                    "error": f"bad request JSON: {exc}"})
                    continue

                req_id = req.get("id", 0)
                action = req.get("action", "")
                payload = req.get("payload")

                if not authed:
                    # TCP only: the first frame must carry the host's token.
                    # Anything else is not our host — serve it nothing.
                    import hmac
                    given = str((payload or {}).get("token", "")) if action == "auth" else ""
                    if not hmac.compare_digest(given.encode("utf-8"), token.encode("utf-8")):
                        return
                    authed = True
                    continue

                if action == "shutdown":
                    _send_response({"id": req_id, "ok": True,
                                    "result": {"shutdown": True}})
                    return

                if action == "stream_set":
                    # Inline (no pool): it only swaps a set; the stream thread
                    # does the network work.
                    try:
                        n_syms = streamer.set_symbols((payload or {}).get("symbols") or [])
                        _send_response({"id": req_id, "ok": True, "result": {"streaming": n_syms}})
                    except Exception as exc:
                        _send_response({"id": req_id, "ok": False, "error": f"streaming unavailable: {exc}",
                                        "result": {"error": f"streaming unavailable: {exc}",
                                                   "transient": False}})
                    continue

                pool = compute_pool if action in _COMPUTE_ACTIONS else network_pool
                pool.submit(_handle, req_id, action, payload)
    finally:
        network_pool.shutdown(wait=False)
        compute_pool.shutdown(wait=False)
        try:
            conn.close()
            srv.close()
            if socket_path:
                os.unlink(socket_path)
        except OSError:
            pass


def run_daemon():
    """Main daemon loop — read frame, dispatch, write frame, repeat."""
    import io
    # Use the raw binary stdio streams; we do our own framing.
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer
    # Ready marker so the C++ host knows imports are done and the worker is
    # ready to accept requests. Uses the same framing.
    try:
        ready = json.dumps({"ready": True, "pid": __import__("os").getpid()}).encode("utf-8")
        _daemon_write_frame(stdout, ready)
    except Exception:
        pass

    while True:
        frame = _daemon_read_frame(stdin)
        if frame is None:
            break  # EOF or bad frame — exit
        try:
            req = json.loads(frame.decode("utf-8"))
        except Exception as e:
            err = {"id": 0, "ok": False, "error": f"bad request JSON: {e}"}
            _daemon_write_frame(stdout, json.dumps(err).encode("utf-8"))
            continue

        req_id = req.get("id", 0)
        action = req.get("action", "")
        if action == "shutdown":
            resp = {"id": req_id, "ok": True, "result": {"shutdown": True}}
            _daemon_write_frame(stdout, json.dumps(resp).encode("utf-8"))
            break

        try:
            _trace_reset()   # this request's trace only (see _handle)
            result = _daemon_dispatch(action, req.get("payload"))
            resp = _reply_for(req_id, result)   # same rule as the socket transport
        except Exception as e:
            resp = _exception_reply(req_id, e)
        try:
            # allow_nan=False is the safety net — if anything still slips
            # through _sanitize_for_json (e.g. numpy.float64 NaN we missed),
            # we get a ValueError here instead of writing invalid JSON that
            # the C++ side will silently drop.
            body = json.dumps(resp, allow_nan=False).encode("utf-8")
        except (ValueError, TypeError) as e:
            err_resp = {"id": req_id, "ok": False,
                        "error": f"non-JSON-safe result: {e}",
                        "result": {"error": f"non-JSON-safe result: {e}", "transient": False}}
            body = json.dumps(err_resp).encode("utf-8")
        try:
            _daemon_write_frame(stdout, body)
        except Exception:
            break


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        # Parse optional --socket <path> / --tcp argument
        socket_path = None
        args = sys.argv[2:]
        for i, arg in enumerate(args):
            if arg == "--socket" and i + 1 < len(args):
                socket_path = args[i + 1]
                break
        if "--tcp" in args:
            run_daemon_socket(tcp=True)
        elif socket_path:
            run_daemon_socket(socket_path)
        else:
            run_daemon()   # fallback: stdin/stdout (backward compat)
    else:
        main()
