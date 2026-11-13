The **Sortino ratio** is a risk-adjusted return metric that measures portfolio return relative to *downside* volatility only — penalizing only the harmful (negative) volatility, unlike the [[sharpe-ratio|Sharpe ratio]] which penalizes all volatility including upside.

The rationale: investors dislike losses, not gains. A strategy that has high upside volatility should not be penalized the same way as one with high downside volatility.

## Formula

```
Sortino Ratio = (Rp − Rf) / σ_downside

Rp        = Portfolio return
Rf        = Risk-free rate (target / minimum acceptable return)
σ_downside = Standard deviation of negative return deviations only

σ_downside = sqrt( (1/N) × Σ min(Rᵢ − Rf, 0)² )
```

## Worked example

```
Monthly returns: +3%, −1%, +5%, −4%, +2%, −2%, +7%, −1%, +3%, −3%
Rf (monthly): 0.35%

Downside deviations (only negative vs. Rf):
  −1% − 0.35% = −1.35%  → (−1.35%)² = 1.82%
  −4% − 0.35% = −4.35%  → (−4.35%)² = 18.92%
  −2% − 0.35% = −2.35%  → (−2.35%)² = 5.52%
  −1% − 0.35% = −1.35%  → (−1.35%)² = 1.82%
  −3% − 0.35% = −3.35%  → (−3.35%)² = 11.22%

Variance = (1.82+18.92+5.52+1.82+11.22) / 10 = 3.93%
σ_downside (monthly) = √3.93% = 1.98%  → annualized ≈ 6.9%

Average monthly return = 0.9%, Rf = 0.35%
Annualized excess return = (0.9% − 0.35%) × 12 = 6.6%

Sortino = 6.6% / 6.9% = 0.96
```

## Interpretation thresholds

| Sortino Ratio | Assessment |
|---|---|
| < 0 | Negative: strategy underperforms risk-free |
| 0–1 | Poor to acceptable |
| 1–2 | Good — earns meaningful return per unit of downside risk |
| 2–3 | Very good |
| > 3 | Excellent |

## Sortino vs. Sharpe comparison

| Feature | Sharpe | Sortino |
|---|---|---|
| Risk denominator | Total volatility (up + down) | Downside volatility only |
| Penalizes upside volatility | Yes | No |
| Better for | Symmetric strategies | Trend-following, asymmetric return profiles |
| Favors | Low-vol consistent returns | Positively-skewed return distributions |

Strategies with positive skew (e.g., trend following, risk-off hedges, long volatility) often look better on Sortino than Sharpe.

## When Sortino is preferred

- **Trend-following strategies**: large upside moves don't get penalized.
- **Asymmetric payoff strategies** (options strategies with positive skew).
- **Tail-protected portfolios**: a portfolio that gives up some upside for downside protection looks worse on Sharpe than on Sortino.

## Pitfalls

- Sortino ratio requires sufficient data to estimate downside deviation reliably; with few observations, downside vol estimate is noisy.
- A low Sortino ratio doesn't mean absolute safety — it measures *relative* downside vs. return, not absolute loss magnitude.
- As with Sharpe, past Sortino ratios don't guarantee future performance.

See also: [[sharpe-ratio|Sharpe Ratio]], [[drawdown|Drawdown]], [[information-ratio|Information Ratio]], [[tail-risk|Tail Risk]], [[volatility|Volatility]].
