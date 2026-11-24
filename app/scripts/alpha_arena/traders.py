"""What a model is asked, and the decision in its answer; the grid bot.

An answer that holds no decision is no decision: it is recorded as that, with
the reason. It never becomes a HOLD, which is a choice a model made.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field

from .book import FEE_RATE, Book, Limits
from .features import price_text
from .market import PredictionMarket, Quote

GRID_PROVIDER = "rules"


class NoDecision(Exception):
    pass


@dataclass
class Decision:
    model_name: str
    symbol: str
    action: str  # buy | sell | short | hold | none (there was no decision)
    size: float = 0.0  # share of the portfolio's value, 0..1
    confidence: float | None = None
    reasoning: str = ""
    error: str = ""  # why there is none, or why it was not carried out
    status: str = ""  # executed | rejected | hold | none
    quantity: float = 0.0
    price: float | None = None  # of the fill
    pnl: float | None = None  # what the fill realized
    market: PredictionMarket | None = field(default=None, repr=False)
    outcome: int = 0


# ── The prompts ──────────────────────────────────────────────────────────────

_MODES = {
    "baseline": "Weigh all the data and size your trades with care.",
    "situational": "You compete with other AI models trading the same market; only the standing at the end counts.",
    "monk": "Preserving capital comes first: trade only when you are confident (0.75 or more), and small.",
    "max_leverage": "Use the capital you have: large positions are allowed.",
}


def _percent(x: float) -> str:
    return f"{x * 100:g}%"


def crypto_system(mode: str, limits: Limits, extra: str) -> str:
    rules = [
        "You trade one cryptocurrency with paper money, against other AI models.",
        _MODES[mode],
        f"One trade is at most {_percent(limits.max_trade)} of your portfolio's value, the position at most "
        f"{_percent(limits.max_position)} of it, long or short. Every trade costs {_percent(FEE_RATE)}.",
    ]
    if limits.min_confidence:
        rules.append(f"A trade with a confidence below {limits.min_confidence} is not carried out.")
    if extra.strip():
        rules.append(extra.strip())
    rules += [
        "Answer with one JSON object and nothing else:",
        '{"action": "buy" | "sell" | "short" | "hold", "size_pct": <percent of your portfolio\'s value to trade, '
        '0 for hold>, "confidence": <0 to 1>, "reasoning": "<one or two sentences>"}',
        "buy: covers a short first, then goes long. sell: closes a long. short: closes a long, then goes short. "
        "In the reasoning use only figures given to you.",
    ]
    return "\n".join(rules)


def _portfolio_lines(book: Book, prices: dict[str, float]) -> list[str]:
    value = book.value(prices)
    return [
        f"Started with: ${book.initial_capital:,.2f}",
        f"Cash: ${book.cash:,.2f}",
        f"Portfolio value: ${value:,.2f} ({value / book.initial_capital - 1:+.2%})",
        f"Trades so far: {book.trades}",
    ]


def crypto_prompt(q: Quote, technical: list[str], news: list[dict], book: Book, recent: list[dict], cycle: int) -> str:
    lines = [f"Cycle {cycle}", "", "MARKET", f"{q.symbol} on {q.exchange}: {price_text(q.price)}",
             f"Bid {price_text(q.bid)}, ask {price_text(q.ask)}"]
    if q.high_24h and q.low_24h:
        lines.append(f"24h range: {price_text(q.low_24h)} to {price_text(q.high_24h)}")
    if q.volume_24h:
        lines.append(f"24h volume: {q.volume_24h:,.0f} {q.symbol.split('/')[0]}")
    if technical:
        lines += ["", "INDICATORS", *technical]
    if news:
        lines += ["", "HEADLINES (last two days)", *(f"- {n['title']} ({n['publisher']})" for n in news)]
    lines += ["", "YOUR PORTFOLIO", *_portfolio_lines(book, {q.symbol: q.price})]
    pos = book.positions.get(q.symbol)
    if pos:
        side = "long" if pos.quantity > 0 else "short"
        lines.append(f"Position: {side} {abs(pos.quantity):.8g} {q.symbol.split('/')[0]}, entered at "
                     f"{price_text(pos.entry_price)}, now {pos.unrealized(q.price):+,.2f} USD")
    else:
        lines.append("Position: none")
    if recent:
        lines += ["", "YOUR LAST DECISIONS", *(f"- cycle {d['cycle_number']}: {d['action']} ({d['status']})" for d in recent)]
    return "\n".join(lines)


def prediction_system(mode: str, limits: Limits, extra: str) -> str:
    rules = [
        "You trade prediction markets with paper money, against other AI models. A share pays $1 if its outcome "
        "happens and nothing if it does not; its price is what the market thinks the chance is.",
        _MODES[mode],
        f"One trade is at most {_percent(limits.max_trade)} of your portfolio's value, one outcome at most "
        f"{_percent(limits.max_position)} of it. Every trade costs {_percent(FEE_RATE)}.",
    ]
    if limits.min_confidence:
        rules.append(f"A trade with a confidence below {limits.min_confidence} is not carried out.")
    if extra.strip():
        rules.append(extra.strip())
    rules += [
        "Answer with one JSON array and nothing else, one object for each market you decide on:",
        '[{"market_id": "<id>", "outcome": "<one of its outcomes>", "action": "buy" | "sell" | "hold", '
        '"size_pct": <percent of your portfolio\'s value, 0 for hold>, "confidence": <0 to 1>, '
        '"reasoning": "<one sentence>"}]',
        "sell: shares you hold. In the reasoning use only figures given to you.",
    ]
    return "\n".join(rules)


def prediction_prompt(markets: list[PredictionMarket], book: Book, prices: dict[str, float], cycle: int) -> str:
    lines = [f"Cycle {cycle}", "", "MARKETS"]
    for m in markets:
        lines.append(f"- id {m.id}: {m.question}")
        for i, name in enumerate(m.outcomes):
            bid, ask = m.bid_ask(i)
            lines.append(f"    {name}: {m.prices[i]:.3f} (bid {bid:.3f}, ask {ask:.3f})")
        about = [f"ends {m.end_date}"] if m.end_date else []
        if m.liquidity:
            about.append(f"liquidity ${m.liquidity:,.0f}")
        if about:
            lines.append("    " + ", ".join(about))
    lines += ["", "YOUR PORTFOLIO", *_portfolio_lines(book, prices)]
    names = {m.symbol(i): f"{m.outcomes[i]} — {m.question}" for m in markets for i in (0, 1)}
    for symbol, pos in book.positions.items():
        now = prices.get(symbol)
        lines.append(f"Holds {pos.quantity:.2f} of {names.get(symbol, symbol)}, entered at {pos.entry_price:.3f}"
                     + (f", now {now:.3f}" if now is not None else ""))
    return "\n".join(lines)


# ── The answers ──────────────────────────────────────────────────────────────

def _json_values(text: str) -> list:
    """Every top-level JSON object or array in the text, in order."""
    out, at, decoder = [], 0, json.JSONDecoder()
    while (at := min((i for i in (text.find("{", at), text.find("[", at)) if i >= 0), default=-1)) >= 0:
        try:
            value, at = decoder.raw_decode(text, at)
            out.append(value)
        except ValueError:
            at += 1
    return out


def _number(v, low: float, high: float, what: str) -> float:
    if isinstance(v, str):
        v = v.strip().rstrip("%")
    try:
        f = float(v)
    except (TypeError, ValueError):
        raise NoDecision(f"its {what} is not a number: {str(v)[:40]!r}") from None
    if not math.isfinite(f) or not low <= f <= high:
        raise NoDecision(f"its {what} is out of range: {f:g}")
    return f


def _read(d: dict, actions: tuple[str, ...]) -> dict:
    action = str(d.get("action") or "").strip().lower()
    if action not in actions:
        raise NoDecision(f"its action is {action!r}, not one of {', '.join(actions)}")
    confidence = d.get("confidence")
    reasoning = d.get("reasoning")
    return {
        "action": action,
        "size": 0.0 if action == "hold" else _number(d.get("size_pct"), 0, 100, "size_pct") / 100,
        "confidence": None if confidence is None else _number(confidence, 0, 1, "confidence"),
        "reasoning": " ".join(str(reasoning).split())[:600] if reasoning else "",
    }


def read_crypto(name: str, symbol: str, text: str) -> Decision:
    found = [v for v in _json_values(text) if isinstance(v, dict) and "action" in v]
    if not found:
        raise NoDecision(f"its answer holds no decision: {' '.join(text.split())[:80]!r}")
    return Decision(name, symbol, **_read(found[-1], ("buy", "sell", "short", "hold")))


def read_predictions(name: str, markets: list[PredictionMarket], text: str) -> list[Decision]:
    """One for every market it decided on; a market it left out is left alone."""
    by_id = {m.id: m for m in markets}
    found: list = []
    for v in _json_values(text):
        rows = v if isinstance(v, list) else [v]
        rows = [r for r in rows if isinstance(r, dict) and "market_id" in r and "action" in r]
        found = rows or found
    if not found and not any(v == [] for v in _json_values(text)):
        raise NoDecision(f"its answer holds no decision: {' '.join(text.split())[:80]!r}")
    out = []
    for row in found:
        market = by_id.get(str(row["market_id"]))
        if market is None:
            out.append(Decision(name, f"pm:{str(row['market_id'])[:20]}", "none", status="none",
                                error="it names a market that is not in the competition"))
            continue
        try:
            read = _read(row, ("buy", "sell", "hold"))
            outcomes = [o.lower() for o in market.outcomes]
            outcome = str(row.get("outcome") or "").strip().lower()
            if outcome not in outcomes:
                if read["action"] != "hold":
                    raise NoDecision(f"its outcome is {outcome!r}, not one of {', '.join(market.outcomes)}")
                outcome = outcomes[0]
            at = outcomes.index(outcome)
            out.append(Decision(name, market.symbol(at), **read, market=market, outcome=at))
        except NoDecision as e:
            out.append(Decision(name, market.symbol(0), "none", status="none", error=str(e), market=market))
    return out


# ── The grid bot ─────────────────────────────────────────────────────────────

@dataclass
class Grid:
    """Buys a unit for every line the price falls through, sells one for every
    line it rises through. The lines are set when the competition starts; how
    many units it should hold follows from the price and what it holds, so
    nothing else has to be kept."""

    lower: float
    upper: float
    levels: int
    capital: float

    @classmethod
    def around(cls, price: float, range_pct: float, levels: int, capital: float) -> "Grid":
        return cls(price * (1 - range_pct / 100), price * (1 + range_pct / 100), levels, capital)

    @property
    def step(self) -> float:
        return (self.upper - self.lower) / self.levels

    @property
    def unit(self) -> float:
        """All units together cost the capital, less a little for fees and the spread."""
        return self.capital * 0.95 / (self.levels * (self.upper + self.lower) / 2)

    def target(self, price: float, units: int) -> int:
        """A unit is sold a whole line above where it was bought; at one line
        alone the bot would buy and sell on every tick."""
        depth = max(0.0, min(float(self.levels), (self.upper - price) / self.step))
        if depth >= units + 1:
            return math.floor(depth)
        if depth <= units - 1:
            return math.ceil(depth)
        return units

    def decide(self, name: str, q: Quote, book: Book) -> Decision:
        units = round(book.held(q.symbol) / self.unit)
        target = self.target(q.price, units)
        value = book.value({q.symbol: q.price})
        where = f"{target} of {self.levels} units between {price_text(self.lower)} and {price_text(self.upper)}"
        if target == units or value <= 0:
            return Decision(name, q.symbol, "hold", reasoning=f"Holds {where}; no line was crossed.")
        size = abs(target - units) * self.unit * q.price / value
        action = "buy" if target > units else "sell"
        return Decision(name, q.symbol, action, size=min(1.0, size), reasoning=f"Goes from {units} to {where}.")

    def rows(self, price: float, held: float) -> list[dict]:
        units = round(held / self.unit)
        return [{"price": self.upper - self.step * i, "units": i, "held": i <= units} for i in range(1, self.levels + 1)]
