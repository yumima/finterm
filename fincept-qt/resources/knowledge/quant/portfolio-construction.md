# Portfolio Construction: Mean-Variance & Kelly

You have a set of expected returns, an estimated covariance matrix, and a budget. How do you size positions? Two foundational answers — both Nobel-adjacent, both routinely misapplied — frame every modern construction technique.

**Markowitz mean-variance** (1952) gives the optimal portfolio for the next single period given your inputs. **Kelly** (1956, Bell Labs) gives the optimal compound growth rate over many periods. They answer different questions, and the practical wisdom is to apply both as bounds, never either as a literal sizing rule.

## Markowitz mean-variance — the framework

Given expected returns vector μ and covariance matrix Σ, the optimal portfolio weights w that maximize utility (return − risk-aversion × variance) are:

```
w* = (1/λ) · Σ⁻¹ · μ
```

For a no-leverage constraint Σ w_i = 1, you scale w* afterwards. The set of all efficient portfolios — the highest expected return achievable at each variance level — traces out the **efficient frontier**. Anything below the frontier is dominated.

**Why this is correct in principle.** It minimizes risk for any target return, or maximizes return for any target risk. With perfect knowledge of μ and Σ, you cannot do better.

**Why this is catastrophic in practice.** You don't have perfect knowledge of μ and Σ. You have estimates, and the optimizer is **maximally sensitive to estimation error in expected returns** (Michaud 1989). Tiny changes in μ produce huge changes in w*. Real implementations of naive Markowitz routinely concentrate the whole portfolio in 1–3 assets, lever those up, and short the rest — clearly insane.

## Why MV fails — and the fixes

### Estimation error

A sample mean of asset returns over 5 years has standard error ≈ σ / √5, often comparable to the mean itself. You are dividing μ̂ by Σ̂⁻¹ where both are noisy — the optimizer amplifies the noise.

**Fix 1: Shrinkage.** Ledoit & Wolf (2003) pull the sample covariance toward a structured target (constant-correlation or identity). Reduces estimation error in Σ; for cross-sectional portfolios this alone improves out-of-sample Sharpe meaningfully.

**Fix 2: Don't estimate μ.** Risk-parity, minimum-variance, and equal-weight portfolios all step around the most-noisy input. **Minimum-variance** (w ∝ Σ⁻¹·1) ignores expected returns entirely; **risk-parity** allocates so each asset contributes equal volatility; **equal-weight** is the trivial baseline that beats most fancy approaches over long horizons (DeMiguel, Garlappi, Uppal 2009).

**Fix 3: Black-Litterman.** Combine the CAPM equilibrium prior with explicit views on a subset of assets, weighted by your confidence in the view. Produces sensible, diversified portfolios when you have specific opinions. He & Litterman (1999, Goldman Sachs) is the classic reference.

### Concentration

Even with cleaned inputs, MV will concentrate. Practical implementations add:

- **Position caps** (no single asset > 5% of portfolio).
- **Sector caps**.
- **Turnover penalty** (regularization on |w_new − w_old|) to prevent rebalancing every period.

These are constraints, not approximations of the "true" optimum. The "true" optimum is fictional; the constrained version is what trades.

### Out-of-sample reality

DeMiguel, Garlappi, Uppal's "Optimal Versus Naive Diversification" (2009) tested 14 portfolio-construction techniques against equal-weight (1/N) on real data. **Equal-weight beat every sophisticated method on out-of-sample Sharpe** for typical estimation windows. The lesson: estimation noise overwhelms the theoretical optimality of MV unless your sample is huge.

For long-horizon, capacity-unconstrained allocation, **risk parity** is the dominant practitioner choice — Bridgewater's All Weather popularized it; it survived 2008 and 2020 better than 60/40 (though 2022 was painful).

## Kelly criterion — the long-run perspective

Kelly answers a different question: given a sequence of bets with known edge, what fraction of capital maximizes the long-run compound growth rate?

For a single binary bet (probability p of winning b times stake, probability q=1−p of losing the stake):

```
f* = p − q/b
```

(For p = 55%, b = 1: f* = 10% of capital per bet.) Larger edge or higher win rate → larger fraction. Negative edge → don't bet.

For continuous returns with expected return μ and variance σ²:

```
f* = μ / σ²
```

(The "Kelly fraction" is the same thing as the mean-variance optimum at risk-aversion λ = 1.)

**The growth-vs-volatility trade-off.** Kelly maximizes expected log return (geometric growth). Half-Kelly (f = ½ · f*) sacrifices about 25% of long-run growth but cuts the volatility of growth by half. This is why practitioners run **fractional Kelly** — typically 1/4 to 1/2 Kelly — to survive the drawdowns that full Kelly produces.

