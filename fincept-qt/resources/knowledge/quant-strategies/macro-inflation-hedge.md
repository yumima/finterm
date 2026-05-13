# Global Macro Inflation Hedge

When inflation rises unexpectedly, **traditional 60/40 portfolios suffer** — bonds lose value to higher rates, equities suffer multiple compression and margin pressure. The classic inflation hedge is **commodity exposure**, but commodities are volatile and don't always trade in lockstep with realised inflation.

The strategy: use the **headline-vs-core inflation spread** as a leading indicator of inflation pressure, and **scale commodity allocation accordingly**. When headline inflation is rising faster than core, an oil-driven shock is in progress; commodity allocation up. When headline and core converge, the shock is passing through; commodity allocation can be reduced.

Kakushadze & Serur (2018) §19.3 give the formula in one paragraph; this entry expands the economic intuition and the practical hedging implementation.

## The two-step inflation pass-through

The book lays out the chain:

1. **Exogenous shocks** (oil supply disruption, currency depreciation, food prices) push **commodity prices** up.
2. Commodity prices flow into **headline inflation (HI)**, which is the broad CPI measure including food and energy.
3. Then over months, headline inflation flows into **core inflation (CI)** — the CPI excluding the volatile food/energy components — as the shock embeds in wage demands and broader price-setting.

**Headline reflects shocks quickly; core reflects shocks slowly.** When HI - CI is wide and positive, an inflation shock is in progress that hasn't yet been absorbed.

## The signal

```
CA  =  max( 0, min( (HI_YoY − CI_YoY) / HI_YoY, 1 ) )    (Eq. 19.1)
```

Where:
- `HI_YoY` = year-over-year headline inflation (annualised %).
- `CI_YoY` = year-over-year core inflation.
- `CA` = commodity allocation percentage (bounded between 0 and 1, or 0%–100%).

### Interpreting the formula

- If `HI_YoY = 6%` and `CI_YoY = 3%`: CA = (6−3)/6 = 50%. **Half the portfolio in commodities.**
- If `HI_YoY = 8%` and `CI_YoY = 2%`: CA = (8−2)/8 = 75%. **Three-quarters in commodities.**
- If `HI_YoY = CI_YoY`: CA = 0%. No commodity allocation needed (no headline-core gap).
- If `HI_YoY < CI_YoY` (deflationary headline shock): CA = 0%.

So **the strategy ramps up commodity exposure when an oil/food shock is fresh and ramps down as it passes through to core**.

## The economic logic

### Why headline-minus-core is a leading indicator

Core inflation removes the most volatile components (food, energy) precisely because **they're shock-driven, not trend-driven**. Headline includes them. The gap measures **how much fresh shock is in the system**.

The shock's transmission to broader prices is gradual:
- **0–3 months**: shock visible in headline only.
- **3–9 months**: shock starts flowing into core via wage pressure, margin protection, and second-round effects.
- **12+ months**: core fully reflects the shock.

During the **transmission window**, commodities are still ahead of the broader inflation curve. After core catches up, the curve flattens and commodities lose their differential edge.

### Why commodities specifically

Commodities have positive correlation with realised inflation (Bodie 1983, Greer 1978, Gorton-Rouwenhorst 2006). Stocks and bonds have negative correlation with inflation surprises. So commodities are the **natural inflation hedge** in a multi-asset portfolio.

The economic mechanism: high inflation usually coincides with commodity price increases (the same supply-demand factors drive both), so commodities directly capture the inflation move.

## Implementing the hedge

The book mentions buying "a basket of various commodities through ETFs, futures, etc." (footnote 6). Practical implementations:

### Broad commodity index ETFs

- **DBC** (Invesco DB Commodity Index Tracking Fund) — diversified commodities.
- **GSG** (iShares S&P GSCI Commodity-Indexed Trust) — broad commodity index.
- **PDBC** (Invesco Optimum Yield Diversified Commodity Strategy).

### Sector-specific

- **Energy**: USO (oil), UNG (nat gas), XLE (energy stocks as proxy).
- **Metals**: GLD (gold), SLV (silver), CPER (copper).
- **Agriculture**: DBA (broad), CORN, WEAT, SOYB.

### Futures-direct

For institutional implementations: directly trade futures (CL crude, GC gold, HG copper, etc.) for cheaper execution and more control.

## When the strategy works

- **Post-1973 oil shocks**: classic case of headline shock pass-through to core. Commodity hedges would have generated substantial returns.
- **2007–2008 commodity rally**: HI minus CI was wide for months. The hedge would have been profitable.
- **2021–2022 inflation surge**: HI ran 7–9%, CI lagged at 4–5%. The signal flagged commodity exposure aggressively. The 2022 commodity rally (oil >$120, broad commodities +30%) validated this.

## When it fails

- **Stagflation regimes** (1970s late stage): inflation high but commodities also weak as demand falls. The hedge captures less of the realised inflation.
- **Demand-shock inflation** (e.g., COVID early 2020): inflation rose due to supply shock, but commodities were also disrupted; the signal worked but with high volatility.
- **Deflationary shocks** (1998 Asian crisis, 2014–2015 oil collapse): the signal correctly went to zero, but the trade is "no hedge needed" rather than alpha.

