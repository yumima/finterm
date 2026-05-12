# Trend Following

Trend following is the bet that an asset that has gone up over the past N months is more likely to keep going up over the next M months — and that an asset that has gone down keeps going down. It is the oldest documented systematic strategy (David Ricardo allegedly traded a version in the 1810s), the foundation of the **CTA / managed-futures industry**, and the empirical pattern with the broadest cross-asset, multi-century evidence in quant finance.

Moskowitz, Ooi & Pedersen (2012) published the canonical academic formulation as **Time-Series Momentum (TSMOM)**: signed past returns predict signed future returns, across 58 futures contracts spanning equity indices, bonds, FX and commodities, 1965–2009. Hurst, Ooi & Pedersen (2017) at AQR extended the evidence to *137 years* of data ("A Century of Evidence on Trend-Following Investing") and found positive Sharpe in every decade since 1880.

## The TSMOM specification

The Moskowitz-Ooi-Pedersen version, almost verbatim:

```
for each asset i, each month t:
    1. r_lookback = total return over t−12 months to t−1 month
       (the "12-1" lookback: 12 months ending one month ago)
    2. sigma_i = recent realised volatility (3-month rolling)
    3. position_i = sign(r_lookback) · (vol_target / sigma_i)
hold for one month. Rebalance monthly.
```

The 12-1 lookback (skipping the most recent month) was originally a microstructure-noise reduction trick borrowed from cross-sectional momentum (Jegadeesh & Titman 1993). At the time-series level the one-month skip matters less, but the convention stuck.

**Vol scaling** is the non-negotiable part. Without it, equity-index trends dominate any cross-asset book because equities have ~16% annual vol and 2-year notes have ~5%. With vol scaling, each asset contributes equal risk and the strategy's Sharpe comes from diversification across uncorrelated trends rather than from any single market.

## Why the asset-class universe matters

Trend works best when applied across a broad, low-correlation futures universe. Hurst-Ooi-Pedersen and the AQR/Man AHL/Winton track records all rely on:

- **Equity indices** — S&P 500, EuroStoxx, Nikkei, FTSE, Hang Seng, etc.
- **Government bonds** — 10Y notes across G10, Bund, Gilt, JGB.
- **FX majors and crosses** — EUR, JPY, GBP, AUD, CAD, CHF, EM where liquid.
- **Commodities** — energy (WTI, Brent, nat gas), metals (gold, copper, silver), grains (corn, wheat, soy), softs (coffee, sugar, cotton).

The Sharpe of TSMOM on a single asset class (say, equity indices only) is ~0.5. The Sharpe of the full diversified book has historically been ~1.0–1.4. The lift is pure diversification: trends across asset classes are roughly uncorrelated, so risk reduces faster than expected return.

This is also why **long-only equity trend usually disappoints**. Equity index trends are dominated by a few decade-long bull markets (1982–2000, 2009–2021) punctuated by sharp crashes that wreck trend models. The full benefit of trend appears only when you can short bonds in 2022, short JPY in 2024 carry, long gold in 2008, etc.

## The defensive convexity

Trend following's most valuable property is its **return profile in crises**. Because the signal is long after up-trends and short after down-trends, the strategy is positioned with the prevailing direction by the time a sustained move arrives. Historically:

- **2008** — Trend was short equities, long bonds, long USD. AQR's trend programmes returned roughly +20%; the S&P returned −37%.
- **2022** — Trend was short bonds and short equities into the Fed-pivot collapse, long energy. Managed-futures indices returned ~+30%; 60/40 returned −16%.

This convex payoff is why endowments and pensions allocate to managed futures as a crisis hedge, not as a return centre. The flip side: in choppy, mean-reverting years (2011, 2015, 2018, 2023), trend underperforms steadily. The cumulative Sharpe is the average across crisis booms and chop drawdowns.

## Hyperparameters worth knowing

