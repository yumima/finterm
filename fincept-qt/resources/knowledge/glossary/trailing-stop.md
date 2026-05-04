# Trailing Stop

A stop-loss order that **adjusts upward (for longs) as the price rises** — locking in gains while letting winners run. Defined as either a fixed dollar amount or a percentage below the most-recent high.

```
Buy AAPL @ $175
Trailing stop: 5% below high

Stock climbs to $200:
  New trailing stop: 5% below $200 = $190

Stock pulls back to $192:
  Stop unchanged ($190 — high is still $200)

Stock falls to $190:
  Stop triggers → market sell at ~$189.95
  Realized: $189.95 (vs cost $175) = +$14.95 = +8.5%
```

The trail moved up with price, locking in much of the rally.

## Why it exists

Static stop-losses force a binary decision: cut at thesis violation, or never. Trailing stops let you **keep riding a winner without giving back too much**. Trend followers depend on them.

## Trail methodologies

| Method | How it's set | Best for |
|---|---|---|
| **Fixed %** | 5–15% below high | Simple; doesn't adapt to volatility |
| **Fixed $** | $5 below high | Same; awkward if price climbs much |
| **ATR multiple** | 2–4× ATR below high | **Adapts to vol; preferred** |
| **Chandelier exit** | High − 3 × ATR(22) | Popular for trend-following |
| **Volatility stop** | High − k × stdev(returns) | Statistical |

## Worked example — ATR vs fixed %

```
Stock vol: 30% annual ≈ 1.9% daily ATR
Position size: 100 shares @ $100

Fixed 8% trail:
  Stop starts at $92
  Stock moves $98 ↔ $103 (normal noise) — no stop-out
  Stock climbs $105 → stop at $96.60
  Stock pulls back to $96 — STOPPED OUT on a 4-day pullback

ATR(2×) trail:
  Stop starts at $96.20 (100 - 2 × 1.9)
  Stock climbs $105 → stop at $101.20
  Stock pulls back to $99 (still well above ATR-based stop)
  Holds; resumes uptrend
```

The ATR trail respects the stock's natural noise; the fixed % trail does not.

## When trailing stops shine

- Trending markets with defined momentum
- Trend-following strategies (50-day breakouts, etc.)
- Capturing big winners while having an exit plan
- Reducing emotional decisions about when to sell

## When they fail

- Whipsaws in choppy / range-bound markets
- News events causing one-off gaps that exceed normal noise
- Pre-earnings (consider exiting or hedging instead)
- Low-liquidity names where the trail can be triggered by a single bad print

## Pitfalls

- **Tight trails** in volatile markets → frequent stop-outs at noise
- **Wide trails** can give back 20–30% of gains before triggering
- **Hard down-gaps** still execute as market orders → same gap risk as static stops
- **GTC trailing stops** may need re-confirmation periodically — check broker
- **Resets after position adjustments** — adding to a position can re-anchor the high

## Variants

- **Trailing stop-loss (market)** — most common
- **Trailing stop-limit** — combines trail with limit; adds the same no-fill risk
- **Volatility stop / SAR (Parabolic SAR)** — algorithmic trailing stop systems
- **Time-based trail** — only adjusts on N-day intervals to reduce noise

## Decision rules

- **Trend-following swing trades** → ATR-based trail (2–3×)
- **Long-term holdings** → trailing stops not appropriate; use fundamental exit triggers
- **Highly volatile names** (>40% annual vol) → 3–5× ATR; tighter trails get whipsawed
- **News-event holds** → exit before event; trail can't protect from 20% gaps

## In finterm

Equity Trading supports trailing-stop orders with either % or $ trail. Recommend ATR-based triggers for active swing strategies.
