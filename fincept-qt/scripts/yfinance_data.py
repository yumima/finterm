"""
YFinance Data Fetcher
Fetches real-time stock quotes and historical data using yfinance
Returns JSON output for Qt/C++ integration
"""

import sys
import json
import os
import threading
import time
import heapq
import concurrent.futures
import yfinance as yf
import pandas as pd
from datetime import datetime

# ── yfinance crumb/session state ──────────────────────────────────────────────
# Yahoo Finance periodically rotates its internal auth crumb.  When this
# happens every outgoing request returns HTTP 401 "Invalid Crumb".  The daemon
# must reset yfinance's cached session so the next request re-authenticates
# transparently rather than failing for the rest of the process lifetime.

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

def get_orderbook(symbol):
    """Best-effort live order-book snapshot (bid/ask + sizes) for a single symbol.

    Uses `Ticker.info` (Yahoo quoteSummary endpoint), NOT `fast_info`. fast_info
    advertises `bid`/`ask` attributes but the free-tier feed leaves them at 0
    for *every* symbol — including liquid US large-caps during regular hours
    (verified 2026-05-12 against AAPL/MSFT/NVDA/SPY/TSLA). The quoteSummary
    payload returned by `info` does carry real values (`bid`, `ask`, `bidSize`,
    `askSize`).

    Trade-off: `info` is ~1-2s per symbol vs fast_info's ~150ms — but bid/ask
    is only fetched on focus-mode enter (PortfolioPerfChart::fetch_focus_orderbook),
    never inside the batch-quote hot path. One-off latency is acceptable; an
    empty spread is not. Returns zeros outside RTH / for illiquid tickers /
    on exchanges Yahoo can't quote, which consumers treat as "unavailable".
    """
    if not symbol:
        return {"symbol": "", "bid": 0, "ask": 0, "bid_size": 0, "ask_size": 0}
    try:
        import io, contextlib
        # ticker.info chatters to stdout (deprecation notices, progress bars)
        # which would corrupt the JSON protocol; redirect to a sink.
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            info = yf.Ticker(symbol).info
        bid = float(info.get("bid", 0) or 0)
        ask = float(info.get("ask", 0) or 0)
        bid_size = float(info.get("bidSize", 0) or 0)
        ask_size = float(info.get("askSize", 0) or 0)
    except Exception:
        bid = ask = bid_size = ask_size = 0
    return {
        "symbol": symbol,
        "bid": round(bid, 2) if bid > 0 else 0,
        "ask": round(ask, 2) if ask > 0 else 0,
        "bid_size": bid_size,
        "ask_size": ask_size,
        "timestamp": int(datetime.now().timestamp()),
    }

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
    prev_close_raw = getattr(fi, "previous_close", None)
    prev_close = float(prev_close_raw) if prev_close_raw is not None else current
    change = current - prev_close
    pct = (change / prev_close * 100.0) if prev_close else 0.0

    def _f(name):
        v = getattr(fi, name, None)
        if v is None: return None
        try:
            v = float(v)
            return round(v, 2)
        except (TypeError, ValueError):
            return None

    def _i(name):
        v = getattr(fi, name, None)
        if v is None: return None
        try: return int(v)
        except (TypeError, ValueError): return None

    return {
        "symbol":          symbol,
        "price":           round(current, 2),
        "change":          round(change, 2),
        "change_percent":  round(pct, 2),
        "volume":          _i("last_volume"),
        "high":            _f("day_high"),
        "low":             _f("day_low"),
        "open":            _f("open"),
        "previous_close":  round(prev_close, 2),
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
        previous_close = info.get('previousClose', current_price)
        change = current_price - previous_close
        change_percent = (change / previous_close) * 100 if previous_close else 0

        quote_data = {
            "symbol": symbol,
            "price": round(float(current_price), 2),
            "change": round(float(change), 2),
            "change_percent": round(float(change_percent), 2),
            "volume": int(hist['Volume'].iloc[-1]) if not hist['Volume'].empty else None,
            "high": round(float(hist['High'].iloc[-1]), 2) if not hist['High'].empty else None,
            "low": round(float(hist['Low'].iloc[-1]), 2) if not hist['Low'].empty else None,
            "open": round(float(hist['Open'].iloc[-1]), 2) if not hist['Open'].empty else None,
            "previous_close": round(float(previous_close), 2),
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
            historical_data.append({
                "symbol": symbol,
                "timestamp": int(index.timestamp()),
                "open": round(float(row['Open']), 2),
                "high": round(float(row['High']), 2),
                "low": round(float(row['Low']), 2),
                "close": round(float(row['Close']), 2),
                "volume": int(row['Volume']),
                "adj_close": round(float(row['Close']), 2)
            })

        return historical_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

def batch_closes(symbols, start_date, end_date):
    """Daily close history for many tickers over [start_date, end_date] in ONE
    download. Returns {"closes": {ticker: [[unix_ts, close], ...]}} with real
    auto-adjusted closes only — tickers/days with no data are omitted (never
    fabricated). Used to value disclosed congressional trades at the real close
    on their transaction date.
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
        single = len(syms) == 1

        def _closes_for(s):
            # Layouts vary (single vs multi-ticker, ticker on column level 0 or 1).
            # Mirror the defensive access used elsewhere for grouped downloads.
            try:
                if single:
                    return data["Close"]
                if isinstance(data.columns, pd.MultiIndex):
                    if s in data.columns.get_level_values(0):
                        return data[s]["Close"]
                    return data.xs(s, axis=1, level=1)["Close"]
                return data["Close"]
            except Exception:
                return None

        out = {}
        for s in syms:
            closes = _closes_for(s)
            if closes is None:
                continue
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
            if idx_date.date() <= target.date():
                closest_date = idx_date
                closest_price = round(float(row['Close']), 2)

        if closest_price is None:
            # If no date before or on target, take the first available
            first_row = hist.iloc[0]
            closest_date = hist.index[0].to_pydatetime()
            closest_price = round(float(first_row['Close']), 2)

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
    """Fetch company info for a symbol"""
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.info

        # Extract comprehensive information - many more fields available from yfinance
        info_data = {
            "symbol": symbol,
            "company_name": info.get('longName', info.get('shortName', 'N/A')),
            "sector": info.get('sector', 'N/A'),
            "industry": info.get('industry', 'N/A'),
            "market_cap": info.get('marketCap'),
            "pe_ratio": info.get('trailingPE'),
            "forward_pe": info.get('forwardPE'),
            "dividend_yield": info.get('dividendYield'),
            "beta": info.get('beta'),
            "fifty_two_week_high": info.get('fiftyTwoWeekHigh'),
            "fifty_two_week_low": info.get('fiftyTwoWeekLow'),
            "average_volume": info.get('averageVolume'),
            "description": info.get('longBusinessSummary', 'N/A'),
            "website": info.get('website', 'N/A'),
            "country": info.get('country', 'N/A'),
            "currency": info.get('currency', 'USD'),
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
            "current_price": info.get('currentPrice'),
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
        try:
            info = yf.Ticker(sym).info or {}
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
            return {"symbol": sym, "error": str(e)}

    out = []
    with ThreadPoolExecutor(max_workers=8) as ex:
        futures = [ex.submit(_one, s) for s in symbols]
        for f in as_completed(futures):
            try:
                out.append(f.result())
            except Exception as e:
                out.append({"symbol": "", "error": str(e)})
    return out


def get_financials(symbol):
    """Fetch financial statements for a symbol"""
    try:
        ticker = yf.Ticker(symbol)

        # Get income statement, balance sheet, and cash flow
        income_stmt = ticker.financials
        balance_sheet = ticker.balance_sheet
        cash_flow = ticker.cashflow

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

        financials_data = {
            "symbol": symbol,
            "income_statement": dataframe_to_dict(income_stmt),
            "balance_sheet": dataframe_to_dict(balance_sheet),
            "cash_flow": dataframe_to_dict(cash_flow),
            "timestamp": int(datetime.now().timestamp())
        }

        return financials_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

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
        items = t.news or []
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
        import requests, re
        from bs4 import BeautifulSoup
        import warnings
        try:
            from bs4 import XMLParsedAsHTMLWarning
            warnings.filterwarnings("ignore", category=XMLParsedAsHTMLWarning)
        except Exception:
            pass

        # SEC requires the User-Agent header. Same identifier our C++ client
        # sends so SEC's rate limiter treats both as one client.
        headers = {"User-Agent": "FinceptTerminal admin@hanlexon.com"}
        r = requests.get(url, headers=headers, timeout=30)
        if r.status_code != 200:
            return {"url": url, "error": f"HTTP {r.status_code}"}

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
        SECTION_BOUNDARY = re.compile(
            r"\n\s*(item\s+\d+|exhibits\s+and\s+financial|signatures|"
            r"undertakings|index\s+to\s+(?:financial|exhibits)|"
            r"principal\s+stockholders|use\s+of\s+proceeds|underwriting|"
            r"recent\s+sales\s+of\s+unregistered\s+securities|"
            r"plan\s+of\s+distribution|risk\s+factors|"
            r"description\s+of\s+capital\s+stock|"
            r"selected\s+(?:consolidated\s+)?financial\s+data|"
            r"summary\s+(?:consolidated\s+)?financial\s+data|"
            r"management's\s+discussion)",
            re.IGNORECASE,
        )
        def extract_section(pattern, cap=12000, lookahead_skip=200):
            mm = re.search(pattern, text, re.IGNORECASE)
            if not mm:
                return ""
            chunk = text[mm.start(): mm.start() + cap]
            # Find the NEXT boundary heading after the current one (skip the
            # current heading itself by skipping the first `lookahead_skip`
            # chars).
            nxt = SECTION_BOUNDARY.search(chunk[lookahead_skip:])
            if nxt:
                chunk = chunk[: nxt.start() + lookahead_skip]
            return chunk.strip()

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
            r"principal\s+(?:and\s+selling\s+)?stockholders?", cap=10000)

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
        for mm in money_re.finditer(section):
            if len(rounds) >= 20: break
            lo = max(0, mm.start() - 250)
            hi = min(len(section), mm.end() + 100)
            ctx = section[lo:hi].strip()
            dm = date_re.search(ctx)
            if not dm: continue
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


def get_batch_quotes(symbols):
    """Fetch quotes for multiple symbols at once using yfinance batch download"""
    try:
        import io, contextlib
        # Suppress any stdout noise from yfinance (progress bars, deprecation notices)
        # that would corrupt the JSON output parsed by the host
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            # Use 5d period to guarantee at least 2 trading days for futures/commodities
            data = yf.download(symbols, period="5d", group_by='ticker', progress=False, threads=True, auto_adjust=True)

        if data is None or data.empty:
            return []

        results = []
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
                    continue
                current_price = float(raw_price)
                # Use previous trading day close for accurate daily change.
                # With period="5d" we always have >= 2 rows for normally-traded instruments.
                raw_prev = hist['Close'].iloc[-2] if len(hist) >= 2 else raw_price
                previous_close = float(raw_prev) if not pd.isna(raw_prev) else current_price
                change = current_price - previous_close
                change_percent = (change / previous_close) * 100 if previous_close else 0

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
                    "price": round(current_price, 2),
                    "change": round(change, 2),
                    "change_percent": round(change_percent, 2),
                    "volume": int(hist['Volume'].iloc[-1]) if not pd.isna(hist['Volume'].iloc[-1]) else 0,
                    "high": round(float(hist['High'].iloc[-1]), 2) if not pd.isna(hist['High'].iloc[-1]) else None,
                    "low": round(float(hist['Low'].iloc[-1]), 2) if not pd.isna(hist['Low'].iloc[-1]) else None,
                    "open": round(float(hist['Open'].iloc[-1]), 2) if not pd.isna(hist['Open'].iloc[-1]) else None,
                    "previous_close": round(previous_close, 2),
                    "timestamp": int(datetime.now().timestamp()),
                    "exchange": ""
                })
            except Exception:
                continue

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
                r["change"] = round(chg, 2)
                r["change_percent"] = round((chg / prior) * 100, 2)
                r["previous_close"] = round(prior, 2)
        except Exception as e:
            # Helper unavailable or failed — fall through with the
            # daily-bar values rather than break the whole batch. Log
            # so the silent fallback is visible.
            import sys
            print(f"[yfinance_data] session-aware fixup failed: {e!r}",
                  file=sys.stderr)

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
    norm_to_orig = {}
    download_syms = []
    for s in symbols:
        n = re.sub(r'\.([A-Z])$', r'-\1', s)
        norm_to_orig[n] = s
        download_syms.append(n)

    try:
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            data = yf.download(download_syms, period=period, interval=interval,
                               group_by='ticker', progress=False, threads=True, auto_adjust=True)

        if data is None or data.empty:
            return {"error": "No data"}

        result = {}
        for norm_sym in download_syms:
            orig_sym = norm_to_orig.get(norm_sym, norm_sym)
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
                result[orig_sym] = [round(float(v), 4) for v in closes.tolist()]
            except Exception:
                continue

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
        out["quotes"] = get_batch_quotes(quote_syms)

    spark_syms = payload.get("sparklines") or []
    if spark_syms:
        out["sparklines"] = get_batch_sparklines(spark_syms)

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


def get_company_profile(symbol):
    """Get company profile data formatted for peer comparison"""
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.info

        if not info or 'symbol' not in info:
            return {"error": f"No data found for symbol: {symbol}"}

        # Format data to match FMP structure
        profile = {
            "symbol": info.get("symbol", symbol),
            "companyName": info.get("longName", info.get("shortName", "")),
            "sector": info.get("sector", ""),
            "industry": info.get("industry", ""),
            "website": info.get("website", ""),
            "description": info.get("longBusinessSummary", ""),
            "exchange": info.get("exchange", ""),
            "country": info.get("country", ""),
            "city": info.get("city", ""),
            "address": info.get("address1", ""),
            "phone": info.get("phone", ""),
            "marketCap": info.get("marketCap", 0),
            "employees": info.get("fullTimeEmployees", 0),
            "currency": info.get("currency", "USD"),
            "beta": info.get("beta", 0),
            "price": info.get("currentPrice", info.get("regularMarketPrice", 0)),
            "changes": info.get("regularMarketChangePercent", 0),
        }

        return profile
    except Exception as e:
        return {"error": str(e)}

def get_financial_ratios(symbol):
    """Get financial ratios formatted for peer comparison"""
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.info

        if not info or 'symbol' not in info:
            return {"error": f"No data found for symbol: {symbol}"}

        # Calculate free cash flow per share
        free_cashflow = info.get("freeCashflow", 0)
        shares_outstanding = info.get("sharesOutstanding", 1)
        fcf_per_share = free_cashflow / shares_outstanding if shares_outstanding else 0

        ratios = {
            "symbol": symbol,
            "peRatio": info.get("trailingPE", 0),
            "forwardPE": info.get("forwardPE", 0),
            "priceToBook": info.get("priceToBook", 0),
            "priceToSales": info.get("priceToSalesTrailing12Months", 0),
            "pegRatio": info.get("trailingPegRatio", 0),
            "debtToEquity": info.get("debtToEquity", 0),
            "returnOnEquity": info.get("returnOnEquity", 0),
            "returnOnAssets": info.get("returnOnAssets", 0),
            "profitMargin": info.get("profitMargins", 0),
            "operatingMargin": info.get("operatingMargins", 0),
            "grossMargin": info.get("grossMargins", 0),
            "currentRatio": info.get("currentRatio", 0),
            "quickRatio": info.get("quickRatio", 0),
            "dividendYield": info.get("dividendYield", 0),
            "revenuePerShare": info.get("revenuePerShare", 0),
            "bookValuePerShare": info.get("bookValue", 0),
            "freeCashFlowPerShare": fcf_per_share,
        }

        return ratios
    except Exception as e:
        return {"error": str(e)}

def get_multiple_profiles(symbols):
    """Get company profiles for multiple symbols"""
    results = []
    for symbol in symbols:
        profile = get_company_profile(symbol)
        if profile and "error" not in profile:
            results.append(profile)
    return results

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


def search_symbols(query, limit=20, asset_type=""):
    """
    Search for stock symbols using yfinance's search API.
    Fast — uses Yahoo Finance's search endpoint instead of brute-force .info calls.

    asset_type (stock/fund/index/crypto) filters by Yahoo's quoteType; empty
    means "any". Results carry the canonical Yahoo `symbol` (foreign listings
    already include their exchange suffix, e.g. "RELIANCE.NS"), a friendly
    exchange display name, and the UI asset-type slug.
    """
    asset_type = (asset_type or "").strip().lower()
    try:
        from urllib.request import urlopen, Request
        from urllib.parse import quote as url_quote
        import ssl

        results = []
        query_str = query.strip()
        if not query_str:
            return {"results": [], "query": query, "count": 0}

        # Use Yahoo Finance search/autocomplete API (fast, no auth needed)
        url = f"https://query2.finance.yahoo.com/v1/finance/search?q={url_quote(query_str)}&quotesCount={limit}&newsCount=0&enableFuzzyQuery=false&quotesQueryId=tss_match_phrase_query"
        ctx = ssl.create_default_context()
        req = Request(url, headers={"User-Agent": "Mozilla/5.0"})

        try:
            with urlopen(req, timeout=5, context=ctx) as response:
                data = json.loads(response.read().decode())
                quotes = data.get("quotes", [])

                for q in quotes:
                    symbol = q.get("symbol", "")
                    if not symbol:
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
                        "currency": q.get("currency", "USD"),
                        "sector": "",
                        "industry": q.get("industry", "")
                    })
                    if len(results) >= limit:
                        break
        except Exception as search_err:
            # Fallback: try the exact symbol as a yfinance Ticker
            query_upper = query_str.upper()
            suffixes = ["", ".NS", ".BO"]
            for suffix in suffixes:
                candidate = query_upper + suffix
                try:
                    ticker = yf.Ticker(candidate)
                    info = ticker.info
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
                            "currency": info.get("currency", "USD"),
                            "sector": info.get("sector", ""),
                            "industry": info.get("industry", "")
                        })
                        break
                except Exception:
                    continue

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
    """
    from datetime import datetime, timedelta, timezone
    try:
        ticker = yf.Ticker(symbol)
        df = ticker.earnings_dates
        if df is None or getattr(df, "empty", True):
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
        return {"error": str(e), "symbol": symbol, "dates": []}


def get_news(symbol, count=20):
    """Fetch news articles for a symbol using yfinance"""
    try:
        import re
        ticker = yf.Ticker(symbol)
        raw_news = ticker.news
        if not raw_news:
            return {"articles": [], "symbol": symbol}

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

        return {"articles": articles, "symbol": symbol}
    except Exception as e:
        return {"error": str(e), "symbol": symbol, "articles": []}

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
    # Bare ticker — try US first, then Toronto, NSE, BSE, LSE, ASX, Hong Kong.
    return [s, f"{s}.TO", f"{s}.NS", f"{s}.BO", f"{s}.L", f"{s}.AX", f"{s}.HK"]


def _resolve_for_history(symbols, period):
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
                auto_adjust=True,
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
            if single:
                return data["Close"].dropna()
            return data[ticker]["Close"].dropna()
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


def get_portfolio_nav_history(positions, period='6mo'):
    """Reconstruct a daily NAV time series from current positions.

    positions: list of {"symbol": str, "quantity": float} dicts.
    Returns: {"dates": [...], "navs": [...], "resolved": {sym: yf_sym},
              "missing": [sym, ...]}.

    Each input symbol is resolved to a yfinance ticker via _resolve_for_history,
    which tries common exchange suffixes (.TO, .NS, .BO, .L, .AX, .HK) when the
    bare symbol returns no data. Missing closes are forward-filled per symbol so
    a symbol that listed mid-period doesn't drop the whole row.

    NAV on each date = sum(close_i * quantity_i) over all resolved symbols.

    NOTE: this back-projects NAV using CURRENT quantity at every date — it
    does not account for buys/sells inside the window. That's acceptable for
    risk metrics (Beta, MDD) which measure basket variability, not realised P&L.
    """
    try:
        if not positions:
            return {"dates": [], "navs": []}
        symbols = [p["symbol"] for p in positions if p.get("symbol")]
        qty_map = {p["symbol"]: float(p.get("quantity", 0)) for p in positions if p.get("symbol")}
        if not symbols:
            return {"dates": [], "navs": []}

        resolved = _resolve_for_history(symbols, period)
        if not resolved:
            return {"dates": [], "navs": [], "error": "no historical data for any symbol",
                    "missing": symbols}

        nav_series = None
        for sym, (yf_sym, closes) in resolved.items():
            contrib = closes.ffill() * qty_map[sym]
            if nav_series is None:
                nav_series = contrib
            else:
                nav_series = nav_series.add(contrib, fill_value=0)

        if nav_series is None or nav_series.empty:
            return {"dates": [], "navs": [], "error": "no usable closes",
                    "missing": [s for s in symbols if s not in resolved]}

        nav_series = nav_series.dropna()
        dates = [idx.strftime("%Y-%m-%d") for idx in nav_series.index]
        navs = [float(v) for v in nav_series.values]
        return {
            "dates": dates,
            "navs": navs,
            "resolved": {s: yf for s, (yf, _) in resolved.items()},
            "missing": [s for s in symbols if s not in resolved],
        }
    except Exception as e:
        return {"dates": [], "navs": [], "error": str(e)}


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


def get_historical_period(symbol, period='6mo', interval='1d'):
    """Fetch historical data using a period string ('1mo', '6mo', '1y', '5y')
    or a custom date range encoded as 'range:YYYY-MM-DD:YYYY-MM-DD'. The
    range form drives yfinance's start/end parameters so the C++ side can
    pass any user-chosen window through the same daemon action."""
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
                hist = ticker.history(start=range_start, end=range_end, interval=interval)
            else:
                hist = ticker.history(period=period, interval=interval)

        # BRK.B-style dot notation returns empty history for periods > 1d
        # even though the 1d quote endpoint succeeds.  Retry with the
        # canonical hyphen form (BRK-B) before giving up.
        if hist.empty:
            ticker_alt, _ = _yf_ticker_with_fallback(symbol)
            if ticker_alt.ticker != symbol:
                with contextlib.redirect_stdout(_buf):
                    if is_range:
                        hist = ticker_alt.history(start=range_start, end=range_end, interval=interval)
                    else:
                        hist = ticker_alt.history(period=period, interval=interval)

        if hist.empty:
            return []

        historical_data = []
        for index, row in hist.iterrows():
            historical_data.append({
                "timestamp": int(index.timestamp()),
                "open": round(float(row['Open']), 2),
                "high": round(float(row['High']), 2),
                "low": round(float(row['Low']), 2),
                "close": round(float(row['Close']), 2),
                "volume": int(row['Volume'])
            })

        return historical_data
    except Exception as e:
        return {"error": str(e), "symbol": symbol}

def resolve_symbol(symbol):
    """
    Resolve a bare stock symbol to its correct yfinance-compatible form.

    Tries the symbol as-is first, then with common exchange suffixes
    (.NS for NSE India, .BO for BSE India).

    Returns a dict with:
      - resolved_symbol: the working yfinance symbol (e.g., "PIDILITIND.NS")
      - original_symbol: what the user typed (e.g., "PIDILITIND")
      - exchange: detected exchange name (if available)
      - found: bool indicating if any variant returned data
    """
    symbol = symbol.strip().upper()
    if not symbol:
        return {"resolved_symbol": symbol, "original_symbol": symbol, "exchange": "", "found": False}

    # If it already has a suffix, verify it works and return as-is
    if '.' in symbol or symbol.startswith('^') or '-' in symbol or '=' in symbol:
        try:
            ticker = yf.Ticker(symbol)
            hist = ticker.history(period="5d")
            exchange = ticker.info.get("exchange", "") if hasattr(ticker, 'info') else ""
            if not hist.empty:
                return {"resolved_symbol": symbol, "original_symbol": symbol, "exchange": exchange, "found": True}
        except Exception:
            pass
        return {"resolved_symbol": symbol, "original_symbol": symbol, "exchange": "", "found": False}

    # Try bare symbol first (US stocks, crypto, etc.)
    suffixes_to_try = ["", ".NS", ".BO"]
    for suffix in suffixes_to_try:
        candidate = symbol + suffix
        try:
            ticker = yf.Ticker(candidate)
            hist = ticker.history(period="5d")
            if hist is not None and not hist.empty and len(hist) >= 1:
                exchange = ""
                try:
                    exchange = ticker.info.get("exchange", "")
                except Exception:
                    pass
                return {
                    "resolved_symbol": candidate,
                    "original_symbol": symbol,
                    "exchange": exchange,
                    "found": True
                }
        except Exception:
            continue

    # Nothing worked — return original
    return {"resolved_symbol": symbol, "original_symbol": symbol, "exchange": "", "found": False}


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

    elif command == "company_profile":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py company_profile <symbol>"}
        else:
            symbol = args[1]
            result = get_company_profile(symbol)

    elif command == "financial_ratios":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py financial_ratios <symbol>"}
        else:
            symbol = args[1]
            result = get_financial_ratios(symbol)

    elif command == "multiple_profiles":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py multiple_profiles <symbol1,symbol2,...>"}
        else:
            symbols = args[1].split(",")
            result = get_multiple_profiles(symbols)

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

    elif command == "portfolio_nav_history":
        # Args layout: <period> <sym1> <qty1> <sym2> <qty2> ...
        # Period defaults to 1y if first arg looks like a symbol.
        if len(args) < 3:
            result = {"error": "Usage: yfinance_data.py portfolio_nav_history <period> <sym1> <qty1> ..."}
        else:
            period = args[1]
            tail = args[2:]
            if len(tail) % 2 != 0:
                result = {"error": "symbol/quantity arguments must be paired"}
            else:
                positions = []
                for i in range(0, len(tail), 2):
                    try:
                        positions.append({"symbol": tail[i], "quantity": float(tail[i + 1])})
                    except ValueError:
                        result = {"error": f"invalid quantity for {tail[i]}"}
                        positions = None
                        break
                if positions is not None:
                    result = get_portfolio_nav_history(positions, period)

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
        # (or @tempfile via PythonRunner's arg-spill) describing quotes + sparklines
        # + histories to fetch. One process, one yfinance warm-up, three family
        # results. See MarketDataService::refresh().
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py batch_all <json_payload>"}
        else:
            raw = args[1]
            # Handle PythonRunner arg-spill: args > ~8KB land as "@/tmp/…" and
            # must be read back. Payloads for large symbol lists easily exceed
            # that threshold on Windows (32KB cmdline limit).
            if raw.startswith("@"):
                path = raw[1:]
                try:
                    with open(path, "r", encoding="utf-8") as f:
                        raw = f.read()
                    try:
                        import os as _os
                        _os.remove(path)
                    except Exception:
                        pass
                except Exception as e:
                    result = {"error": f"batch_all: failed to read spill file: {e}"}
                    raw = None
            if raw is not None:
                try:
                    payload = json.loads(raw)
                    result = get_batch_all(payload)
                except json.JSONDecodeError as e:
                    result = {"error": f"batch_all: invalid JSON payload: {e}"}

    elif command == "resolve_symbol":
        if len(args) < 2:
            result = {"error": "Usage: python yfinance_data.py resolve_symbol <symbol>"}
        else:
            symbol = args[1]
            result = resolve_symbol(symbol)

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
    "extended_hours": 8, "batch_sparklines": 30, "top_movers": 30,
    # slow-moving fundamentals / metadata — long (change at most daily)
    "info": 600, "batch_info": 600, "financials": 600, "financial_ratios": 600,
    "multiple_ratios": 600, "ipo_extras": 600, "earnings_dates": 600,
    # historical / news / search — medium
    "historical_period": 120, "news": 120, "search": 300,
    # batched trade-date close history (power-trader real returns) — long: past
    # daily closes don't change, so a 1h TTL avoids re-downloading wide ranges.
    "batch_closes": 3600,
    # static SEC filing — very long
    "parse_s1": 3600,
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
        return len(result) > 0
    if isinstance(result, dict):
        if not result:
            return False
        for k in result:
            if k in ("error", "fatal") or (isinstance(k, str) and k.endswith("_error")):
                return False
        if set(result.keys()) <= {"symbol"}:
            return False
        return True
    return True


def _daemon_dispatch(action, payload):
    """Run one action and return the raw result object (not wrapped).

    Read-only actions are served from a short-TTL in-process cache (see
    _CACHE_TTL) so duplicate requests don't re-hit Yahoo. The cache is checked
    before, and populated after, the crumb-retry path below. The actual upstream
    call happens OUTSIDE the cache lock, so concurrent misses run in parallel
    (the lock only guards the dict).

    On the first HTTP 401 / Invalid-Crumb error the yfinance session is reset
    and the action is retried once so callers don't see a cascade of failures
    every time Yahoo Finance rotates its auth crumb mid-session.
    """
    ttl = _CACHE_TTL.get(action)
    key = None
    if ttl:
        key = _cache_key(action, payload)
        hit = _cache_get(key)
        if hit is not None:
            return hit

    result = _daemon_dispatch_with_crumb_retry(action, payload)

    if ttl and _is_cacheable_result(result):
        _cache_put(key, ttl, result)
    return result


def _daemon_dispatch_with_crumb_retry(action, payload):
    try:
        return _daemon_dispatch_inner(action, payload)
    except Exception as e:
        if _is_crumb_error(e):
            _reset_yfinance_session()
            return _daemon_dispatch_inner(action, payload)
        raise


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
        return get_historical_period(
            p.get("symbol"), p.get("period", "6mo"), p.get("interval", "1d"))
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
    if action == "ipo_extras":
        # IPO Watch detail-rail aggregator: quarterly financials, holders,
        # news. One daemon round-trip on row click.
        return get_ipo_extras((payload or {}).get("symbol"))
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
    if action == "news":
        p = payload or {}
        return get_news(p.get("symbol"), p.get("count", 20))
    if action == "portfolio_nav_history":
        p = payload or {}
        positions = p.get("positions") or []
        period = p.get("period", "6mo")
        return get_portfolio_nav_history(positions, period)
    if action == "extended_hours":
        p = payload or {}
        symbols = p.get("symbols") or []
        return get_extended_hours_quotes(symbols)
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
    Compute the full indicator suite from a candle list. Mirrors what
    compute_technicals.py does standalone, but reuses the daemon's already-
    imported pandas + cached `ta` modules.

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
        # Order mirrors compute_technicals.py: trend, momentum, volatility,
        # volume (only if available), others.
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


# Extended-hours helper — returns regular / pre / post for each symbol so the
# Portfolio Futures view (and Portfolio Heatmap AFT mode) shows after-hours
# moves for every holding. The previous implementation looped one
# `Ticker(sym).info` call per symbol — a heavy Yahoo quote-page scrape
# (~1-3 s/symbol, throttled, preMarketPrice/postMarketPrice often missing).
# For a 20-row portfolio it routinely returned empty rows or timed out.
#
# This implementation does ONE batched intraday download
# (period=5d, interval=1m, prepost=True) and derives the regular /
# pre-market / post-market fields directly from the bar timeline. Same
# JSON shape for the C++ side; faster, more reliable, no info-scrape.
def get_extended_hours_quotes(symbols):
    if not symbols:
        return []
    try:
        import io
        import contextlib
        import yfinance as _yf
        import pandas as _pd
        from datetime import datetime
        from zoneinfo import ZoneInfo
    except Exception:
        return []

    _buf = io.StringIO()
    with contextlib.redirect_stdout(_buf):
        try:
            data = _yf.download(
                symbols, period="5d", interval="1m",
                prepost=True, group_by="ticker", progress=False,
                threads=True, auto_adjust=True,
            )
        except Exception as e:
            import sys as _sys
            print(f"[yfinance_data] extended-hours download failed: {e!r}",
                  file=_sys.stderr)
            return []

    # Defer session label so we can short-circuit on empty data without
    # tripping the et zoneinfo lookup.
    et = ZoneInfo("America/New_York")
    now_et = datetime.now(et)

    if data is None or getattr(data, "empty", True):
        return [_empty_ext_row(s, _ext_session_label(now_et)) for s in symbols]

    session = _ext_session_label(now_et)
    today = now_et.date()

    # Today's pre-market / open boundaries in UTC (only meaningful for
    # equities; futures/crypto ignore these).
    pre_open_utc = _pd.Timestamp(now_et.replace(hour=4,  minute=0,  second=0, microsecond=0)).tz_convert("UTC")
    open_utc     = _pd.Timestamp(now_et.replace(hour=9,  minute=30, second=0, microsecond=0)).tz_convert("UTC")

    # Per-symbol "most recent completed session-end" comes from the
    # exchange_sessions registry — equities: 16:00 ET, futures: each
    # exchange's settle time, crypto: 00:00 UTC, FX: 17:00 ET.
    try:
        from exchange_sessions import previous_session_end_utc as _prev_end
    except Exception:
        _prev_end = None

    rows = []
    for sym in symbols:
        try:
            if isinstance(data.columns, _pd.MultiIndex):
                level0 = data.columns.get_level_values(0).unique().tolist()
                level1 = data.columns.get_level_values(1).unique().tolist()
                if sym in level0:
                    hist = data[sym]
                elif sym in level1:
                    hist = data.xs(sym, axis=1, level=1)
                else:
                    rows.append(_empty_ext_row(sym, session))
                    continue
            else:
                hist = data
            hist = hist.dropna(how="all")
            if hist.empty or "Close" not in hist.columns:
                rows.append(_empty_ext_row(sym, session))
                continue

            idx = hist.index
            if idx.tz is None:
                idx = idx.tz_localize("UTC")

            # "regular_end" = the most recent COMPLETED regular session-end
            # for this symbol. Past 20:00 ET or before 04:00 ET we'd
            # otherwise be tempted to use "today's 16:00 ET" — but if
            # today's hasn't happened yet that reference is in the future
            # and any "before today's 16:00 ET" filter would pick up the
            # previous day's post-market bars, badly distorting `regular`.
            if _prev_end is not None:
                regular_end_utc = _pd.Timestamp(_prev_end(sym))
            else:
                # Fallback: equity convention (16:00 ET) on the most
                # recent weekday <= now.
                t = now_et.replace(hour=16, minute=0, second=0, microsecond=0)
                if t > now_et:
                    from datetime import timedelta as _td
                    t -= _td(days=1)
                while t.weekday() >= 5:
                    from datetime import timedelta as _td
                    t -= _td(days=1)
                regular_end_utc = _pd.Timestamp(t).tz_convert("UTC")

            # regular = last bar at or before regular_end. For a query at
            # 00:10 ET Thursday this lands on Wed 16:00 ET (yesterday's
            # close) — which is exactly the right denominator for
            # Wednesday's post-market or Thursday's pre-market.
            regular = None
            mask_reg = idx <= regular_end_utc
            if bool(mask_reg.any()):
                v = hist["Close"][mask_reg].iloc[-1]
                if not _pd.isna(v):
                    regular = float(v)

            # post-market: bars strictly after regular_end whose ET date
            # equals regular_end's ET date (i.e. the same trading day —
            # we don't want tomorrow's pre-market to bleed in as "post").
            post = None
            mask_post = idx > regular_end_utc
            if bool(mask_post.any()):
                idx_et = idx[mask_post].tz_convert(et)
                same_day = (idx_et.date == regular_end_utc.tz_convert(et).date())
                if same_day.any():
                    close_series = hist["Close"][mask_post]
                    v = close_series[same_day].iloc[-1]
                    if not _pd.isna(v):
                        post = float(v)

            # pre-market: bars in TODAY's [04:00, 09:30) ET window.
            # Only populated when we're currently past 04:00 ET today.
            pre = None
            mask_pre = (idx >= pre_open_utc) & (idx < open_utc)
            if bool(mask_pre.any()):
                idx_et = idx[mask_pre].tz_convert(et)
                same_day = (idx_et.date == today)
                if same_day.any():
                    close_series = hist["Close"][mask_pre]
                    v = close_series[same_day].iloc[-1]
                    if not _pd.isna(v):
                        pre = float(v)

            # Pick the relevant extended-hours price for display:
            # match current session; otherwise show most recently
            # observed extended-hours print (post if both exist).
            if session == "PRE" and pre is not None:
                ext = pre
            elif session == "POST" and post is not None:
                ext = post
            elif post is not None:
                ext = post
            elif pre is not None:
                ext = pre
            else:
                ext = None

            ext_chg = None
            ext_pct = None
            if ext is not None and regular:
                ext_chg = ext - regular
                ext_pct = (ext_chg / regular) * 100.0

            rows.append({
                "symbol": sym,
                "regular": regular,
                "pre_market": pre,
                "post_market": post,
                "ext_price": ext,
                "ext_change": ext_chg,
                "ext_change_pct": ext_pct,
                "session": session,
                # name/currency are no longer fetched — they required
                # the per-symbol Ticker.info call we just removed. The
                # consumer (PortfolioFuturesView, PortfolioHeatmap) gets
                # display names elsewhere; an empty string is fine.
                "currency": "",
                "name": sym,
            })
        except Exception:
            rows.append(_empty_ext_row(sym, session))
    return rows


def _empty_ext_row(sym, session):
    return {
        "symbol": sym, "regular": None,
        "pre_market": None, "post_market": None,
        "ext_price": None, "ext_change": None, "ext_change_pct": None,
        "session": session, "currency": "", "name": sym,
    }


def _ext_session_label(now_et):
    """Derive a session label (PRE/REGULAR/POST/CLOSED) from the current ET
    clock. Saturday/Sunday and overnight gaps map to CLOSED. Half-day or
    holiday closes still report by clock alone — that's acceptable here
    since the session label is purely cosmetic in the consumer view."""
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


def run_daemon_socket(socket_path):
    """Multi-threaded daemon over a Unix domain socket.

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
        ready_bytes = json.dumps({"ready": True, "pid": os.getpid()}).encode("utf-8")
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
            body = json.dumps({
                "id": resp_dict.get("id", 0), "ok": False,
                "error": f"non-JSON-safe result: {exc}"
            }).encode("utf-8")
        frame = len(body).to_bytes(4, "big") + body
        with write_lock:
            conn.sendall(frame)

    def _handle(req_id, action, payload):
        try:
            result = _daemon_dispatch(action, payload)
            _send_response({"id": req_id, "ok": True,
                            "result": _sanitize_for_json(result)})
        except Exception as exc:
            _send_response({"id": req_id, "ok": False, "error": str(exc)})

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

                if action == "shutdown":
                    _send_response({"id": req_id, "ok": True,
                                    "result": {"shutdown": True}})
                    return

                pool = compute_pool if action in _COMPUTE_ACTIONS else network_pool
                pool.submit(_handle, req_id, action, payload)
    finally:
        network_pool.shutdown(wait=False)
        compute_pool.shutdown(wait=False)
        try:
            conn.close()
            srv.close()
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
            result = _daemon_dispatch(action, req.get("payload"))
            resp = {"id": req_id, "ok": True, "result": _sanitize_for_json(result)}
        except Exception as e:
            resp = {"id": req_id, "ok": False, "error": str(e)}
        try:
            # allow_nan=False is the safety net — if anything still slips
            # through _sanitize_for_json (e.g. numpy.float64 NaN we missed),
            # we get a ValueError here instead of writing invalid JSON that
            # the C++ side will silently drop.
            body = json.dumps(resp, allow_nan=False).encode("utf-8")
        except (ValueError, TypeError) as e:
            err_resp = {"id": req_id, "ok": False,
                        "error": f"non-JSON-safe result: {e}"}
            body = json.dumps(err_resp).encode("utf-8")
        try:
            _daemon_write_frame(stdout, body)
        except Exception:
            break


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        # Parse optional --socket <path> argument
        socket_path = None
        args = sys.argv[2:]
        for i, arg in enumerate(args):
            if arg == "--socket" and i + 1 < len(args):
                socket_path = args[i + 1]
                break
        if socket_path:
            run_daemon_socket(socket_path)
        else:
            run_daemon()   # fallback: stdin/stdout (backward compat)
    else:
        main()
