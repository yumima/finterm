# Volatility as an Asset Class

For most investors, volatility is a property of returns. For quants, it's a tradeable asset class with its own term structure, its own factor premia, its own crashes. Strategies that buy and sell volatility — directly or via options — have been among the highest Sharpe-ratio anomalies in the academic literature *and* among the most spectacular blow-ups in trading history. Both facts trace to the same root: volatility is short-asymmetric.

## The instruments

### VIX — the spot volatility index

The CBOE Volatility Index (VIX) is a market-implied 30-day forward volatility of the S&P 500, computed from a basket of out-of-the-money SPX options. Not directly tradeable, but the reference for:

- **VIX futures** (tradeable since 2004). One-month, two-month, ... up to nine-month forwards. Most liquid at the front two contracts.
- **VIX ETPs** (VXX, UVXY, SVXY, VIXY). Daily-rolled exposure to the front two VIX futures. Inverse ETPs (XIV until 2018) deliver short-vol exposure.
- **VIX options.** Listed European options on VIX futures.

### Variance swaps

OTC contracts that pay the difference between realised variance over a period and a fixed strike variance. Settled at expiry: payoff = (RV² − K²) × multiplier, where RV is the realised volatility of the underlying.

Why important: variance swaps give pure exposure to realised vs implied volatility, hedge-able by a static portfolio of options weighted as `1/K²` across strikes. The static replication is mathematically clean (Carr & Madan 1998).

### Listed equity options

The primary instrument retail uses to gain volatility exposure. Long straddle = long vol; short straddle = short vol. Strategies in Kakushadze & Serur's *151 Trading Strategies* §2.6–§2.57 catalog the structured spreads.

## The volatility risk premium (VRP)

The most documented anomaly in volatility: **implied volatility, on average, exceeds subsequent realised volatility**. The difference is the VRP — the compensation buyers of insurance pay to sellers.

Empirical evidence (Bollerslev, Tauchen, & Zhou 2009 — "Expected Stock Returns and Variance Risk Premia"):

- S&P 500 implied volatility minus subsequent realised volatility averaged ~3 percentage points per month, 1990–2007.
- The premium is countercyclical — highest in crisis periods (insurance is most expensive when most demanded), lowest in calm.
- Selling at-the-money straddles delivers Sharpe ratios of 0.8–1.5 over long horizons, **but with massive left-tail drawdowns**.

The mechanism: investors are systematically risk-averse to large losses, so they pay a premium for protection. Sellers earn the premium in calm periods and lose it (and more) in crash periods.

### How to harvest VRP

1. **Sell front-month at-the-money straddles, delta-hedged.** Daily delta hedging removes directional exposure; what remains is realised-vol vs implied-vol. The classic "short gamma" trade.
2. **Sell variance swaps.** Cleanest exposure, but OTC and only accessible to institutions.
3. **Short VIX futures.** Capture the term-structure premium in addition to VRP. Operationally simpler than option-based but requires futures access.
4. **Short volatility ETPs.** SVXY (post-2018) is daily-rolled inverse VIX futures exposure. Convenient but suffers from compounding decay in volatile periods.

In all cases, **size small**. Full-Kelly VRP harvesting goes to zero in 2008, 2018, 2020. Half-Kelly or quarter-Kelly is the practitioner default.

## Term structure: contango and backwardation

VIX futures form a term structure (front, second-month, etc.). The shape carries information and is itself tradeable.

- **Contango** (typical): front-month futures are below higher-numbered months. Reflects: "near-term vol is lower than expected vol further out." This is the usual state — futures roll down toward spot, generating a positive carry for short-vol positions.
- **Backwardation** (stress): front-month above further months. Crisis spot vol is elevated; expectation is for vol to decline. Backwardation kills short-vol strategies — the negative carry compounds with the realised loss.

The VIX futures basis (front-month minus spot, or front minus second-month) is a leading indicator. Practitioner shorthand: "VIX > VXV (3-month VIX) for more than a few days = risk-off regime."

## Variance swap mathematics — why it's clean

A variance swap on the S&P 500 with annualized strike K and notional V pays:

```
Payoff = V × (RV² − K²)
```

The remarkable result: **variance swaps can be replicated statically by a portfolio of European options weighted by 1/K²**. Derman & Demeterfi (1999) and Carr & Madan (1998) showed:

```
RV² ≈ (2/T) ∫ Call(K) / K² dK + (2/T) ∫ Put(K) / K² dK
```

The integrals are over all strikes. In practice, you can't get every strike, so the replication is approximate. The implication: variance-swap pricing is a model-free statement about the option-implied probability distribution.

This matters because it gives a non-parametric way to extract market expectations of future volatility from listed options. The CBOE's VIX formula is exactly this calculation applied to SPX options.

