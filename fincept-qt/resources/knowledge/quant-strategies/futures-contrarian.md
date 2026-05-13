# Futures Contrarian (Mean-Reversion) Trading

The cross-sectional mean-reversion strategy from equities (see [[mean-reversion|Mean Reversion]]) translates directly to futures markets: long the worst-performing futures contracts, short the best-performing ones, with the expectation that recent extreme moves will partially reverse.

Kakushadze & Serur (2018) §10.3 give the construction. The strategy works in futures for similar reasons it works in stocks — microstructure noise, position-unwinding pressure, and behavioural overreaction — but with the important difference that the futures universe is much smaller (~20–40 actively-traded contracts), so the statistical edge per name is critical.

## The signal — cross-sectional mean reversion

For each futures contract `i` over the past week, compute the return `R_i`. Then:

```
Market index return:
  R_m  =  (1/N) · Σ_i R_i                          (Eq. 10.7)

Position weights (proportional to negative deviation from market):
  w_i  =  −γ · (R_i − R_m)                          (Eq. 10.8)

Normalisation:
  Σ |w_i|  =  1                                     (Eq. 10.9)
```

The position weights are:
- **Long** for futures that underperformed the average (`R_i < R_m`).
- **Short** for futures that outperformed (`R_i > R_m`).

The strategy is **automatically dollar-neutral** because the demeaning ensures the sum of weights is zero.

## Why this works

The same three explanations as single-stock mean reversion ([[mean-reversion|see Mean Reversion]]):

### 1. Microstructure noise

End-of-day flows, exchange-driven rebalancing (index roll dates), and large speculative trades can push a single futures contract away from fair value temporarily. The next-day reversion captures that.

In futures specifically, this is amplified by:
- **CTA herding**: when multiple CTAs hit the same direction, they push prices further; when they reduce risk, prices snap back.
- **Index roll mechanics**: USO, GSCI, BCOM all roll on specific dates; the resulting front-contract pressure creates predictable patterns.

### 2. Behavioural overreaction

Same as in equities — futures traders can over-react to news. Subsequent reversion is the unwinding of the overreaction.

### 3. Position-limit-induced unwinds

When a fund hits its position limit on a profitable contract, they begin unwinding — leading to mean reversion.

## Variants

### Basic strategy (§10.3 Eq. 10.8)

Equal weighting on the demeaned returns. Simple, robust.

### Vol-suppressed weights

```
w_i  =  −γ · (R_i − R_m) / σ_i           or
w_i  =  −γ · (R_i − R_m) / σ_i²
```

The first is risk-parity-like; the second is closer to Sharpe-optimal under a diagonal covariance assumption. Both prevent the strategy from overweighting volatile futures (oil, nat gas) at the expense of low-vol contracts (Treasuries).

### Filtered by volume and open interest (book "Contrarian Trading—Market Activity")

The book adds an extra filter: combine the cross-sectional reversion signal with volume and open-interest changes.

```
Define week-over-week changes:
  v_i  =  ln(V_i / V'_i)                                          (Eq. 10.10)
  u_i  =  ln(U_i / U'_i)                                          (Eq. 10.11)
```

