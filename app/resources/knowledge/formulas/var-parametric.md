**Parametric VaR** estimates the maximum dollar loss a portfolio is unlikely to exceed over a given time horizon at a specified confidence level, assuming returns follow a normal distribution.

```
VaR = σ × z × √t × V

σ   = daily standard deviation of portfolio returns (as a decimal)
z   = z-score for chosen confidence level
      (1.65 for 95 %, 2.33 for 99 %)
t   = time horizon in trading days
V   = current portfolio value
```

The result is the loss threshold: there is a (1 − confidence) probability that the actual loss exceeds VaR over the specified period.

## Worked example — $1 million equity portfolio

```
Portfolio value (V)          $1,000,000
Daily volatility (σ)         1.2 %  (annualized vol ÷ sqrt(252) = 19.06 % / 15.87)
Confidence level             99 %   → z = 2.33
Time horizon (t)             1 trading day

VaR = 0.012 × 2.33 × sqrt(1) × 1,000,000
    = 0.012 × 2.33 × 1 × 1,000,000
    = $27,960

10-day VaR (regulatory):
VaR₁₀ = 0.012 × 2.33 × sqrt(10) × 1,000,000
       = 0.012 × 2.33 × 3.162 × 1,000,000
       = $88,400
```

There is a **1 % chance** of losing more than $27,960 in a single trading day, or more than $88,400 over 10 trading days.

## What the result means

VaR defines a **confidence band on losses** — it does not say anything about how large the loss could be in the tail beyond VaR. Two portfolios with identical VaR can have wildly different expected losses when VaR is breached. For that, use [[cvar|CVaR/Expected Shortfall]].

Basel III requires banks to hold capital against a 10-day, 99 % VaR. Risk managers typically monitor 1-day 95 % and 99 % VaR daily.

## Variants

- **Historical VaR** — sorts actual historical P&L and reads the percentile; no normality assumption.
- **Monte Carlo VaR** — simulates thousands of return paths from a fitted distribution; captures non-linearities (e.g., options books).
- **[[cvar|CVaR (Conditional VaR / Expected Shortfall)]]** — averages all losses beyond VaR; preferred by regulators under Basel IV (expected shortfall replaces VaR).
- **Component VaR** — decomposes portfolio VaR into each position's marginal contribution; used for risk budgeting.

## Common mistakes

- Treating VaR as the maximum possible loss — it is a percentile threshold, not a ceiling; tail losses can be multiples of VaR.
- Using parametric VaR for options-heavy portfolios — the normal distribution assumption breaks down when positions have asymmetric payoffs.
- Failing to account for correlation breakdown in stress periods — correlations rise toward 1.0 during crises, making parametric VaR underestimate risk precisely when it matters most.
- Annualizing daily VaR by multiplying by 252 instead of sqrt(252) — the scaling law for volatility uses the square root of time.

See also: [[cvar|CVaR]], [[max-drawdown|Max Drawdown]], [[sortino-ratio|Sortino]], [[sharpe-ratio|Sharpe Ratio]].
