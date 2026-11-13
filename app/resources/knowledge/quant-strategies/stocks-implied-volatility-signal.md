# Implied Volatility Signal (Cross-Sectional)

This is the **lesser-known stock-level analogue of the [[volatility-quant|volatility risk premium]]**. The empirical finding (An, Ang, Bali, Cakici 2014; Cremers, Weinbaum 2010): stocks with **larger increases in call implied volatility** over the past month tend to have **higher future returns**, and stocks with **larger increases in put implied volatility** tend to have **lower future returns**.

Kakushadze & Serur (2018) §3.5 give the construction in one paragraph. The signal is widely used in factor-investing and option-informed equity strategies, though it has decayed materially since the early 2010s.

## The signal

For each stock with liquid options:

```
ΔIV_call_i  =  IV_call_i (today) − IV_call_i (1 month ago)
ΔIV_put_i   =  IV_put_i  (today) − IV_put_i  (1 month ago)
```

Where `IV_call` and `IV_put` are typically near-the-money, 1-month-to-expiry implied volatilities.

The strategy (§3.5):

```
1. Each month, rank stocks by ΔIV_call.
2. Long the top decile (largest call-IV increases).
3. Rank stocks by ΔIV_put.
4. Short the top decile (largest put-IV increases).
```

A variation uses the **difference**:
```
Spread_i  =  ΔIV_call_i − ΔIV_put_i
```
and longs the top spread decile.

## Why does this work?

The economic mechanism is **informed trading in options markets**.

### Information flow story

Informed traders often prefer options to stocks because:
- **Leverage** — they get more bang per dollar of capital.
- **Asymmetric payoff** — defined-loss on long options.
- **Information advantage** — they may not want to signal in the stock market through visible block trades.

When informed traders buy calls (because they expect the stock to rise), call IV rises. The stock hasn't moved yet — the information is in the option market first. The stock follows.

Same logic for puts: informed bearish traders push up put IV before the stock declines.

This is the **option-implied information leakage** story, supported by:

- **Easley, O'Hara, Srinivas (1998)**: informed traders use options for leverage.
- **Cremers & Weinbaum (2010)**: deviations from put-call parity (which is related to ΔIV_call − ΔIV_put) predict future stock returns.
- **An, Ang, Bali, Cakici (2014)**: the signal works across the full liquid options universe.

### Alternative interpretation — fundamental signal

The increase in call IV might reflect upcoming corporate events (earnings beat, merger, drug approval) that the market is already pricing into options. Stocks subject to these "binary events" see their option vol bid up *before* the event.

If the event is positive (which it tends to be when implied vol increases for upside-leveraged options), the stock rallies after the announcement.

## Empirical record

The signal was strong in the early 2000s. Modern data:

- **Cremers & Weinbaum (2010)**: long-short portfolios sorted on put-call IV spread earned ~50 bps per month, Sharpe ~0.6.
- **An, Ang, Bali, Cakici (2014)**: extended sample with similar magnitudes.
- **Post-2014**: the signal has decayed. Sharpe is now closer to 0.2 in academic samples, reflecting both crowding and the rise of HFT/options-market-making that arbitrages cross-market dislocations faster.

## Implementation details

- **Data**: requires implied volatility surface data — Bloomberg, OptionMetrics, IvyDB, or CBOE.
- **Universe**: liquid optionable stocks. The ~500 most-traded names.
- **IV measurement**: 30-day, ATM (delta ≈ 0.50) is standard. Other choices include 1m 25-delta calls and puts, which capture skew information.
- **Rebalance**: monthly. Faster rebalancing typically loses to transaction costs in option markets.

## Variants

### Option-implied skew

Define skew as `IV_put − IV_call` (or as ATM-vol minus OTM-vol). Stocks with **steepening skew** (puts getting relatively richer) tend to underperform.

### Realised-implied spread

Compare realised volatility to implied. Stocks where implied is higher than recent realised may be expected to disappoint; the opposite for stocks where implied is lower.

### Open interest changes

