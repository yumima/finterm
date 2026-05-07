**CVaR (Conditional Value at Risk)**, also called **Expected Shortfall (ES)**, is the average loss expected in the worst scenarios beyond the VaR threshold — it answers "given that we exceeded VaR, how bad is the expected loss?"

While [[var|VaR]] only tells you the maximum loss at a given confidence level without telling you what happens if that level is breached, CVaR quantifies the *average* loss in that tail scenario. It is a more coherent and more complete risk measure.

## Formula

```
CVaR_α = E[Loss | Loss > VaR_α]
       = (1 / (1−α)) × ∫ from α to 1  of  F^{-1}(u) du

Simplified for discrete returns:
CVaR_95% = Average of the worst 5% of daily losses
```

## Worked example

```
Portfolio: $1,000,000
Daily returns over 1 year (252 trading days)

Sort worst 5% = worst 12.6 days (round to 13):
Day 1 (worst): −4.2%
Day 2:         −3.8%
...
Day 13:        −2.3%
Average:       −3.1%

VaR (95%, 1-day):   $23,000 (the 13th worst loss)
CVaR (95%, 1-day):  $31,000 (average of 13 worst days)
```

CVaR = $31,000 says: "In our worst 5% of days, we lose $31,000 on average — not just $23,000."

## VaR vs CVaR comparison

| Feature | VaR | CVaR / Expected Shortfall |
|---|---|---|
| What it measures | Loss threshold at confidence level | Average loss beyond threshold |
| Tail behavior | Ignores | Captures |
| Mathematical properties | Not sub-additive | Sub-additive (coherent) |
| Regulatory use | Basel II/III (moving to CVaR) | Basel III + (internal models) |
| Interpretability | Easy (one number) | Slightly harder |
| Sensitivity to extreme events | Low | High |

"Sub-additive" means CVaR of a portfolio ≤ sum of CVaRs of components — i.e., diversification always reduces CVaR. VaR lacks this desirable property.

## CVaR in portfolio optimization

CVaR can be used directly in portfolio optimization as an objective or constraint:

```
Minimize CVaR_95% subject to:
  Expected return ≥ target
  Position constraints
```

This produces portfolios optimized for tail risk, not just volatility. CVaR-optimized portfolios tend to be more diversified and less concentrated in volatile assets than mean-variance portfolios.

## Regulatory context

Basel III (and its "Basel IV" revisions) moved banks from VaR to Expected Shortfall (97.5%) for internal model approaches, effective 2023. The shift was driven by the 2008 crisis, where VaR-based models systematically underestimated actual tail losses.

## Pitfalls

- CVaR is still model-dependent — if the underlying return distribution assumption is wrong (as it often is in crises), CVaR estimates will be wrong.
- Historical simulation-based CVaR relies on the past tail events being representative of future tails.
- Monte Carlo-based CVaR requires accurate correlation estimates — correlations spike in crises, and historical correlation matrices understate this.
- A low CVaR portfolio can still suffer catastrophic loss if a genuine black swan occurs that's outside the historical observation window.

See also: [[var|VaR]], [[tail-risk|Tail Risk]], [[drawdown|Drawdown]], [[volatility|Volatility]], [[sortino-ratio|Sortino Ratio]].