**Full Kelly is brutal.** A full-Kelly bettor with a 55%-edge coin flip experiences a 50% drawdown roughly once per 100 bets. Most humans (and most allocators) liquidate the strategy before the drawdown recovers. Half-Kelly drawdowns are dramatically smaller.

**Estimation error compounds the problem.** If your true edge is 10% but you estimate it as 15%, full Kelly is 50% over-sized — you lever up a bet that doesn't actually exist. Half-Kelly buys you margin against estimation error.

See the dedicated [[kelly-criterion|Kelly criterion formula]] entry in formulas for the derivation and worked examples.

## Hierarchical Risk Parity (HRP)

López de Prado (2016) proposed HRP as a compromise between MV's elegance and equal-weight's robustness. The algorithm:

1. Build a distance matrix from the correlation matrix.
2. Hierarchically cluster assets — similar assets group together.
3. Allocate capital top-down: split capital between clusters by inverse-variance, then recurse within each cluster.

Result: a diversified portfolio that respects asset similarity without inverting Σ (which is what breaks MV). HRP outperforms naive MV on out-of-sample Sharpe in López de Prado's tests and is gaining adoption in long-only multi-asset allocation.

## Practical workflow

When sizing a strategy:

1. **Start with Markowitz on cleaned, shrunken Σ** to get an order-of-magnitude theoretical answer.
2. **Apply position and sector caps** to prevent over-concentration.
3. **Scale total leverage by half-Kelly or quarter-Kelly** of estimated portfolio Sharpe — never full Kelly, never naive MV's implied leverage.
4. **Stress-test the allocation** against historical correlation breaks. See [[var-cvar|VaR/CVaR]] for the methodology.
5. **Walk-forward the construction.** Re-estimate Σ and μ at each rebalance with the same lookback you'll use in production.

For a multi-asset long-only book, **risk parity** is usually the right baseline; you add tactical tilts (over/under-weight equities vs bonds vs commodities) on top of it. For a long-short equity factor portfolio, **minimum-variance with sector caps** dominates naive MV. For a single-strategy book (one signal, one universe), **fractional Kelly with hard volatility targeting** is the practical sizing rule.

## Common pitfalls

- **Stale covariance.** Σ estimated from 2017–2019 doesn't reflect post-COVID correlation regimes. Use exponentially weighted Σ with half-life ~6 months.
- **Forgetting correlations break in crashes.** Diversification benefits disappear in stress periods (2008, March 2020). Stress-test the allocation under a 3× correlation increase.
- **Position-size ratcheting.** Some practitioners scale positions to "constant dollar risk" using realized vol. This levers up during low-vol regimes (2017) and gets crushed in the volatility spike. Either cap leverage or scale by forecast vol (GARCH), not realized.
- **Mistaking Kelly for prescription.** Kelly tells you the optimum given perfect knowledge of edge. With estimation error, **always** size at a fraction of Kelly.

## See also

- [[kelly-criterion|Kelly criterion (formula)]]
- [[capm|CAPM (formula)]] — Black-Litterman builds on the equilibrium implied by CAPM
- [[risk-adjusted-returns|Risk-adjusted return measures]]
- [[var-cvar|VaR / CVaR]] — risk-constraint side of construction
- [[strategy-patterns|Strategy patterns]] — different patterns have different ideal construction

## External references

- Markowitz (1952). "Portfolio Selection." *Journal of Finance*.
- Sharpe (1964). "Capital Asset Prices." *Journal of Finance*. (CAPM)
- Michaud (1989). "The Markowitz Optimization Enigma." *Financial Analysts Journal*.
- Ledoit & Wolf (2003). "Improved Estimation of the Covariance Matrix." *Journal of Empirical Finance*.
- He & Litterman (1999). "The Intuition Behind Black-Litterman Model Portfolios." Goldman Sachs.
- DeMiguel, Garlappi, & Uppal (2009). "Optimal Versus Naive Diversification." *Review of Financial Studies*.
- Kelly (1956). "A New Interpretation of Information Rate." *Bell System Technical Journal*.
- Thorp (2006). "The Kelly Criterion in Blackjack, Sports Betting, and the Stock Market." Wilmott Magazine.
- MacLean, Thorp, Ziemba (2011). *The Kelly Capital Growth Investment Criterion*. World Scientific.
- López de Prado (2016). "Building Diversified Portfolios that Outperform Out of Sample." *Journal of Portfolio Management*.
