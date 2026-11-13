# Residual Momentum

**Residual momentum** is [[stocks-price-momentum|standard momentum]] applied to the **residuals from a Fama-French factor regression** instead of raw stock returns. The empirical finding (**Blitz, Huij, Martens 2011 "Residual Momentum"**): residual momentum has roughly the same expected return as raw momentum but with **half the volatility** — implying roughly **double the Sharpe ratio**.

Kakushadze & Serur (2018) §3.7 give the procedure step by step. This is one of the cleaner factor refinements, and it directly addresses the well-known **momentum crash** problem.

## The construction

For each stock `i` over the past 36 months:

```
1. Run a serial regression of monthly returns on the 3 Fama-French factors:

   R_i(t) = α_i + β_{1,i} · MKT(t) + β_{2,i} · SMB(t) + β_{3,i} · HML(t) + ε_i(t)    (Eq. 3.13)

   where MKT = market excess return, SMB = small-minus-big, HML = high-minus-low B/M.

2. Compute residuals over a recent 12-month formation period (skipping the most recent month):

   ε_i(t) = R_i(t) − β_{1,i}·MKT(t) − β_{2,i}·SMB(t) − β_{3,i}·HML(t)              (Eq. 3.14)

   Note: α_i is NOT subtracted here (it was estimated over 36 months;
   the residuals for the 12-month formation use the betas only).

3. Compute mean and standard deviation of these residuals:

   ε_i^mean  =  (1/T) · Σ_{t=S}^{S+T−1} ε_i(t)                                     (Eq. 3.15)
   σ̃_i²     =  (1/(T−1)) · Σ_{t=S}^{S+T−1} (ε_i(t) − ε_i^mean)²                  (Eq. 3.17)

4. Form risk-adjusted residual return:

   R̃_i^risk.adj  =  ε_i^mean / σ̃_i                                                (Eq. 3.16)

5. Long top decile by R̃_i^risk.adj. Short bottom decile.
   Hold for 1 month, rebalance.
```

## Why does this beat raw momentum?

The intuition is that **raw momentum mixes two signals**:

- **Factor exposure momentum**: stocks with high recent market/size/value beta are riding factor returns.
- **Stock-specific (idiosyncratic) momentum**: the stock has done better than its factor exposures alone would predict.

When factor returns reverse violently (e.g., March 2009 when high-beta stocks rallied after the GFC trough), **raw momentum is positioned long the just-fallen high-beta names**, which then rally back — a catastrophic short-leg / long-leg crash.

Residual momentum **strips out the factor-beta exposure**. The remaining signal is purely stock-specific. When factor returns reverse, residual momentum is largely unaffected because it never had factor-beta tilts in the first place.

**Blitz, Huij, Martens (2011)** documented:
- Raw momentum Sharpe: ~0.5 in their sample.
- Residual momentum Sharpe: ~1.0 in the same sample.
- The improvement came primarily from **reduced volatility**, not increased returns.

## Connection to the momentum crash

The classic 2009 momentum crash hit raw momentum because:
- The long leg (12-month winners) was dominated by defensive, low-beta names that did *not* rally in March 2009.
- The short leg (12-month losers) was dominated by high-beta financials and cyclicals that rallied 100–500% in weeks.

A residual-momentum portfolio at that time had **lower beta dispersion between long and short**. When the market reversed, both legs moved similarly, and the long-short P&L didn't blow up.

**Daniel & Moskowitz (2016) "Momentum Crashes"** confirmed: residual momentum is far less crash-prone than raw momentum. Their proposed crash hedge (vol-scaled momentum) and Blitz et al.'s residual momentum both work, by different mechanisms.

## Why this works empirically

The factor-adjustment can be thought of in three complementary ways:

1. **Beta-neutralisation**: ensures the long and short legs have similar market, size, and value exposures. The trade is purely about stock-specific performance.
2. **Information isolation**: separates the "lucky factor exposure" portion of recent returns from the "actually good stock" portion.
3. **Risk-adjustment**: the `/σ̃` step further normalises by the stock's idiosyncratic vol, so the signal isn't dominated by high-vol names.

## Variants

### Four-factor residual momentum

Use the Carhart 4-factor model (FF3 + momentum factor UMD) for the regression. The residuals are momentum-factor-neutral as well as market/size/value-neutral. **Gutierrez & Prinsky (2007) "Momentum, Reversal, and the Trading Behaviors of Institutions"** explored this variation.

### Five-factor residual momentum

Add profitability (RMW) and investment (CMA) factors. **Fama-French (2015)**. Cleaner residuals; harder to estimate.

### Industry-residual momentum

