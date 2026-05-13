# Multi-Asset Trend Following on ETFs

**Trend following** is one of the few systematic strategies with a **multi-century track record** (Hurst, Ooi, Pedersen 2017 "A Century of Evidence on Trend-Following Investing"). The traditional implementation uses futures across asset classes. The ETF version achieves nearly the same exposure with **far less operational complexity** — buying and selling a dozen ETFs instead of managing futures rolls, margin calls, and contract specifications.

Kakushadze & Serur (2018) §4.6 give the ETF-trend construction in one page. This entry covers the broader trend-following context and the specific operational differences between the futures and ETF implementations.

## The strategy

```
1. Define a universe of ETFs across asset classes:
   - Equity (broad market: SPY, ACWX, VEA, VWO)
   - Equity (sector: XLE, XLF, XLK, ...)
   - Bonds (TLT, IEF, AGG, HYG)
   - Real estate (VNQ, REM)
   - Commodities (GLD, USO, DBC, DBA)
   - Currencies (UUP, FXE, FXY) — optional

2. For each ETF, compute trailing T-month cumulative return:
   R^cum_i  over T = 6–12 months                        (Eq. 4.1 reused)

3. Optionally apply moving-average filter:
   Only include ETFs where P_i > MA_i(T'), T' = 100–200 days.

4. From the filtered set, take ETFs with R^cum_i > 0.
   For each, assign a weight:
       w_i ∝ R^cum_i             (returns-weighted)        (Eq. 4.11)
       w_i ∝ R^cum_i / σ_i       (vol-adjusted)             (Eq. 4.12)
       w_i ∝ R^cum_i / σ_i²      (Sharpe-optimal, diagonal C)  (Eq. 4.13)
   Normalise so Σ w_i = 1.

5. Hold for 1 month. Rebalance.
```

For a long-short version, add short positions in ETFs with negative `R^cum`.

## Why does this work?

Trend-following has the most empirical evidence of any systematic premium. **Moskowitz, Ooi, Pedersen (2012) "Time-Series Momentum"** and **Hurst, Ooi, Pedersen (2017)** are the canonical references.

The premium has been measured:

- **Across 67 markets** spanning equities, bonds, commodities, currencies.
- **From 1880 to 2016** (yes, 136 years).
- **In every decade** of that sample.

Net of costs, an equal-volatility-weighted multi-asset TSMOM strategy has delivered Sharpe ~0.7–1.0 over the full sample. Outsized returns occurred during sustained crises (1929 crash, 1973–74 stagflation, 2000–02 bear market, 2008 GFC).

### The economic explanations

Three complementary stories:

1. **Slow information diffusion across markets**. A macro shift (oil shock, recession, central-bank pivot) doesn't price into all assets instantaneously. Trend-following rides the staged adjustment.

2. **Behavioral underreaction**. **Hong & Stein (1999)** model: investors anchor on prior beliefs; new information moves prices in steps. Trend-following captures the staged response.

3. **Risk-aversion shocks**. **Asness, Moskowitz, Pedersen (2013)** argue that systematic risk-aversion shocks (e.g., flight to quality during 2008) generate trends in safe-haven assets that trend-following captures.

## Why use ETFs instead of futures

The classical CTA / managed-futures version trades futures: ES (S&P), TY (10y Treasury), CL (crude oil), GC (gold), DX (USD), etc. Why use ETFs instead?

| | Futures | ETFs |
|---|---|---|
| **Capital efficiency** | Excellent (futures use margin) | Lower (ETFs need full notional) |
| **Operational complexity** | High (rolls, margin calls, contract specs) | Low (just buy and sell shares) |
| **Tax treatment** | 60/40 for many; complex | Standard short/long-term cap gains |
| **Liquidity** | Excellent in majors; thin in many minors | Generally good in major ETFs |
| **Access** | Requires futures account, margin clearing | Available in any brokerage |
| **Costs** | Low (a few bps round-trip in liquid contracts) | Slightly higher (5–10 bps round-trip in liquid ETFs) |
| **Universe breadth** | Wider (every futures contract) | Narrower (~50 liquid investable ETFs) |

The ETF version is roughly 80% of the futures version's Sharpe but is **operationally far simpler**. For individual investors and small-AUM funds, ETFs are the practical implementation.

## Three weighting schemes — when to use which

### Method 1 — Returns-weighted (`w_i ∝ R^cum_i`, Eq. 4.11)

Simplest. ETFs with the strongest trends get the most weight.

**Problem**: high-volatility ETFs (e.g., oil, country ETFs) tend to have larger `R^cum` magnitudes. This biases the portfolio toward volatile assets. A 12-month gain of 30% in oil represents the same trend strength as a 10% gain in bonds, but Method 1 weights them 3:1.

### Method 2 — Vol-adjusted (`w_i ∝ R^cum_i / σ_i`, Eq. 4.12)

Each ETF contributes equal *risk-adjusted* return. This is the **risk-parity** approach to trend.

**Empirically the most-used** in modern practitioner implementations. Smooths drawdowns; ensures one asset doesn't dominate.

### Method 3 — Sharpe-optimal with diagonal C (`w_i ∝ R^cum_i / σ_i²`, Eq. 4.13)

Maximises Sharpe assuming the covariance matrix is diagonal (zero cross-asset correlations). Penalises high-vol assets more aggressively than Method 2.

**Issue**: in reality, asset correlations are non-zero (especially during crises). Method 3 ignores this. Method 2 is more robust.

## Risk-managed variants

### Vol-targeted portfolio

