# Leveraged ETF Decay (Short Both Sides)

A **leveraged ETF (LETF)** promises 2× or 3× the *daily* return of an underlying index. Through a mathematical property of compounded volatility, LETFs systematically **decay against their stated multiple over multi-day horizons**. The decay is highest when the underlying is choppy; it's lowest when the underlying is trending steadily.

The strategy: **short both a leveraged long ETF and a leveraged short ETF** on the same underlying. Both legs decay over time. The combined short position profits from the compounded decay regardless of direction.

Kakushadze & Serur (2018) §4.5 give this in two sentences. The strategy works in theory and works in calm markets; it occasionally blows up in trending markets when one of the short legs goes against you violently.

## The mathematics of LETF decay

For a 2× LETF tracking index `I`:

```
LETF daily return  =  2 × I daily return

After T days:
LETF return  ≈  2 × (cumulative I return)  −  (T × σ_I²)

(approximate, in continuous time)
```

The `(T × σ_I²)` term is the **volatility decay**. For T = 252 trading days and σ_I = 16% annualised (1% daily vol), the annual decay is roughly:

```
Decay ≈ 252 × (0.01)² = 2.52% per year
```

For a 3× LETF, the multiplier on σ² becomes 9 vs the 4 for 2×, so 3× LETFs decay more steeply.

### The intuition

The compounded daily 2× return is not the same as 2× the cumulative return:

```
Day 1: index +5%  → 2× LETF +10%
Day 2: index −5%  → 2× LETF −10%

Cumulative index: (1.05)(0.95) − 1 = −0.25%.
2× of cumulative: −0.50% (the "expected" cumulative).
Actual LETF: (1.10)(0.90) − 1 = −1.00%.

Decay: 1.00% − 0.50% = 0.50% in 2 days.
```

The more choppy the path, the worse the decay relative to 2×-the-cumulative.

## The strategy — short both legs

For a given underlying index (e.g., S&P 500), there is typically:

- A 2× or 3× **long-leveraged ETF**: SSO (2× S&P), UPRO (3× S&P).
- A 2× or 3× **inverse-leveraged ETF** (which is structurally short): SDS (-2× S&P), SPXU (-3× S&P).

The strategy:

```
1. Short equal-dollar positions of both the leveraged long and leveraged short ETFs.
2. Invest the short-sale proceeds into a Treasury ETF or money market.
3. Hold long-term. Rebalance periodically.
4. Each leg decays; the combined short captures the decay.
```

### Why this isn't just a free arbitrage

Two reasons:

1. **Short-borrow cost.** Borrowing both LETFs costs you. Typical borrow rate on volatile LETFs is 50–200 bps annualised. This eats much of the decay.

2. **Asymmetric tail risk.** When the underlying trends sharply in one direction:
   - The leveraged-long ETF rallies 50%+ in a quarter (e.g., March–May 2020).
   - You're short it — you take a heavy loss.
   - The leveraged-short ETF crashes, so you make on that side.
   - **But the asymmetry of compounding means your gains on the crashing side are bounded** (the ETF can only go to zero) while losses on the rallying side are theoretically unbounded.

So shorting both legs is a **carry trade with a left-tail** — typical of "selling vol" strategies.

## Empirical record

In calm-to-normal markets, the strategy works steadily. In **trending markets**, it can suffer serious drawdowns:

- **2010–2013** (steady bull market in equities): the strategy worked well. ~5–8% annualised net of borrow costs on the SSO/SDS pair.
- **2017** (low-vol bull market): great year for the strategy. Decay was high relative to flat outcomes.
- **March 2020** (COVID crash + recovery): the leveraged long (SPXL, UPRO) crashed 70%+, then partially recovered. The short side made huge profits initially; then losses when the rebound happened. Net result depended on timing, but the strategy was very lumpy.
- **2022 bear market**: leveraged longs crashed; leveraged shorts rallied. Short-both worked, but with a deep drawdown mid-year before recovering.

Avellaneda & Zhang (2010) "Path-Dependence of Leveraged ETF Returns" formalised this analysis and is the academic foundation.

## Variants

### Short LETF only, hedged with the underlying

Instead of shorting both 2× long and 2× short:

```
Short the 2× long ETF.
Hedge with 1× short of the underlying (e.g., short SPY).
```

