# Limit Order

An order to buy at or below — or sell at or above — a **specified price**. Price is guaranteed; **fill is not**.

## How it works

You submit a limit order. The exchange places it in the order book at your price. It executes when:
- Someone hits your bid (you buy)
- Someone lifts your ask (you sell)

If price never reaches your limit, you don't fill.

## Worked example

```
AAPL @ $175.00
  Bid: $174.99 × 1,000
  Ask: $175.01 × 1,200

Buy limit at $174.50:
  Order sits in the book at $174.50
  Fills only if price drops to $174.50 or below

Buy limit at $175.05:
  Crosses the spread → fills immediately at $175.01 (the ask)
```

## Aggressive vs passive limits

| Style | Description | Outcome |
|---|---|---|
| **Marketable limit** | At or above ask (buy) / at or below bid (sell) | Fills immediately at displayed price |
| **At-mid limit** | Between bid and ask | May or may not fill; common for retail |
| **Passive limit** | Below bid (buy) / above ask (sell) | Adds liquidity to the book; waits for price |

Passive limits earn rebates on some exchanges (you're providing liquidity). Marketable limits pay the spread.

## Time in force

- **Day** — expires at end of regular session (default for many platforms)
- **GTC (Good 'Til Canceled)** — stays open until filled or canceled (typically 60–90 days max on most platforms)
- **IOC (Immediate or Cancel)** — fill what's possible right now, cancel rest
- **FOK (Fill or Kill)** — fill entire order immediately or cancel
- **GTD (Good 'Til Date)** — expires on a specific date

## When to use

- ✓ **Default for retail trades** — control execution price
- ✓ Small-cap, micro-cap, anything with wide spread
- ✓ Pre-market and after-hours
- ✓ Around news (earnings, Fed)
- ✓ Options trades (basically always)

## Pitfalls

- **Limit too far from market** → never fills; you miss the move
- **GTC limits expire** silently on most platforms; check expiration policy
- **Partial fills** on low-volume names leave odd-lot positions
- **Hidden orders** (iceberg) don't show full size — your limit may rest behind invisible inventory
- **Limit price lock-in** can be costly if your thesis was right but you wanted a "better fill"

## Common mistakes

- Setting limit at the **last trade price** — that may not be where the next trade happens
- Putting a limit at a round number ($100.00) — clusters and gets executed late, after offsets fade
- Forgetting to update the limit when price moves significantly
- Trying to "save 1 cent" on a thesis-driven trade — pennies can cost you the whole opportunity

## Decision rules

- **Want immediate fill at displayed quote** → marketable limit (just inside the spread)
- **Want better-than-displayed price** → passive limit; expect partial fills
- **Iceberg-aware** for institutional levels → use hidden order types if available
- **Always check spread before submitting** — limit at mid in a 5% spread market makes no sense

## Variants worth knowing

- **Stop-limit order** — combines stop trigger with limit price; see [[stop-limit-order]]
- **Trailing limit** — limit that trails price; uncommon
- **Pegged limit** — anchored to NBBO bid/ask/mid; advanced

## In finterm

Equity Trading and Crypto Trading order tickets default to limit orders for safety. Always confirm your limit price before submitting.
