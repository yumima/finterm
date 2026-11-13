# ETF Mean Reversion (Internal Bar Strength)

Most mean-reversion strategies on equity ETFs use price-vs-moving-average signals. **Internal Bar Strength (IBS)** is a much simpler signal that has been documented to work consistently on broad-market and country ETFs since the 1990s: rank ETFs by where the closing price sits within the previous day's high-low range, and bet on reversal.

Kakushadze & Serur (2018) §4.4 give the construction in one paragraph. **Pagonidis (2014) "The IBS Effect: Mean Reversion in Equity ETFs"** is the canonical empirical paper.

## The signal

For each ETF, using the previous day's close `P_C`, high `P_H`, and low `P_L`:

```
IBS  =  (P_C − P_L) / (P_H − P_L)                       (Eq. 4.10)
```

IBS is bounded in [0, 1]:
- `IBS = 0` — the close was at the low of the day (weak close, ETF is "cheap").
- `IBS = 1` — the close was at the high of the day (strong close, ETF is "rich").
- `IBS = 0.5` — the close was the midpoint of the day's range.

The strategy:

```
1. Each day, rank ETFs by IBS.
2. Long ETFs in the bottom decile (close near the day's low).
3. Short ETFs in the top decile (close near the day's high).
4. Hold for 1–5 days. Rebalance.
```

For a long-only version: long ETFs with IBS < 0.2, hold until IBS > 0.5.

## Why this works

The economic story: **a weak close in an ETF reflects end-of-day selling pressure** that is often disconnected from underlying fundamentals. The reverse — strong close, IBS near 1 — reflects buying pressure that also tends to revert. Sources of this end-of-day pressure:

- **MOC (market-on-close) order imbalances** from rebalancing programs.
- **Retail panic selling** during the last hour of the trading day.
- **Index-tracking flows** in the closing auction.
- **Algorithmic profit-taking** at end-of-day.

In single stocks, this kind of microstructure effect is washed out by stock-specific news. In broad-market or country ETFs (where idiosyncratic news is rare), the end-of-day flow effect is more visible and more reliably mean-reverting.

## Empirical record

**Pagonidis (2014)** tested IBS-based mean reversion on a large universe of equity ETFs:

- Average daily return ~5–10 bps on low-IBS portfolios held for 1 day.
- Sharpe of 1.5–2.5 in clean academic samples (gross).
- Net of costs (assuming 1–2 bps each way), Sharpe drops to ~0.8–1.5.
- The effect is **stronger in country ETFs** (EWJ, EWZ, EWG, EWA) than in S&P sector ETFs.

The effect has persisted since at least the early 2000s, suggesting it captures a real microstructure inefficiency that hasn't been arbitraged away.

## Why country ETFs especially?

Country ETFs have a unique characteristic: **the underlying market is closed when the ETF trades**. EWJ (Japan) trades on NYSE; when NYSE is open, Tokyo is closed. The ETF price during US hours is a *forecast* of where Japanese stocks will open the next day.

This forecast is imperfect. End-of-US-day flows in EWJ create dislocations that revert when:
- The US market opens the next day with a more informed view.
- Tokyo opens and trades into actual Japanese stocks, which feed back to the ETF arbitrage mechanism.

So the IBS signal in country ETFs captures a **time-zone-induced mean-reversion**: today's close was distorted by US flow; tomorrow morning, prices revert toward fair value as the home market trades.

## Variants

### Symmetric scale

A more symmetric version of IBS:
```
Y  =  IBS − 1/2  =  (P_C − P_*) / (P_H − P_L)
```
where `P_* = (P_H + P_L) / 2`. This ranges from −1/2 to +1/2, with zero at the day's midpoint.

### Multi-day IBS

Use a 2-day or 5-day rolling IBS to smooth the signal. Reduces noise but slows the entry.

### Combined with RSI

Combine IBS (intraday positioning) with the 2-day RSI (short-horizon momentum). The combination filter — IBS < 0.2 AND RSI < 30 — generates fewer signals but with higher hit rate.

### Sector-relative IBS

Demean IBS by sector or country group. Captures within-group mean reversion rather than against the entire universe.

## When the strategy fails

