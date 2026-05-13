# ETF Sector Rotation

**Sector rotation** is the ETF version of cross-sectional momentum: rank sector ETFs by recent performance and rotate into the top-decile sectors. It works because sector returns persist over multi-month horizons (Moskowitz & Grinblatt 1999 "Do Industries Explain Momentum?"), and because **trading 9 sector ETFs is operationally far simpler than trading 3000 individual stocks** with the same exposure.

Kakushadze & Serur (2018) §4.1–§4.3 cover three flavours: basic sector momentum, sector momentum with moving-average filter, and dual momentum (cross-sectional + time-series). This entry covers all three together because they are progressive refinements.

## Strategy 1 — Sector Momentum Rotation (basic)

For each sector ETF `i`, compute the cumulative return over a trailing T-month formation period (typically 6–12 months):

```
R^cum_i(t)  =  P_i(t) / P_i(t+T) − 1                    (Eq. 4.1)
```

Where `t` is now and `t+T` is `T` months ago (the book's time-axis convention).

```
Rule:
  Buy ETFs in the top decile (or top half / top 3) by R^cum.
  Hold for 1–3 months. Rebalance.
```

For dollar-neutral construction: short the bottom decile as well.

### The universe

A typical sector-ETF universe in the US:
- **SPDR Sector ETFs**: XLF (financials), XLK (tech), XLE (energy), XLV (healthcare), XLI (industrials), XLP (staples), XLY (discretionary), XLU (utilities), XLB (materials), XLRE (real estate), XLC (communication).
- **Vanguard sector ETFs** (VFH, VGT, etc.) — alternative, lower-fee.
- **Country ETFs** (EWZ Brazil, EWJ Japan, etc.) for cross-country rotation.

### Why does sector momentum work?

Two stories:

1. **Industry-level information diffusion**. Moskowitz & Grinblatt (1999) found that **industry momentum drives much of stock momentum**. If a sector is benefiting from a macroeconomic shift (e.g., rising oil prices → energy sector), the gains accrue over weeks-to-months as analysts revise estimates and capital flows in.

2. **Slow rebalancing by allocators**. Pension funds, target-date funds, asset allocators rebalance quarterly or annually. They flow into outperforming sectors with a lag, sustaining the trend.

### Empirical record

In US sector ETFs since the 2000s:
- 12-1 sector momentum has earned roughly 3–5% annualised excess return.
- Sharpe: ~0.4–0.6, depending on sample.
- The strategy is less crash-prone than single-stock momentum because sector breadth diversifies idiosyncratic shocks.

## Strategy 2 — Sector Momentum with Moving-Average Filter

The basic momentum rotation can buy at sector tops. A simple guard: only buy a momentum-leader sector if its current price is above its long-term moving average.

```
Rule:
  Buy top-decile ETFs only if P > MA(T')
  Short bottom-decile ETFs only if P < MA(T')           (Eq. 4.2)
```

Where `MA(T')` is typically the 100–200-day moving average.

### Why this helps

The MA filter is a **state filter**: it confirms that the sector is still in an uptrend (price above MA), not just one that *was* strong recently but is now rolling over. It catches the case where a leading sector has had a sharp recent rally that's already reversing.

Empirically (Faber 2007 "A Quantitative Approach to Tactical Asset Allocation"), adding the MA filter to momentum rotation:
- **Reduces drawdowns** (the strategy is in cash during bear markets).
- **Trades off some upside** (sometimes the MA crossover signal exits a sector before its rally is complete).
- **Improves risk-adjusted return** on Sharpe basis modestly, but materially on Calmar ratio basis.

## Strategy 3 — Dual Momentum (Antonacci)

The cleanest synthesis. **Dual momentum** (Antonacci 2014) combines:
- **Relative (cross-sectional) momentum**: rank sectors by recent return.
- **Absolute (time-series) momentum**: only take long positions if the broad market is also trending up.

```
Rule:
  IF P_market > MA(T') of the market:
    Buy top-decile sector ETF(s) by cumulative return.
  ELSE:
    Hold an uncorrelated safe asset (gold ETF, Treasury ETF).   (Eq. 4.3)
```

The market filter usually uses the **S&P 500 ETF (SPY)** with a 200-day MA.

### Why dual momentum works

The two signals address different failure modes:

- **Relative momentum** picks the best sector — but if the *whole market* is in a downtrend, the "best" sector is still losing money.
- **Absolute momentum** keeps you out of bear markets entirely.

Combined, the strategy is in the best sector during bull markets and in cash/safe-assets during bear markets. Antonacci documented Sharpe ~0.7 and dramatically reduced drawdowns (max DD around 20% in his sample, vs 50%+ for buy-and-hold).

### Variants

- **Multi-asset dual momentum**: replace sector universe with asset-class universe (stocks, bonds, REITs, commodities, gold).
- **Country rotation dual momentum**: applied to international country ETFs.
- **Style rotation**: applied to style ETFs (value, growth, large, small).

## Variants — Alpha Rotation and R-Squared (book §4.2–§4.3)

### Alpha rotation (§4.2)

Instead of ranking by cumulative return, rank by **Fama-French alpha** — the intercept from a serial regression of ETF returns on the 3 FF factors:

```
R_i(t)  =  α_i + β_{1,i}·MKT(t) + β_{2,i}·SMB(t) + β_{3,i}·HML(t) + ε_i(t)   (Eq. 4.4)

Buy ETFs in top decile by α_i.
```

The idea: pure cumulative return rewards ETFs that got lucky on size or value factor exposure. Alpha strips those out and rewards genuine outperformance.

In practice, ETF alphas are noisy when estimated over short windows. Use ≥ 12 months of data, and acknowledge that the "alpha" of a sector ETF is mostly an estimation artifact.

### R-squared augmentation (§4.3)

R² of the regression measures how much of the ETF's return is *explained* by the systematic factors. Low R² = high "selectivity" (the ETF has unique alpha that's not just factor exposure).

