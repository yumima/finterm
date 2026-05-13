# Technical Analysis — Moving Averages, Support/Resistance, Channels

This entry covers the **technical-analysis (TA) family** of stock strategies: moving-average crossovers, support and resistance levels, and channel/breakout trades. Kakushadze & Serur (2018) §3.11–§3.15 cover these in five short sections.

**A frank disclaimer**: TA strategies have a fundamentally different epistemic status from the factor strategies elsewhere in this catalogue. Academic evidence for TA's predictive power is **mixed at best in modern liquid markets**. They are included here for completeness and because the underlying signals (moving averages especially) form the basis of more sophisticated trend-following systems used by CTAs. **Don't confuse "appears in this catalogue" with "should be the basis of your portfolio."**

## What TA is — the honest framing

Technical analysis is the practice of inferring future prices from **price and volume history alone**, without reference to fundamentals. The strategies in this entry are entirely chart-and-history based.

The book itself acknowledges this (§3.21 "A Few Comments"): TA strategies are deemed "unscientific by many professionals and academics" but, importantly, **trend-following / momentum strategies are based on moving averages** — so dismissing TA wholesale is too strong. The right framing:

- **Single-stock TA** is hard to validate scientifically.
- **Cross-sectional TA across hundreds of stocks** introduces a statistical dimension that *can* be tested.

That's the framing for this entry. The signals themselves are described as the book describes them. Their efficacy is contested.

## Strategy 1 — Single Moving Average (§3.11)

The simplest signal: compare current price to its T-day moving average.

```
SMA(T)  =  (1/T) · Σ_{t=1}^{T} P(t)                       (Eq. 3.54)

Or exponential weighting:
EMA(T, λ)  =  [Σ_{t=1}^{T} λ^{t-1} · P(t)] / [Σ_{t=1}^{T} λ^{t-1}]    (Eq. 3.55)

Signal:
  Long  if P > MA(T)
  Short if P < MA(T)                                      (Eq. 3.56)
```

Common parameters: T = 20, 50, 100, 200 days.

**Interpretation**: when the price is above its T-day average, the recent trend is up. The strategy bets on continuation.

**Why it might work**: this is a simple momentum-detector. If trends persist (and Jegadeesh-Titman documented they do at certain horizons), then SMA crossover signals will catch the persistence.

**Why it might not work in single stocks**: individual stock returns at the daily/weekly horizon are dominated by noise. Single-stock SMA signals fire constantly and lose to transaction costs.

## Strategy 2 — Two Moving Averages (§3.12)

Replace the price with a *shorter* moving average. Trigger when the short MA crosses the long MA.

```
Signal:
  Long  if MA(T') > MA(T)   where T' < T
  Short if MA(T') < MA(T)                                  (Eq. 3.57)
```

Classic parameters: T' = 50, T = 200 days (the famous **"golden cross"** when 50-day crosses above 200-day; **"death cross"** when it crosses below).

**With stop-loss**:
```
Long position liquidated if current price drops below (1 − Δ) × P_1
```
where Δ might be 2% and P_1 is yesterday's price (§3.58).

**Interpretation**: smoother signal than single-MA, fewer whipsaws. The golden/death cross is a famously-watched indicator in financial media.

**Empirical track record**: **Brock, Lakonishok, LeBaron (1992)** found MA crossover signals had statistical significance on the DJIA from 1897–1986. **Lo, Mamaysky, Wang (2000)** found similar with more sophisticated TA patterns. **More recent papers find these signals have decayed sharply since the 1990s** — likely arbitraged away as quant funds incorporate them.

## Strategy 3 — Three Moving Averages (§3.13)

Add a third MA to filter signals.

```
T_1 < T_2 < T_3   (e.g., 3, 10, 21 days)

Signal:
  Long  if MA(T_1) > MA(T_2) > MA(T_3)
  Short if MA(T_1) < MA(T_2) < MA(T_3)                    (Eq. 3.59)
  Liquidate long if MA(T_1) ≤ MA(T_2)
```

The strict ordering reduces false signals; you only trade when all three MAs are aligned. The cost is missing fast moves.

## Strategy 4 — Support and Resistance (§3.14)

Pivot-point-based levels.

```
Pivot:  C  =  (P_H + P_L + P_C) / 3                       (Eq. 3.60)

Resistance:  R  =  2·C − P_L                              (Eq. 3.61)
Support:     S  =  2·C − P_H                              (Eq. 3.62)
```

Where P_H, P_L, P_C are the previous day's high, low, and close.

Rule:
```
Long  if P > C
Short if P < C
Exit long  if P ≥ R
Exit short if P ≤ S                                       (Eq. 3.63)
```

**Interpretation**: support and resistance are levels where buyers/sellers are expected to step in. Breaking above C suggests the day will be bullish; reaching R is a take-profit level.

**Why traders use this**: it's a simple structural recipe — calculated from yesterday's bar, applied today. It's psychologically anchored — many discretionary traders also watch these levels, which can be self-fulfilling at short horizons.

**Empirically**: the academic evidence for pivot points is thin. **Osler (2000, 2003)** documented some statistical evidence for support/resistance in FX markets, but the effect is small and may not exceed transaction costs in stocks.

## Strategy 5 — Channels (Donchian) (§3.15)

A channel is a band around the price defined by recent highs and lows.

