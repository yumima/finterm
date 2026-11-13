**The Sortino ratio** is a risk-adjusted return metric that improves on the [[sharpe-ratio|Sharpe ratio]] by penalizing only downside volatility — upward price swings are not treated as risk.

```
Sortino = (Rp − Rf) / σ_d

Rp    = portfolio return (annualized)
Rf    = target return (minimum acceptable return, often Rf or 0)
σ_d   = downside deviation — the standard deviation of returns
        that fall *below* the target return
```

Downside deviation is calculated using only the below-target return observations:

```
σ_d = sqrt( (1/n) × Σ min(Rt − Rf, 0)² )
```

## Worked example

```
Monthly returns (%)  : +3, +1, −4, +2, −1, +5, −3, +4, +2, −2, +1, +3
Target (Rf per month): 0.37 %  (4.5 % / 12)

Below-target observations: −4, −1, −3, −2
  Min(−4 − 0.37, 0)² = (−4.37)² = 19.10
  Min(−1 − 0.37, 0)² = (−1.37)² =  1.88
  Min(−3 − 0.37, 0)² = (−3.37)² = 11.36
  Min(−2 − 0.37, 0)² = (−2.37)² =  5.62
  Min(all above-target, 0)² = 0 for each

σ_d (monthly) = sqrt( (19.10+1.88+11.36+5.62) / 12 )
              = sqrt( 37.96 / 12 )
              = sqrt(3.163)
              = 1.78 %

Annualized σ_d = 1.78 × sqrt(12) = 6.16 %
Annual return  = (product of monthly) ≈ 12.4 %
Sortino = (12.4 − 4.5) / 6.16 ≈ 1.28
```

## What the result means

The Sortino ratio of **1.28** means the portfolio earns 1.28 units of excess return for every unit of downside risk. Values above 1.0 are generally considered acceptable; above 2.0 is strong.

The key advantage over Sharpe: two portfolios with equal Sharpe ratios may have very different Sortinos if one generates volatility mostly on the upside (desirable) while the other generates it equally in both directions.

## Variants

- **Upside potential ratio** — divides expected upside return by downside deviation; focuses on the asymmetry of the return distribution.
- **Omega ratio** — ratio of probability-weighted gains above a threshold to probability-weighted losses below it; captures the full distribution, not just second moments.

## Common mistakes

- Using total volatility instead of downside deviation — that is the Sharpe ratio, not Sortino.
- Setting the target return to zero when a meaningful hurdle (e.g., Rf or a benchmark) exists — understates downside deviation and inflates the ratio.
- Comparing Sortino ratios computed with different target returns — the target must be consistent across comparisons.
- Placing too much weight on Sortino for short return histories — downside deviation is estimated from few observations and is noisy.

See also: [[sharpe-ratio|Sharpe Ratio]], [[treynor-ratio|Treynor]], [[information-ratio|Information Ratio]], [[var-parametric|VaR]].