The Amihud-Goyenko (2013) idea: combine alpha **and** low R² to identify the most genuinely active ETFs.

Procedure:
1. Sort ETFs into R² quintiles.
2. Within each R² quintile, sort by alpha into sub-quintiles.
3. Long: lowest-R² × highest-alpha sub-quintile.
4. Short: highest-R² × lowest-alpha sub-quintile.

This double-sort is the academic refinement. In practice it requires a large enough ETF universe (several hundred ETFs) to populate 25 sub-quintiles meaningfully.

## When sector rotation fails

- **Sector leadership churn**. In quickly-changing macro regimes (2020 March COVID, 2022 inflation shock), sector leadership flipped monthly. Momentum signals are stale before they can be acted on.
- **Cross-sector correlations spike in crises**. The 2008 sell-off was correlated across all sectors; "rotating to the best sector" still lost money.
- **ETF tracking error in stress**. Bid-ask spreads on sector ETFs widen in stress; small-AUM ETFs can deviate from NAV. This eats execution P&L.
- **Crowded sector rotation funds**. The strategy is run by many tactical-allocation managers; the marginal alpha has compressed since the 2010s.

## Practical implementation

- **Rebalance frequency**: monthly is standard. Weekly is too noisy and incurs costs; quarterly is too slow.
- **Universe**: 9-11 US sector SPDR ETFs is the simplest universe. Adding country ETFs or styles adds diversification at the cost of complexity.
- **Cost-aware sizing**: ETF round-trip costs are small (~3–5 bps in liquid sector ETFs). Don't over-trade.
- **Tax considerations**: monthly rebalancing in a taxable account triggers short-term capital gains. Most academic backtests ignore taxes.

## Common mistakes

- **Backtesting on ETFs that didn't exist.** XLC (communication services) was created in 2018. Pre-2018 backtests can't include it. Stick to ETFs with at least 5+ years of history.
- **Equal-weighting across mismatched ETFs.** A 1% position in XLU has very different risk from a 1% position in XLF. Vol-scale or beta-adjust.
- **Ignoring underlying index changes**. The S&P sector classifications changed in 2018 (REITs split out, communication services created). Sector definitions are not static.
- **Treating "alpha" of a sector ETF as truly informative.** Sector ETF alphas are mostly noise estimated from short windows. The cumulative-return signal is more robust.

## Risk management essentials

- **MA filter or volatility filter on long-only**: even pure relative momentum should have an absolute-momentum gate.
- **Per-sector concentration limit**: even when the signal is strong, cap any single sector at 30–40% of book.
- **Stress periods**: reduce gross exposure when VIX > 25.
- **Earnings season clusters**: avoid full rotation during earnings-heavy weeks; sector signals are noisy.

## Where to do this in the terminal

- **AI Quant Lab** — sector rotation template with all three variants (basic, MA filter, dual momentum).
- **ETF screen** — sector-ETF cumulative-return rankings.
- **Backtesting** — sector rotation P&L with regime-period flagging.

## See also

- [[stocks-price-momentum|Stock Price Momentum]] — the underlying signal scaled up to sector level
- [[trend-following|Trend Following (TSMOM)]] — the time-series version
- [[etf-multi-asset-trend|Multi-Asset Trend Following]] — cross-asset version
- [[stocks-multifactor|Multifactor Portfolio]] — alpha rotation as a factor strategy
- [[strategy-patterns|Strategy Patterns]] — momentum as an archetype

## External references

- Antonacci, G. (2014). *Dual Momentum Investing: An Innovative Strategy for Higher Returns with Lower Risk*. McGraw-Hill.
- Antonacci, G. (2017). "Risk Premia Harvesting Through Dual Momentum." *Journal of Management & Entrepreneurship* 11(1), 27–55.
- Moskowitz, T., Grinblatt, M. (1999). "Do Industries Explain Momentum?" *Journal of Finance* 54(4), 1249–1290.
- Faber, M. (2007). "A Quantitative Approach to Tactical Asset Allocation." *Journal of Wealth Management* 9(4), 69–79.
- Hong, H., Torous, W., Valkanov, R. (2007). "Do Industries Lead Stock Markets?" *Journal of Financial Economics* 83(2), 367–396.
- O'Neal, E. (2000). "Industry Momentum and Sector Mutual Funds." *Financial Analysts Journal* 56(4), 37–49.
- Amihud, Y., Goyenko, R. (2013). "Mutual Fund's R² as Predictor of Performance." *Review of Financial Studies* 26(3), 667–694.
- Sassetti, P., Tani, M. (2006). "Dynamic Asset Allocation Using Systematic Sector Rotation." *Journal of Wealth Management* 8(4), 59–70.
- Doeswijk, R., van Vliet, P. (2011). "Global Tactical Sector Allocation: A Quantitative Approach." *Journal of Portfolio Management* 28(1), 29–47.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §4.1–§4.3. https://doi.org/10.1007/978-3-030-02792-6
