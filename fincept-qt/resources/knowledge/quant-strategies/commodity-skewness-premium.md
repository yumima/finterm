# Commodity Skewness Premium

This is a **negative-correlation strategy**: in commodity futures, contracts with **high historical positive skewness** tend to have **lower subsequent returns**, and contracts with **negative skewness** tend to have **higher subsequent returns**. The strategy buys the negatively-skewed commodities and shorts the positively-skewed ones, harvesting the gap.

It is one of the cleaner cross-sectional anomalies in commodities, documented systematically by **Fernandez-Perez, Frijns, Fuertes, Miffre (2018)** "The Skewness of Commodity Futures Returns." Kakushadze & Serur (2018) §9.5 give the construction in a single page.

## The signal

For each commodity `i`, using a time series of `T` historical returns `R_{is}`:

```
R̄_i  =  (1/T) · Σ_s R_{is}                          (Eq. 9.4)

σ_i² =  (1/(T−1)) · Σ_s (R_{is} − R̄_i)²              (Eq. 9.5)

S_i  =  (1/(σ_i³ · T)) · Σ_s (R_{is} − R̄_i)³        (Eq. 9.3)
```

`S_i` is **standardised skewness** — the third standardised moment, dimensionless, scaled by the cube of standard deviation.

- `S_i > 0`: distribution is right-skewed, a long right tail (e.g., occasional big rallies).
- `S_i < 0`: distribution is left-skewed, a long left tail (e.g., occasional big crashes).
- `S_i = 0`: symmetric distribution (Normal is `S = 0`).

The strategy:

```
1. For each commodity, compute trailing skewness over a window (typically 12 months).
2. Sort.
3. Long the BOTTOM quintile (most negatively-skewed).
4. Short the TOP quintile (most positively-skewed).
5. Equal-weight each leg. Hold one month. Re-rank.
```

## Why does this work?

This is one of those anomalies with **a coherent behavioural story** and **decent empirical support**.

### Behavioural — preference for lottery-like payoffs

**Barberis & Huang (2008) "Stocks as Lotteries"** and **Mitton & Vorkink (2007) "Equilibrium Underdiversification and the Preference for Skewness"** show that investors **systematically over-pay for positively-skewed assets**. The "long-shot" feature is psychologically appealing: a small chance of a big payoff, like a lottery ticket.

In commodities, the same effect appears. Investors over-pay for commodity contracts with positive skewness (those with recent or potential big rallies — speculative stories), and under-pay for those with negative skewness (those exposed to occasional crashes). The skewness premium is the gap between the two.

### Risk premium — bearing left-tail risk

**Conventional risk theory** says investors should demand a premium for assets with negative skewness (more downside than upside), since rational risk-averse investors prefer the opposite. So a negatively-skewed asset should earn more on average — directly consistent with the strategy.

Both explanations point in the same direction. The empirical premium combines them.

### Why specifically in commodities?

Commodities have **physical-supply tail risks** that equities don't:

- **Weather** (agricultural commodities — drought, frost create rare upside spikes).
- **Geopolitics** (energy — Middle East tensions create rare upside spikes).
- **Industrial disruptions** (metals — strikes, mine collapses).

These tend to be **positive-skewness events** for natural longs (the commodity rallies hard). So commodities with histories of such events show positive skewness, and the strategy is implicitly short the "lottery ticket" structure.

## Empirical findings

Fernandez-Perez, Frijns, Fuertes, Miffre (2018) tested a long-short skewness strategy on 27 commodities from 1987 to 2014:

- Annualised excess return of the long-short portfolio: **~7%** before costs.
- Sharpe: **0.6–0.7** depending on window length.
- The signal **did not vanish after the 2004 financialisation** in the way that some other signals did.
- The signal **survives controlling for momentum, basis (roll-yield), and hedging pressure** — meaning it captures a genuinely different premium.

## How skewness relates to other commodity factors

| Factor | Captures | Pairwise correlation with skewness |
|---|---|---|
| [[commodity-roll-yield|Roll yield / basis]] | Curve shape (carry) | Low |
| [[commodity-hedging-pressure|Hedging pressure]] | Position-based risk premium | Low |
| Momentum (12-1) | Recent trend | Low to moderately negative |
| [[commodity-value-momentum|Value]] | 5y price reversal | Low |

The skewness factor is **largely independent** of the other commodity premia. Combining it with roll-yield and momentum in a multi-factor portfolio improves the combined Sharpe.

## What the strategy is actually betting on

The long-short skewness portfolio is:

- Long a basket of commodities with histories of small, frequent rallies and occasional sharp drops.
- Short a basket with histories of small, frequent declines and occasional sharp rallies.

By construction, the **expected payoff distribution of the long-short combination has negative skewness** — the strategy is exposed to the rare event in which positively-skewed commodities have another big spike (oil shock, drought) while negatively-skewed commodities continue draining.

So you are *selling* skewness — much like the [[volatility-quant|volatility risk premium]] is selling vol. The premium exists because most investors don't want to sell skewness.

## Practical caveats

