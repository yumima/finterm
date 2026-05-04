# Kelly Criterion

The mathematically optimal fraction of capital to bet on a positive-edge opportunity. Derived by John L. Kelly in 1956; popularized for trading by Edward Thorp.

```
f* = (p × b − q) / b

where:
  p = probability of winning
  q = probability of losing = 1 − p
  b = win/loss payoff ratio
  f* = fraction of capital to bet
```

## Worked example

```
Strategy stats:
  Win rate (p):     60%
  Avg win:          $1.50  (b = 1.5)
  Avg loss:         $1.00

f* = (0.60 × 1.5 − 0.40) / 1.5
   = (0.90 − 0.40) / 1.5
   = 0.50 / 1.5
   = 33.3%

Full Kelly says bet 33.3% of capital per trade.
That's terrifying — and almost no one actually does it.
```

## Why no one uses full Kelly

Even with the **optimal fraction**, full Kelly produces brutal swings:
- Long-run growth rate is maximized
- But intermediate **drawdowns can exceed 50%** with high probability
- A 50% drawdown requires +100% to recover

Most practitioners use **fractional Kelly**:
- **½ Kelly**: bet 50% of f* — much smoother drawdowns; loses ~25% of growth rate
- **¼ Kelly**: bet 25% of f* — very conservative; standard for serious quants
- **⅛ Kelly**: bet 12.5% of f* — for highly uncertain edge

## Worked example — fractional Kelly

```
Same strategy, account $100,000:

Full Kelly:    bet 33.3% = $33,300/trade  → realistic max DD ~50%+
½ Kelly:       bet 16.7% = $16,700/trade  → realistic max DD ~30%
¼ Kelly:       bet 8.3%  = $8,300/trade   → realistic max DD ~20%
1% rule:       bet 1%    = $1,000/trade   → max DD ~10%
```

Most professionals settle around ¼ Kelly or below in real strategies.

## Why estimates kill you

Kelly is **catastrophically sensitive to estimation error**. If you think p=60% but it's actually 55%:

```
True p = 60%, b = 1.5:  f* = 33%
True p = 55%, b = 1.5:  f* = 25%
True p = 50%, b = 1.5:  f* = 17%
True p = 45%, b = 1.5:  f* = 8%

Betting 33% when true f* is only 8% → you go bankrupt with high probability.
```

This is why ¼ Kelly serves as a safety margin — it accommodates estimation error.

## When Kelly applies

- **Repeated independent bets** with stable probabilities
- **You can compute p and b reliably** from a long sample
- **Returns are roughly bounded** (bankruptcy is possible at full Kelly)
- **Bet outcome is binary** (win or lose by defined amounts)

## When Kelly breaks

- **Correlated bets**: portfolio Kelly is much smaller than per-trade Kelly
- **Non-stationary**: if edge degrades over time, Kelly leads to over-betting
- **Continuous outcomes**: real trades have distributions, not binary outcomes
- **Estimation uncertainty**: small sample → use much smaller fraction
- **Path-dependent payoffs**: options, leveraged ETFs, etc., need different math

## Continuous Kelly (for normally-distributed returns)

For normally-distributed return strategies:

```
f* = (μ − r) / σ²

where:
  μ = expected return
  r = risk-free rate
  σ = standard deviation of returns
```

This is the Sharpe-ratio-squared formulation. A strategy with Sharpe 1.0 (annualized) has f* ≈ Sharpe² for full Kelly leverage.

## Decision rules

- **Don't use Kelly until you have 100+ trades** of data on the strategy
- **Always fractional** — full Kelly is theoretical, fractional is practical
- **Recompute periodically** — your edge may have degraded
- **Cap at 25% of capital per single bet** even if Kelly says higher
- **Account for correlation** in portfolio Kelly
- **Halve Kelly during drawdowns** (e.g., when DD > 15%)

## Famous Kelly stories

- **Thorp's Princeton-Newport Partners** ran ~14% annualized for 20 years using fractional Kelly + market-neutral
- **Renaissance Medallion** uses Kelly-like sizing on thousands of small uncorrelated bets
- **LTCM 1998** is the cautionary tale of theoretical "Kelly-optimal" sizing meeting fat tails
- **Ed Thorp's Beat the Dealer** used Kelly for blackjack edge-betting

## In finterm

Portfolio sizing tools provide a Kelly calculator. Backtesting strategies surface implied Kelly fractions based on backtest stats — always discount for live-vs-backtest degradation.

## Related

- [[r-multiple]] — Kelly inputs come from R-distribution stats
- [[expected-value]] — Kelly maximizes the expected log-return
- [[expectancy]] — required positive expectancy for Kelly to make sense
