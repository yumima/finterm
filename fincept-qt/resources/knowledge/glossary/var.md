# Value at Risk (VaR)

The **largest expected loss** over a specified horizon at a given confidence level.

> "1-day 95% VaR of $50,000" means: on 19 out of 20 days, the portfolio should not lose more than $50,000.

## Worked example — parametric VaR

```
Portfolio value:           $1,000,000
Portfolio annual vol:      18%
Daily vol = 18% / √252  ≈ 1.13%

1-day 95% VaR (z = 1.65):
  $1,000,000 × 1.13% × 1.65  ≈  $18,650

1-day 99% VaR (z = 2.33):
  $1,000,000 × 1.13% × 2.33  ≈  $26,330

10-day 99% VaR (square-root-of-time):
  $26,330 × √10  ≈  $83,250
```

So 19 out of 20 days, you shouldn't lose more than ~$18.7k. **One day in 20 you lose more — and VaR doesn't say how much more.**

## Three common methods

| Method | How | Strengths | Weaknesses |
|---|---|---|---|
| **Historical** | Use the actual distribution of past returns | No distributional assumptions | Bounded by the past — won't see novel crises |
| **Parametric** (variance-covariance) | Assume normal returns; multiply σ by a z-score | Fast, simple | Normal assumption is wrong in tails |
| **Monte Carlo** | Simulate thousands of return paths | Flexible, handles non-linear positions | Heavy compute; results depend on the model |

## Famous VaR failures

| Event | What VaR missed |
|---|---|
| LTCM 1998 | "Once-in-a-million" Russian default + flight-to-quality cascade |
| 2007–08 GFC | Correlations across "uncorrelated" books all went to 1 |
| 2020 COVID March | 6-sigma daily moves repeated for weeks |
| 2022 UK gilt blowup | Pension LDI feedback loop blew through model assumptions |

The pattern: VaR understates tail risk in regime change.

## Why it can mislead you

VaR tells you the *threshold* of bad days, not how bad they get past the threshold. If your 95% VaR is $50k, the 1-in-20 day might lose $51k — or $5M. VaR is silent on the difference.

This is exactly what burned many funds in 2008. Their VaR models said "you're fine" right up until they weren't, because the *tail* was far fatter than the model assumed.

## Variants worth knowing

- **CVaR (Conditional VaR / Expected Shortfall)** — average of losses *beyond* VaR; captures tail magnitude
- **Component VaR** — decomposes total VaR by position; identifies risk concentrations
- **Marginal VaR** — change in total VaR from a 1-unit change in a position
- **Incremental VaR** — change in total VaR from adding/removing an entire position
- **Stressed VaR** — VaR computed using historical stress periods (Basel III requirement for banks)

## Decision rules

- **Daily VaR exceeded > 5 days/year at 95%** → model underestimating; recalibrate
- **CVaR / VaR ratio > 1.5** → tail risk is concentrated; consider hedges
- **Concentrated position contributing > 30% of portfolio VaR** → reduce or hedge
- **VaR stable but actual P&L volatility rising** → stale data; refresh inputs

## What to use alongside

- **CVaR (Expected Shortfall)**: average loss in the tail beyond VaR. Captures the size of disasters, not just their frequency.
- **Stress tests**: explicit "what happens if rates jump 200bps overnight" scenarios.
- **[[drawdown|Maximum drawdown]]**: realized worst-case from history, no model assumptions.

## In finterm

Portfolio risk panel reports VaR using a configurable method. Always look at it together with CVaR and the historical max drawdown — never in isolation.