- **Lookback length.** The 12-1 month is conventional; 3-month and 6-month lookbacks add a faster signal. Most production CTAs use an ensemble of 1m / 3m / 12m to smooth turnover.
- **Vol-target.** Typically 10–15% portfolio volatility. Higher leverages make 2008-style drawdowns ugly during whipsaws.
- **Rebalance frequency.** Monthly is standard. Daily rebalancing increases turnover with marginal Sharpe gain; weekly is a common compromise.
- **Risk parity across assets.** Equal-vol weighting (the MOP version) vs equal-cluster weighting (group commodities into energy/metals/grains and equalise across clusters) — the latter is sometimes preferred to avoid commodity over-representation.

## Costs and capacity

Trend at the 12-1 frequency turns over slowly — typically 200–400% per year. Transaction costs in liquid G10 futures are 1–3 bps round-trip, so cost drag is 5–15 bps annualised. Capacity is enormous; AQR, Man AHL and Winton together manage tens of billions in trend strategies. See [[transaction-costs|transaction-cost modelling]] for the cost stack.

The cost advantage over single-name strategies is why managed-futures funds scale to ~$50B AUM while equity stat-arb caps below $10B. Trend in liquid futures is genuinely the most scalable systematic strategy.

## Common mistakes

- **Long-only equity trend.** "Buy SPY when SPY's 12-month return is positive." Historically works but barely beats buy-and-hold after costs because the short leg, where trend earns most of its crisis convexity, is missing.
- **Skipping vol scaling.** The strategy becomes an equity-vol bet. Without vol-targeting, 2008 drawdown is double what a properly scaled book would have endured.
- **Too short a lookback.** Daily-return-sign trend is mostly noise. Sub-month lookbacks need a much bigger toolbox (filters, breakout rules, exhaustion detectors).
- **Trading single names with trend.** Cross-sectional momentum at the single-stock level is a different strategy (the [[momentum-factor|momentum factor]]) — different signal construction, different risk profile. Don't confuse the two.
- **Ignoring carry.** A bond trend that's long Treasuries has positive carry; an FX trend that's long JPY/USD has negative carry. Subtract carry from the trend signal to avoid double-counting; see [[carry-factor|carry factor]].

## Risk management essentials

- **Drawdown discipline.** Trend strategies have 15–25% maximum drawdowns even in good decades. Position size so the worst historical drawdown is survivable.
- **Whipsaw periods.** 2011–2015 was a multi-year drawdown for trend. Investors who allocated based on a 3-year track record bailed at the bottom. See [[drawdown-and-risk|drawdown discipline]].
- **Crowding monitoring.** When too many managers are positioned the same way (long USD long bonds short equities), unwinds can be violent. Track the BarclayHedge CTA index for crowding signals.

## Where to do this in the terminal

The **AI Quant Lab** has a TSMOM module with configurable lookback ensemble and vol-targeting. The **Backtesting** screen reports trend P&L bucketed by VIX regime and decomposed into long-only vs short-only contributions, which is the cleanest way to see crisis convexity.

## See also

- [[momentum-factor|Momentum factor]] — the cross-sectional cousin (rank stocks within universe; don't sign individual returns)
- [[carry-factor|Carry factor]] — orthogonal to trend in FX and bond books
- [[time-series-basics|Time-series basics]] — autocorrelation at the multi-month horizon is the empirical foundation
- [[position-sizing|Position sizing]] — vol-target sizing is half the strategy
- [[drawdown-and-risk|Drawdown discipline]] — managing the long whipsaw periods

## External references

- Moskowitz, Ooi & Pedersen (2012). "Time Series Momentum." *Journal of Financial Economics* 104(2).
- Hurst, Ooi & Pedersen (2017). "A Century of Evidence on Trend-Following Investing." *Journal of Portfolio Management* 44(1).
- Jegadeesh & Titman (1993). "Returns to Buying Winners and Selling Losers." *Journal of Finance* 48(1) — the cross-sectional counterpart.
- Asness, Moskowitz & Pedersen (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3).
- Lemperière, Deremble, Seager, Potters & Bouchaud (2014). "Two Centuries of Trend Following." *Journal of Investment Strategies* — independent replication going back to 1800.
