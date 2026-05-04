# Convexity

The **second-order interest-rate sensitivity** of a bond — captures the curvature in the price/yield relationship that [[duration]] (linear) misses.

```
%ΔP ≈ −Duration × Δy + ½ × Convexity × (Δy)²
```

For small rate moves, duration alone is fine. For large moves, convexity matters.

## Why it exists

The bond price/yield relationship is **convex**: a 100bp rate drop helps *more* than a 100bp rate rise hurts (for non-callable bonds). The "extra" gain on the down-move and "less than expected" loss on the up-move come from convexity.

## Worked example

```
30-year Treasury, 4% coupon, currently at par
Duration: 17
Convexity: 280

Yields rise 100bp:
  Linear (duration only):  -17%
  With convexity:          -17% + ½ × 280 × (0.01)² = -17% + 1.4% = -15.6%

Yields fall 100bp:
  Linear (duration only):  +17%
  With convexity:          +17% + ½ × 280 × (0.01)² = +17% + 1.4% = +18.4%
```

Convexity adds ~1.4% in your favor on either side. For 200 bp moves, it's ~5.6% — material.

## Convexity profile

| Bond | Convexity |
|---|---|
| Short-duration | Low |
| Long-duration, low coupon | **High** (most convex) |
| Zero-coupon (long) | Maximum convexity |
| Callable bond at low yields | **Negative** (capped upside) |
| Mortgage-backed (MBS) | **Negative** (prepayment optionality) |
| Vanilla long Treasury | Strongly positive |

## Negative convexity — the danger zone

When yields fall, callable bonds **don't** appreciate as much as a non-callable bond would, because the issuer can call them away at a fixed price. Same for mortgages — homeowners refinance when rates fall, prepaying the bond.

For a callable bond near its call price:
- Yields fall → price climbs to call price and stops
- Yields rise → price falls like a normal bond

The asymmetry is **always against you as a long holder**.

## Convexity arbitrage

Hedge funds historically traded convexity:
- Long convex (Treasuries) + short negatively-convex (MBS), duration-matched
- Profit from convexity differential — until LTCM-style liquidity events break the trade

## Pitfalls

- **Mortgage prepayment** during rate drops — your bond shortens at exactly the wrong time
- **Callable bonds** at low yields — limited upside, full downside
- **Convexity hedging is itself convex** — small positions have small effect, large positions can blow up
- Forgetting that convexity benefits the long bond holder — short bond positions have negative convexity

## Decision rules

- **Big rate moves expected** → factor convexity in alongside duration
- **Callable bonds near call price** → minimal upside; consider non-callables
- **MBS allocation** → understand prepayment dynamics; convexity is negative
- **Risk parity / leveraged bond strategies** → convexity is a feature, not just an asterisk

## In finterm

For most retail bond holdings, duration is sufficient. Convexity becomes important when (a) holding leveraged bond positions, (b) holding callable / MBS / mortgage exposure, or (c) sizing for tail rate moves.
