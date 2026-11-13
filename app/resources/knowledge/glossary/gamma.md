# Gamma (Γ)

The **second-order Greek** — measures how much delta changes for a $1 move in the underlying.

```
Gamma = ∂(delta) / ∂(underlying price) = ∂²(option price) / ∂(price)²
```

If delta is the option's "speed," gamma is its **acceleration**.

## Reading gamma

- **Long options** are always **+gamma**: as the underlying moves, your delta (and gain) accelerates in your favor.
- **Short options** are always **−gamma**: losses accelerate against you on big moves.

## Worked example

```
AAPL @ $175
Long 30-day ATM call:
  Delta: 0.50
  Gamma: 0.04

Stock rises $1 → $176:
  New delta ≈ 0.50 + 0.04 = 0.54
  Premium gain ≈ ½ × (0.50 + 0.54) × $1 = $0.52

Stock rises $5 → $180 (continuous compounding):
  Delta drifts: 0.50 → 0.70 over the move
  Premium gain larger than 5 × 0.50 = $2.50
  Actual: ~$3.00 (gamma adds ~$0.50)
```

The asymmetry — bigger moves add disproportionately more — is gamma at work.

## Gamma profile

| Moneyness | Gamma |
|---|---|
| Deep ITM | Low (delta near 1, can't grow much) |
| ATM | **Maximum** (especially short-dated) |
| Deep OTM | Low (delta near 0, can't grow much) |

Gamma also **peaks near expiration** for ATM options.

## Why dealers fear short gamma

Market makers who sell options are short gamma. To stay delta-neutral, they must:
- Buy underlying as price rises (chasing strength)
- Sell underlying as price falls (chasing weakness)

Both actions amplify the move — feedback loop. **Gamma squeezes** (e.g., GameStop January 2021) happen when dealers are aggressively short gamma in calls and forced to buy increasingly large amounts of underlying as price climbs.

## Pitfalls

- Selling **short-dated ATM premium** without sizing for gamma — losses compound fast.
- Confusing gamma with vega — gamma reacts to *price* moves, vega to *vol* changes.
- Long gamma positions can still lose money: you also pay theta every day.
- **Delta hedging short gamma** in fast markets is impossible — slippage eats you.

## Decision rules

- **Selling weekly options** → keep position size such that 3σ daily move = <2% account loss.
- **Buying gamma into a known event** → ensure vega (IV expansion) doesn't dominate.
- **Long-dated ATM gamma is cheaper** than short-dated; consider for thesis with longer horizon.

## In finterm

Surface Analytics renders gamma across the option surface — useful for spotting dealer-positioning hot spots.
