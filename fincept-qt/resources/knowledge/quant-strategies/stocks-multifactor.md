# Multifactor Equity Portfolio

A **multifactor portfolio** combines several individually-validated factor signals (value, momentum, low-vol, quality, profitability, etc.) into a single book. The empirical justification is straightforward: the individual factors are **roughly uncorrelated**, so combining them increases Sharpe ratio above any single-factor strategy.

This is the workhorse of the **smart-beta** and **quantitative active management** industries — every major asset manager (BlackRock, Vanguard, AQR, Dimensional) offers some version. Kakushadze & Serur (2018) §3.6 cover the construction tersely. The intellectual lineage runs from **Fama-French (1993)** through **Asness, Frazzini, Pedersen (2014)** to modern composite-score implementations.

## The setup

You have `F` individual factors (typically 3–6): say value (V), momentum (M), low-vol (L), quality (Q). For each stock, you have a factor score in each.

The simplest combination: equal weight.

```
score_i  =  (1/F) · Σ_A rank(f_Ai)
```

where `f_Ai` is the value of factor `A` for stock `i`, and `rank` is the cross-sectional rank.

Long the top decile/quintile by combined score, short the bottom decile/quintile.

## Two flavours of "combination"

### Method 1 — Combine signals, then rank

(The book's §3.6 default, equation 3.10–3.12.)

```
Demean ranks:
  s_Ai  =  rank(f_Ai) − (1/N) · Σ_j rank(f_Aj)             (Eq. 3.11)

Combined score:
  s_i  =  (1/F) · Σ_A s_Ai                                   (Eq. 3.12)
```

Then long/short by `s_i`.

This treats all factors as equally weighted unit-variance signals. Simple, robust, hard to argue with.

### Method 2 — Build sub-portfolios, then combine

Each factor produces its own long-short portfolio (long top decile of V, short bottom decile of V; long top decile of M; etc.). Combine the resulting **return streams**.

```
R_combo(t)  =  w_V · R_V(t) + w_M · R_M(t) + w_L · R_L(t) + w_Q · R_Q(t)
```

With `Σ w_A = 1`. The weights can be:
- **Equal** (default).
- **Risk-parity** (`w_A ∝ 1/σ_A`).
- **Min-variance** (using the covariance matrix of factor returns).
- **Expected-return-weighted** (using historical Sharpe).

### Which method wins?

**Asness, Frazzini, Pedersen (2014, 2019)** argued **Method 1 is generally better** ("the integrated portfolio") because:
- It avoids buying and selling the same stock for offsetting reasons (e.g., V says long, M says short → net zero in Method 1, but two opposing positions in Method 2).
- It reduces turnover.
- It implicitly captures interactions (a stock that scores well on multiple factors is more compelling than one that scores great on one factor and bad on another).

**Method 2** is easier to attribute returns (you see exactly which factor earned what).

In practice, large quant managers use Method 1; performance attribution tools post-hoc decompose into the underlying factor contributions.

## Why factor combination works — the empirical fact

Individual factor Sharpe ratios in US equities (long-only, 1972–2020 academic estimates):

| Factor | Sharpe | Source |
|---|---|---|
| Value (HML) | ~0.4 | Fama-French |
| Momentum (UMD) | ~0.5 | Jegadeesh-Titman |
| Low-vol (BAB) | ~0.6 | Frazzini-Pedersen |
| Quality (QMJ) | ~0.5 | Asness, Frazzini, Pedersen |
| Profitability (RMW) | ~0.4 | Novy-Marx |

Pairwise correlations of these factor returns:

| | V | M | L | Q | P |
|---|---|---|---|---|---|
| V | 1.0 | −0.4 | +0.1 | +0.3 | +0.4 |
| M |  | 1.0 | +0.2 | +0.1 | +0.1 |
| L |  |  | 1.0 | +0.4 | +0.3 |
| Q |  |  |  | 1.0 | +0.6 |
| P |  |  |  |  | 1.0 |

The key insight: **value and momentum are negatively correlated** (~−0.4). Holding both diversifies the book. The 5-factor portfolio has Sharpe ~1.0 — roughly double the best single factor.

## Building it correctly — practical issues

### Factor definitions matter

The same "factor name" can be defined in many ways. "Value" can be B/P, E/P, CF/P, S/P, or a composite. "Momentum" can be 12-1, 6-1, risk-adjusted. Different definitions earn slightly different premia.

**Best practice**: use composites within each factor (multiple value definitions averaged, multiple momentum lookbacks averaged), then combine factors.

### Cross-sectional standardisation

Before combining, each factor signal must be on the same scale. Common choices:
- **Rank-based**: standardise to uniform [0,1] across the cross-section. Most robust.
- **Z-score**: standardise to mean 0, std 1. More sensitive to extreme values.
- **Winsorised z-score**: z-score with extreme tails clipped to ±3. Compromise.

### Sector / industry neutralisation

Without it, the multifactor portfolio tilts toward whichever sector currently has the best factor scores (often energy/financials on value, tech on momentum, utilities/staples on low-vol). Sector-neutralisation by ranking *within* sector before combining cleans up these tilts.

### Rebalance frequency

Monthly is the academic standard. Quarterly reduces transaction costs at the cost of stale signals. For US large-cap, monthly with proper turnover-control optimisation (e.g., portfolio optimiser with turnover penalty) is the practitioner standard.

## What's the actual return profile?

The 5-factor portfolio in academic samples:
- Annualised long-short return: ~12–15%.
- Volatility: ~10–12%.
- Sharpe: ~1.0.
- Maximum drawdown: ~20% (mid-1990s, late 1990s, 2007–2009).
- Beta to market: ~0 (zero-cost long-short).

In live performance:
- Net of costs: Sharpe ~0.5–0.7 (transaction costs and capacity constraints).
- 2018–2020 drawdown: ~−25% peak-to-trough as growth dominated value, low-vol, and quality.
- 2021–2023 partial recovery as value revived.

## Variants — beyond the classic five

### Machine learning factor combination

Use a non-linear model (gradient boosting, neural networks) to combine factor signals. Captures interactions like "value works better when momentum is also positive."

**Performance**: in-sample improvements are real but out-of-sample fragile. **Gu, Kelly, Xiu (2020) "Empirical Asset Pricing via Machine Learning"** found machine-learning combinations beat linear ones by a meaningful margin, but the gains depend heavily on careful regularisation and out-of-sample validation.

### Alpha combos (Kakushadze §3.20)

A more aggressive version: combine **thousands** of individual signals (not just 4–6). Treat each as an "alpha." Estimate the covariance matrix of alpha returns. Apply optimisation to combine them.

**Kakushadze & Yu (2017, 2018)** describe the procedure (§3.20). It's the methodology of large quant funds. The challenge is **noise**: with thousands of signals, estimation error in the covariance matrix dominates. Shrinkage and dimensional reduction are essential.

### Factor timing (controversial)

Some practitioners argue factors can be **timed** — increase value exposure when value spreads are wide, reduce when narrow. **Asness, Chandra, Ilmanen, Israel (2017)** showed empirical evidence is **weak**; the costs of timing typically exceed the benefits.

## Common mistakes

- **Over-fitting on small factor universes.** With 5–10 factors and a 50-year sample, you have plenty of degrees of freedom to fit noise. Hold out a clean test sample.
- **Equal-weighting in standardised but not vol-adjusted space.** Factor vol differs (BAB is typically more volatile than HML). Equal-weight ranks ≠ equal-weight risk contributions.
- **Forgetting transaction costs.** Multifactor portfolios have meaningful turnover (50–150% per year). A backtest that ignores costs is wildly optimistic.
- **Treating "smart beta ETF" performance as the factor return.** Real factor returns are long-short; smart-beta ETFs are long-only and contaminated with market beta.

## Risk management essentials

- **Cap single-name exposure** (1–3%).
- **Cap sector exposure** (±5% vs benchmark, or sector-neutral).
- **Monitor factor crowding.** When factor positioning becomes very crowded (high-momentum hedge fund exposure to the same factor longs), reduce. **Lou & Polk (2022)** show factor returns suffer in high-crowding periods.
- **Stress-test for factor coincidence**: 2007 quant quake (value, momentum, low-vol all hurt simultaneously); 2018 vol spike; 2020 March COVID.

## Where to do this in the terminal

- **AI Quant Lab** — multifactor portfolio template; users select factors and weighting scheme.
- **Equity Research** — single-stock factor scores across all 5 canonical factors.
- **Backtesting** — multifactor strategy with per-factor attribution.
- **Portfolio** — multifactor-tilt overlay on existing positions.

## See also

- [[stocks-price-momentum|Price Momentum]] — one component
- [[stocks-value-factor|Value Factor]] — one component
- [[stocks-low-volatility-anomaly|Low-Volatility Anomaly]] — one component
- [[stocks-residual-momentum|Residual Momentum]] — refinement of vanilla momentum
- [[portfolio-construction|Portfolio Construction]] — the optimisation framework
- [[strategy-patterns|Strategy Patterns]] — where multifactor fits

## External references

- Asness, C., Frazzini, A., Israel, R., Moskowitz, T. (2014). "Fact, Fiction, and Momentum Investing." *Journal of Portfolio Management* 40(5), 75–92.
- Asness, C., Frazzini, A., Pedersen, L. (2019). "Quality Minus Junk." *Review of Accounting Studies* 24(1), 34–112.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Fama, E., French, K. (2015). "A Five-Factor Asset Pricing Model." *Journal of Financial Economics* 116(1), 1–22.
- Gu, S., Kelly, B., Xiu, D. (2020). "Empirical Asset Pricing via Machine Learning." *Review of Financial Studies* 33(5), 2223–2273.
- Barber, J., Bennett, S., Gvozdeva, E. (2015). "How to Choose a Strategic Multifactor Equity Portfolio?" *Journal of Index Investing* 6(2), 34–45.
- Amenc, N., Goltz, F., Sivasubramanian, S., Lodh, A. (2015). "Robustness of Smart Beta Strategies." *Journal of Index Investing* 6(1), 17–38.
- Kakushadze, Z., Yu, W. (2017). "How to Combine a Billion Alphas." *Journal of Asset Management* 18(1), 64–80.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.6 and §3.20. https://doi.org/10.1007/978-3-030-02792-6
