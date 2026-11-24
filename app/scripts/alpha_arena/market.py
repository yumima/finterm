"""Prices. Crypto: an exchange's public API. Prediction markets: Polymarket's."""

from __future__ import annotations

import functools
import http.client
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import asdict, dataclass

_TIMEOUT_S = 15
GAMMA = "https://gamma-api.polymarket.com"


class MarketError(Exception):
    """What could not be had, and why, for the reader."""


def _get(url: str, who: str):
    req = urllib.request.Request(url, headers={"User-Agent": "finterm", "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=_TIMEOUT_S) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        raise MarketError(f"{who} answered {e.code}") from e
    except (urllib.error.URLError, http.client.HTTPException, OSError, ValueError) as e:
        raise MarketError(f"{who} could not be reached: {getattr(e, 'reason', e)}") from e


def _readable(f):
    """An answer of another shape than expected is a MarketError too."""
    @functools.wraps(f)
    def wrapped(*args):
        try:
            return f(*args)
        except (KeyError, IndexError, TypeError, AttributeError, ValueError) as e:
            raise MarketError(f"{args[-1] if args else 'Polymarket'}: an answer that can't be read ({type(e).__name__})") from e
    return wrapped


def _num(v) -> float | None:
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if f == f and abs(f) != float("inf") else None


@dataclass
class Quote:
    symbol: str
    exchange: str
    price: float
    bid: float
    ask: float
    high_24h: float | None = None
    low_24h: float | None = None
    volume_24h: float | None = None  # in the coin
    at: float = 0.0

    @property
    def mark(self) -> float:
        """What a position is worth: between the prices one can trade at. The last
        trade may be outside them, and a trade then shows a profit at once."""
        return (self.bid + self.ask) / 2

    def to_dict(self) -> dict:
        return asdict(self)


def _quote(symbol: str, exchange: str, price, bid, ask, high, low, volume) -> Quote:
    price, bid, ask = _num(price), _num(bid), _num(ask)
    if not (price and bid and ask) or min(price, bid, ask) <= 0 or bid > ask:
        raise MarketError(f"{exchange} has no usable price for {symbol}")
    return Quote(symbol, exchange, price, bid, ask, _num(high), _num(low), _num(volume), time.time())


# ── Exchanges ────────────────────────────────────────────────────────────────

def _kraken(path: str, symbol: str) -> dict:
    pair = symbol.replace("/", "")
    data = _get(f"https://api.kraken.com/0/public/{path}pair={urllib.parse.quote(pair)}", "Kraken")
    if data.get("error"):
        raise MarketError(f"Kraken has no {symbol}: {', '.join(map(str, data['error']))}")
    result = {k: v for k, v in (data.get("result") or {}).items() if k != "last"}
    if not result:
        raise MarketError(f"Kraken has no {symbol}")
    return next(iter(result.values()))


def _kraken_quote(symbol: str) -> Quote:
    t = _kraken("Ticker?", symbol)
    return _quote(symbol, "kraken", t["c"][0], t["b"][0], t["a"][0], t["h"][1], t["l"][1], t["v"][1])


def _kraken_closes(symbol: str) -> list[float]:
    return [c for row in _kraken("OHLC?interval=60&", symbol) if (c := _num(row[4]))]


def _coinbase_quote(symbol: str) -> Quote:
    product = urllib.parse.quote(symbol.replace("/", "-"))
    t = _get(f"https://api.exchange.coinbase.com/products/{product}/ticker", "Coinbase")
    s = _get(f"https://api.exchange.coinbase.com/products/{product}/stats", "Coinbase")
    return _quote(symbol, "coinbase", t.get("price"), t.get("bid"), t.get("ask"), s.get("high"), s.get("low"),
                  t.get("volume"))


def _coinbase_closes(symbol: str) -> list[float]:
    product = urllib.parse.quote(symbol.replace("/", "-"))
    rows = _get(f"https://api.exchange.coinbase.com/products/{product}/candles?granularity=3600", "Coinbase")
    return [c for row in sorted(rows) if (c := _num(row[4]))]  # [time, low, high, open, close, volume]


EXCHANGES = {"kraken": (_kraken_quote, _kraken_closes), "coinbase": (_coinbase_quote, _coinbase_closes)}


@_readable
def quote(symbol: str, exchange: str) -> Quote:
    if exchange not in EXCHANGES:
        raise MarketError(f"{exchange} is not one of {', '.join(EXCHANGES)}")
    return EXCHANGES[exchange][0](symbol)


@_readable
def hourly_closes(symbol: str, exchange: str) -> list[float]:
    """Oldest first; the last hour is the one still running."""
    return EXCHANGES[exchange][1](symbol)


# ── Polymarket ───────────────────────────────────────────────────────────────

@dataclass
class PredictionMarket:
    id: str
    question: str
    outcomes: list[str]
    prices: list[float]
    bid: float | None  # of the first outcome; the second's are 1 - ask and 1 - bid
    ask: float | None
    liquidity: float | None
    volume: float | None
    end_date: str
    open: bool
    settled: bool

    def symbol(self, outcome: int) -> str:
        return f"pm:{self.id}:{outcome}"

    def bid_ask(self, outcome: int) -> tuple[float, float]:
        """What the outcome can be sold and bought at; 0 where it can't."""
        bid, ask = self.bid or 0.0, self.ask or 0.0
        if outcome == 1:
            bid, ask = (1 - ask if ask else 0.0), (1 - bid if bid else 0.0)
        return bid, (ask if ask < 1 else 0.0)

    def mark(self, outcome: int) -> float:
        """What a share is worth: see Quote.mark."""
        bid, ask = self.bid_ask(outcome)
        return (bid + ask) / 2 if self.open and 0 < bid <= ask else self.prices[outcome]

    def to_dict(self) -> dict:
        return asdict(self)


def _list(v) -> list:
    if isinstance(v, str):
        try:
            v = json.loads(v)
        except ValueError:
            return []
    return v if isinstance(v, list) else []


def _prediction_market(d: dict) -> PredictionMarket | None:
    outcomes = [str(o) for o in _list(d.get("outcomes"))]
    prices = [_num(p) for p in _list(d.get("outcomePrices"))]
    if not d.get("id") or len(outcomes) != 2 or len(prices) != 2 or None in prices:
        return None
    closed = bool(d.get("closed"))
    return PredictionMarket(
        id=str(d["id"]), question=str(d.get("question") or ""), outcomes=outcomes, prices=prices,
        bid=_num(d.get("bestBid")), ask=_num(d.get("bestAsk")),
        liquidity=_num(d.get("liquidityNum")), volume=_num(d.get("volumeNum")), end_date=str(d.get("endDate") or ""),
        open=not closed and bool(d.get("acceptingOrders")),
        # Closed and decided, not "its price is near 1".
        settled=closed and d.get("umaResolutionStatus") == "resolved")


@_readable
def prediction_market(market_id: str) -> PredictionMarket:
    d = _get(f"{GAMMA}/markets/{urllib.parse.quote(str(market_id))}", "Polymarket")
    m = _prediction_market(d if isinstance(d, dict) else {})
    if m is None:
        raise MarketError(f"Polymarket has no usable market {market_id}")
    return m


@_readable
def active_prediction_markets(limit: int = 40) -> list[PredictionMarket]:
    """The most traded today that are still undecided."""
    rows = _get(f"{GAMMA}/markets?closed=false&active=true&order=volume24hr&ascending=false&limit=100", "Polymarket")
    markets = [m for d in rows if isinstance(d, dict) and (m := _prediction_market(d))]
    return [m for m in markets if m.open and 0.02 <= m.prices[0] <= 0.98][:limit]