The hedge cancels the directional exposure of the 2× long; only the decay remains. Simpler than shorting both LETFs but requires daily rebalancing of the hedge (the 2× ETF's effective beta changes daily).

### Calendar-spread on LETFs

Some LETFs use options instead of futures for their leverage. Calendar effects in those options create predictable patterns. Trading the calendar is a specialist strategy.

### LETF on volatility ETFs (VXX-style)

VXX, UVXY, SVXY are leveraged ETFs/ETNs tracking VIX futures. They have **much higher decay** than equity LETFs because the VIX futures curve is usually in contango.

UVXY (1.5× VXX) historically lost ~80–90% per year in calm vol regimes. Shorting it (when borrow is available) was extremely profitable until February 2018, when XIV (an inverse VIX ETN) lost 96% in one day and was liquidated.

The lesson: **vol-related LETFs have catastrophic tail risk**. Don't run shorts on them without strict stop-losses and hedging.

## When the strategy works and fails

| Market state | Strategy outcome |
|---|---|
| Sideways / choppy (high realised vol, low directional move) | **Best regime**. Decay is high, both legs lose. |
| Slow grind (low realised vol, mild trend) | **Decent**. Decay is modest but positive. |
| Steady bull market (low realised vol, strong trend up) | **Risky**. The leveraged long rallies and decay can be smaller than mark-to-market loss on the short. |
| Steady bear market (high vol, strong trend down) | **Risky**. The leveraged short rallies sharply. |
| Crash + recovery (March 2020 pattern) | **Catastrophic mid-trade**. The leveraged short rallies first, leveraged long crashes; then both move in reverse, generating huge realised volatility in the P&L. |

## Common mistakes

- **Treating LETFs as buy-and-hold longs**. They are not. They are designed for daily rebalancing and decay against their multiple over time. The decay can be 5–10% per year on equity LETFs; 30–90% on vol LETFs.
- **Underestimating short-borrow cost**. Volatile LETFs (especially small-AUM ones) have high borrow rates. Some are "hard to borrow" and shorting is unavailable.
- **Ignoring tax treatment**. LETF dividends are often substantial (because the ETF generates trading income); shorting them means paying these dividends. Tax-treated as ordinary income, not qualified dividends.
- **Equal-notional sizing vs. equal-beta sizing**. When the underlying moves, the 2× long's effective leverage changes. Equal notional doesn't stay duration-neutral.
- **Holding through earnings or central bank decisions**. Volatility spikes; both legs become very volatile; the trade is much riskier.

## Risk management essentials

- **Position size**: small. Maybe 5–10% of book max. Even when the strategy is working, mark-to-market is volatile.
- **Stop-loss**: hard. If the underlying moves 10%+ in one direction in a short period, exit. The decay strategy needs time and choppiness.
- **Avoid vol-related LETFs without hedging.** The 2018 XIV liquidation is the warning.
- **Diversify across underlyings**: shorting LETFs on different uncorrelated indices (S&P, Nasdaq, Russell, gold) diversifies the path risk.
- **Monitor borrow costs**: if borrow rate spikes, exit. The carry has flipped.

## Where to do this in the terminal

- **ETF screen** — leveraged ETF universe with borrow rates and historical decay metrics.
- **Backtesting** — LETF-decay strategy with realistic borrow-cost modelling and crash-period replay.
- **Algo Trading** — execution module for delta-hedged LETF shorts.

## See also

- [[volatility-quant|Volatility Trading]] — the broader vol-selling context (LETF decay is a form of selling vol)
- [[etf-sector-rotation|ETF Sector Rotation]] — directional ETF strategies
- [[strategy-patterns|Strategy Patterns]] — decay/carry as a strategy archetype

## External references

- Avellaneda, M., Zhang, S. (2010). "Path-Dependence of Leveraged ETF Returns." *Journal on Financial Mathematics* 1(1), 586–603.
- Cheng, M., Madhavan, A. (2010). "The Dynamics of Leveraged and Inverse Exchange-Traded Funds." *Journal of Investment Management* 7(4), 43–62.
- Tang, H., Xu, X. (2013). "Solving the Return Deviation Conundrum of Leveraged Exchange-Traded Funds." *Journal of Financial and Quantitative Analysis* 48(1), 309–342.
- Charupat, N., Miu, P. (2011). "The Pricing and Performance of Leveraged Exchange-Traded Funds." *Journal of Banking & Finance* 35(4), 966–977.
- Jiang, X., Peterburgsky, S. (2017). "Investment Performance of Shorted Leveraged ETF Pairs." *Applied Economics* 49(44), 4410–4427.
- Jarrow, R. (2010). "Understanding the Risk of Leveraged ETFs." *Finance Research Letters* 7(3), 135–139.
- Tuzun, T. (2013). "Are Leveraged and Inverse ETFs the New Portfolio Insurers?" *FEDS Working Paper* 2013-48.
- Trainor, W. (2010). "Do Leveraged ETFs Increase Volatility?" *Technology and Investment* 1(3), 215–220.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §4.5. https://doi.org/10.1007/978-3-030-02792-6
