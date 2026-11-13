# Time-Series Basics

Financial data is time-indexed. Almost every quant tool — backtests, factor regressions, pairs trades, mean-reversion strategies — rests on a handful of time-series concepts. Misunderstand any of them and the strategy looks great in-sample and collapses in production.

This entry covers the four that matter most: **returns vs prices**, **stationarity**, **autocorrelation**, and **cointegration**. Plus a working knowledge of what the diagnostic plots and tests actually mean.

## Returns, not prices

Prices are non-stationary — they wander upward or downward over decades, so their mean and variance are not constants. Almost every model in finance is built on **log returns** instead:

```
r_t = log(P_t / P_{t-1}) = log(P_t) − log(P_{t-1})
```

Properties of log returns that make them the right unit:

- **Approximately stationary.** Equity log returns over the past century have a relatively stable mean (~7%/yr) and volatility (~16%/yr) at the index level.
- **Additive across time.** Cumulative log return over N periods is the sum of single-period log returns. Simple arithmetic returns compound multiplicatively, which is awkward for regression.
- **Symmetric around zero.** A +10% log return followed by a −10% log return brings you back to the same price; in arithmetic returns, +10% then −10% leaves you at 0.99 of starting price.

When in doubt, model log returns. Use arithmetic returns only when you need actual P&L in currency units.

## Stationarity — the central assumption

A series is **stationary** if its statistical properties (mean, variance, autocorrelation structure) don't change over time. Stationarity is the unspoken assumption behind every backtest result — if it doesn't hold, the past tells you nothing reliable about the future.

Three forms:

- **Strict stationarity** — the joint distribution is invariant to time shifts. Rare in practice.
- **Weak (covariance) stationarity** — mean, variance, and autocovariance are time-invariant. The working definition for most quant work.
- **Trend stationary** — the series has a deterministic trend, but residuals around the trend are stationary. Detrending recovers stationarity.

**Test it.** The Augmented Dickey-Fuller (ADF) test is the standard diagnostic. Null hypothesis: the series has a unit root (= is non-stationary). If the ADF test statistic is more negative than the critical value, reject the null — series is stationary. Most price series fail ADF; most log return series pass.

**Why it matters in practice.** A regression on non-stationary series gives **spurious correlations** — Granger and Newbold (1974) showed that two unrelated random walks regressed on each other will have a "significant" t-statistic ~75% of the time. The fix: first-difference both series (i.e. work with returns) or test for cointegration (see below).

## Autocorrelation — the signal in the gaps

The autocorrelation function (ACF) measures correlation between a series and its lagged self:

```
ρ(k) = Cov(r_t, r_{t−k}) / Var(r_t)
```

For pure white noise, ρ(k) ≈ 0 at all lags k > 0. Real financial returns show characteristic patterns:

- **Equity returns at daily frequency.** ρ(1) close to zero (no day-over-day return predictability, broadly). This is the empirical foundation of the random-walk hypothesis.
- **Volatility (squared returns).** Strong positive autocorrelation persisting for weeks. The "volatility clustering" stylized fact — high-vol days cluster, low-vol days cluster. Basis for GARCH models.
- **Cross-sectional momentum.** Individual-stock 12-month returns are positively autocorrelated at the 1-month horizon (the [[factor-investing|momentum factor]]).
- **Mean reversion at extremes.** Returns following ±3σ moves tend to be opposite-signed at the next bar (the basis for short-term reversion strategies).

**Diagnostic plot: ACF vs PACF.** ACF shows total correlation at each lag including indirect effects; PACF (partial autocorrelation) shows the direct effect at each lag with intermediate lags partialled out. Together they identify AR vs MA model structure.

**Box-Pierce / Ljung-Box test.** Asks "are the first k autocorrelations jointly zero?" — a portmanteau test for serial correlation. If a residual series from a model still has significant Ljung-Box statistic, the model has missed structure.

## Cointegration — the foundation of pairs trading

Two non-stationary series are **cointegrated** if a linear combination of them is stationary. Visually: each series wanders, but their spread oscillates around a constant.

