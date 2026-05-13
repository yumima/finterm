# Fundamental Macro Momentum

**Macro momentum** is the time-series momentum strategy applied at the **country/macro level**, using fundamental state variables — business cycle, international trade, monetary policy, and risk sentiment — to predict cross-asset returns. The strategy: rank countries by macro-trend signals, then go long the top quantile (across asset classes: equities, bonds, currencies) and short the bottom.

Kakushadze & Serur (2018) §19.2 give the construction. **Brooks (2017) "A Half Century of Macro Momentum"** (AQR working paper) is the canonical empirical reference, documenting the premium across 50+ years and 30+ markets.

## The four macro state variables

For each country, define four trend signals on real economic variables:

### 1. Business cycle trend

Estimated using the 1-year change in **real GDP growth** and **CPI inflation forecast**, each contributing 50% weight. A country with rising growth + declining inflation is in an accelerating cycle; the opposite is decelerating.

### 2. International trade trend

Estimated using 1-year changes in **spot FX rates** against an **export-weighted basket** of trading partners. Currency strengthening or weakening on a multi-year horizon reflects shifting competitive positioning.

### 3. Monetary policy trend

Estimated using **1-year changes in short-term rates**. A central bank that has been hiking (Fed 2022) shows monetary policy in a tightening trend; one cutting (most ZIRP-era central banks) is in an easing trend.

### 4. Risk sentiment trend

Estimated using **1-year equity market excess returns**. A market that has rallied for 12 months shows risk-on sentiment; one that has sold off shows risk-off.

## The strategy

```
1. For each country in the universe (~20 developed markets), compute the four
   state-variable trends.

2. For each asset class (equities, bonds, currencies, commodities):
   - Rank countries by each state variable.
   - Combine the ranks (typically equal-weighted average).
   - Long the top decile of countries; short the bottom decile.

3. Combine the asset-class portfolios with equal weights for the final book.

4. Hold for 3–6 months. Rebalance.
```

The book notes (§19.2 footnote 2): different asset classes respond to macro trends differently. Increasing growth is positive for equities and currencies but negative for bonds. So the *direction* of each state variable's effect varies by asset class.

## How each asset class responds to the trends

### Equity markets

- **Business cycle**: rising → long equities.
- **Trade trend** (FX strengthening): mixed — depends on whether the country is export-oriented or import-oriented.
- **Monetary policy**: hiking → typically short equities (cooling cycle).
- **Risk sentiment**: positive → long equities (momentum self-reinforces).

### Bond markets

- **Business cycle**: rising → short bonds (rates rise with growth).
- **Trade trend**: lesser direct effect.
- **Monetary policy**: hiking → short bonds (yields rise).
- **Risk sentiment**: positive → mixed; in risk-on, rates tend to rise (short bonds), but in risk-off bonds rally.

### Currencies (FX)

- **Business cycle**: rising → long the currency (capital flows in).
- **Trade trend**: continuing strength → long.
- **Monetary policy**: hiking → long currency (rate differential).
- **Risk sentiment**: positive → mixed (risk-on USD typically falls).

## Why does macro momentum work?

The economic case is the same as for [[trend-following|trend-following]] but at the macro level:

### 1. Slow information diffusion in macro

Macro variables (GDP, inflation, policy stances) move slowly. Markets price them with lags. A central bank that has been hiking for 12 months will likely continue hiking for several more months — the consensus catches up gradually.

### 2. Capital flows respond to fundamentals slowly

Pension funds, sovereign wealth funds, and asset allocators rebalance quarterly or annually. A country that has shown strong macro fundamentals will receive flows for years after the trend establishes.

### 3. Self-reinforcing momentum

A currency that has appreciated for 12 months attracts more carry-trade flows (it has higher real returns from FX appreciation + rate differentials), reinforcing the trend.

## Empirical record

Brooks (2017) tested macro momentum on:
- ~30 developed and emerging markets.
- 4 asset classes (equity, bond, FX, commodity proxies).
- 1968–2016 (a 50-year sample).

Results:
- **Annualised Sharpe**: 0.7–0.9 for the combined strategy.
- **Drawdowns**: typical worst-quarter ~10–15%.
- **Correlation with traditional 60/40 portfolio**: near zero.
- **Performance was relatively stable** across different decades, unlike some factor strategies that have decayed.

The strategy is offered as a managed-futures-style overlay by several investment managers (AQR, Bridgewater, Man Group).

## When macro momentum fails