## Dispersion trading

Implied correlation between index components, derived from option prices, has historically been higher than realised correlation. The trade: sell index volatility, buy single-stock volatility. If implied correlation drops to realised correlation, the spread converges and you collect.

Capacity-limited and operationally complex (requires hedging 50+ option legs), but Sharpe ratios in the 1.5–2.5 range have been documented for institutional dispersion books over long horizons.

In *151 Trading Strategies*: §6.3 (index dispersion), §6.3.1 (subset-portfolio dispersion).

## Volatility crashes

The history of vol trading is a series of crashes that wipe out years of carry:

- **August 2007.** "Quant quake" — many statistical-arb books delevered simultaneously; short-vol positions were caught in the crossfire. Recovery within months.
- **August 2015.** Sudden VIX spike from ~13 to 53 in a week (Chinese yuan devaluation). Inverse-VIX ETPs (XIV) lost 30%+ in a session.
- **February 2018 — "Volmageddon."** XIV (the dominant inverse-VIX ETP) lost > 90% in a single day when the underlying VIX futures spiked, triggering a forced rebalance that cascaded. Wiped out the product entirely; XIV was terminated days later.
- **March 2020.** VIX hit 82.7 (highest since 2008). Vol short-sellers were either liquidated or had margin calls served to them; survivors clipped fractional Kelly.

Pattern: each crash followed an extended low-vol period that drew in capital. The implicit short-vol exposure across the industry built up; the unwind was violent. **Vol crashes are correlated across funds** because everyone is short the same factor.

## Modeling volatility

Standard models for backtesting and risk:

- **EWMA** (RiskMetrics). Exponentially weighted realised variance. Reactive, simple, robust.
- **GARCH(1,1).** Variance with memory. Industry standard for value-at-risk; see [[var-cvar|VaR / CVaR]] for the role.
- **Heston model.** Stochastic variance for options pricing. Standard for equity-derivatives desks.
- **Local volatility (Dupire).** Solves for the volatility surface consistent with observed option prices. Used by exotic-options traders.
- **SABR.** Stochastic-alpha-beta-rho, standard for interest-rate vol surfaces.

For backtests of vol strategies, **start with EWMA on realised variance plus the front-month VIX future as the implied input**. Move to GARCH or Heston only when the simpler model is insufficient.

## Practitioner rules

1. **Never run pure short-vol full-Kelly.** A position that works 90% of the time and wipes out the other 10% is a Pareto-distributed disaster — its expected drawdown is unbounded.
2. **Layered tail hedges.** Short the front-month vol, buy out-of-the-money longer-dated puts. The cost of the tail hedge is ~25–40% of the carry premium, but it caps the left tail.
3. **Backtest including 2008 and 2020.** A vol-strategy backtest excluding those years is dishonest by construction. Most backtests are.
4. **Watch the term structure.** Short-vol positions should be reduced or covered when VIX moves into backwardation; one of the better empirical signals for "regime change."
5. **Capacity-aware.** VIX futures and variance swaps have real depth limits. Strategies that work at $100M can't run at $10B.

## In this terminal

- **surface_analytics** screen — implied-volatility surface visualization for individual underlyings.
- **derivatives** screen — option chain, Greeks, strategy P&L.
- **futures** screen — VIX futures curve.
- **Kakushadze & Serur §7** — comprehensive catalog of vol strategies (VIX futures basis, vol carry, VRP with Gamma hedging, vol skew, dispersion, variance swaps).

## See also

- [[options-basics|Options basics]] — primer on options
- [[implied-volatility|Implied volatility (concept)]]
- [[var-cvar|VaR / CVaR]] — vol models feed into risk
- [[strategy-patterns|Strategy patterns]] — VRP is pattern #5
- [[factor-investing|Factor investing]] — low-vol factor in equities (different from VRP)

## External references

- Bollerslev, Tauchen, & Zhou (2009). "Expected Stock Returns and Variance Risk Premia." *Review of Financial Studies*.
- Carr & Madan (1998). "Towards a Theory of Volatility Trading."
- Derman & Demeterfi (1999). "More Than You Ever Wanted to Know About Volatility Swaps." Goldman Sachs.
- Heston (1993). "A Closed-Form Solution for Options with Stochastic Volatility." *Review of Financial Studies*.
- Bollerslev (1986). "Generalized Autoregressive Conditional Heteroskedasticity." *Journal of Econometrics*.
- Sinclair (2010). *Volatility Trading*. Wiley. Practitioner reference.
- Kakushadze & Serur (2018). *151 Trading Strategies* §2 (option spreads), §7 (volatility strategies).
- CBOE white papers on VIX methodology and variance swaps — freely available at cboe.com.
