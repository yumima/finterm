**CVaR** (Conditional Value at Risk), also called **Expected Shortfall (ES)**, measures the average loss in the worst scenarios beyond the VaR threshold — it answers "when things go really wrong, how bad does it get on average?"

```
CVaR = E[ Loss | Loss > VaR ]

Formally, at confidence level α:
CVaR_α = (1 / (1−α)) × ∫_{VaR_α}^{∞} x f(x) dx

For a parametric (normal) distribution:
CVaR = μ − σ × φ(z) / (1−α)

z    = VaR z-score (1.65 for 95 %, 2.33 for 99 %)
φ(z) = standard normal PDF evaluated at z
α    = confidence level (0.95 or 0.99)
```

## Worked example — $1 million portfolio

```
Portfolio value          $1,000,000
Daily volatility σ       1.2 %
Mean daily return μ      ≈ 0 % (conservative assumption)
Confidence level         95 %  → z = 1.65

VaR₉₅ = σ × z × V = 0.012 × 1.65 × 1,000,000 = $19,800

φ(1.65) = 0.1023   (standard normal PDF at 1.65)
(1 − α) = 0.05

CVaR₉₅ = 0 − 0.012 × (0.1023 / 0.05) × 1,000,000
        = −0.012 × 2.046 × 1,000,000
        = −$24,552

Interpretation: on the worst 5 % of days, the portfolio loses
$24,552 on average — about 24 % more than the VaR threshold.
```

## What the result means

CVaR is always equal to or greater than VaR in magnitude. The ratio CVaR/VaR indicates tail heaviness:

- Near **1.0×** — thin-tailed distribution; losses cluster just beyond VaR.
- **1.2 – 1.5×** — normal-ish tails (parametric assumption holds reasonably).
- **> 2×** — fat-tailed distribution (common in equities during crises); VaR severely underestimates tail risk.

Basel IV (effective 2025) replaced the 99 % VaR requirement with a 97.5 % ES (CVaR) requirement for market risk capital because ES is coherent (sub-additive) while VaR is not.

## Variants

- **Historical CVaR** — average of the worst (1−α)% actual P&L observations; no distribution assumption.
- **Monte Carlo CVaR** — average tail loss across thousands of simulated paths; captures non-linear payoffs.
- **Conditional Drawdown at Risk (CDaR)** — similar concept applied to drawdown paths rather than single-period returns.

## Common mistakes

- Confusing CVaR with "the worst-case loss" — it is still an average of the tail, not an absolute worst case.
- Estimating CVaR from short return histories — the tail is defined by a small fraction of observations; estimates are noisy without at least 500+ data points.
- Ignoring the distinction between parametric and historical CVaR during stress periods — historical CVaR picks up actual crisis behavior; parametric CVaR misses it if the distribution has fat tails.
- Treating CVaR as a complete risk measure without scenario analysis — black-swan events may not appear in any historical sample.

See also: [[var-parametric|VaR (Parametric)]], [[max-drawdown|Max Drawdown]], [[kelly-criterion|Kelly Criterion]], [[sortino-ratio|Sortino]].
