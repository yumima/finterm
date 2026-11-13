# Market Order

An order to **buy or sell immediately at the best available price**. Speed of execution is guaranteed; price is not.

## What actually happens

You submit a market order. The broker routes it to an exchange, dark pool, or wholesaler. It fills against:
- The current bid (if you're selling)
- The current ask (if you're buying)
- Or — for large orders — sweeps multiple price levels until fully filled

## Worked example — when market orders work fine

```
AAPL @ $175.00
  Bid: $174.99 × 1,000 shares
  Ask: $175.01 × 1,200 shares
  Spread: $0.02 (~0.01%)

Market buy 500 shares:
  Fill: $175.01  (entire order at the displayed ask)
  Slippage: ~zero
```

For mega-cap stocks during regular hours, market orders are clean. **But:**

## When market orders go wrong

```
Small-cap XYZ @ $20.00 (last)
  Bid: $19.50 × 100
  Ask: $20.50 × 200
  Spread: $1.00 (5%)

Market buy 1,000 shares — sweeps the book:
  200 @ $20.50
  300 @ $21.00 (next level)
  300 @ $21.75
  200 @ $22.50
Avg fill: $21.36 — that's 6.8% above last trade
```

You paid 6.8% in slippage. A "$20" stock cost you $21.36/share.

## When to use market orders

- ✓ Mega-cap stocks during regular hours
- ✓ Highly liquid ETFs (SPY, QQQ, IWM)
- ✓ When immediate fill matters more than 1–2 ticks of price
- ✓ Closing a position in panic (sometimes the right call)

## When to avoid market orders

- ✗ Small-cap, micro-cap, illiquid names
- ✗ Pre-market and after-hours sessions
- ✗ Around news events (earnings, Fed)
- ✗ Stop orders that trigger market orders (use stop-limit if liquidity is questionable)
- ✗ Options on illiquid strikes (always use limit)
- ✗ Crypto except on the largest exchanges

## Variants

- **Market on Open (MOO)** — fills at the official opening print
- **Market on Close (MOC)** — fills at the closing auction price
- **Immediate or Cancel (IOC)** — fills what's available immediately, cancels the rest
- **Fill or Kill (FOK)** — fills entire order or cancels (no partial)

## Pitfalls

- **Stop-loss orders are market orders triggered at a level** — same liquidity risk applies
- Wholesalers / payment-for-order-flow brokers give you "price improvement" but also sometimes worse fills
- Market orders submitted near close or open during high-vol days can fill 5%+ from the displayed quote
- ETF market orders during the open or close can cross the actual indicative NAV by 1%+

## Decision rules

- **Default for retail** → limit orders, even on liquid names
- **Use market only when** spread < 0.1% of price AND you actually need immediate fill
- **For stop-losses on illiquid names** → consider stop-limit instead
- **For after-hours trades** → never market; always limit, narrow band

## In finterm

Equity Trading order ticket lets you choose order type. Default to limit; switch to market only when you've confirmed liquidity is adequate.
