# Commodity Value and Momentum

The two factors that **work across every asset class ever tested** are **value** and **momentum** (Asness, Moskowitz, Pedersen 2013, "Value and Momentum Everywhere"). Commodities are no exception. The two factors are roughly uncorrelated within commodities, and combining them produces a Sharpe materially higher than either alone.

Kakushadze & Serur (2018) §9.4 cover the value construction directly (and reference momentum in stocks at §3.1). This entry treats them as the integrated pair they almost always are in practice.

## Factor 1 — Commodity Value

The "value" idea in commodities is tricky because there is no Book-to-Market analogue — commodities don't have a balance sheet. The standard solution (Asness, Moskowitz, Pedersen 2013) is:

```
v_i  =  P_i(5 years ago)  /  P_i(today)              (Eq. 9.2)
```

A high `v_i` means the commodity is **trading below its 5-year-ago price** — it has had a long decline and is "cheap" in a mean-reversion sense. A low `v_i` means the commodity is trading well above its 5-year-ago price — it has rallied and is "expensive."

The strategy:

```
1. Each month, compute v_i for every commodity in the universe.
2. Sort.
3. Long the top tercile (cheapest by long-horizon reversal).
4. Short the bottom tercile (most expensive).
5. Equal-weight each leg. Rebalance monthly.
```

The horizon is critical. **5 years** is roughly the half-life of the long-horizon mean reversion that drives this factor — shorter windows pick up momentum (the wrong direction), longer windows are too sluggish to be tradeable.

### Why this works in commodities

Three reasons:

1. **Supply response.** Low prices over a multi-year horizon discourage investment in new production capacity. Existing producers cut output. The commodity's supply curve shifts back, and eventually prices recover. This is **decades-long mean reversion** in commodities driven by capital-cycle dynamics.
2. **Demand substitution.** Persistently high prices encourage substitution — natural gas displaces coal in power generation, electric vehicles displace oil. High-price periods are usually followed by demand-side erosion.
3. **Behavioural over-extrapolation.** Investors extrapolate recent price trends. A commodity that has rallied for years gets bid up beyond fundamental value; one that has declined gets sold below fundamental value.

The combination produces the slow, painful 5-year mean reversion that the value signal captures.

### Empirical record

Asness, Moskowitz, Pedersen (2013) report a Sharpe of roughly **0.4–0.6** for commodity value in long samples. The factor is moderately correlated with cross-asset value (equity, bond, currency value), suggesting it taps into a broader "long-horizon reversal" premium across asset classes.

### When it fails

- **Persistent supply shocks.** If a commodity has reset to a structurally higher price level (e.g., oil after the 1973 embargo, lithium after the EV boom), the 5-year-ago reference price is meaningless. The value signal says "expensive!" and shorts at exactly the wrong time.
- **Bubble years.** 2007–2008 commodity boom; 2020–2022 supply shocks. The signal flagged "expensive" repeatedly during sustained price spikes and lost.

## Factor 2 — Commodity Momentum

The momentum signal in commodities mirrors equity momentum closely.

```
1. Each month, compute trailing 12-month return for each commodity
   (skipping the most recent 1 month — the "12-1" convention).
2. Sort.
3. Long the top tercile.
4. Short the bottom tercile.
5. Equal-weight each leg. Rebalance monthly. Hold 1 month.
```

**Miffre & Rallis (2007) "Momentum Strategies in Commodity Futures Markets"** is the key empirical reference. They documented Sharpe ratios of 0.5–0.7 in their 1979–2004 sample.

### Why momentum works in commodities

Two complementary mechanisms:

1. **Slow information diffusion in physical markets.** Supply-and-demand information takes time to be priced in. A drought in soybean country today shows up in cash markets, then in nearby futures, then in deferred futures, then in related grains — a multi-week diffusion process. Momentum traders ride this diffusion.
2. **Herding and trend amplification.** CTAs (Commodity Trading Advisors) and trend-following funds are major commodity participants. Their systematic trend-following amplifies price moves and creates the persistence that momentum strategies exploit.

This is the commodity version of the [[trend-following|trend-following]] story. The single-commodity time-series version (TSMOM) is one thing; the cross-sectional version (long winners, short losers) is the same idea applied across commodities.

### When commodity momentum fails

- **Sharp regime turns.** When a long uptrend abruptly reverses (oil 2008 collapse from $147 to $35; nat gas 2008 collapse), momentum is long at exactly the top.
- **Choppy markets.** In rangebound commodities, momentum repeatedly whipsaws — buying after a rally, selling after a decline, paying spreads both ways.
- **2009–2014 sideways period.** Many commodities went sideways; momentum strategies bled.

## Why value and momentum work *together*

This is the key practical insight. **Commodity value and momentum are nearly uncorrelated**:

- Value picks up on **slow mean reversion** (5-year horizons).
- Momentum picks up on **medium-term trends** (12-month horizons).

They earn in different regimes:

- **Trending markets** → momentum wins, value loses.
- **Mean-reverting markets** → value wins, momentum loses.