```
B_up    =  max(P(1), P(2), ..., P(T))                      (Eq. 3.64)
B_down  =  min(P(1), P(2), ..., P(T))                      (Eq. 3.65)

Signal:
  Long  if P = B_down  (touched the floor)
  Short if P = B_up   (touched the ceiling)                (Eq. 3.66)
```

This is the **Donchian Channel** (Donchian 1960) — the basis of the famous **Turtle Trading** system that **Richard Dennis** taught in the 1980s.

**Counter-intuitive note**: the rule above is a **mean-reversion** rule (fade the breakout). The original Turtle system was the **opposite** — buy on breakouts above B_up (riding momentum), sell on breaks below B_down. The book gives the contrarian version. **Both have been used historically**. The momentum version is more associated with CTAs and trend-following funds.

**Empirically**: the breakout (momentum) version works at long horizons (T = 55+ days) in commodities and currencies. The mean-reversion version works at short horizons (T = 5–20 days) in equities. They are essentially opposite strategies sharing the same channel-construction logic.

## Combining TA with volume

Several of the rules above gain credibility when combined with **volume**:

- A break above resistance on high volume is more meaningful than a low-volume break.
- A MA crossover with rising volume suggests genuine momentum, not noise.

The book mentions this in §3.15: "the signal can be more robust when a price reversal (or a channel break) occurs with an increase in the traded volume."

## When TA strategies work, when they don't

**More likely to work**:
- **Across many stocks cross-sectionally** (not single-stock). Pooling diversifies noise.
- **At longer horizons** (50+ days). Trend-following on monthly data works; on daily data it mostly doesn't.
- **In trending asset classes** (currencies, commodities historically). In equities — weak.
- **With proper transaction-cost modelling**. Most TA backtests ignore costs; live performance suffers.

**Less likely to work**:
- **Single-stock daily TA**. Noise dominates.
- **Pattern recognition without statistical validation**. "Head and shoulders," "ascending triangles" etc. — no rigorous evidence.
- **Indicator combinations chosen post-hoc**. RSI + MACD + Stochastic ≠ alpha; it's curve-fitting.

## What this means for portfolio construction

The honest practitioner view:

- **Use moving averages as part of a trend-following system** that operates across many assets and at multi-week-or-longer horizons.
- **Don't run single-stock TA strategies as standalone alpha**. The Sharpe net of costs is too thin.
- **Use support/resistance / pivot points as execution heuristics** — not signal-generation. ("Don't sell into a strong support level if you can wait" is reasonable; "buy every time a stock hits support" is not.)

## Common mistakes

- **Optimising parameters on one period.** A 10-day MA that worked beautifully in 2015 may not work in 2018. Walk-forward validation is mandatory.
- **Cherry-picking indicators.** With ~50 popular TA indicators and thousands of parameter combinations, p-hacking is nearly inevitable. Most TA papers fail replication on hold-out samples.
- **Ignoring transaction costs.** Most TA rules fire frequently. Round-trip costs of 10–20 bps wipe out edges of similar magnitude.
- **Treating TA as a substitute for risk management.** "Buy on golden cross, sell on death cross" is a strategy. "Buy and sit through 50% drawdown waiting for death cross" is a way to blow up.

## Risk management essentials

- **Position sizing**: small sizes per signal. TA strategies fire often.
- **Stop-loss**: mandatory. TA signals don't have built-in risk management.
- **Don't pyramid**: adding to losing positions on a TA signal "for the bounce" is a classic blowup pattern.
- **Volume confirmation**: ignore signals on abnormally low volume.

## Where to do this in the terminal

- **Equity Trading** — single-stock charts with overlayed MA, support/resistance, channel indicators.
- **AI Quant Lab** — TA strategy templates (single MA, two MA, channels) with parameter sweep and walk-forward validation.
- **Backtesting** — TA strategy P&L with realistic transaction cost modelling.

## See also

- [[stocks-price-momentum|Price Momentum]] — the statistical version of MA-crossover applied cross-sectionally
- [[trend-following|Trend Following (TSMOM)]] — the trend-following framework where MA-crossover-like signals are tested rigorously
- [[mean-reversion|Mean Reversion]] — the short-horizon counterpart of channel-fade strategies

## External references

- Brock, W., Lakonishok, J., LeBaron, B. (1992). "Simple Technical Trading Rules and the Stochastic Properties of Stock Returns." *Journal of Finance* 47(5), 1731–1764.
- Lo, A., Mamaysky, H., Wang, J. (2000). "Foundations of Technical Analysis: Computational Algorithms, Statistical Inference, and Empirical Implementation." *Journal of Finance* 55(4), 1705–1765.
- Osler, C. (2000). "Support for Resistance: Technical Analysis and Intraday Exchange Rates." *FRBNY Economic Policy Review* 6(2), 53–68.
- Osler, C. (2003). "Currency Orders and Exchange Rate Dynamics: An Explanation for the Predictive Success of Technical Analysis." *Journal of Finance* 58(5), 1791–1820.
- Donchian, R. (1960). "High Finance in Copper." *Financial Analysts Journal* 16(6), 133–142.
- Murphy, J. (1986). *Technical Analysis of the Financial Markets* — the standard practitioner reference.
- Faber, M. (2007). "A Quantitative Approach to Tactical Asset Allocation." *Journal of Wealth Management* 9(4), 69–79.
- Park, C.-H., Irwin, S. (2007). "What Do We Know About the Profitability of Technical Analysis?" *Journal of Economic Surveys* 21(4), 786–826.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.11–§3.15. https://doi.org/10.1007/978-3-030-02792-6
