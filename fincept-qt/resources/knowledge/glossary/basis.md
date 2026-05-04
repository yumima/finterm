# Basis

```
Basis = Spot Price − Futures Price
```

The price difference between an asset for **immediate delivery** (spot) and for **future delivery** (a specific futures contract).

A *positive* basis (spot > futures) corresponds to [[backwardation]]; *negative* basis to [[contango]].

## Worked example — corn farmer hedge

```
March (planting):
  Local cash corn price:         $4.50/bushel
  Dec corn futures (CBOT):       $4.80/bushel
  Basis:                         −$0.30 (typical for region)

  Farmer sells 50 Dec contracts (250,000 bushels) at $4.80 to lock in price.

October (harvest):
  Local cash corn price:         $4.20/bushel  (price dropped, but…)
  Dec corn futures:              $4.30/bushel
  Basis:                         −$0.10 (basis tightened — local supply shortage)

  Effective sale price:
    Cash: $4.20  + Futures gain: ($4.80 − $4.30) = $0.50
    = $4.70/bushel
  vs unhedged sale at $4.20 → hedge delivered +$0.50/bushel
```

The basis tightened from −$0.30 to −$0.10 — that's a **+$0.20/bushel basis gain** on top of the standard hedge benefit. If basis had widened instead, the hedge would have been less effective.

## Why hedgers care

A farmer who'll harvest corn in three months sells futures today to lock in a price. But the futures contract delivers at a specific hub on a specific date — the farmer's actual sale will be at a local elevator on harvest day. The difference between the local cash price and the futures settlement is **basis risk**.

The hedge eliminates *price-level* risk but leaves *basis* risk. A widening basis can erase the hedge benefit — and is often the larger risk for cash-and-carry traders than outright price moves.

## Basis convergence

As a futures contract approaches expiration, basis must converge to zero (otherwise arbitrageurs would profit risklessly). This convergence is the reason the **front-month** futures price tracks spot tightly while back-month prices reflect the term structure.

## Variants worth knowing

- **Calendar basis** — between two futures contracts (e.g., Dec24 vs Mar25)
- **Inter-market basis** — same commodity, different exchanges (Brent vs WTI)
- **Quality basis** — different specifications (high-sulfur vs low-sulfur crude)
- **Location basis** — same commodity, different delivery point

## Trading basis itself

Sophisticated traders build positions purely in the basis:
- **Cash-and-carry**: buy spot, sell futures, capture the carry
- **Reverse cash-and-carry**: short spot, buy futures
- **Inter-month spreads**: trade the curve shape rather than the level

These are core operations in commodity desks, gold/silver arbitrage, and ETF arbitrage.

## Decision rules

- **Hedger** → track historical basis range; if current basis is at extreme, consider waiting/lifting hedge
- **Basis trader** → look for forced selling/buying that pushes basis off fair value
- **Index investor** → roll period (when basis is most volatile) creates entry/exit risk
- **ETF arbitrage** → tight basis windows mean tighter ETF tracking; wide basis = arbitrage opportunity

## In finterm

The Futures spread view displays basis directly. For commodity portfolios, basis history is often more revealing than spot price history.
