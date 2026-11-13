# Backwardation

The opposite of [[contango]] — the futures curve slopes **downward**, with near-term contracts priced *higher* than far-dated ones (and often higher than spot).

```
Spot > F(near) > F(far)
```

## Worked example — backwardated WTI crude

```
Spot WTI:           $90.00 / barrel
1-month future:     $89.50
3-month future:     $87.80
6-month future:     $85.00
12-month future:    $82.00
```

A long-only ETF rolling monthly sells the expiring contract at $89.50 and buys the next-month at lower → **positive +0.5%/month roll yield**. Even with flat spot, the position gains ~6% annually from roll alone.

## What it usually signals

- **Tight current supply** — buyers will pay a premium for delivery now rather than later
- **Inverted convenience yield** — owning the physical asset right now is worth more than the cost of carry
- Sometimes preceding a **supply shock** that hasn't fully resolved

Common in oil during geopolitical crises, in industrial metals during mine disruptions, and in agricultural products at harvest-shortage news.

## Historical backwardation episodes

| Period | Trigger | Curve depth |
|---|---|---|
| 2007–08 (oil) | Supercycle demand + tight supply | Heavy backwardation; +20%/yr roll |
| 2022 (oil) | Russia / Ukraine; OPEC+ discipline | Steep backwardation through year |
| 2008 (industrial metals) | Pre-GFC China demand spike | Persistent backwardation in copper |
| 2024 (cocoa) | West African crop failure | Extreme backwardation, multi-year highs |

## Why it's good for futures longs

If you hold a long futures position and roll forward, you sell expensive near-term contracts and buy cheaper longer-dated ones — **positive roll yield**. Over time, even with flat spot, you accrue gains.

This is why some commodity index strategies (Bloomberg Roll Select, S&P GSCI Dynamic Roll) attempt to systematically pick the most backwardated points on the curve.

## Variants worth knowing

- **Mild backwardation** — common, indicates modest tightness
- **Steep backwardation** — strong supply signal; >5%/yr roll
- **Front-end vs back-end** — sometimes only the prompt month is backwardated; rest of curve in contango
- **Calendar spread trade** — long backwardated front month vs short far-dated; pure roll-yield play

## Decision rules

- **New backwardation in your asset** → re-examine bull thesis; supply may be tightening
- **Persistent backwardation + falling inventories** → strong bullish signal
- **Backwardation with rising inventories** → temporary; will likely flip
- **Long-only commodity ETF in backwardation** → favorable; you're being paid to hold
- **Curve flips back to contango** → tightness easing; bearish near-term catalyst

## In finterm

The Futures curve view shows the term structure shape. Watch for inversions — they're rarely random. The Economics screen often correlates curve regime changes with macro indicators.
