**Beta** measures a stock's sensitivity to market movements — how many percentage points the stock tends to move for every 1 % move in the benchmark index.

```
β = Cov(R_stock, R_market) / Var(R_market)
  = ρ × (σ_stock / σ_market)

Cov(R_stock, R_market)  = covariance of stock and market returns
Var(R_market)           = variance of market returns
ρ                       = correlation between stock and market
σ_stock                 = standard deviation of stock returns
σ_market                = standard deviation of market returns
```

## Worked example — 5-year monthly regression

```
Period: 60 monthly returns (Jan 2021 – Dec 2025)
Stock:  NVDA (hypothetical for illustration)
Market: SPY

Monthly data (summary stats):
Variance(SPY)           = 0.00185   (monthly)
Covariance(NVDA, SPY)   = 0.00296   (monthly)

β = 0.00296 / 0.00185 = 1.60

Interpretation: NVDA moves ~1.60 % for every 1 % move in SPY on average.
When SPY is up +10 % in a year, NVDA is expected to be up ~16 %.
When SPY is down −20 %, NVDA is expected to be down ~32 %.
```

## What the result means

- **β = 1.0** — moves in line with the market (by definition, the market index has β = 1).
- **β > 1.0** — amplifies market moves (high-growth tech, leveraged equities).
- **β < 1.0** — dampens market moves (utilities, consumer staples, low-volatility equity).
- **β < 0** — inversely correlated with the market (some gold miners, inverse ETFs).

Beta is the core input to [[capm|CAPM]] for the cost of equity, and to [[treynor-ratio|Treynor ratio]] for risk-adjusted performance.

## Variants

- **Raw beta** — direct output of the regression; can be volatile over short windows.
- **Adjusted beta** — Blume adjustment: (2/3) × raw β + (1/3) × 1; pushes beta toward 1.0 over time; used by Bloomberg and most vendors.
- **Unlevered (asset) beta** — strips out capital structure effects; compares operating risk across firms with different debt levels.
  ```
  β_unlevered = β_equity / (1 + (D/E) × (1 − Tc))
  ```
- **Sector beta** — average of betas within an industry; used when individual company history is short (IPOs, restructured firms).
- **Predicted beta** — factor-based estimate from fundamental variables (leverage, earnings volatility, size) rather than historical returns.

## Common mistakes

- Using daily returns for beta estimation — microstructure noise (bid-ask bounce) biases beta; monthly 60-period regressions are standard.
- Not adjusting for dividends — use total return series (price + dividends reinvested), not price-only series.
- Assuming beta is stable — beta changes with leverage, business mix, and market regime; recompute regularly.
- Applying a 5-year historical beta to a company that underwent major transformation (acquisition, spin-off, restructuring) — the historical data may no longer represent the current business.

See also: [[capm|CAPM]], [[wacc|WACC]], [[treynor-ratio|Treynor]], [[r-squared|R-Squared]].