- **Trending days at the start of major moves**. If today's IBS is 0.05 because the ETF crashed 4% on a real macro news event (e.g., flash crash, surprise central-bank announcement), tomorrow's open will continue the move, not reverse.
- **Earnings or central-bank weeks**. For country ETFs, days surrounding their home central bank meetings (BOJ for EWJ, ECB for EZU) have unreliable IBS reversion.
- **Low-volume days**. IBS is a microstructure signal. On low-volume days (early December, summer holidays), the signal is noisier.
- **Bid-ask spread costs**. For less liquid country ETFs (e.g., EIDO Indonesia, EZA South Africa), 2-3 bps round-trip spread eats much of the alpha.

## Practical issues

- **Liquidity filter**: stick to ETFs with > $500M AUM and > 1M shares average daily volume.
- **Survivorship**: include de-listed ETFs in backtests. Some country ETFs were liquidated (e.g., some single-country small ETFs); excluding them biases backtests upward.
- **Holding period**: 1-day held positions capture most of the reversion. Longer holds add noise.
- **Rebalance time**: enter at the close (using today's IBS measured at close), exit at next day's close. The signal lives intraday-to-overnight.

## Common mistakes

- **Using IBS on individual stocks.** Single-stock IBS is contaminated by stock-specific news; the signal doesn't work cleanly. The book and Pagonidis specifically applied this to broad and country ETFs.
- **Ignoring overnight gap risk.** IBS-based mean reversion is held overnight. The strategy is exposed to overnight news. Position-size accordingly.
- **Equal-weighting across very different ETFs.** EWJ vs. EWZ have very different volatilities. Vol-scale.
- **Backtesting with mid-prices instead of bid/ask.** The strategy's edge is on the order of 5–10 bps per day. Mid-price backtests miss the bid-ask cost and overstate Sharpe.

## Risk management essentials

- **Stop-loss on next-day open**: if the ETF gaps against you by more than 2 standard deviations, exit at market open.
- **Diversify across country/sector**: don't load up on 5 country ETFs from the same region.
- **Event-day filter**: exclude rebalance days, major central-bank-meeting days, and earnings season for sector ETFs.
- **Vol-target**: realised vol of the IBS book can spike during turbulent weeks; scale position size to a target daily vol.

## Where to do this in the terminal

- **AI Quant Lab** — IBS mean-reversion template for ETF universe.
- **ETF screen** — daily IBS column, cross-sectional ranking.
- **Backtesting** — IBS strategy with overnight-gap analysis and bid-ask cost modelling.

## See also

- [[mean-reversion|Mean Reversion (Single-Name)]] — the broader framework
- [[etf-sector-rotation|ETF Sector Rotation]] — the momentum counterpart
- [[pairs-trading|Pairs Trading]] — another mean-reversion approach
- [[strategy-patterns|Strategy Patterns]] — where mean reversion fits

## External references

- Pagonidis, A. (2014). "The IBS Effect: Mean Reversion in Equity ETFs." Working paper. http://www.naaim.org/wp-content/uploads/2014/04/00V_Alexander_Pagonidis_The-IBS-Effect-Mean-Reversion-in-Equity-ETFs-1.pdf
- Levy, A., Lieberman, O. (2013). "Overreaction of Country ETFs to US Market Returns: Intraday vs. Daily Horizons and the Role of Synchronized Trading." *Journal of Banking & Finance* 37(5), 1412–1421.
- Marshall, B., Nguyen, N., Visaltanachoti, N. (2013). "ETF Arbitrage: Intraday Evidence." *Journal of Banking & Finance* 37(9), 3486–3498.
- Cole, R., Daniel, K., Titman, S. (1996). "Underreaction to Self-Selected News Events." *Working paper.*
- Avellaneda, M., Lee, J.-H. (2010). "Statistical Arbitrage in the U.S. Equities Market." *Quantitative Finance* 10(7), 761–782.
- Bowen, D., Hutchinson, M. (2016). "Pairs Trading in the UK Equity Market: Risk and Return." *European Journal of Finance* 22(14), 1363–1387.
- Madhavan, A. (2016). *Exchange-Traded Funds and the New Dynamics of Investing*. Oxford University Press.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §4.4. https://doi.org/10.1007/978-3-030-02792-6