Combining them gives smoother returns. In Asness, Moskowitz, Pedersen's commodity sample, the equal-weighted combo had Sharpe ~0.8 — meaningfully higher than either ~0.5 factor alone.

### The combined construction

```
1. Compute v_i (5y reversal) and m_i (12-1 momentum) for each commodity.
2. Cross-sectionally standardise each signal: z_v_i, z_m_i.
3. Combined score: s_i = z_v_i + z_m_i.
4. Long top tercile by s_i, short bottom tercile.
5. Rebalance monthly.
```

The standardisation ensures each factor contributes equally regardless of its absolute range.

## How value-momentum relates to the other commodity factors

| Factor | Signal type | Horizon | Correlation with Value | Correlation with Momentum |
|---|---|---|---|---|
| [[commodity-roll-yield|Roll yield (basis)]] | Term-structure shape | 1 month | Low | Low-moderate |
| [[commodity-hedging-pressure|Hedging pressure]] | Position data | 6 months | Low | Low |
| [[commodity-skewness-premium|Skewness]] | Return distribution | 12 months | Low | Low |
| **Value** | 5y reversal | 60 months | — | Low (typically negative within equity, near zero in commodities) |
| **Momentum** | 12-1 return | 12 months | Low | — |

All five are largely independent. The full **multi-factor commodity portfolio** combines all of them, and is the strategy that quantitative commodity funds actually run.

## A unified construction the practitioner can use

```
1. For each commodity, compute:
   - Roll yield signal:        z_R = z-score of (P₁/P₂)
   - Hedging pressure signal:  z_H = z-score of producer HP
   - Skewness signal:          z_S = − z-score of 12m standardised skewness
   - Value signal:             z_V = z-score of (P_{−5y} / P_now)
   - Momentum signal:          z_M = z-score of (12-1 month return)

2. Combined score: s_i = z_R + z_H + z_S + z_V + z_M

3. Long top tercile by s_i, short bottom tercile.

4. Vol-scale each leg.

5. Rebalance monthly.
```

This is roughly what commodity-pool quants actually run. Annualised Sharpes net of costs have been documented in the 0.8–1.2 range in academic samples; live performance with realistic costs has been somewhat lower.

## Common mistakes

- **Using too-short a value horizon.** A 1-year or 2-year price ratio captures *momentum*, not value, in commodities. Use 5 years.
- **Equal-notional weighting across very different commodities.** A 1% position in natural gas has 4× the vol of a 1% position in corn. Always vol-scale.
- **Trading the signals individually.** Each factor on its own has periods of severe drawdown. The combination is what makes the strategy tradeable.
- **Forgetting to skip the most recent month in momentum.** The 12-1 convention exists because short-horizon reversal contaminates the trailing 12-month signal. Skipping the last month avoids this.

## Risk management essentials

- **Per-commodity vol-targeting**: scale each position to a target volatility contribution.
- **Cap aggregate sector exposure**: don't let energy or agriculturals dominate the book.
- **Stress test the 2008 and 2014 episodes**: both saw correlated commodity drawdowns that hurt all multi-factor commodity portfolios.
- **Be ready for regime change**: factor returns are stable within regimes and unstable across them.

## Where to do this in the terminal

- **Futures screen** — value and momentum signal columns for every commodity; cross-sectional ranks.
- **AI Quant Lab** — multi-factor commodity template (value + momentum + roll + HP + skew); user toggles which factors to include.
- **Backtesting** — historical P&L of value alone, momentum alone, combined; drawdown decomposition.

## See also

- [[commodity-roll-yield|Commodity Roll Yield]] — the term-structure factor
- [[commodity-hedging-pressure|Commodity Hedging Pressure]] — the COT-data factor
- [[commodity-skewness-premium|Commodity Skewness Premium]] — the higher-moment factor
- [[trend-following|Trend Following (TSMOM)]] — the broader time-series momentum framework
- [[mean-reversion|Mean Reversion]] — the broader reversal framework
- [[strategy-patterns|Strategy Patterns]] — where value and momentum sit among the archetypes

## External references

- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Miffre, J., Rallis, G. (2007). "Momentum Strategies in Commodity Futures Markets." *Journal of Banking & Finance* 31(6), 1863–1886.
- Blitz, D., Van Vliet, P. (2008). "Global Tactical Cross Asset Allocation: Applying Value and Momentum Across Asset Classes." *Journal of Portfolio Management* 35(1), 23–28.
- Fuertes, A., Miffre, J., Rallis, G. (2010). "Tactical Allocation in Commodity Futures Markets: Combining Momentum and Term Structure Signals." *Journal of Banking & Finance* 34(10), 2530–2548.
- Bernardi, S., Leippold, M., Lohre, H. (2018). "Maximum Diversification Strategies along Commodity Risk Factors." *European Financial Management* 24(1), 53–78.
- Erb, C., Harvey, C. (2006). "The Strategic and Tactical Value of Commodity Futures." *Financial Analysts Journal* 62(2), 69–97.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §9.4. https://doi.org/10.1007/978-3-030-02792-6
