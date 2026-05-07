**The Sharpe ratio** measures how much excess return a portfolio earns for each unit of total risk (volatility) — it is the most widely used single number for comparing risk-adjusted performance.

```
Sharpe = (Rp − Rf) / σp

Rp  = portfolio return (annualized)
Rf  = risk-free rate (annualized, e.g., 3-month T-bill)
σp  = standard deviation of portfolio returns (annualized)
```

The numerator is called the **excess return** or **risk premium**; the denominator is total volatility including both up and down moves.

## Worked example — two funds compared

```
                Fund A        Fund B        SPY (benchmark)
Annual return   12.0 %        18.0 %        14.0 %
Rf              4.5 %         4.5 %         4.5 %
Volatility σ    8.0 %         22.0 %        16.0 %

Sharpe          (12.0 − 4.5) / 8.0   (18.0 − 4.5) / 22.0   (14.0 − 4.5) / 16.0
                = 7.5 / 8.0           = 13.5 / 22.0           = 9.5 / 16.0
                = 0.94                = 0.61                  = 0.59
```

Fund A has a lower absolute return than Fund B, but it is a far better risk-adjusted performer: it earns more excess return per unit of volatility. Fund B takes on nearly three times the risk for only 50 % more raw return.

## What the result means

Higher is better; the scale is relative, not absolute.

| Sharpe ratio | Interpretation |
|---|---|
| < 0          | Portfolio underperforms the risk-free rate |
| 0 – 0.5      | Weak; barely justifies taking market risk |
| 0.5 – 1.0    | Acceptable; typical for passive equity funds |
| 1.0 – 2.0    | Good; most active strategies target this range |
| > 2.0        | Excellent; rare in liquid markets over long horizons |

## Variants

- **Ex-ante Sharpe** — uses expected (forward-looking) return and volatility; useful in portfolio construction.
- **Ex-post Sharpe** — uses realized returns; the standard performance-evaluation metric.
- **[[sortino-ratio|Sortino ratio]]** — replaces total volatility with downside deviation only; penalizes only bad volatility.
- **[[treynor-ratio|Treynor ratio]]** — replaces σ with beta; appropriate for a single fund inside a diversified portfolio.
- **[[information-ratio|Information ratio]]** — uses excess return over a benchmark and tracking error instead of Rf and total volatility.

## Common mistakes

- Comparing Sharpe ratios across different measurement periods (monthly vs annual) without annualizing consistently — always annualize both return and volatility before computing.
- Using total return without subtracting Rf — inflates Sharpe mechanically when rates are high.
- Relying on Sharpe for strategies with non-normal return distributions (options selling, merger arb) — it will overstate risk-adjusted performance because σ does not capture fat tails.
- Interpreting a high Sharpe as "safe" — a strategy can have high Sharpe and still suffer large drawdowns.

See also: [[sortino-ratio|Sortino]], [[treynor-ratio|Treynor]], [[information-ratio|Information Ratio]], [[max-drawdown|Max Drawdown]].