- **Window length matters.** A 6-month window catches recent regime shifts but is noisier. A 36-month window is more stable but slow. 12-month is a common compromise.
- **Skewness is unstable.** Sample skewness is a noisy estimator, especially with small samples or fat-tailed distributions. Robust skewness measures (e.g., quantile-based) sometimes give cleaner signals.
- **Microstructure noise.** Daily returns can pick up bid-ask bounce that distorts skewness estimates. Use monthly returns or filter out micro-structure noise.
- **Watch for delivery-month distortions.** Approaching expiry, contract behaviour changes; include only liquid front-month or two-month-out contracts.

## When the strategy fails

- **Major positive-skewness events.** The 2007–2008 oil rally to $147, the 2010–2011 agricultural commodity rally, the 2022 commodity supply shock — all of these episodes punished short positions in the positively-skewed leg.
- **Crisis-driven correlation breakdown.** In March 2020, all commodities sold off together; the long-short structure offered no protection.
- **Financialisation regimes.** When index flow dominates (2004–2008 era), individual commodity skewness patterns get drowned in correlated flow.

The strategy is *not* uncorrelated with macro shocks — it tilts toward selling positive tail-risk insurance, which makes it look great in quiet years and bad in regime-change years.

## Variants

- **Idiosyncratic skewness.** Strip out the market component (e.g., S&P GSCI return) and use skewness of residuals. Cleaner signal but more complex.
- **Coskewness with equity market.** Use the *covariance* of commodity returns with equity market squared returns — captures hedging value during equity downturns.
- **Realised vs. implied skewness.** For commodities with liquid options, implied skewness from the option surface is forward-looking. Combining realised and implied skew improves predictive power.
- **Combined with momentum.** Negatively-skewed *and* in a downtrend → strong long candidate (the rebound trade). Positively-skewed *and* in an uptrend → strong short candidate (the bubble trade).

## Risk management essentials

- **Cap single-commodity exposure**: 10–15% per commodity in the long-short book.
- **Vol-scale legs**: equal-vol weighting prevents one volatile commodity from dominating P&L.
- **Time-stop on positive-skew event**: if a shorted commodity spikes by N standard deviations, exit; don't average down.
- **Combine with macro overlay**: in periods of extreme macro stress (VIX > 30), reduce position size or stand down.

## Common mistakes

- **Estimating skewness on too-short windows.** Skewness needs at least 12 months of daily data, and even then is high-variance. Don't trust monthly skewness estimates from 6 months of data.
- **Using raw skewness rather than standardised.** The third raw moment is dominated by volatility scale. Always use standardised skewness `μ₃/σ³`.
- **Ignoring relevant fundamentals.** Some skewness patterns reflect *real* fundamentals (e.g., natural gas has positive skewness because of winter demand spikes). Don't short these blindly.
- **Confusing skewness with kurtosis.** Skewness is asymmetry; kurtosis is tail-heaviness. They measure different things. A symmetric fat-tailed distribution has zero skewness and high kurtosis.

## Where to do this in the terminal

- **Futures screen** — rolling skewness for all liquid commodities, ranked cross-sectionally; tier-coded visually.
- **AI Quant Lab** — skewness-factor template with window length and rebalance choices; combinable with roll-yield, HP, momentum.
- **Backtesting** — historical strategy P&L with skewness-event flags (the 2008 oil rally, 2011 commodity boom, 2020 collapse).

## See also

- [[commodity-roll-yield|Commodity Roll Yield]] — the term-structure factor
- [[commodity-hedging-pressure|Commodity Hedging Pressure]] — the COT-based factor
- [[commodity-value-momentum|Commodity Value and Momentum]] — value and trend factors
- [[volatility-quant|Volatility Trading]] — the related practice of selling volatility-risk-premium

## External references

- Fernandez-Perez, A., Frijns, B., Fuertes, A., Miffre, J. (2018). "The Skewness of Commodity Futures Returns." *Journal of Banking & Finance* 86, 143–158.
- Barberis, N., Huang, M. (2008). "Stocks as Lotteries: The Implications of Probability Weighting for Security Prices." *American Economic Review* 98(5), 2066–2100.
- Mitton, T., Vorkink, K. (2007). "Equilibrium Underdiversification and the Preference for Skewness." *Review of Financial Studies* 20(4), 1255–1288.
- Christie-David, R., Chaudry, M. (2001). "Coskewness and Cokurtosis in Futures Markets." *Journal of Empirical Finance* 8(1), 55–81.
- Junkus, J. (1991). "Systematic Skewness in Futures Contracts." *Journal of Futures Markets* 11(1), 9–24.
- Kumar, A. (2009). "Who Gambles in the Stock Market?" *Journal of Finance* 64(4), 1889–1933.
- Eastman, A., Lucey, B. (2008). "Skewness and Asymmetry in Futures Returns and Volumes." *Applied Financial Economics* 18(10), 777–800.
- Tversky, A., Kahneman, D. (1992). "Advances in Prospect Theory: Cumulative Representation of Uncertainty." *Journal of Risk and Uncertainty* 5(4), 297–323.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §9.5. https://doi.org/10.1007/978-3-030-02792-6
