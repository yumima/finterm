# How to Allocate Across Asset Classes

The single most important investment decision is **how you split your capital across stocks, bonds, cash, and real assets**. This decision drives ~90% of long-term returns; security selection drives the rest.

## Frameworks

### Classic 60/40

```
60% stocks (broad index)
40% bonds (intermediate-term Treasuries / IG)
```

The default for decades. Diversifies between growth (stocks) and stability (bonds). Failed in 2022 when both fell together (stagflation regime).

### Age-based glide path

```
Stock allocation (%) ≈ 110 − age
40-year-old: 70% stocks
60-year-old: 50% stocks
80-year-old: 30% stocks
```

Conservative because of withdrawal-rate risk near/in retirement. Better as a starting point than a rule.

### Risk parity (Bridgewater All Weather)

Equal **risk contribution** per asset, not equal $ allocation. Bonds are levered to make their risk match equities':

```
Approx weights:
  30% stocks (high-vol)
  55% intermediate Treasuries (medium-vol, levered)
  15% commodities + gold (inflation hedge, levered)
```

Diversifies across **economic regimes** (growth/inflation matrix), not just asset classes.

### David Swensen's Endowment Model

```
30% US stocks
15% foreign developed equity
10% emerging market equity
20% real estate
15% TIPS / bonds
10% absolute return / hedge funds
```

Diversifies across geography and asset type.

### Boglehead 3-fund

```
60% Total US stock market (VTI)
20% Total international (VXUS)
20% Total bond market (BND)
```

Simple, low-cost, doesn't try to be clever. Good baseline for retail.

## Worked example — building a 70/30 allocation

```
Portfolio: $100,000
Target: 70% stocks, 30% bonds

Equity sleeve ($70,000):
  US large cap (VOO):       $35,000   50%
  US small cap (VBR):       $7,000    10%
  International (VXUS):     $14,000   20%
  Emerging markets (VWO):   $7,000    10%
  REIT (VNQ):               $7,000    10%

Bond sleeve ($30,000):
  Intermediate Treasury (IEF): $12,000   40%
  TIPS (SCHP):                 $9,000    30%
  IG corporate (LQD):          $6,000    20%
  Cash / T-bills (BIL):        $3,000    10%
```

This is more diversified than a single SPY+TLT split, while staying simple.

## Asset class characteristics

| Asset | Long-run return | Vol | Correlation to stocks |
|---|---|---|---|
| Cash / T-bills | 1–4% | ~0% | 0 |
| Intermediate Treasuries | 3–5% | 5–8% | −0.2 to +0.2 |
| Long Treasuries | 4–6% | 12–20% | varies (sometimes negative) |
| TIPS | 1–3% real | 5–10% | varies |
| US large-cap equity | 8–10% | 15–18% | 1.0 |
| International equity | 6–9% | 16–20% | 0.7–0.85 |
| EM equity | 7–10% | 22–28% | 0.6–0.8 |
| REIT | 7–9% | 18–25% | 0.5–0.8 |
| Gold | 4–6% | 14–18% | varies; sometimes negative |
| Commodities (broad) | 4–7% | 18–25% | 0.3–0.6 |
| Bitcoin | very high range | 60–80% | 0.2–0.6 |

## Rebalancing

Allocations drift as assets perform differently. **Rebalance** to bring back to target:

| Method | Rule |
|---|---|
| Calendar | Rebalance quarterly or annually |
| Threshold | Rebalance when allocation drifts >5% from target |
| Hybrid | Annual + threshold check at any drift point |

Rebalancing forces "sell high, buy low" discipline. Don't over-do it (taxes, costs); don't skip entirely (drift accumulates).

## The big regimes

Different allocations work in different regimes:

| Regime | Best assets | Worst assets |
|---|---|---|
| **Growth + low inflation** (1990s, 2010s) | US equities, growth stocks | Commodities, gold |
| **Growth + high inflation** (1970s, 2022) | Commodities, gold, TIPS | Long bonds |
| **Low growth + low inflation** (2008–09) | Long Treasuries, gold | Equities, commodities |
| **Low growth + high inflation** (stagflation) | Gold, TIPS, short cash | Both stocks AND bonds |

Risk parity / All Weather aims to perform tolerably across all four. Concentrated 60/40 fails in the bottom-right (2022 was painful).

## Pitfalls

- **Recency bias**: building allocation based on what worked recently
- **Home bias**: 60–80% in your own country's equities; ignores 50%+ of global market cap
- **Rebalancing too often**: tax + cost drag
- **Not rebalancing**: portfolio drifts to whatever's been winning
- **Treating bonds as "safe"** when long-duration in a hike cycle is anything but
- **Ignoring liquidity needs** in allocation (illiquid alts in a near-term withdrawal portfolio)

## Decision rules

- **Allocation drives ~90% of long-run returns** — spend more time here than on stock picking
- **Diversify across regimes**, not just asset names
- **Rebalance threshold > 5%** drift; check at calendar intervals too
- **Cash buffer** = 6–12 months expenses outside investment portfolio
- **Withdrawal rate** drives sustainable allocation (4% rule = ~50–70% stocks)
- **Annual review**: are weights still right for your life situation? (Age, risk tolerance, time horizon)

## Famous allocation models

- **Yale endowment**: heavy alts; only works at scale + access
- **Bridgewater All Weather**: risk parity across regimes; high leverage on bonds
- **Permanent Portfolio (Browne)**: 25% stocks / 25% LT Treasuries / 25% gold / 25% cash
- **Boglehead 3-fund**: simple, low-cost, robust
- **Trinity 4% rule**: based on 60/40, suggests 4% safe withdrawal rate

## In finterm

Portfolio allocation view shows your current % weights vs target. The risk panel shows aggregate beta, duration, and concentration. Use them to identify drift and rebalancing opportunities.