- **Regime breaks**: 2008 financial crisis, 2020 COVID — sudden state-variable resets that catch the strategy long old trends.
- **Central-bank intervention**: 2010–2015 QE created persistent low-vol regimes that disrupted some trends.
- **Inflation regime changes**: 2021–2022 transition from low-inflation to high-inflation caused some macro signals (especially bond signals) to whipsaw.
- **EM crises**: idiosyncratic political/financial crises in EM (Argentina 2018, Turkey 2018) trigger sharp reversals.

## Combining macro momentum with other strategies

The natural pairings:

- **+ Trend following on prices**: macro fundamentals reinforce price trends; combining the two filters out noise.
- **+ Carry strategies**: macro variables identify which carry trades are sustainable.
- **+ Value strategies**: when macro is supportive and the country is also cheap (deep value + accelerating cycle), high conviction.

## Variants

### Equal-weighted vs. risk-weighted combination

Equal-weighting across state variables is robust but loses information about correlations. Risk-parity weighting across variables can improve Sharpe at the cost of complexity.

### Faster vs. slower signals

The 1-year horizon is the canonical choice. Faster (3-month) signals are more responsive but more noisy. Slower (3-year) signals are more stable but miss regime shifts.

### Single-asset-class macro momentum

Equity-only or FX-only versions are simpler and avoid the multi-asset combination complexity. Each has its own academic literature (e.g., Asness, Moskowitz, Pedersen 2013 covers cross-asset value and momentum).

## Practical implementation

- **Universe**: 20–30 developed + EM countries with reliable macro data.
- **Data source**: IMF, OECD, World Bank, Bloomberg for the macro variables.
- **Rebalance**: quarterly is the standard. Monthly is too frequent (data revisions); annually is too slow.
- **Instruments**: country-equity ETFs (EWG, EWJ, EWZ), 10-year bond futures or country-bond ETFs (TIP, BNDX), FX futures.
- **Transaction costs**: relatively low for liquid country ETFs and futures. The strategy is capacity-friendly.

## Common mistakes

- **Using nominal GDP growth instead of real**: nominal contains inflation; the strategy needs the real cycle component.
- **Treating EM and DM identically**: EM has higher vol, different macro dynamics, more political risk. Often a separate model is appropriate.
- **Re-estimating signals too frequently**: quarterly is the sweet spot. Monthly recomputation generates excess turnover.
- **Ignoring data revisions**: macro data gets revised. Use point-in-time data (what was known when the trade was made), not revised data.

## Risk management essentials

- **Per-country concentration cap**: 15–20% maximum per country in the cross-asset book.
- **Asset-class diversification**: equal-weight across equity, bond, FX legs.
- **Drawdown trigger**: reduce gross exposure when trailing-12-month return is materially negative.
- **Stress-test regime shifts**: 2008, 2020 are the relevant case studies.

## Where to do this in the terminal

- **Economics screen** — country-by-country macro data (GDP, CPI, rates).
- **AI Quant Lab** — macro momentum strategy template with state-variable selection.
- **Backtesting** — historical macro momentum P&L with regime decomposition.

## See also

- [[trend-following|Trend Following (TSMOM)]] — the price-trend cousin
- [[etf-multi-asset-trend|Multi-Asset Trend on ETFs]] — operational version on ETFs
- [[macro-inflation-hedge|Global Macro Inflation Hedge]] — sister macro strategy
- [[macro-economic-announcement|Trading on Economic Announcements]] — event-driven macro
- [[fx-carry-trade|FX Carry Trade]] — natural diversifier

## External references

- Brooks, J. (2017). "A Half Century of Macro Momentum." AQR working paper. https://www.aqr.com/-/media/AQR/Documents/Insights/White-Papers/A-Half-Century-of-Macro-Momentum.pdf
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Moskowitz, T., Ooi, Y., Pedersen, L. (2012). "Time Series Momentum." *Journal of Financial Economics* 104(2), 228–250.
- Bernanke, B., Kuttner, K. (2005). "What Explains the Stock Market's Reaction to Federal Reserve Policy?" *Journal of Finance* 60(3), 1221–1257.
- Clarida, R., Waldman, D. (2007). "Is Bad News About Inflation Good News for the Exchange Rate?" NBER working paper.
- Eichenbaum, M., Evans, C. (1995). "Some Empirical Evidence on the Effects of Shocks to Monetary Policy on Exchange Rates." *Quarterly Journal of Economics* 110(4), 975–1009.
- Hurst, B., Ooi, Y. H., Pedersen, L. (2017). "A Century of Evidence on Trend-Following Investing." *Journal of Portfolio Management* 44(1), 15–29.
- Cochrane, J., Piazzesi, M. (2005). "Bond Risk Premia." *American Economic Review* 95(1), 138–160.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §19.2. https://doi.org/10.1007/978-3-030-02792-6
