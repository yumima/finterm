"""Indicators from hourly closes (oldest first), as lines for a prompt."""

from __future__ import annotations

import math


def price_text(p: float) -> str:
    """A price with the digits it needs: PEPE trades at $0.0000041."""
    if p >= 1000:
        return f"${p:,.2f}"
    digits = 2 if p >= 100 else 4 if p >= 1 else min(12, 3 - math.floor(math.log10(p))) if p > 0 else 2
    return f"${p:.{digits}f}"


def sma(xs: list[float], n: int) -> float | None:
    return sum(xs[-n:]) / n if len(xs) >= n else None


def ema_series(xs: list[float], n: int) -> list[float]:
    """From the n-th value on, started at the mean of the first n."""
    if len(xs) < n:
        return []
    out = [sum(xs[:n]) / n]
    k = 2 / (n + 1)
    for x in xs[n:]:
        out.append(out[-1] + k * (x - out[-1]))
    return out


def rsi(xs: list[float], n: int = 14) -> float | None:
    """Wilder's."""
    if len(xs) <= n:
        return None
    moves = [b - a for a, b in zip(xs, xs[1:])]
    up = sum(max(m, 0) for m in moves[:n]) / n
    down = sum(max(-m, 0) for m in moves[:n]) / n
    for m in moves[n:]:
        up = (up * (n - 1) + max(m, 0)) / n
        down = (down * (n - 1) + max(-m, 0)) / n
    if up == down == 0:
        return 50.0
    return 100.0 if down == 0 else 100 - 100 / (1 + up / down)


def macd(xs: list[float]) -> tuple[float, float] | None:
    """(line, signal), 12/26/9."""
    fast, slow = ema_series(xs, 12), ema_series(xs, 26)
    line = [f - s for f, s in zip(fast[-len(slow):], slow)] if slow else []
    signal = ema_series(line, 9)
    return (line[-1], signal[-1]) if signal else None


def bollinger(xs: list[float], n: int = 20, width: float = 2) -> tuple[float, float] | None:
    mid = sma(xs, n)
    if mid is None:
        return None
    sd = math.sqrt(sum((x - mid) ** 2 for x in xs[-n:]) / n)
    return mid - width * sd, mid + width * sd


def technical_lines(closes: list[float], price: float) -> list[str]:
    """What the closes are enough for; nothing is said of the rest."""
    xs = closes[:-1] + [price] if closes else [price]
    out = []
    for hours, name in ((24, "24 hours"), (168, "7 days")):
        if len(xs) > hours:
            out.append(f"Change over {name}: {(price / xs[-1 - hours] - 1) * 100:+.2f}%")
    for n in (20, 50):
        if (v := sma(xs, n)) is not None:
            out.append(f"SMA({n}h): {price_text(v)}")
    if (v := rsi(xs)) is not None:
        out.append(f"RSI(14h): {v:.1f}")
    if (m := macd(xs)) is not None:
        out.append(f"MACD(12,26,9 hours): line {m[0]:.6g}, signal {m[1]:.6g}")
    if (b := bollinger(xs)) is not None:
        out.append(f"Bollinger(20h, 2 sd): {price_text(b[0])} to {price_text(b[1])}")
    return out