Run regression of returns on industry-portfolio returns. Residuals capture stock-specific deviation from industry. **Asness, Porter, Stevens (2000)**.

### Long-formation residual momentum

Replace 12-month formation with 36 or 60 months. The longer formation captures slower-moving idiosyncratic information. Less standard but used in some long-horizon variants.

## Empirical record

In US large/mid-cap equities:

- **Pre-2007**: residual momentum Sharpe ~0.9–1.0 (Blitz et al.).
- **2007–2009 crash**: residual momentum drawdown ~10%, vs ~45% for raw momentum.
- **Post-GFC**: residual momentum has decayed less than raw momentum. Current estimates around Sharpe 0.4–0.6.
- **2020 March**: held up better than raw momentum; modest drawdown.

The strategy has not been arbitraged away in part because it requires factor-model machinery that is harder to deploy than simple price-based rules.

## Practical implementation

- **Data requirements**: 36 months of monthly returns + FF factor returns. The Fama-French factors are publicly available on Kenneth French's website.
- **Frequency**: monthly is standard. Daily versions exist but are noisy.
- **Universe**: liquid US large/mid-cap is the bread and butter. Smaller-cap and international versions have been published but with shorter, noisier samples.
- **Regression window**: 36 months is the original Blitz et al. choice. 24 months is also reasonable; 60 months risks the betas being stale (companies change).
- **Skip period**: 1 month, same as raw momentum. This handles short-horizon reversal.
- **Formation period**: 12 months (same as raw momentum).
- **Holding period**: 1 month, can be extended to 3 or 6 months.

## Common mistakes

- **Using rolling betas with too-short windows.** 6 or 12 month betas are too noisy. The signal becomes unstable.
- **Including α in the residual computation.** Don't subtract α — the 36-month-estimated α is itself noisy, and including it makes the signal worse, not better.
- **Forgetting risk-adjustment.** The `/σ̃` step is part of the strategy. Without it, you have raw residuals, which still have meaningful cross-sectional vol dispersion.
- **Re-estimating betas every month.** Use rolling 36-month betas updated monthly, but don't change the strategy because betas slightly moved — that creates noise.

## Risk management essentials

- **Vol-scale the book at the strategy level**, just like raw momentum. Even residual momentum has crash periods.
- **Monitor factor exposures of the residual-momentum portfolio.** If after construction it still has meaningful MKT, SMB, or HML beta, something's off in the regression.
- **Pair with other factors.** Residual momentum + value + low-vol is one of the better multi-factor combinations.
- **Earnings filter**: drop stocks with upcoming earnings to avoid event-driven contamination.

## Where to do this in the terminal

- **AI Quant Lab** — residual momentum template; users choose FF3 or FF5 specification.
- **Equity Research** — single-stock factor regression diagnostics.
- **Backtesting** — historical residual momentum P&L vs. raw momentum side-by-side; crash-period comparison.

## See also

- [[stocks-price-momentum|Price Momentum]] — the raw signal this refines
- [[stocks-multifactor|Multifactor Portfolio]] — residual momentum as one ingredient
- [[stocks-low-volatility-anomaly|Low-Volatility Anomaly]] — diversifying factor
- [[stocks-value-factor|Value Factor]] — diversifying factor
- [[strategy-patterns|Strategy Patterns]] — momentum-factor hierarchy

## External references

- Blitz, D., Huij, J., Martens, M. (2011). "Residual Momentum." *Journal of Empirical Finance* 18(3), 506–521.
- Gutierrez, R., Prinsky, C. (2007). "Momentum, Reversal, and the Trading Behaviors of Institutions." *Journal of Financial Markets* 10(1), 48–75.
- Chaves, D. (2012). "Eureka! A Momentum Strategy That Also Works in Japan." Working paper, SSRN 1982100.
- Grundy, B., Martin, J. (2001). "Understanding the Nature of the Risks and the Source of the Rewards to Momentum Investing." *Review of Financial Studies* 14(1), 29–78.
- Daniel, K., Moskowitz, T. (2016). "Momentum Crashes." *Journal of Financial Economics* 122(2), 221–247.
- Hühn, H., Scholz, H. (2017). "Alpha Momentum and Price Momentum." Working paper.
- Asness, C., Porter, R., Stevens, R. (2000). "Predicting Stock Returns Using Industry-Relative Firm Characteristics." Working paper, SSRN 213872.
- Carhart, M. (1997). "On Persistence in Mutual Fund Performance." *Journal of Finance* 52(1), 57–82.
- Fama, E., French, K. (1993). "Common Risk Factors in the Returns on Stocks and Bonds." *Journal of Financial Economics* 33(1), 3–56.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.7. https://doi.org/10.1007/978-3-030-02792-6