Where `V_i` is total volume over the last week, `V'_i` is the prior week's, `U_i` is open interest, `U'_i` is the prior week's.

The strategy:
- Take the **upper half of futures** by volume factor `v_i` (large recent volume changes — likely overreaction candidates).
- Within those, take the **lower half by open-interest factor `u_i`** (low open interest indicates lower market depth, where price impact is bigger).
- Apply the basic mean-reversion strategy (Eq. 10.8) to this filtered subset.

The rationale (book footnote 11):
- Large volume changes → overreaction → bigger snap-back.
- Lower open-interest changes (less market depth) → larger price impacts to absorb → more reversion.

This double filter targets futures most likely to be mean-reverting in the next week.

## Variants from the broader literature

### Multi-horizon mean reversion

Use 1-week, 2-week, 1-month reversion signals combined. Different horizons capture different types of overreaction.

### Sector-relative mean reversion

Group futures by sector (energy, metals, ags, financials, FX). Compute reversion within each sector. Combine across sectors. Captures sector-specific overreaction patterns.

### Combined with momentum (Bianchi, Drew, Fan 2015)

The book has both contrarian (§10.3, weekly reversion) and trend following (§10.4, monthly momentum) for futures. Combining them — using shorter-horizon mean reversion as an entry filter for longer-horizon trend — improves Sharpe over either alone.

## Empirical record

In commodity futures (mostly):

- **Cross-sectional weekly reversion**: Sharpe ~0.4–0.6 in academic samples.
- **With volume/OI filter**: Sharpe improvement to ~0.6–0.8.
- **Combined with momentum**: combined Sharpe ~0.8–1.0.

In FX futures the signal is weaker (FX is closer to a random walk at the weekly horizon).
In bond futures the signal is weak (rates move slower; weekly reversion is less reliable).

## When the strategy fails

- **Strong directional trends.** When commodities are in a sustained move (2008 oil, 2020 COVID, 2022 inflation), the reversion signal fails — what looks like overreaction is actually persistent trend.
- **Liquidity crises**. March 2020: many futures temporarily decoupled from fundamentals; reversion signals fired but didn't reverse for weeks.
- **Curve-shape distortions**. When the futures curve is distorted (e.g., due to storage issues or supply disruptions), reversion can flip into trend.
- **Major news / event days**. Reversion strategies are noise-trading strategies; they don't work when news dominates.

## Capacity considerations

The futures universe is small (~30–50 liquid contracts globally). The strategy has **limited capacity** compared to equity mean reversion (which has ~3000 names). A $1B fund running this systematically would significantly move prices.

For institutional implementation:
- Trade only the most-liquid 20–25 contracts.
- Size position to a fraction of average daily volume.
- Diversify across sectors to avoid concentration.

## Practical implementation

- **Universe**: 20–40 liquid commodity, FX, and bond futures.
- **Lookback**: 1 week (5 trading days). Some implementations use 2 weeks.
- **Rebalance**: weekly. Daily rebalancing increases turnover without improving Sharpe meaningfully.
- **Position sizing**: vol-targeted; risk-parity-weighted across contracts.
- **Filter**: liquidity floor (e.g., > 10,000 contracts/day average volume).

## Common mistakes

- **Equal-dollar weights without vol adjustment.** Crude oil's 2% daily move is very different from US bond's 0.3% daily move. Vol-scale.
- **Not handling roll dates.** When a contract rolls, the apparent "return" includes the roll yield. Strip out roll P&L from the signal.
- **Ignoring open-interest information.** As the book shows, OI changes carry signal. Most simple implementations skip this.
- **Mean-reversion in trending regimes.** Without a trend filter, the strategy bleeds during sustained moves.

## Risk management essentials

- **Vol-target the book**: realised vol must stay within bounds.
- **Per-contract concentration cap**: no single contract more than 15–20% of book.
- **Trend filter**: combine with a longer-horizon trend signal; reduce or reverse exposure when trend is strong.
- **News / event-day flag**: reduce position during major scheduled events (OPEC meetings, USDA reports, FOMC).
- **Sector diversification**: spread risk across energy / metals / ags / financials.

## Where to do this in the terminal

- **Futures screen** — cross-sectional reversion rankings; volume/OI columns.
- **AI Quant Lab** — futures contrarian template with volume/OI filter.
- **Backtesting** — historical contrarian P&L with trend-regime decomposition.

## See also

- [[mean-reversion|Mean Reversion (Single-Name)]] — the equity analogue
- [[trend-following|Trend Following (TSMOM)]] — the natural diversifier
- [[etf-multi-asset-trend|Multi-Asset Trend on ETFs]] — combined strategy framework
- [[commodity-value-momentum|Commodity Value and Momentum]] — the commodity-specific factor pair
- [[futures-calendar-spread|Calendar Spread]] — another curve-related trade

## External references

- Bessembinder, H. (1992). "Systematic Risk, Hedging Pressure, and Risk Premiums in Futures Markets." *Review of Financial Studies* 5(4), 637–667.
- Bessembinder, H., Coughenour, J., Seguin, P., Smoller, M. (1995). "Mean Reversion in Equilibrium Asset Prices: Evidence from the Futures Term Structure." *Journal of Finance* 50(1), 361–375.
- Bianchi, R., Drew, M., Fan, J. (2015). "Combining Momentum with Reversal in Commodity Futures." *Journal of Banking & Finance* 59, 423–444.
- Fuertes, A., Miffre, J., Rallis, G. (2010). "Tactical Allocation in Commodity Futures Markets: Combining Momentum and Term Structure Signals." *Journal of Banking & Finance* 34(10), 2530–2548.
- Irwin, S., Zulauf, C., Jackson, T. (1996). "Monte Carlo Analysis of Mean Reversion in Commodity Futures Prices." *American Journal of Agricultural Economics* 78(2), 387–399.
- Leung, T., Li, J., Li, X., Wang, Z. (2016). "Speculative Futures Trading Under Mean Reversion." *Asia-Pacific Financial Markets* 23(4), 281–304.
- Monoyios, M., Sarno, L. (2002). "Mean Reversion in Stock Index Futures Markets." *Journal of Futures Markets* 22(4), 285–314.
- Wang, C., Yu, M. (2004). "Trading Activity and Price Reversals in Futures Markets." *Journal of Banking & Finance* 28(6), 1337–1361.
- Chaves, D., Viswanathan, V. (2016). "Momentum and Mean-Reversion in Commodity Spot and Futures Markets." *Journal of Commodity Markets* 3(1), 39–53.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §10.3. https://doi.org/10.1007/978-3-030-02792-6
