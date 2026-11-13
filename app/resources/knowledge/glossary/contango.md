# Contango

A futures market is in **contango** when contracts further from expiration are **priced higher** than near-term contracts (and the spot price).

```
F(Dec 2026) > F(Sep 2026) > F(Jun 2026) > Spot
```

The futures curve slopes upward.

## Worked example — WTI crude futures

```
Spot WTI:           $78.00 / barrel
1-month future:     $78.40
3-month future:     $79.20
6-month future:     $80.50
12-month future:    $82.00
```

Long-only ETF holding 1-month futures rolls every month. To stay invested, it sells the expiring contract at $78.40 and buys the next-month contract at $79.20 — a **−1.0%** roll cost per month, or roughly **−12%/year** in roll drag, *before* any spot price change.

## Why it happens

For storable commodities (oil, grain, metals), the carrying cost of holding the physical asset (storage, insurance, financing) plus the convenience of *not* holding it now adds up. Futures must price in that cost — otherwise arbitrageurs would buy spot and sell futures forever.

The "fair" relationship: `F(t) = S × e^((r + storage − convenience) × t)`

## Why it matters to investors

If you hold a long futures position (or a long-only commodity ETF that rolls contracts monthly), contango is **bleeding you**. Each month you sell the expiring cheap contract and buy a more-expensive one — the "negative roll yield."

USO (the WTI oil ETF) famously underperformed crude oil itself by enormous margins during the 2014–2016 contango regime, even when crude rose, because of this rolling cost.

## Historical contango episodes

| Period | Curve regime | Long-only ETF impact |
|---|---|---|
| 2009 (post-GFC oil glut) | Super-contango (+5%/mo) | USO lost ~50% while crude was flat |
| 2014–16 (shale glut) | Persistent contango (~2%/mo) | USO underperformed WTI by ~30% |
| Apr 2020 (COVID + tank shortage) | Front-month went **negative** ($-37) | Catastrophic for many oil ETFs |
| Natural gas (most years) | Steep seasonal contango | Long-only NG ETFs decay continually |

## Variants worth knowing

- **Mild contango** — typical, <1%/mo
- **Steep contango** — supply-glut signal, 2–5%/mo
- **Super-contango** — physical storage saturated; rare and brief
- **Roll yield strategies** — Bloomberg Roll Select, S&P GSCI Dynamic Roll pick the most-attractive contract instead of always the front-month

## Decision rules

- **Long-only commodity ETFs in steep contango** → expect significant performance drag; consider direct miner equity exposure instead
- **Steep contango + falling spot** → bearish thesis confirmed; supply pressure
- **Curve flips from contango to backwardation** → tightness signal; potential bullish catalyst
- **Backwardation in your asset** → favorable roll for long futures positions

## In finterm

The Futures curve view plots the term structure. Look at the slope — and ask whether you're being paid (backwardated) or charged (contango'd) by the roll.
