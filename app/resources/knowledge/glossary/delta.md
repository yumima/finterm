# Delta (Δ)

The **first-order Greek** — measures how much an option's price changes for a $1 move in the underlying.

```
Delta = ∂(option price) / ∂(underlying price)
```

Also approximates the **probability the option finishes in-the-money** at expiration.

## Reading delta

| Position | Delta range | Behavior |
|---|---|---|
| Long call | 0 to +1 | Gains $1 per $1 stock rise at delta=1 |
| Short call | 0 to −1 | Loses $1 per $1 stock rise at delta=1 |
| Long put | 0 to −1 | Gains as stock falls |
| Short put | 0 to +1 | Loses as stock falls |
| Long stock | +1 (per share) | One-for-one |

## Worked example — long call

```
AAPL @ $175
Buy 30-day $180 call:
  Premium: $2.50
  Delta:   0.40

Stock rises to $176 (+$1):
  New premium ≈ $2.50 + 0.40 = $2.90 (+16%)

Stock rises to $180 (+$5):
  Approximate new premium: $2.50 + (0.40 + halfway through gamma adjustment) × $5
  Actual: depends on gamma — likely ~$5–6 (delta drifts toward 0.55–0.60 as ATM)
```

## Delta as probability proxy

A 0.30 delta call has roughly 30% probability of finishing ITM. This is **approximate** — the math assumes lognormal returns, which understates fat-tail outcomes.

## Hedging applications

To **delta-hedge** a long option position: short shares equal to (option delta × contracts × 100).

```
Long 10 calls, delta 0.40 each
Total delta = 10 × 0.40 × 100 = 400 shares equivalent
Hedge: short 400 shares
Result: position is delta-neutral; profit comes from gamma + vega
```

## Pitfalls

- **Delta drifts** with price moves (gamma); your hedge ratio shifts continuously.
- **Approximation breaks down** for short-dated, far-OTM options under stress.
- **Forgetting put delta is negative**: long puts hedge long stock at delta ≈ −1 each (deep ITM).
- **Vega and theta change too** when delta changes — never look at delta in isolation.

## In finterm

The Derivatives screen displays delta per strike. Surface Analytics shows delta across strikes/expirations as a heatmap.
