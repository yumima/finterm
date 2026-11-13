# Stock Price Momentum

**Buy past winners, sell past losers.** The momentum effect — past returns predict near-future returns over horizons of 3 to 12 months — is the **single most documented anomaly** in equity markets, surviving every cross-section, every developed country, and every sample period since **Jegadeesh & Titman (1993)**. The Fama-French three-factor model couldn't explain it; **Carhart (1997)** added it as a fourth factor.

Kakushadze & Serur (2018) §3.1 give the construction in two pages. This entry expands the why, the post-1993 evolution (including the 2009 momentum crash), and the practical details that separate working implementations from ones that bleed.

## The math — what "momentum" actually means

For stock `i` at time `t` (measured in months, `t = 0` is now):

```
Cumulative return over T-month formation period, skipping S most recent months:

  R^cum_i = P_i(S) / P_i(S+T) − 1                      (Eq. 3.2)

Mean monthly return over the formation:
  R^mean_i = (1/T) · Σ_{t=S}^{S+T−1} R_i(t)            (Eq. 3.3)

Risk-adjusted version:
  R^risk.adj_i = R^mean_i / σ_i                        (Eq. 3.4)
```

Typical convention: **T = 12 months, S = 1 month**. So the signal is "12-month return ending one month ago" — the famous **12-1 momentum**.

The 1-month skip exists because the very-recent month tends to **mean revert**, contaminating the signal. Skipping it cleans the trend signal.

## The strategy

```
1. Each month, rank all stocks (or all stocks above a liquidity threshold)
   by R^cum_i over the trailing 12 months, skipping the most recent month.

2. Long the top decile (winners).
3. Short the bottom decile (losers).
4. Hold for 1–6 months. Rebalance.
5. Multi-month holdings can be implemented as overlapping 1-month portfolios
   (Jegadeesh-Titman 1993 convention).
```

Equal-weighting is the default. Some constructions vol-scale (`w_i ∝ 1/σ_i`) for risk-budgeting.

## Why does this work?

Three economic stories, all probably partly correct:

### 1. Underreaction to information

**Hong & Stein (1999)** model: news diffuses slowly through investor populations. Some traders see new information; others see it later; the price adjusts in stages over weeks to months. Momentum traders ride that adjustment.

Empirical support: momentum is **stronger in low-coverage stocks** (less analyst attention → slower information diffusion). It's weaker in mega-caps where information is instantly priced.

### 2. Overconfidence and self-attribution bias

**Daniel, Hirshleifer, Subrahmanyam (1998)**: investors are overconfident in their private information and underweight contradictory public news. Stocks rise on private positive information, then rise *further* as public information confirms it (because confirmation breeds confidence). The result: short-term momentum, then long-term reversal.

This story is consistent with the **3–5 year long-horizon reversal** documented by De Bondt-Thaler (1985) — the same momentum effect that wins at 12 months reverses at 4 years.

### 3. Slow-moving capital

Some investors face institutional constraints (mandates, benchmarks, redemption cycles) that prevent immediate reallocation toward outperforming stocks. The strategy that doesn't face those constraints captures the gradual reallocation.

## The historical record — and the 2009 crash

Pre-2007, momentum was the strategy that worked everywhere. **Asness, Moskowitz, Pedersen (2013) "Value and Momentum Everywhere"** documented it in stocks, bonds, FX, commodities — every asset class tested. The factor had Sharpe ~0.6–0.8 over decades.

Then March–April 2009 happened.

After Lehman, the worst-performing stocks (financials, deeply distressed names) were in the momentum *short leg*. When the market bottomed in March 2009 and rallied violently, those exact names rallied 200–500% in weeks. A long-winner / short-loser portfolio lost roughly **−45% in 8 weeks**, the largest single-quarter drawdown in momentum history.

This is the **momentum crash**, and it's now well-documented:
- **Barroso & Santa-Clara (2014) "Momentum Has Its Moments"** — proposed vol-scaling momentum to mitigate crashes.
- **Daniel & Moskowitz (2016) "Momentum Crashes"** — showed momentum crashes are predictable from market state and recent realised volatility.
- **Asem & Tian (2010)** — momentum is asymmetric across market states (up vs down market).

The takeaway: **momentum is conditionally good and unconditionally great except when it isn't**. The crashes are correlated with sharp risk-on/risk-off reversals.

## Practical adjustments — what real funds run

The textbook 12-1 strategy is the academic version. Working implementations layer several modifications:

### Vol-scaling

Scale momentum exposure inversely to recent realised volatility:
```
weight_t = target_vol / realised_vol_t × signal_t
```
Reduces exposure heading into crash regimes. **Barroso & Santa-Clara (2014)** showed this approach roughly doubled Sharpe.

### Quality overlay

Replace raw momentum with **quality-adjusted momentum**: rank by momentum within high-quality (high ROE, low debt, stable earnings) buckets. **Asness, Frazzini, Pedersen (2014, 2019) "Quality Minus Junk"** found this combination outperforms momentum alone.

