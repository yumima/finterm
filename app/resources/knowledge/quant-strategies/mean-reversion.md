# Mean Reversion

Mean reversion is the bet that a deviation from a stable reference level will be corrected. In single-name equities at short horizons (1–10 days), this is one of the most reliably documented empirical patterns in finance. In FX majors at any horizon shorter than a year, it mostly doesn't work — exchange rates between developed-market currencies behave very close to random walks (Meese & Rogoff 1983 is still depressingly current).

This entry covers the workhorse implementations: **z-score reversion** on residuals, **Ornstein-Uhlenbeck calibration** for half-life estimation, and the empirical limits of where the pattern actually exists.

## The underlying stylised fact

Lo & MacKinlay (1990) and Jegadeesh (1990) documented short-horizon negative serial correlation in US single-stock weekly returns: a stock that goes down this week is slightly more likely to go up next week, conditional on cross-sectional ranking. Lehmann (1990) showed the contrarian strategy was profitable before costs at the weekly horizon. This is *the* origin of statistical reversion as a trading discipline.

Why it exists, in two parts:

- **Microstructure noise.** Bid-ask bounce, temporary liquidity demand from large traders, end-of-day rebalancing — all push price away from fundamental value, then snap back.
- **Behavioural overreaction.** Daniel-Hirshleifer-Subrahmanyam (1998) and others document overreaction to firm-specific news that corrects over days to weeks.

Importantly: the pattern is *cross-sectional* and *short-horizon*. Single-stock returns at the monthly horizon show positive serial correlation (the [[momentum-factor|momentum factor]]) — the same name, same data, different horizon. Don't confuse them.

## Z-score reversion — the basic recipe

The single-name implementation:

```
1. Pick a reference price level. Options:
   - rolling N-day mean of close
   - rolling N-day VWAP
   - factor-residual (see stat-arb)
2. Compute z = (price − mean) / rolling_std.
3. Entry rule:    enter short if z > +k_entry
                  enter long  if z < −k_entry
4. Exit rule:     exit when z crosses 0 (or within a band ±k_exit).
5. Stop rule:     stop out if z reaches ±k_stop without reverting.
```

Typical hyperparameters for US large-cap equities at the daily frequency: N = 20–60, k_entry = 2.0, k_exit = 0.5, k_stop = 3.5. These are starting points, not endorsements — see [[walk-forward-validation|walk-forward validation]] for honest calibration.

The strategy bleeds in trending regimes and earns in choppy / range-bound ones. Pair it with a regime filter (low recent realised volatility of the index, or absence of strong cross-sectional momentum) for any chance of surviving live.

## Ornstein-Uhlenbeck calibration

Treat the deviation x_t = price_t − mean_t as a continuous-time mean-reverting process:

```
dx = −θ · x · dt + σ · dW
```

θ is the speed of reversion. The implied **half-life** is the time for the expected deviation to halve:

```
half_life = ln(2) / θ
```

Calibrate by regressing the daily change Δx on the level x:

```
Δx_t = α + β · x_{t−1} + ε_t
θ ≈ −β / Δt
```

Half-life is the diagnostic that decides whether the strategy is tradeable:

- **Half-life < 1 day** — the reversion is mostly bid-ask bounce. HFT regime; you need sub-second execution and rebates to capture it.
- **Half-life 2–10 days** — sweet spot for cash-equity stat-arb. The classic Avellaneda-Lee window.
- **Half-life 30–100 days** — capital lock-up risk; the trade may be right but you'll be early and drawdown can be ugly.
- **Half-life > 200 days** — you are not trading reversion, you are trading something else with a slow component. Re-examine.

## Where it works and where it doesn't

The pattern is *not* universal. Empirically:

- **US large-cap equities, single-name, short horizon.** Works. Documented continuously since Lehmann (1990). Decays with crowding — short-term reversion Sharpes have fallen from ~3 in the 1990s to ~1 today, gross of costs.
- **Equity index level, daily.** Weak. The S&P 500 daily return has near-zero autocorrelation at lag 1.
- **Equity index, intraday at the 30-min scale.** Works mildly. Heston, Korajczyk & Sadka (2010) document intraday reversion patterns.
- **FX majors at daily/weekly.** Mostly does not work. Engel & West (2005) document that FX rates near-random-walk at short horizons. Carry trades aren't reversion plays — they're [[carry-factor|carry factor]] exposures.
- **Commodities, single contract.** Mixed. Front-month crude oil and metals show some reversion; agricultural futures show seasonality dressed as reversion.
- **Rates and Treasury futures.** Some reversion at the spread / butterfly level. Outright duration is closer to a unit-root.

## Mean reversion vs momentum — same data, different horizon

A surprising feature of equity returns:

- **1 day to 4 weeks** — reversion dominates.
- **2 months to 12 months** — momentum dominates (the [[momentum-factor|12-1 momentum factor]]).
- **3 to 5 years** — reversion dominates again (De Bondt & Thaler 1985 "Does the Stock Market Overreact?").

This nested structure means any single-name strategy must be horizon-explicit. A "long-short reversion" strategy that holds 30 days will sit awkwardly between the short-horizon and momentum regimes — usually losing to both.

## Risk management essentials

Mean reversion has a brutal failure mode: when the deviation is a real signal (earnings miss, bankruptcy, fraud disclosure), the price keeps going. You are short the corrective tail.

Mandatory controls:

- **Hard stops at z > ±3.5σ.** No exception.
- **Filter for upcoming events.** Earnings within N days, FDA decisions, M&A announcements — exclude or halve size.
- **Cross-sectional sizing.** Equal-weight the basket of names with active reversion signals; never concentrate. The Sharpe of single-name reversion is dwarfed by basket Sharpe because the noise diversifies. See [[position-sizing|position sizing]].
- **Stop on regime change.** When VIX > 30, microstructure breaks and reversion patterns invert briefly (the March 2020 problem). Stand down.

## Common mistakes

- **Optimising k_entry and k_exit jointly on one window.** Classic two-parameter overfit. Lock entry, sweep exit (or vice versa), and walk forward.
- **Ignoring borrow costs on the short leg.** Hard-to-borrow names have negative carry that easily eats the reversion edge. Pre-screen on borrow availability.
- **Mean reverting an index without checking trend strength.** In 2017 and 2021, S&P daily reversion strategies lost steadily — the trend was too strong for reversion to dominate.
- **Calibrating OU on one regime, trading in another.** Re-calibrate half-life every rebalance; do not trust a 2010 estimate in 2024.

## Where to do this in the terminal

The **AI Quant Lab** exposes a z-score reversion module with explicit OU calibration and half-life diagnostics. The **Backtesting** screen plots cumulative reversion P&L bucketed by realised-vol regime. The **Node Editor** lets you stack a reversion signal with a regime filter visually.

## See also

- [[time-series-basics|Time-series basics]] — stationarity, autocorrelation, the mathematical foundation
- [[pairs-trading|Pairs trading]] — reversion on the residual of a cointegrating regression
- [[stat-arb|Statistical arbitrage]] — cross-sectional residual reversion at scale
- [[momentum-factor|Momentum factor]] — the longer-horizon opposite
- [[position-sizing|Position sizing]] — the diversification math that makes the basket worth more than any single name

## External references

- Lo & MacKinlay (1990). "When Are Contrarian Profits Due to Stock Market Overreaction?" *Review of Financial Studies* 3(2).
- Jegadeesh (1990). "Evidence of Predictable Behavior of Security Returns." *Journal of Finance* 45(3).
- Lehmann (1990). "Fads, Martingales, and Market Efficiency." *Quarterly Journal of Economics* 105(1).
- De Bondt & Thaler (1985). "Does the Stock Market Overreact?" *Journal of Finance* 40(3).
- Avellaneda & Lee (2010). "Statistical Arbitrage in the U.S. Equities Market." *Quantitative Finance* 10(7).
- Meese & Rogoff (1983). "Empirical Exchange Rate Models of the Seventies." *Journal of International Economics*.
