**R-squared (R²)** measures the proportion of a portfolio's return variance that is explained by movements in a benchmark — it quantifies how much of the portfolio's performance is driven by market exposure versus idiosyncratic factors.

```
R² = [Cov(Rp, Rb)]² / [Var(Rp) × Var(Rb)]
   = ρ²  (square of the Pearson correlation coefficient)

Where:
Rp  = portfolio returns
Rb  = benchmark returns
ρ   = correlation between portfolio and benchmark

Range: 0 (no relationship) to 1 (perfect linear relationship)
Often expressed as a percentage: 0 % to 100 %
```

## Worked example — active equity fund vs S&P 500

```
Monthly returns (12 months):
SPY:   +2, −1, +3, +1, −2, +4, +1, +2, −1, +3, +2, +1
Fund:  +2.5, −0.5, +3.2, +0.8, −1.8, +4.5, +0.6, +2.1, −0.8, +3.1, +2.4, +1.2

Covariance(Fund, SPY)   = 3.62  (computed from the return pairs)
Variance(Fund)          = 3.14
Variance(SPY)           = 2.74

ρ  = 3.62 / sqrt(3.14 × 2.74) = 3.62 / 2.93 = 1.235... (error check needed)

Simpler: run a regression of Fund on SPY; R² = 0.91

Interpretation: 91 % of the fund's return variance is explained by the S&P 500.
```

## What the result means

R² measures how much of a fund's behavior is attributable to market movements:

| R²           | Interpretation |
|---|---|
| 85 – 100 %   | Fund moves nearly in lockstep with benchmark; likely a closet indexer |
| 70 – 85 %    | High market correlation; limited active positioning |
| 50 – 70 %    | Moderate; meaningful active risk |
| < 50 %       | Low; fund is substantially independent of benchmark (hedge fund, macro) |

For risk attribution: R² = 1 means beta alone explains performance (active risk ≈ 0). R² close to 0 means the benchmark is not a useful reference for the fund.

## Variants

- **Adjusted R²** — penalizes for additional variables in a multi-factor regression; R² + k regressors = use adjusted R².
- **Rolling R²** — compute over a moving 36-month window to detect regime changes in benchmark sensitivity.
- **Factor R²** — run the regression against a multi-factor model (Fama-French) to see how much is explained by size, value, and market factors jointly.

## Common mistakes

- High R² does not imply good performance — a closet indexer that underperforms by 1 % annually has R² ≈ 0.99; R² measures correlation, not quality.
- Using R² to infer causation — two assets can be highly correlated without a causal relationship.
- Computing R² over too short a period — monthly data over 12 months gives 12 observations; 36–60 months is the standard for stable estimates.
- Ignoring that R² is always non-negative — unlike alpha or beta, R² cannot tell you the direction of the relationship; check the regression slope (beta) separately.

See also: [[beta-formula|Beta]], [[information-ratio|Information Ratio]], [[sharpe-ratio|Sharpe]], [[treynor-ratio|Treynor]].
