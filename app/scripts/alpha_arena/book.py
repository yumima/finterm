"""A trader's portfolio, worked out from its fills.

Fills are all that is stored. Cash, positions, realized profit and value follow
from them and from today's prices; kept as numbers of their own they came apart
(a purchase left the cash where it was and still counted as a position, so the
leaderboard ranked who had bought most).
"""

from __future__ import annotations

from dataclasses import dataclass, field

FEE_RATE = 0.001  # of a fill's value, both ways
_DUST = 1e-12


@dataclass
class Fill:
    symbol: str
    side: str  # "buy" | "sell" | "settle" (a prediction market's payout)
    quantity: float
    price: float
    fee: float = 0.0


@dataclass
class Position:
    quantity: float = 0.0  # below zero: short
    entry_price: float = 0.0
    fees: float = 0.0  # paid to enter what is held

    def unrealized(self, price: float) -> float:
        return (price - self.entry_price) * self.quantity


@dataclass
class Book:
    initial_capital: float
    cash: float = 0.0
    fees: float = 0.0
    positions: dict[str, Position] = field(default_factory=dict)
    closed: list[float] = field(default_factory=list)  # realized profit of each fill that closed something
    trades: int = 0

    @classmethod
    def of(cls, initial_capital: float, fills: list[Fill]) -> "Book":
        book = cls(initial_capital=initial_capital, cash=initial_capital)
        for fill in fills:
            book.apply(fill)
        return book

    def apply(self, fill: Fill) -> float | None:
        """Returns the profit it realized, None when it closed nothing."""
        pos = self.positions.setdefault(fill.symbol, Position())
        signed = fill.quantity
        if fill.side != "buy":  # a settlement sells what was held
            signed = -fill.quantity
        self.cash -= signed * fill.price + fill.fee
        self.fees += fill.fee
        if fill.side != "settle":
            self.trades += 1

        realized, opening_fee = None, fill.fee
        if pos.quantity * signed < 0:
            closing = min(abs(signed), abs(pos.quantity))
            gain = (fill.price - pos.entry_price) * closing * (1 if pos.quantity > 0 else -1)
            # What it cost, both ways: its share of this fill's fee and of the fees paid to enter.
            closing_fee, entry_fee = fill.fee * closing / abs(signed), pos.fees * closing / abs(pos.quantity)
            realized = gain - closing_fee - entry_fee
            pos.fees -= entry_fee
            opening_fee = fill.fee - closing_fee
            self.closed.append(realized)
        left = pos.quantity + signed
        if abs(left) < _DUST:
            pos.quantity, pos.entry_price = 0.0, 0.0
        elif pos.quantity * signed >= 0:  # opened, or added to
            pos.entry_price = (pos.entry_price * abs(pos.quantity) + fill.price * abs(signed)) / abs(left)
            pos.quantity = left
        else:
            if left * pos.quantity < 0:  # turned round
                pos.entry_price = fill.price
            pos.quantity = left
        pos.fees += opening_fee
        if pos.quantity == 0.0:
            del self.positions[fill.symbol]
        return realized

    def held(self, symbol: str) -> float:
        pos = self.positions.get(symbol)
        return pos.quantity if pos else 0.0

    def value(self, prices: dict[str, float]) -> float:
        """Cash and positions at `prices`; one without a price at what it was entered at."""
        return self.cash + sum(p.quantity * prices.get(s, p.entry_price) for s, p in self.positions.items())

    def win_rate(self) -> float | None:
        return sum(1 for p in self.closed if p > 0) / len(self.closed) if self.closed else None


@dataclass
class Limits:
    """What a competition's mode allows, of the portfolio's value."""

    max_trade: float = 0.25
    max_position: float = 0.5
    min_confidence: float = 0.0


MODES = {
    "baseline": Limits(0.25, 0.5),
    "situational": Limits(0.25, 0.5),
    "monk": Limits(0.10, 0.3, min_confidence=0.75),
    "max_leverage": Limits(0.5, 0.95),
}


class Rejected(Exception):
    """An order the rules do not allow; the message says which."""


def order(book: Book, limits: Limits, symbol: str, action: str, size: float, bid: float, ask: float,
          prices: dict[str, float], allow_short: bool = True, max_value: float | None = None) -> Fill:
    """The fill of an order for `size` (a share of the portfolio's value, 0..1):
    a purchase at the ask, a sale at the bid, cut to what the rules allow.

    `buy` covers a short first, then opens; `sell` closes what is held; `short`
    closes it too, then goes below zero. The limits are for what is opened: a
    position can always be closed, also one that has outgrown them.
    """
    value, held = book.value(prices), book.held(symbol)
    price = ask if action == "buy" else bid
    if not price > 0:
        raise Rejected("there is no price to trade at")
    if action not in ("buy", "sell", "short") or (action == "short" and not allow_short):
        raise Rejected(f"{action} is not possible here")

    against = max(0.0, -held) if action == "buy" else max(0.0, held)  # units there are to close
    closing = min(against, size * max(value, against * price) / price)
    opening = 0.0
    if action != "sell" and closing >= against and value > 0:
        most = limits.max_position * value / price - max(0.0, held if action == "buy" else -held)
        opening = max(0.0, min(min(size, limits.max_trade) * value / price - closing, most))
        if action == "buy":
            opening = min(opening, max(0.0, book.cash / (price * (1 + FEE_RATE)) - closing))
    quantity = closing + opening
    if max_value is not None:
        quantity = min(quantity, max_value / price)
    if quantity * price < 0.01:
        raise Rejected("there is nothing to sell" if action == "sell" and held <= 0
                       else "there is too little liquidity" if max_value is not None and max_value < 0.01
                       else "its size is too small" if action == "sell"
                       else "the portfolio is worth nothing" if value <= 0
                       else "the position limit is reached, or no cash is left")
    return Fill(symbol, "buy" if action == "buy" else "sell", quantity, price, quantity * price * FEE_RATE)
