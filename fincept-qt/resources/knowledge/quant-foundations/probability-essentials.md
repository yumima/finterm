# Probability for Traders

Every backtest, every Sharpe, every VaR number is a probability statement in disguise. If you don't have a working feel for distributions, conditional probability, and where the Gaussian assumption breaks, you will systematically underprice tail risk and overpay for "edges" that are noise.

This entry covers the primitives that recur in everything else: **distributions, conditional probability, Bayes, and the heavy-tail problem**. Read it once, then keep coming back when something in [[var-cvar|VaR/CVaR]] or [[regression-basics|regression]] feels off.

## Random variables and distributions

A random variable maps outcomes to numbers; a distribution is the probability of each outcome. The two summaries that matter most:

- **Mean / expected value.** `E[X] = Σ x · P(X=x)` (discrete) or `∫ x · f(x) dx` (continuous). For returns, the population mean is what compounds — but sample means are notoriously noisy. With 20 years of monthly data (240 obs) and σ ≈ 16%/yr, the standard error on the annual mean return is ~1.0% — so a "5% Sharpe-improving" alpha is statistically indistinguishable from zero.
- **Variance / standard deviation.** `Var(X) = E[(X−E[X])²]`. The square root is the working unit because it scales linearly with the data.

Two distributions that recur:

- **Normal (Gaussian).** Symmetric, characterized fully by (μ, σ). The default null hypothesis in most stats packages. It is the wrong default for asset returns (see below).
- **Student's t.** Heavier-tailed than normal. Degrees-of-freedom parameter ν controls tail thickness; ν → ∞ recovers the normal. Empirically, daily equity returns are well fit by t with ν ≈ 3–5.

## Conditional probability and Bayes

Conditional probability is the entire game in trading — you never care about `P(market up)`, you care about `P(market up | VIX > 30, yields rising, …)`.

```
P(A | B) = P(A ∩ B) / P(B)
```

**Bayes' rule** rearranges this into the form that actually appears in research:

```
P(H | D) = P(D | H) · P(H) / P(D)
```

Where H is the hypothesis ("strategy has positive edge"), D is the data ("observed Sharpe of 1.2 over 3 years"). The posterior is the prior times the likelihood, normalized.

Why this matters in practice:

- **Base rates dominate.** If 1 in 1000 backtests genuinely has edge and your test has 95% power and 5% false-positive rate, a "significant" result is still 95% likely to be a false positive. This is the false-discovery problem — see [[backtesting-discipline|backtesting discipline]] and Harvey, Liu, Zhu (2016) "...and the Cross-Section of Expected Returns."
- **Update beliefs continuously.** A strategy with a strong prior (factor with 30 years of out-of-sample evidence) updates slowly on a bad quarter. A strategy with weak prior (data-mined backtest) should be killed by a bad quarter.

## Higher moments — where returns betray Gaussianity

The normal distribution has zero skewness and excess kurtosis of zero. Real returns have neither.

- **Skewness.** `E[((X−μ)/σ)³]`. Asymmetry of the distribution. Equity index returns are **negatively skewed** (big down days more extreme than big up days). Selling options is positive-carry but negatively-skewed — the inverse trade.
- **Kurtosis.** `E[((X−μ)/σ)⁴]`. Tail thickness. Gaussian = 3 (or 0 in "excess" units). Daily S&P 500 returns: ~25 (excess ~22). Daily individual-stock returns: 10–40. The tails are not a rounding error — they are where the money is made and lost.

Cont (2001), "Empirical Properties of Asset Returns," catalogs eleven stylized facts. The three that matter for sizing:

1. Heavy tails (kurtosis well above 3).
2. Volatility clustering (squared returns are autocorrelated — see [[time-series-basics|time-series basics]]).
3. Leverage effect (negative returns predict higher subsequent volatility).

## Why returns aren't Gaussian — and what to do about it

The 1987 crash was a 22σ event under a Gaussian model fit to pre-crash data. So was Black Wednesday 1992. So was August 2007's quant quake. So was March 2020. A "5σ" Gaussian event should occur once every ~14,000 years; markets see one every few years.

**Taleb's black swan intuition** (Taleb 2007, *The Black Swan*): rare events dominate long-run P&L, and our models systematically underweight them because they are absent from the sample we calibrated on. The Renaissance Long-Term Capital blow-up (1998) and many subsequent fund failures came from selling tail insurance under Gaussian risk models.

**The serious treatment is extreme-value theory** (EVT). Embrechts, Klüppelberg, Mikosch (1997), *Modelling Extremal Events for Insurance and Finance*, is the standard reference. The Pickands-Balkema-de Haan theorem says that exceedances over a high threshold converge to a Generalized Pareto Distribution regardless of the parent distribution. Fit a GPD to the tail of your loss distribution and you get well-calibrated extreme quantiles — far better than extrapolating from a Gaussian fit.

For day-to-day work:

- **Use empirical / historical quantiles**, not parametric Gaussian quantiles, for any tail metric.
- **Bootstrap, don't assume.** When testing whether a Sharpe is significant, block-bootstrap the return series rather than relying on the asymptotic normal approximation.
- **Stress test independently.** Don't just run a 99% VaR; also ask "what is my P&L if the worst day in this asset's 30-year history repeats tomorrow?"

## Independence and the iid fallacy

Two events are independent if `P(A ∩ B) = P(A) · P(B)`. "iid" (independent and identically distributed) is the assumption under which the central limit theorem gives you Gaussian sample means.

Returns are **not iid** along either dimension:

- Not identically distributed — volatility regimes shift (GARCH).
- Not independent — squared returns are autocorrelated; correlations across assets spike in crashes (Longin & Solnik 2001, "Extreme Correlation of International Equity Markets").

The practical consequence: square-root-of-time scaling (`σ_yearly = σ_daily · √252`) is **wrong** at long horizons under volatility clustering and mean reversion. Aggregate variance grows slower than t under mean reversion, faster under momentum.

## Common pitfalls

- **Confusing P(A|B) with P(B|A).** A test with 95% sensitivity says `P(positive | sick)`, not `P(sick | positive)`. Same trap in strategy stats.
- **Assuming Gaussian tails for risk sizing.** Will reliably blow up once a decade.
- **Treating Sharpe as a sufficient statistic.** Sharpe is a moment-2 metric on a moment-4 problem. See [[risk-adjusted-returns|risk-adjusted returns]].
- **Ignoring sample uncertainty in the mean return.** Most "alpha" disappears inside a 95% CI.

## See also

- [[time-series-basics|Time-series basics]] — stationarity, volatility clustering
- [[regression-basics|Regression basics]] — where Gaussian errors get assumed
- [[var-cvar|VaR / CVaR]] — direct application of tail probability
- [[risk-adjusted-returns|Risk-adjusted returns]] — why Sharpe lies about non-normal returns

## External references

- Embrechts, Klüppelberg, Mikosch (1997). *Modelling Extremal Events for Insurance and Finance*. Springer. The canonical EVT reference.
- Taleb (2007). *The Black Swan*. Random House. The intuition, not the math.
- Cont (2001). "Empirical Properties of Asset Returns." *Quantitative Finance* 1(2).
- Harvey, Liu, Zhu (2016). "…and the Cross-Section of Expected Returns." *Review of Financial Studies* 29(1).
- Longin & Solnik (2001). "Extreme Correlation of International Equity Markets." *Journal of Finance* 56(2).