### Sector or industry neutralisation

A naive 12-1 portfolio often ends up overweight one sector (the one that's been winning). Industry-neutral construction (rank within sector, then combine) reduces sector concentration risk.

### Risk-adjusted scoring

Use `R^risk.adj_i = R^mean_i / σ_i` (Eq. 3.4) instead of raw cumulative return. Penalises stocks whose recent gain came with extreme volatility.

## The post-2007 reality

Post-GFC, **the basic 12-1 momentum factor has earned roughly half its pre-2007 Sharpe**. Several explanations:

- **Crowding.** Hundreds of factor-investing ETFs and quant funds run versions of momentum. The premium gets arbitraged at the margin.
- **More frequent crashes.** Post-GFC volatility regimes (2011 euro crisis, 2015 China devaluation, 2018 vol spike, 2020 COVID, 2022 tightening) all featured sharp momentum reversals.
- **Compression in dispersion.** When all stocks move together (passive flows, ETF dominance), there's less cross-sectional dispersion for momentum to exploit.

Vol-scaled and quality-overlaid momentum remains tradeable. Pure 12-1 momentum on the unconditional long-short basis has materially decayed.

## Common mistakes

- **Skipping the 1-month skip.** The most recent month exhibits short-term reversal. Without the skip, you're partly buying recent losers — a mean-reversion signal contaminating the momentum signal.
- **Holding too long.** Beyond 3–6 months, the momentum signal stale, transaction costs eat the alpha. Don't try to hold "winners" for years.
- **Sizing in dollar-equal vs. risk-equal.** Equal-dollar positions concentrate risk in high-vol stocks. Risk-weighting (`w ∝ 1/σ`) is generally better, especially in cross-asset momentum.
- **Ignoring borrow costs on the short leg.** Hard-to-borrow stocks (especially small caps in distress) have negative carry that eats short-leg returns.
- **Backtesting without survivorship-bias correction.** Pre-1990s data sets sometimes drop delisted names. Make sure the loser leg includes them.

## Risk management essentials

- **Vol-target the book.** Use realised vol of the long-short P&L to gate exposure. Reduce position size when realised vol exceeds historical average.
- **Filter for upcoming events.** Earnings, FDA decisions, M&A targets in the long leg: known event risk can swamp the momentum signal.
- **Watch market state.** Momentum performs poorly in the months immediately following a market trough (the 2009 case). The Asem-Tian and Daniel-Moskowitz papers give actionable filters.
- **Cap single-stock exposure.** No single name should be more than 1–3% of the book. Diversification is the strategy's main edge.
- **Stop on momentum-crash signal.** If long-short momentum loses more than 2× its historical monthly vol in a single week, halve the book.

## Where to do this in the terminal

- **AI Quant Lab** — momentum strategy template; user picks formation, skip, holding periods; vol-scaling and sector-neutralisation toggle on/off.
- **Backtesting** — runs 12-1 momentum on US universe with crash-period analysis; flags 2009-style risk-on regimes.
- **Equity Research** — single-stock momentum dashboard for individual names.

## See also

- [[stocks-earnings-momentum|Earnings Momentum]] — fundamental-based momentum (SUE)
- [[stocks-residual-momentum|Residual Momentum]] — momentum after stripping FF factors
- [[stocks-multifactor|Multifactor Portfolio]] — combining momentum with value, quality, low-vol
- [[trend-following|Trend Following (TSMOM)]] — the time-series cousin
- [[mean-reversion|Mean Reversion]] — the short-horizon antagonist

## External references

- Jegadeesh, N., Titman, S. (1993). "Returns to Buying Winners and Selling Losers: Implications for Stock Market Efficiency." *Journal of Finance* 48(1), 65–91.
- Carhart, M. (1997). "On Persistence in Mutual Fund Performance." *Journal of Finance* 52(1), 57–82.
- Hong, H., Stein, J. (1999). "A Unified Theory of Underreaction, Momentum Trading, and Overreaction in Asset Markets." *Journal of Finance* 54(6), 2143–2184.
- Daniel, K., Hirshleifer, D., Subrahmanyam, A. (1998). "Investor Psychology and Security Market Under- and Over-reactions." *Journal of Finance* 53(6), 1839–1885.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Barroso, P., Santa-Clara, P. (2014). "Momentum Has Its Moments." *Journal of Financial Economics* 116(1), 111–120.
- Daniel, K., Moskowitz, T. (2016). "Momentum Crashes." *Journal of Financial Economics* 122(2), 221–247.
- Grinblatt, M., Moskowitz, T. (2004). "Predicting Stock Price Movements from Past Returns." *Journal of Financial Economics* 71(3), 541–579.
- Asness, C., Frazzini, A., Pedersen, L. (2019). "Quality Minus Junk." *Review of Accounting Studies* 24(1), 34–112.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.1. https://doi.org/10.1007/978-3-030-02792-6
