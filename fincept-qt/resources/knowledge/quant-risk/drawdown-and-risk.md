# Drawdowns and Path-Dependent Risk

Volatility and VaR are point-in-time risk measures. Drawdowns are about **the path** — and the path is what determines whether you actually stay in a strategy long enough to harvest its expected return. The single biggest reason quant strategies are abandoned isn't that the math was wrong; it's that the path got too painful.

This entry covers **max drawdown, the Calmar ratio, the Ulcer Index, time underwater**, and why path-dependent metrics deserve equal billing with [[risk-adjusted-returns|Sharpe-style measures]].

## What a drawdown is

Drawdown at time t is the percentage decline from the running peak:

```
peak_t = max(P_0, P_1, …, P_t)
DD_t   = (P_t − peak_t) / peak_t        # always ≤ 0
```

The series `{DD_t}` is the entire path-of-pain curve. From it you can read every drawdown statistic.

## Maximum drawdown

The single deepest point of the curve:

```
MDD = min_t DD_t
```

What it tells you: how bad it got, once. What it doesn't tell you: how often, how long, or whether the strategy has structurally changed.

Empirical anchors:

- **S&P 500 since 1928** — MDD of ~−86% (Sept 1929 → June 1932). 1973-74: −48%. 2007-09: −56%. 2020 COVID flash: −34% (intra-month). 2022: −25%.
- **Diversified 60/40** — MDD of ~−33% (2008-09).
- **Trend-following CTAs** — typical MDD −15% to −25%; worst (2011-14 chop) reached −30% for many.
- **Long-only equity factor portfolios** — value HML drew down −55% peak-to-trough 2007-2020 (see [[value-factor|value factor]]).

The rule of thumb: **your strategy's worst drawdown is, in expectation, deeper than the worst observed in your backtest**. The empirical max is an order statistic — bounded below by the sample length. Magdon-Ismail & Atiya (2004) derived the expected MDD for Brownian motion: `E[MDD] ≈ σ · √(π·T/2) − μ·T/2`, growing with √T. A strategy with µ=10%, σ=15% over 30 years has expected MDD around 30% — even with zero alpha lost and zero regime change.

## Calmar ratio

```
Calmar = annualized return / |max drawdown|
```

Introduced by Young (1991) as the "Calmar" (California Managed Account Reports) ratio. Used heavily by CTA / managed-futures investors because they care about path. Typical thresholds:

- Calmar > 0.5 — acceptable for most diversifying strategies.
- Calmar > 1.0 — strong.
- Calmar > 2.0 — usually too good; check for overfit or short sample.

Critique: Calmar is built on a single observation (the deepest drawdown), so it's noisy and sample-length-sensitive. A 5-year Calmar of 3.0 and a 20-year Calmar of 1.0 can describe the same strategy.

## Ulcer Index (Martin & McCann 1989)

Peter Martin's solution to "Calmar uses one bad day" — the **Ulcer Index** is the RMS of the drawdown series, integrating depth and duration:

```
UI = sqrt( mean( DD_t² ) )
```

A strategy that hits −20% for one day and recovers has a small UI. A strategy that sits at −15% for two years has a large UI. The latter is what actually drives investor capitulation.

Derived measure — the **Ulcer Performance Index** (a.k.a. Martin ratio):

```
UPI = (annualized return − risk-free) / Ulcer Index
```

UPI is the most defensible single-number summary of risk-adjusted return for path-conscious investors. Martin & McCann (1989), *The Investor's Guide to Fidelity Funds*, is the original reference; Kestner (2003) extended its use into systematic trading.

## Time underwater

How long does the strategy spend below its previous peak? Two metrics:

- **Average drawdown duration.** Mean length (in days, months) of all underwater spells.
- **Max time-to-recovery (TTR).** The longest gap between a peak and the next time that peak is reclaimed.

Anchors:

- S&P 500 (1929 peak) — TTR of 25 years on a nominal price basis; ~15 years total return.
- S&P 500 (2000 peak, real returns) — TTR ~13 years.
- AQR long-short value (2007 peak) — still underwater in 2020, recovered partially through 2023.
- Trend-following (managed futures index, 2014 peak) — TTR ~7 years.

The behavioral truth: **clients fire managers around 3 years underwater, regardless of whether the underlying edge has decayed**. This is the operational risk in a Sharpe-1 strategy with σ=10% — Lo (2002) shows roughly a 1-in-3 chance of being 2+ years underwater over a 10-year window, just from random walk.

## Why path-dependent metrics matter

A strategy with Sharpe 1.0 and a strategy with Sharpe 1.0 can have very different paths:

- **High autocorrelation in losses.** A trend-following strategy bleeds in choppy markets — 30-day losing streaks are common. Same Sharpe, much worse UI.
- **Negative skew.** Short-vol / short-gamma / carry trades have small steady gains punctuated by large drawdowns. Sharpe overstates them; Calmar / UI penalize them honestly.
- **Path-dependent leverage.** A vol-targeted strategy that delevers into drawdowns realizes the drawdown but loses the upside of recovery. The arithmetic mean stays similar; the geometric mean (= compounded wealth) drops.

For an investor (or for your own discipline), the question is never just "what's my Sharpe?" but "**will I still be running this strategy three years from now if it has a bad two-year stretch?**" Sharpe doesn't answer that; drawdown metrics do.

## Drawdown control as a strategy choice

Some strategies have inherently bounded drawdowns:

- **Vol targeting** (see [[position-sizing|position sizing]]) — reduces exposure as realized vol rises, mechanically dampens deep drawdowns of vol-driven strategies. Costs upside in trending regimes.
- **Stop-out rules.** Hard de-risk at −10% MTD, full halt at −20%. Introduces path-dependent path-dependence; backtests must respect the rule.
- **Risk-parity allocation** (Asness-Frazzini-Pedersen 2012). Lower drawdowns at portfolio level via diversification; only works when the assets actually diversify in the tail.

## Common pitfalls

- **Reporting Sharpe without drawdown stats.** A 2.0 Sharpe with a 60% MDD will not be held by anyone.
- **In-sample MDD as forecast.** The forward MDD is, in expectation, larger than the backtest MDD — sample-length artifact.
- **Calmar gaming.** Pick a short window that avoids the worst drawdown. Always show Calmar on full history AND rolling windows.
- **Ignoring recovery.** A −30% drawdown that recovers in 6 months is operationally different from one that takes 6 years. Always report TTR alongside MDD.

## See also

- [[risk-adjusted-returns|Risk-adjusted returns]] — Sharpe, Sortino, and why drawdown sits alongside them
- [[var-cvar|VaR / CVaR]] — point-in-time tail risk vs path risk
- [[position-sizing|Position sizing]] — vol targeting as a drawdown-control tool
- [[backtesting-discipline|Backtesting discipline]] — drawdown sample-size warnings

## External references

- Martin & McCann (1989). *The Investor's Guide to Fidelity Funds*. Wiley. Origin of the Ulcer Index.
- Magdon-Ismail & Atiya (2004). "Maximum Drawdown." *Risk* 17(10).
- Young (1991). "Calmar Ratio: A Smoother Tool." *Futures* 20(1).
- Lo (2002). "The Statistics of Sharpe Ratios." *Financial Analysts Journal* 58(4).
- Kestner (2003). *Quantitative Trading Strategies*. McGraw-Hill.
