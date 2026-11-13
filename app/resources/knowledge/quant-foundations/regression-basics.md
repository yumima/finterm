# Regression for Traders

Regression is the single most over-used and under-understood tool in quant. Every factor model, every beta estimate, every "alpha" calculation is a regression. The mechanics are simple; the assumptions are where the money is.

This entry covers **OLS mechanics, the assumption violations that matter in finance (heteroskedasticity, autocorrelation, multicollinearity), the explanatory-vs-predictive split, and why R² is the worst single metric to optimize**.

## OLS in one paragraph

Ordinary least squares finds the (β₀, β₁, …, β_k) that minimize the sum of squared residuals:

```
y_i = β₀ + β₁ · x_{1,i} + … + β_k · x_{k,i} + ε_i
```

Under the Gauss-Markov assumptions (linearity, exogeneity, homoskedasticity, no perfect multicollinearity, no autocorrelation), OLS is BLUE — the Best Linear Unbiased Estimator. In real data, at least three of those assumptions are usually violated.

## Beta estimation — the single most common regression in finance

CAPM beta is just:

```
r_stock − r_f = α + β · (r_market − r_f) + ε
```

Empirical points worth knowing:

- **Sample-size tradeoff.** Five years of monthly data (60 obs) is the textbook default. Shorter windows are noisier; longer windows mix regimes (e.g., a 20-year beta on Tesla is meaningless).
- **Frequency matters.** Daily betas pick up microstructure noise (bid-ask bounce, asynchronous trading); monthly betas smooth too much. Weekly is the common compromise for institutional risk.
- **Beta is unstable.** A bank's beta in 2006 was ~1.0; in 2009 it was ~2.5. Backward-looking betas are a guide, not a forecast. Vasicek (1973) shrinkage toward 1.0 is the standard practitioner adjustment.
- **Dimson (1979) beta.** For thinly-traded stocks, regress on contemporaneous AND lagged market returns and sum the betas — corrects for stale prices.

## The three assumption violations that matter

### 1. Heteroskedasticity

Residuals don't have constant variance. In finance, this is universal — variance clusters (high-vol regimes, low-vol regimes). OLS point estimates remain unbiased; **standard errors are wrong**, so t-stats and p-values are misleading.

Fix: **White (1980) heteroskedasticity-consistent (HC0) standard errors**, or the small-sample-corrected HC3. In every regression with cross-sectional residuals, default to HC standard errors.

### 2. Autocorrelation in residuals

Time-series residuals are usually serially correlated, especially with overlapping observations (e.g., 12-month returns sampled monthly).

Fix: **Newey-West (1987) HAC standard errors**. Chooses a lag truncation parameter (rule of thumb: `L = floor(4·(T/100)^(2/9))`) and produces standard errors robust to both heteroskedasticity and autocorrelation up to lag L. This is the default in every factor-portfolio paper from Fama-French onward.

### 3. Multicollinearity

When regressors are highly correlated, OLS coefficients become unstable — small changes in data produce wild swings in individual betas, even though joint predictions stay stable.

Diagnostics:

- **VIF** (Variance Inflation Factor). `VIF_j = 1 / (1 − R²_j)` where R²_j is from regressing x_j on the other regressors. VIF > 10 is the conventional alarm; VIF > 5 is worth a hard look.
- **Condition number** of `X'X`. Above 30, you have a problem.

Fix: drop one of the collinear variables, combine them (orthogonalize, use a PCA factor), or use ridge regression. In factor models this comes up constantly — value and quality have negative correlation, momentum and low-vol overlap in low-vol regimes, etc.

## Explanatory vs predictive regression

These are different problems and the conventions differ.

**Explanatory regression** (Fama-French style): "Does factor X explain the cross-section of returns?" — care about coefficient signs, significance, economic interpretation. R² and t-stats are central.

**Predictive regression** (Goyal-Welch style): "Can dividend yield forecast next year's market return?" — care about out-of-sample R², not in-sample R². The famous Goyal & Welch (2008) paper showed that nearly every published equity-premium predictor failed out of sample. In-sample R² of 0.04 is "significant"; out-of-sample R² of −0.01 means the historical mean beats it.

The shared pitfall: **regression in-sample is almost always optimistic**. Always evaluate predictive claims on a held-out window — see [[backtesting-discipline|backtesting discipline]] and [[ml-pipeline-pitfalls|ML pipeline pitfalls]].

## Why R² is a bad single metric

- **In-sample R² always rises with more regressors.** Add 50 random variables and R² will climb toward 1.0. Use adjusted R² (Wherry 1931), AIC, or BIC if you must rank in-sample.
- **Out-of-sample R² is the only honest version.** Compare model MSE against a "naive" benchmark (e.g., historical mean): `R²_OOS = 1 − MSE_model / MSE_naive`. Negative values are common and meaningful.
- **R² says nothing about economic significance.** A model with R²=0.005 (typical for return prediction) can still produce a tradable Sharpe > 1 if signs are right and turnover is low. Conversely, R²=0.4 in a model with multicollinearity-driven sign flips is useless.

Better diagnostics:

- Plot residuals vs fitted, vs each regressor, vs time. Patterns mean you missed structure.
- Q-Q plot of residuals — if it's a banana shape, you have non-normal errors (use bootstrap for inference).
- Influence statistics (Cook's distance, leverage). One outlier with leverage 0.5 is driving your "result."

## The Fama-MacBeth procedure

For factor models with both time-series and cross-sectional structure, Fama & MacBeth (1973) run cross-sectional regressions period-by-period, then average the time-series of slope estimates. Standard errors come from the time-series variance of the cross-sectional betas — cleanly handles cross-sectional dependence. This is the standard estimator in cross-sectional asset pricing.

## Common pitfalls

- **Using OLS standard errors with serially correlated residuals.** Half of published anomalies become insignificant under Newey-West with proper lag length (Hou, Xue, Zhang 2020, "Replicating Anomalies").
- **Regressing on returns that overlap.** Creates artificial autocorrelation; t-stats balloon. Use non-overlapping returns or HAC.
- **Survivorship-biased data.** Universe of "stocks that exist today" is not the universe of "stocks that existed in 1995." Beta on this universe is systematically off.
- **Spurious regression** on two non-stationary series. See [[time-series-basics|time-series basics]] for the fix.

## See also

- [[time-series-basics|Time-series basics]] — stationarity, the foundation
- [[probability-essentials|Probability essentials]] — where Gaussian-error assumption breaks
- [[factor-investing|Factor investing]] — the entire enterprise rests on regression
- [[risk-adjusted-returns|Risk-adjusted returns]] — Jensen's alpha is a regression intercept

## External references

- Greene (2018). *Econometric Analysis* (8th ed). Pearson. The canonical reference.
- White (1980). "A Heteroskedasticity-Consistent Covariance Matrix Estimator." *Econometrica* 48(4).
- Newey & West (1987). "A Simple, Positive Semi-Definite, Heteroskedasticity and Autocorrelation Consistent Covariance Matrix." *Econometrica* 55(3).
- Fama & MacBeth (1973). "Risk, Return, and Equilibrium: Empirical Tests." *JPE* 81(3).
- Goyal & Welch (2008). "A Comprehensive Look at the Empirical Performance of Equity Premium Prediction." *RFS* 21(4).
- Hou, Xue, Zhang (2020). "Replicating Anomalies." *RFS* 33(5).