The classic example: Coca-Cola and Pepsi. Both prices trend upward over decades; neither is mean-reverting individually. But the spread (KO − β·PEP) for some β oscillates around a near-constant level. Trade the spread: long the cheap leg, short the rich leg, exit at convergence.

**Engle-Granger test** (two-step):
1. Regress one series on the other: `Y_t = α + β·X_t + ε_t`.
2. Run ADF test on the residuals ε_t. If stationary, the pair is cointegrated.

**Johansen test** (multivariate): tests cointegration among ≥ 2 series simultaneously. Identifies the cointegrating vectors (the β's). More general than Engle-Granger; preferred for baskets of 3+ assets.

**Practical caveats:**

- **Cointegration breaks.** A pair that was cointegrated 2010–2020 may not be cointegrated 2021 onward (one company changes business model, gets acquired, etc.). Re-test the cointegration relationship at every walk-forward window.
- **Spurious cointegration.** Test enough pairs and some will appear cointegrated by chance. Pre-screen by economic story — same industry, same supply chain, same business model.
- **Half-life of mean reversion.** Even genuinely cointegrated pairs have spreads with a typical mean-reversion half-life (the [[strategy-patterns|Ornstein-Uhlenbeck]] parameter). Half-life < 1 day = HFT regime, hard to capture. Half-life > 100 days = capital lock-up risk.

## Volatility models

Volatility clustering motivates models where the variance itself is time-varying:

- **GARCH(1,1)** (Bollerslev 1986). Variance at time t depends on yesterday's squared return and yesterday's variance: `σ²_t = ω + α·r²_{t−1} + β·σ²_{t−1}`. The workhorse for VaR estimation under [[var-cvar|VaR/CVaR modelling]].
- **EWMA** (RiskMetrics). Exponentially weighted moving average of squared returns. Simple, no parameters to fit, gives most of GARCH's value for risk applications.
- **Stochastic volatility** (Heston 1993). Variance follows its own stochastic process. Used for option pricing when GARCH's deterministic update isn't enough.

For quick backtesting, EWMA-30-day is usually fine. For production risk models, GARCH(1,1) is the standard. For options, Heston or local-vol.

## Common pitfalls

- **Treating price level as the signal.** Almost always wrong; use returns or detrended price.
- **Mistaking stationarity for predictability.** Stationarity says the *distribution* is stable; it does NOT say next return is predictable. White noise is stationary but pure unpredictable.
- **Using overlapping returns.** Computing 5-day returns daily creates artificial autocorrelation. For backtests with non-daily holding periods, use **non-overlapping** windows or block-bootstrap for inference.
- **Fitting models in-sample, evaluating in-sample.** Standard CV breaks for time-series — see [[ml-pipeline-pitfalls|ML pipeline pitfalls]] for the right cross-validation.

## See also

- [[ml-pipeline-pitfalls|ML pipeline pitfalls]] — time-series cross-validation
- [[strategy-patterns|Strategy patterns]] — pairs and mean-reversion strategies built on cointegration
- [[var-cvar|VaR / CVaR]] — volatility models feed into risk estimation
- [[quant-research-workflow|Quant research workflow]] — where time-series tests fit in the pipeline

## External references

- Hamilton (1994). *Time Series Analysis*. Princeton. The canonical textbook.
- Tsay (2010). *Analysis of Financial Time Series* (3rd ed). Wiley. Practitioner-oriented.
- Granger & Newbold (1974). "Spurious Regressions in Econometrics." *Journal of Econometrics*.
- Engle & Granger (1987). "Co-integration and Error Correction." *Econometrica*.
- Johansen (1991). "Estimation and Hypothesis Testing of Cointegration Vectors." *Econometrica*.
- Bollerslev (1986). "Generalized Autoregressive Conditional Heteroskedasticity." *Journal of Econometrics*.
- Cont (2001). "Empirical Properties of Asset Returns: Stylized Facts and Statistical Issues." *Quantitative Finance*.
