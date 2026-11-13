**Tail risk** refers to the probability and impact of extreme, low-frequency events that fall in the "tails" of a return distribution — outcomes that are far from the average expectation and often far more severe than standard risk models predict.

The term comes from statistics: the tails of a normal distribution represent the extreme outcomes beyond 2–3 standard deviations. In financial markets, actual return distributions have "fat tails" (leptokurtosis) — extreme events occur more often than a normal distribution would suggest.

## Normal distribution vs. fat tails

```
Normal distribution:
  ±1σ: 68.3% of observations
  ±2σ: 95.4% (2σ event happens 4.6% of time = ~12 trading days/year)
  ±3σ: 99.7% (3σ event happens 0.3% = ~1 trading day every 4 years)

Actual S&P 500:
  3σ moves occur ~6× more often than normal distribution predicts
  5σ+ events ("black swans") have occurred multiple times in modern history
```

## Historical tail events (S&P 500)

| Event | Date | S&P 500 Move | Sigma (vs prior vol) |
|---|---|---|---|
| Black Monday | Oct 19, 1987 | −22.6% | ~25σ |
| Global Financial Crisis | Oct 2008 | −16.8% (worst month) | ~8σ |
| COVID crash | Mar 12, 2020 | −9.5% single day | ~8σ |
| Flash Crash | May 6, 2010 | −9.2% intraday | ~10σ |

These events are statistically "impossible" under normality assumptions but happen regularly in practice.

## Measuring tail risk

**VaR (Value at Risk)** — maximum expected loss at a confidence level. Doesn't measure what happens in the tail.

**CVaR / Expected Shortfall** — average loss given that VaR threshold is breached. Captures tail severity. See [[cvar|CVaR]].

**Skewness and Kurtosis** — statistical measures of distribution shape:
- Negative skew: more frequent small gains, rare large losses (most equity portfolios).
- High kurtosis (leptokurtosis): fat tails; extreme events more likely.

## Hedging tail risk

Common tail risk hedges:

| Instrument | Cost | Effectiveness |
|---|---|---|
| OTM puts (SPX, SPY) | High (expensive IV) | Direct, high payoff in crash |
| VIX calls | Moderate | Pays off in vol spikes |
| Gold | Low carry | Diversification; hedge against real rate collapse |
| Long Treasuries | Yield cost | Effective in deflationary crises; poor in inflationary crises |
| Tail risk funds (Universa, Capstone) | Ongoing premium | Systematic, managed |

## The cost of tail hedging

Tail hedges are typically expensive relative to frequency of use. The cost-benefit depends on:
1. Portfolio convexity needs (leveraged portfolios require more protection).
2. Ability to tolerate drawdowns without forced selling.
3. Investor time horizon.

Nassim Taleb's "barbell strategy": hold mostly safe assets + a small position in extreme payoff instruments. Avoid the middle.

## Pitfalls

- Tail risk models that use historical volatility as a starting point embed the assumption that the future resembles the past — it often doesn't in crises.
- VaR models are explicitly bad at measuring tail risk (by construction, they ignore what happens beyond the confidence level).
- Over-hedging tail risk via expensive OTM puts can create a persistent drag that exceeds the benefit across most market regimes.

See also: [[var|VaR]], [[cvar|CVaR]], [[volatility|Volatility]], [[drawdown|Drawdown]], [[sortino-ratio|Sortino Ratio]], [[vix|VIX]].
