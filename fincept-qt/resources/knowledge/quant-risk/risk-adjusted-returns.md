# Risk-Adjusted Return Measures

Raw return is the wrong number to compare strategies. A 40% return earned with 60% volatility is not better than 12% earned with 8% volatility — the first is a coin-flip levered up, the second is a robust edge. Risk-adjusted return measures answer the question quants actually care about: **how much excess return per unit of risk?**

This entry surveys the measures, their formulas, when each is appropriate, and where each breaks down.

## The Sharpe ratio — the default benchmark

```
Sharpe = (Rp − Rf) / σp
```

(Annualized.) The numerator is excess return over the risk-free rate; the denominator is the standard deviation of returns. See [[sharpe-ratio|Sharpe ratio entry]] in formulas for the full treatment.

**When to use it:** comparing strategies whose returns are roughly normal and symmetric (long-short equity factor portfolios, broad index funds, balanced portfolios).

**When it lies:**

- **Asymmetric return distributions.** Strategies that sell options or write convexity (short vol, market-making, carry trade) have positively skewed Sharpe ratios that misrepresent the tail. They look great until the crash.
- **Manipulable.** Reporting Sharpe at low frequency (annual instead of monthly) inflates it; choosing the start date that excludes drawdowns inflates it; using stale prices for illiquid assets inflates it.
- **Doesn't distinguish good vol from bad vol.** A strategy that gains 5% / month consistently and another that ranges between +30% and −20% have very different Sharpe ratios even if both end at the same place.

## Sortino — penalising only downside

```
Sortino = (Rp − Rf) / σ_downside

σ_downside = standard deviation of returns BELOW a target (usually 0 or Rf)
```

See [[sortino-ratio|Sortino entry]] in formulas.

Sortino is the natural fix for "upside is not risk." A momentum strategy that has occasional 8% up-months and steady 0.5% down-months has a Sharpe that punishes the up-months; Sortino correctly ignores them.

**When to use it:** strategies with positive skew or where upside volatility is the goal (trend-following, momentum, long-call-protected).

**Limits:** the denominator becomes unstable at low sample sizes (few downside observations means high estimation variance). Sortino > Sharpe is normal; Sortino >> Sharpe sometimes flags a strategy whose downside hasn't shown up yet.

## Calmar — the drawdown perspective

```
Calmar = annualized return / max drawdown (as positive number)
```

See [[max-drawdown|max-drawdown entry]] for the denominator.

What an institutional allocator actually feels: "if I had been invested at the worst possible moment, how badly was I down before recovery?" Calmar of 0.5 means a strategy returning 10% per year had at some point been down 20%. CTAs and trend-followers are typically evaluated this way because their drawdowns are the dimension that survives across regimes.

**When to use it:** evaluating long-running strategies for capital allocation. Capacity-aware decisions about how much to deploy.

**Limits:** dominated by the single worst drawdown. A strategy that had one −40% event in 2008 looks identical to one that had ten −30% events. Use alongside the *average* drawdown, not just max.

## Information ratio — the active-management measure

```
IR = (Rp − R_benchmark) / σ(Rp − R_benchmark)
```

See [[information-ratio|information-ratio entry]] in formulas.

Like Sharpe but using a benchmark instead of the risk-free rate, and using tracking error instead of total volatility. This is the right measure when the question is "did the active manager add value over a passive benchmark?" — the units are excess return per unit of *active* risk.

An IR of 0.5 over a long horizon is good for an equity manager; 1.0+ is exceptional. Renaissance Technologies' Medallion is reported at IR > 4 — essentially unheard of and capacity-constrained.

## Treynor — risk-premium per unit of beta

```
Treynor = (Rp − Rf) / β
```

See [[treynor-ratio|Treynor entry]].

Useful when the portfolio is part of a larger diversified holding, so only systematic (beta) risk matters; idiosyncratic risk gets diversified away. Less commonly used by quants in isolation but standard in evaluating mutual fund managers.

## What none of these capture

- **Crowding.** A strategy that everyone else also runs is fragile; popular factors mean-revert harder. Risk metrics computed on historical data don't see the current positioning of competitors.
- **Regime.** A strategy that had Sharpe 1.5 during a low-vol regime may have Sharpe 0 during a high-vol regime. Single-number metrics flatten this.
- **Capacity.** A Sharpe 2 strategy at $10M of capital may be Sharpe 0.5 at $1B. Risk metrics don't tell you the slope.
- **Stop-loss path-dependence.** If your strategy is "Sharpe 1.5 but with a hard stop at −20%," the metric doesn't reflect the realised distribution of paths.

## Quick rules of thumb

| Sharpe | Sortino | Calmar | Verdict |
|---|---|---|---|
| < 0 | < 0 | < 0 | Worse than cash |
| 0–0.5 | 0–0.7 | 0.1–0.3 | Marginal; barely beats risk-free |
| 0.5–1.0 | 0.7–1.4 | 0.3–0.6 | Acceptable; broad-index-like |
| 1.0–2.0 | 1.4–2.8 | 0.6–1.2 | Good; well-designed factor portfolio |
| > 2.0 | > 2.8 | > 1.2 | Suspicious — check for [[backtesting-discipline|overfitting]], cost assumptions, illiquidity premium, or capacity blow-up risk |

When evaluating a backtest, look at *all three* of Sharpe, Sortino, Calmar. A strategy with high Sharpe but low Calmar is hiding its worst drawdown in a single event; a strategy with high Calmar but low Sharpe has steady returns with rare tail losses you should investigate.

## See also

- [[sharpe-ratio|Sharpe ratio (formula)]]
- [[sortino-ratio|Sortino ratio (formula)]]
- [[max-drawdown|Max drawdown (formula)]]
- [[information-ratio|Information ratio (formula)]]
- [[treynor-ratio|Treynor ratio (formula)]]
- [[backtesting-discipline|Backtesting discipline]] — why high Sharpe in a backtest is often a red flag
