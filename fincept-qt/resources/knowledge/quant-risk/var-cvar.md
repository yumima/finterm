# Value-at-Risk and Conditional Value-at-Risk

VaR (Value-at-Risk) became the dominant risk metric in the 1990s — partly because RiskMetrics published the framework openly in 1996, partly because Basel regulators built capital requirements around it. CVaR (Conditional VaR, also called Expected Shortfall) has gradually displaced it for serious use because VaR has a critical flaw: it tells you a threshold but says nothing about what happens beyond.

This entry covers both: what they measure, how to estimate them, where each breaks, and how to use them honestly.

## What VaR measures

VaR at confidence level α over horizon h is the loss such that there is at most an (1−α)% probability of a worse loss within horizon h.

```
VaR_α(h) = − inf { x | Prob(L_h ≤ x) ≥ α }
```

Plain language: "95% one-day VaR of $1M" means there is a 5% chance of losing more than $1M tomorrow. Or: "you should expect a loss greater than $1M about once per 20 trading days."

Common confidence levels: 95% and 99% (Basel for trading book uses 99% over 10 days).

## Three estimation methods

### 1. Parametric (variance-covariance)

Assume returns are normal with mean μ and standard deviation σ. Then:

```
VaR_α = − (μ + z_{1−α} · σ) · Portfolio Value
```

(z_{1−α} = 1.645 for 95%, 2.326 for 99%.)

**Pros.** Fast, only needs μ and σ, scales easily to portfolio level via the covariance matrix.

**Cons.** Assumes normality. Equity returns are NOT normal — they have fat tails. Parametric VaR routinely **underestimates tail risk** for individual stocks by a factor of 2–5 at the 99% level.

**When to use it.** Quick risk checks on broad portfolios where the central limit theorem partially saves you (a portfolio of 50 stocks is closer to normal than any one of them). For Basel reporting, regulators historically allowed parametric VaR.

### 2. Historical simulation

Use the empirical distribution of past returns directly — no distributional assumption. To compute one-day 95% VaR from N days of history: sort historical returns ascending, take the 5%-quantile, multiply by portfolio value.

**Pros.** Captures fat tails, skew, and any other non-normal features of the data. No distributional assumption.

**Cons.** Past is not always prologue. If the lookback period was calm (2017), historical VaR understates risk for an upcoming volatile regime (2022). Only as good as the sample includes a representative crash.

**When to use it.** Default choice for most quant work. Use a sufficiently long lookback (3+ years) to catch some stress.

### 3. Monte Carlo

Specify a return-generating process (GARCH, multi-factor model with fat tails, regime-switching, etc.). Simulate forward 10,000+ paths, compute the empirical 5%-quantile.

**Pros.** Most flexible. Can model non-linear payoffs (options, structured products) that parametric and historical can't handle.

**Cons.** Slow. Sensitive to the model specification — garbage process in, garbage VaR out.

**When to use it.** Portfolios with significant non-linear exposure (option books, convertible portfolios). Risk teams at sell-side desks. Internal "economic capital" models.

## Why VaR is dangerous

VaR has three well-documented failure modes, each of which has caused real losses:

### 1. It says nothing about the tail beyond the threshold

VaR is a quantile, not an expectation. "95% one-day VaR of $1M" is consistent with a 5% scenario where you lose $1.1M *or* with a 5% scenario where you lose $50M. Two portfolios with identical VaR can have radically different tail behaviour.

The 1998 LTCM collapse is the canonical example — their VaR models said "$45M one-day at risk" while the actual loss when correlations broke was orders of magnitude larger. The model wasn't lying; it was answering a question that doesn't predict tail loss.

### 2. Not sub-additive

VaR can violate diversification in pathological cases: VaR(A + B) > VaR(A) + VaR(B). For discrete loss distributions or option books with non-linear payoffs, combining two risk-reducing positions can increase reported VaR. This breaks intuition and breaks risk-budgeting algorithms that assume sub-additivity.

### 3. Encourages tail-hugging strategies

If your bonus depends on VaR-adjusted performance, the rational play is to sell deep out-of-the-money options. The losses fall in the 1% tail (not contributing to 99% VaR), the premium income inflates Sharpe, and reported risk looks low — until the tail event arrives. Variance-swap blow-ups (LTCM, AIG 2008, Volmageddon 2018) often trace to this incentive.

## Conditional VaR (Expected Shortfall)

CVaR is the **expected loss given that the loss exceeds VaR**:

```
CVaR_α = E[ L | L > VaR_α ]
```

If 95% VaR is $1M and CVaR is $3M, then "in the worst 5% of days, you lose on average $3M."