Combine ΔIV signals with changes in call/put open interest. A jump in both signals stronger informed interest.

## When the strategy fails

- **Earnings season clusters.** Around earnings, implied vol rises mechanically (uncertainty premium). The signal is contaminated unless you adjust for upcoming events.
- **M&A target effects.** Rumored takeover targets see IV rise across calls and puts. The signal sees this as bullish (calls IV up), but the actual stock outcome depends on whether the deal closes and at what price.
- **Crowding episodes.** Quantitative funds have run versions of this signal since the 2010s. Episodes of factor crowding (Aug 2007 "quant quake", early 2020) hit the signal hard.

## Practical issues

- **Liquidity of options**: many small-cap stocks have wide bid-ask spreads in options, making IV measurements noisy.
- **Day-of-week effects**: option IVs can mean-revert weekly due to weekend-decay effects. Measure IV at consistent times (close-to-close).
- **Anchoring to liquid expiries**: 1-month IV is most reliable. 1-week IV is too noisy; 3-month IV is slow.
- **Cross-section size**: at least 100 stocks needed to construct meaningful deciles.

## Common mistakes

- **Confusing IV level with IV change.** A high absolute IV doesn't predict returns; the *change* over the past month does. Some implementations get this backwards.
- **Ignoring event windows.** Filter out stocks announcing earnings, dividends, splits, or M&A in the upcoming 30 days. Those moves contaminate the signal.
- **Mixing call and put signals incorrectly.** ΔIV_call up is bullish; ΔIV_put up is bearish. The combined spread signal is much cleaner than either alone.
- **Using delta-50 options for skew signals.** Delta-50 misses the skew information. Use 25-delta puts and calls to capture skew, ATM for level.

## Risk management essentials

- **Cap concentration**: like other factor strategies, no single name > 2–3%.
- **Sector neutrality**: optional but improves Sharpe.
- **Filter out very-high-IV stocks**: stocks with IV > 100% are usually in distress or pre-binary-event. The signal is unreliable there.
- **Earnings filter**: exclude stocks with earnings in the next 30 days from the rebalance.

## Where to do this in the terminal

- **Derivatives screen** — IV surface data per stock; ΔIV columns for cross-sectional ranking.
- **AI Quant Lab** — implied-vol signal template with custom universe and lookback.
- **Surface Analytics** — single-stock IV surface visualisation.

## See also

- [[stocks-multifactor|Multifactor Portfolio]] — implied-vol combined with value, momentum, low-vol
- [[volatility-quant|Volatility Trading and the Surface]] — the broader volatility framework
- [[stocks-price-momentum|Price Momentum]] — diversifying signal
- [[strategy-patterns|Strategy Patterns]] — option-market information as a signal category

## External references

- An, B.-J., Ang, A., Bali, T., Cakici, N. (2014). "The Joint Cross Section of Stocks and Options." *Journal of Finance* 69(5), 2279–2337.
- Cremers, M., Weinbaum, D. (2010). "Deviations from Put-Call Parity and Stock Return Predictability." *Journal of Financial and Quantitative Analysis* 45(2), 335–367.
- Easley, D., O'Hara, M., Srinivas, P. (1998). "Option Volume and Stock Prices: Evidence on Where Informed Traders Trade." *Journal of Finance* 53(2), 431–465.
- Pan, J., Poteshman, A. (2006). "The Information in Option Volume for Future Stock Prices." *Review of Financial Studies* 19(3), 871–908.
- Bali, T., Hovakimian, A. (2009). "Volatility Spreads and Expected Stock Returns." *Management Science* 55(11), 1797–1812.
- Xing, Y., Zhang, X., Zhao, R. (2010). "What Does the Individual Option Volatility Smirk Tell Us About Future Equity Returns?" *Journal of Financial and Quantitative Analysis* 45(3), 641–662.
- Bollen, N., Whaley, R. (2004). "Does Net Buying Pressure Affect the Shape of Implied Volatility Functions?" *Journal of Finance* 59(2), 711–753.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.5. https://doi.org/10.1007/978-3-030-02792-6