Scale the entire book to a target realised vol (e.g., 10% annualised). When realised vol of the trend signals exceeds the target, scale exposure down; when it falls short, scale up.

```
exposure_t = target_vol / realised_vol_t
```

This is **the practitioner's overlay**. It controls tail risk without changing signal interpretation.

### Crash hedge

A small fraction (1–3%) of book in OTM index puts. Cheap insurance against the rare cases where trends fail catastrophically (e.g., 2009-style sharp reversal).

### Multiple lookback windows

Combine 3-month, 6-month, 12-month signals with equal weights. Reduces sensitivity to the specific lookback choice; smoother performance.

## Historical context — the 2008 case

Multi-asset trend following had its **best year ever in 2008**:

- Trend-followers were short equities, long Treasuries, short commodities (after the H2 2008 commodity collapse), short USD then long USD.
- Most large CTAs delivered 20–60% returns in 2008.
- This is the textbook case for trend-following — sustained directional moves across many asset classes.

Then 2009–2018 was a **lost decade** for trend-following: the strategy underperformed buy-and-hold equities by a wide margin, central-bank intervention disrupted historical trend patterns, and many CTA funds saw outflows.

Then 2022 reminded everyone: when the macro regime shifts (inflation, central-bank tightening), trend-following pays handsomely again. SocGen Trend Index returned ~30% in 2022.

## When trend following fails

- **Sharp reversals.** A clean upward trend that suddenly inverts (March 2009, January 2018) catches trend-followers long at the wrong time.
- **Choppy / range-bound markets.** Trend signals fire and reverse; whipsaws eat the strategy.
- **Central-bank-engineered stability.** 2010–2018 saw extended periods of QE-induced low volatility and weak trends. Trend-following underperformed.
- **Single-day shocks.** 2010 Flash Crash, 2015 SNB EUR/CHF break: trend signals didn't matter because the move happened in one day.

## Practical implementation

- **Universe**: 15–25 liquid ETFs across asset classes.
- **Rebalance**: monthly. Some implementations are bi-weekly.
- **Lookback**: 6–12 months. Some use ensembles (3, 6, 12 averaged).
- **Position-sizing**: vol-adjusted (Method 2 above).
- **Vol-target**: typically 10–15% annualised.
- **Stop-loss**: optional per-asset; some implementations have a per-ETF stop after a large adverse move.

## Common mistakes

- **Over-optimising the lookback window.** A 7-month lookback that worked on backtest may not work going forward. Use ensembles or fixed canonical lookbacks (12-1 is the academic standard).
- **Ignoring transaction costs.** ETF round-trip costs of 5–10 bps add up to 60–120 bps annual drag with monthly rebalancing.
- **Treating all asset classes as equally trend-friendly.** Commodities tend to trend stronger than bonds. Equities are noisier. Universe selection matters.
- **Comparing absolute returns to equity-buy-and-hold.** Trend-following delivers crisis-alpha but underperforms in long bull markets. The diversification benefit is the value, not the absolute return.

## Risk management essentials

- **Cap per-asset weight**: even strong trends shouldn't lead to 50% in one ETF.
- **Cap per-asset-class weight**: 30–40% maximum per class (equity, bonds, commodities).
- **Vol-target the book**: discussed above.
- **Watch for regime changes**: when trend Sharpe falls below ~0.3 on a 36-month rolling basis, the regime may be unfavourable. Consider tactical reduction.
- **Tail hedge**: small option overlay for crisis insurance.

## Where to do this in the terminal

- **AI Quant Lab** — multi-asset trend-following template with all three weighting schemes and vol-targeting overlay.
- **ETF screen** — cross-asset trend rankings.
- **Backtesting** — historical multi-asset trend P&L with regime decomposition.
- **Portfolio** — trend-following tactical-tilt overlay.

## See also

- [[trend-following|Trend Following (TSMOM)]] — the broader framework
- [[etf-sector-rotation|ETF Sector Rotation]] — equity-specific version
- [[stocks-price-momentum|Stock Price Momentum]] — the stock-level version
- [[fx-momentum-carry-combo|FX Momentum + Carry Combo]] — currency trend
- [[commodity-value-momentum|Commodity Value and Momentum]] — commodity trend
- [[strategy-patterns|Strategy Patterns]] — trend as a strategy archetype

## External references

- Moskowitz, T., Ooi, Y. H., Pedersen, L. (2012). "Time Series Momentum." *Journal of Financial Economics* 104(2), 228–250.
- Hurst, B., Ooi, Y. H., Pedersen, L. (2017). "A Century of Evidence on Trend-Following Investing." *Journal of Portfolio Management* 44(1), 15–29.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Hong, H., Stein, J. (1999). "A Unified Theory of Underreaction, Momentum Trading, and Overreaction in Asset Markets." *Journal of Finance* 54(6), 2143–2184.
- Faber, M. (2007). "A Quantitative Approach to Tactical Asset Allocation." *Journal of Wealth Management* 9(4), 69–79.
- Faber, M. (2015). "Learning to Play Offense and Defense: Combining Value and Momentum from the Bottom Up, and the Top Down." Working paper, SSRN 2669202.
- Antonacci, G. (2014). *Dual Momentum Investing*. McGraw-Hill.
- Doeswijk, R., Lam, T., Swinkels, L. (2014). "The Global Multi-asset Market Portfolio, 1959–2012." *Financial Analysts Journal* 70(2), 26–41.
- Black, F., Litterman, R. (1992). "Global Portfolio Optimization." *Financial Analysts Journal* 48(5), 28–43.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §4.6. https://doi.org/10.1007/978-3-030-02792-6