**Why it's better than VaR:**

- **Tells you about the tail.** Two portfolios with the same VaR can have very different CVaR. CVaR is the more honest number.
- **Sub-additive.** CVaR(A + B) ≤ CVaR(A) + CVaR(B) always. Plays nicely with risk-budgeting.
- **Coherent risk measure.** Satisfies the Artzner-Delbaen-Eber-Heath (1999) axioms; VaR doesn't.

**Why it's not universal yet:**

- Slightly harder to estimate — needs more sample to be reliable at the tail.
- Regulatory inertia. Basel II used VaR, Basel III (2017+) is moving to Expected Shortfall, but the transition is still in progress.
- Historical-data-only CVaR can be dominated by the single worst observation in the lookback (the "1987 in the sample, 1987 not in the sample" problem). Mitigate with EVT-style tail modeling.

## Estimation: Rockafellar-Uryasev linear programming

A surprising practical result: CVaR can be **optimized via linear programming**, while VaR cannot (VaR is non-convex in portfolio weights). Rockafellar & Uryasev (2000) showed that:

```
CVaR_α(w) = min_ξ { ξ + (1/(1−α)) · E[(L(w) − ξ)⁺] }
```

The right-hand side is a linear program over the scenarios. This means **portfolio construction with CVaR constraints** is a well-posed LP — you can build risk-managed portfolios subject to CVaR ≤ threshold, in a single optimization. Several practitioner shops use this directly for portfolio construction (Rockafellar-Uryasev approach to risk-budgeted portfolios).

## Worked example

Equity-long-only portfolio, $10M notional, 50 stocks, equal-weighted, 250 trading-day lookback.

Historical 95% VaR (one day): sort 250 P&L observations ascending; the 13th-worst (5%·250) is −$182,000. So 95% one-day VaR = $182K.

Historical 99% VaR: 3rd-worst = −$405,000.

Historical 95% CVaR: average of the 12 worst observations = −$310K. CVaR exceeds VaR by ~70%.

If 2020 March is in the lookback: CVaR includes those days, will be far higher (CVaR_95 ≈ $500K+). If lookback is 2021–2023 only: CVaR_95 ≈ $250K — gives a false sense of safety.

This is the essential insight: **a 250-day lookback is not enough to capture tail risk unless that lookback includes a stress period.** Practitioners use 3+ years, or layer GARCH-implied vol scaling on top to amplify the tail.

## What to report

A defensible risk report includes:

- 95% and 99% VaR over the relevant horizon.
- 95% and 99% CVaR over the same horizon.
- A breakdown by factor / sector / asset class.
- Historical drawdown distribution (the empirical max-drawdown vs portfolio-life ratio).
- Stress test: simulated loss in 2008-style, 2020 March-style, and 2022 rates-rise scenarios.
- The model assumptions, prominently. If parametric, the assumed distribution. If historical, the lookback window. If Monte Carlo, the simulated model.

## What VaR/CVaR won't tell you

- **Liquidity risk.** VaR assumes you can liquidate at the close. In stress, you can't.
- **Funding risk.** Leveraged positions can be liquidated by the broker before the VaR scenario plays out.
- **Counterparty risk.** Most VaR models ignore the chance that your hedge counterparty defaults.
- **Model risk.** The VaR model itself can be wrong. Use independent stress scenarios as a cross-check.

## See also

- [[risk-adjusted-returns|Risk-adjusted return measures]] — Sharpe / Sortino / Calmar
- [[max-drawdown|Max drawdown (formula)]]
- [[cvar|CVaR (formula)]]
- [[var-parametric|VaR parametric (formula)]]
- [[portfolio-construction|Portfolio construction]] — CVaR-constrained optimization
- [[time-series-basics|Time-series basics]] — GARCH for dynamic volatility input to VaR

## External references

- Jorion (2007). *Value at Risk: The New Benchmark for Managing Financial Risk* (3rd ed). McGraw-Hill. The textbook.
- Artzner, Delbaen, Eber, Heath (1999). "Coherent Measures of Risk." *Mathematical Finance*.
- Rockafellar & Uryasev (2000). "Optimization of Conditional Value-at-Risk." *Journal of Risk*.
- RiskMetrics Technical Document (1996). J.P. Morgan / Reuters. The original parametric framework.
- Basel Committee (2019). "Minimum capital requirements for market risk." (Basel III FRTB — formal move from VaR to Expected Shortfall.)
- Acerbi & Tasche (2002). "On the Coherence of Expected Shortfall." *Journal of Banking and Finance*.
