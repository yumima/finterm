# Sharpe Ratio

Return per unit of risk — the most widely cited risk-adjusted performance metric.

```
Sharpe = (Portfolio Return − Risk-free Rate) / Std Dev of Portfolio Returns
```

Both numerator and denominator are usually annualized.

## Worked example

Strategy A and Strategy B both earned 12% over the year. Risk-free rate was 4%.

```
Strategy A:  return 12%, vol 8%
  Sharpe = (12 − 4) / 8 = 1.0    "good"

Strategy B:  return 12%, vol 25%
  Sharpe = (12 − 4) / 25 = 0.32  "mediocre"
```

Same return, very different experience: A's monthly P&L is gentle; B's is white-knuckle. Sharpe captures that difference.

## How to read it

- **< 0** → you'd have done better in T-bills
- **0 to 1** → mediocre
- **1 to 2** → good
- **2 to 3** → very good
- **> 3** → suspicious; likely backtest overfitting or hidden tail risk

## Reference Sharpe ratios

| Asset / strategy | Long-run Sharpe |
|---|---|
| 60/40 portfolio | 0.4–0.6 |
| S&P 500 | 0.4–0.5 |
| Investment-grade bonds | 0.3–0.5 |
| Top mutual funds (5y) | 0.7–1.2 |
| Best hedge funds (10y, gross) | 1.0–2.0 |
| Renaissance Medallion (since 1988) | ~3+ |
| Backtested strategies that "look great" | often 4+ → red flag |

## Variants worth knowing

- **Sortino ratio** — uses downside deviation only; doesn't penalize upside vol
- **Calmar ratio** — annualized return divided by max drawdown
- **Information ratio** — alpha / tracking error (active management metric)
- **Probabilistic Sharpe Ratio** — accounts for non-normal returns and sample size

## Decision rules

- **Fund Sharpe < 0.5 vs benchmark Sharpe ≈ 0.5** → no edge; consider passive
- **Strategy Sharpe > 2 in backtest** → sniff for overfitting / data mining
- **Sharpe high in calm regime, drops in vol spike** → strategy is hidden short-vol
- **Sharpe stable across 3 different regimes** → genuinely persistent

## What it gets wrong

- **Treats upside and downside volatility identically.** Big up days are "risk." That's why the **Sortino ratio** (downside deviation only) is often preferred.
- **Assumes returns are normally distributed.** Real returns have fat tails — a strategy that quietly earns and occasionally explodes (selling far-out-of-the-money options) shows great Sharpe right up until it doesn't.
- **Sensitive to the window**. A 12-month Sharpe in a calm market overstates skill; in a volatile market it understates.
- **Doesn't penalize lumpy returns.** A strategy with one massive month and 11 flat months can show a great Sharpe even if it's mostly luck.

## In finterm

Strategies in Backtesting report Sharpe alongside [[drawdown|max drawdown]] for exactly this reason — Sharpe alone hides tail behavior. Always look at both, plus the equity curve shape.
