# Rho (ρ)

The Greek that measures **sensitivity to interest rates** — how much an option's price changes per 1 percentage point change in the risk-free rate.

```
Rho = ∂(option price) / ∂(risk-free rate)
```

For most short-dated options, rho is **negligible**. It only becomes meaningful for **long-dated options** (LEAPS, multi-year warrants, structured products).

## Direction

- **Calls have +rho**: rising rates increase call value (the cost-of-carry argument — calls are like a leveraged stock position; higher rates make the leverage cheaper)
- **Puts have −rho**: rising rates decrease put value

## Worked example — LEAPS rho

```
AAPL @ $175
2-year $200 call:
  Premium:  $25.00
  Rho:      $0.40 per 1% rate change

Fed hikes rates by 1%:
  Premium gain ≈ +$0.40 → new premium ~$25.40

Compare to 30-day $200 call:
  Premium:  $1.20
  Rho:      $0.005 (~negligible)
  Same rate hike: premium change ~$0
```

## When rho matters

| Situation | Rho impact |
|---|---|
| 7-DTE options | Ignore |
| 30-DTE options | Tiny |
| 6-month options | Small but measurable |
| **LEAPS (1y+)** | **Material** — can be $1+ on big positions |
| 5-year structured product | Can dominate other Greeks |

## In context: rate cycle effects

In a steep Fed hiking cycle (2022–23), LEAPS calls outperformed expectation partly because rho added meaningful value. Conversely, LEAPS puts were doubly hit (delta + rho).

## Pitfalls

- **Ignoring rho on LEAPS positions** — can be larger than vega for very long-dated options
- **Synthetic positions** (long stock + long put) — rho on the put + carrying cost on stock interact
- Forgetting rho when comparing call vs synthetic structures

## Decision rules

- **Trade < 90 DTE** → rho can be ignored for nearly all decisions
- **LEAPS strategies** → include rho in P&L attribution
- **Structured products / OTC** → request rho exposure from broker; it can be the dominant Greek

## In finterm

Surface Analytics displays rho across long-dated strikes. For most retail option trades, rho remains a footnote — but recognizing when it matters keeps you from being surprised.
