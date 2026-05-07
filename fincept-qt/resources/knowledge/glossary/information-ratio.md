The **information ratio (IR)** measures an active portfolio manager's risk-adjusted outperformance relative to a benchmark — how much excess return (alpha) was earned per unit of active risk (tracking error).

It is the primary performance metric for evaluating active management: does the manager consistently generate alpha, and is that alpha large relative to the risk of deviating from the benchmark?

## Formula

```
Information Ratio = Active Return / Tracking Error
                 = (Portfolio Return − Benchmark Return) / σ(Active Return)

Active Return   = Portfolio Return − Benchmark Return  (alpha)
Tracking Error  = Standard deviation of active returns
```

## Worked example

```
Fund annual returns: +14.5%, +12.1%, +16.8%, +11.2%, +13.9%
S&P 500 returns:     +12.0%, +10.5%, +14.0%, +9.8%, +12.5%
Active returns:       +2.5%, +1.6%, +2.8%, +1.4%, +1.4%

Average active return: 1.94% per year
Std dev of active returns: 0.60%

Information Ratio = 1.94% / 0.60% = 3.23
```

A 3.23 IR is exceptional; the manager added 1.94% alpha per year with high consistency.

## Interpretation thresholds

| IR Range | Assessment |
|---|---|
| < 0 | Destroys alpha; underperforms benchmark on risk-adjusted basis |
| 0–0.3 | Poor |
| 0.3–0.5 | Acceptable for institutional use |
| 0.5–1.0 | Good — strong manager |
| 1.0–2.0 | Excellent — top quartile |
| > 2.0 | Exceptional (rare in practice) |

Academic research suggests the average active equity fund has IR close to zero or slightly negative over long periods after fees.

## IR vs. Sharpe ratio

| Feature | Sharpe Ratio | Information Ratio |
|---|---|---|
| Return vs. | Risk-free rate | Benchmark return |
| Risk denominator | Total volatility | Tracking error (active risk) |
| Context | Absolute return | Relative performance |
| Use case | Compare any portfolio | Evaluate active management |

IR is the Sharpe of alpha — it rewards consistent outperformance regardless of how volatile the underlying portfolio is.

## The Grinold-Kahn Fundamental Law of Active Management

```
IR ≈ IC × √BR

IC = Information Coefficient (correlation between predictions and outcomes)
BR = Breadth (number of independent bets per year)
```

This framework says: IR improves by making many independent bets (breadth) and by having skill in those predictions (IC). A high-frequency quantitative strategy can achieve good IR through breadth even with modest IC.

## Pitfalls

- IR is calculated over historical periods — strong past IR doesn't guarantee future IR; manager skill can decay or disappear.
- Short measurement periods produce unreliable IR estimates; minimum 3–5 years recommended for statistical significance.
- IR can be inflated by a manager concentrating risk in one big bet that happened to work out.
- Negative IR doesn't mean the manager is unskilled in all conditions — it may reflect a regime mismatch.

See also: [[sharpe-ratio|Sharpe Ratio]], [[alpha|Alpha]], [[sortino-ratio|Sortino Ratio]], [[beta|Beta]], [[tracking-error|Tracking Error]].