## Practical issues

- **Data lag**: CPI is released monthly with a 2-week lag. The signal can be 2–6 weeks behind real-time conditions.
- **Negative signal periods**: when CI > HI (deflationary headline), the signal goes to zero. This means no commodity hedge — but if inflation is broadly low (Japan-style), the portfolio doesn't need much hedging anyway.
- **Rebalance frequency**: monthly is the natural cadence (CPI release schedule). More frequent rebalancing doesn't add value.
- **Tax efficiency**: commodity ETFs sometimes have unfavourable tax treatment (futures-based ETFs are taxed as 60/40 long/short-term, with K-1s in some cases). For taxable accounts, consider commodity equity stocks (XLE, GDX) as proxies.

## Variants

### Add other inflation-linked assets

Instead of pure commodity, use a basket:
- 60% broad commodities (DBC).
- 30% TIPS (inflation-linked Treasuries).
- 10% gold (GLD).

TIPS and gold are also inflation hedges; the blend reduces commodity-specific volatility.

### Asymmetric scaling

When commodity prices have already rallied substantially (e.g., 50%+ over 12 months), reduce CA even if the HI-CI signal is high — the inflation hedge is already in the price.

### Combine with macro momentum

The macro inflation hedge is one variable in a broader macro framework. Combining it with [[macro-fundamental-momentum|macro momentum]] gives a richer model: the inflation hedge tells you "how much commodity exposure," while macro momentum tells you "which countries' commodities should I be exposed to."

## Common mistakes

- **Treating commodity allocation as a permanent fixture**. The strategy is dynamic; CA varies from 0% to ~80% over time. Don't anchor on a fixed allocation.
- **Using point-in-time backtest data**. Use **vintage data** (what was known when the signal was generated). CPI revisions are real.
- **Ignoring transaction costs**. Monthly rebalancing of commodity ETFs has costs; choose ETFs with tight spreads.
- **Treating commodity index returns as a hedge**. Commodity futures roll costs (especially in contango) can erode the hedge. Choose ETFs with optimised roll strategies (PDBC, DBC).

## Risk management essentials

- **Max position size**: cap CA at 50–80% even if formula says higher; don't put 100% of portfolio in commodities.
- **Vol-target the commodity sleeve**: commodities have ~25%+ annualised vol; risk-budget appropriately.
- **Diversify across commodities**: don't load all into oil. Use broad indices or multi-commodity baskets.
- **Watch for commodity-specific tail events**: April 2020 negative WTI prices — the broad-index ETF avoided this, but single-commodity exposures were hit.

## Where to do this in the terminal

- **Economics screen** — CPI / Core CPI / spread time series.
- **Portfolio** — inflation-hedge overlay calculator with CA recommendation.
- **AI Quant Lab** — inflation-hedge strategy template.
- **Backtesting** — historical inflation-hedge P&L through major regimes (1970s, 2007–2008, 2021–2022).

## See also

- [[macro-fundamental-momentum|Fundamental Macro Momentum]] — the broader macro framework
- [[commodity-roll-yield|Commodity Roll Yield]] — the carry-yield mechanic of commodity exposure
- [[commodity-value-momentum|Commodity Value and Momentum]] — commodity factor strategies
- [[fi-bond-factors|Bond Factor Investing]] — TIPS as an inflation-linked asset
- [[portfolio-construction|Portfolio Construction]] — how to incorporate inflation hedges

## External references

- Fulli-Lemaire, N. (2013). "An Inflation Hedging Strategy with Commodities: A Core Driven Global Macro." *Journal of Investment Strategies* 2(3), 23–50.
- Bodie, Z. (1983). "Commodity Futures as a Hedge against Inflation." *Journal of Portfolio Management* 9(3), 12–17.
- Greer, R. (1978). "Conservative Commodities: A Key Inflation Hedge." *Journal of Portfolio Management* 4(4), 26–29.
- Gorton, G., Rouwenhorst, K. (2006). "Facts and Fantasies About Commodity Futures." *Financial Analysts Journal* 62(2), 47–68.
- Hamilton, J. (2003). "What Is an Oil Shock?" *Journal of Econometrics* 113(2), 363–398.
- Clark, T., Terry, S. (2010). "Time Variation in the Inflation Passthrough of Energy Prices." *Journal of Finance* 42(6), 1419–1433.
- Blanchard, O., Gali, J. (2007). "The Macroeconomic Effects of Oil Shocks: Why Are the 2000s so Different from the 1970s?" NBER working paper.
- Amenc, N., Martellini, L., Ziemann, V. (2009). "Inflation-Hedging Properties of Real Assets and Implications for Asset-Liability Management Decisions." *Journal of Portfolio Management* 35(4), 94–110.
- Bernanke, B., Kuttner, K. (2005). "What Explains the Stock Market's Reaction to Federal Reserve Policy?" *Journal of Finance* 60(3), 1221–1257.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §19.3. https://doi.org/10.1007/978-3-030-02792-6
