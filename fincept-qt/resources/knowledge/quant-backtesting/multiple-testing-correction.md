# Multiple-Testing Correction

If you run 200 backtests and report the best one, you are reporting the maximum of 200 noise draws — not a strategy. The best of N independent t-statistics from pure noise grows as roughly √(2 ln N): try 100 specifications on data with no edge and you should expect a t-statistic of ~3.0, equivalent to a Sharpe near 1.0 over a 10-year backtest. Quants who ignore this publish careers; quants who account for it survive live trading.

This entry covers the three corrections that matter: **Bonferroni** (worst-case, simplest), **Benjamini-Hochberg FDR** (false-discovery rate, more powerful), and the Bailey-López de Prado pair: **Deflated Sharpe Ratio (DSR)** and **Probability of Backtest Overfitting (PBO)**.

## The arithmetic of inflated tests

Under the null of zero true edge with N independent strategies tested:

```
E[max t-stat across N trials] ≈ √(2 · ln(N)) − constants
N = 10    → expected best |t| ≈ 1.94
N = 100   → expected best |t| ≈ 2.85
N = 1000  → expected best |t| ≈ 3.46
```

Without a correction, "Sharpe 1.5, t = 3" looks publishable when N = 1; from N = 100 trials it is roughly the null median. Harvey, Liu & Zhu (2016) audited the academic factor zoo by this logic and concluded most published anomalies have inflated significance — the credible threshold for new factors should be |t| ≥ 3.0 minimum, often higher.

## Bonferroni — the blunt correction

Multiply each p-value by N (the number of tests) before declaring significance:

```
p_adjusted = min(1, N · p_raw)
```

Equivalently, divide the significance threshold by N. Controls the **family-wise error rate (FWER)**: probability of *any* false positive across the whole batch. Conservative — power drops fast as N grows. Use Bonferroni when:

- You need an iron-clad worst-case guarantee (regulator-facing, capital-allocation decisions).
- N is small (≤ 20). At N = 200, Bonferroni demands raw p < 0.00025 — basically nothing survives.

Holm-Bonferroni is a small refinement that's strictly better; sort p-values ascending, compare the k-th to α / (N − k + 1).

## Benjamini-Hochberg — the practical correction

Benjamini & Hochberg (1995) control the **false discovery rate (FDR)**: the expected fraction of false positives among declared positives. Less paranoid than FWER, more powerful, and the right object for research pipelines.

Procedure: sort raw p-values ascending p₁ ≤ p₂ ≤ … ≤ p_N. Find the largest k where p_k ≤ (k/N) · α. Declare the first k significant.

At α = 0.10 with N = 200, BH typically passes several genuinely-signal strategies that Bonferroni would kill. This is the workhorse correction in modern quant research.

## Deflated Sharpe Ratio (Bailey & López de Prado 2014)

The DSR asks the right question for trading: given that I selected the best Sharpe out of N trials, what is the probability the true Sharpe is positive?

```
DSR = Prob[ true SR > 0 | observed SR, N trials, skew, kurtosis, T bars ]
```

The deflation accounts for four things at once:

- **Number of trials N.** Selecting from more candidates inflates the best.
- **Sample length T.** Short backtests have noisier SR estimates.
- **Non-normality.** Negative skew and high kurtosis (typical for short-vol and credit strategies) lower the effective sample size and inflate the SR estimator.
- **Cross-correlation among trials.** Effective N is lower than naive N when strategies are correlated.

A DSR of 0.95 means: even after deflation, there's a 95% probability the true Sharpe is positive. That's the only Sharpe number worth defending. Bailey & López de Prado provide closed-form expressions in *Journal of Portfolio Management* (2014); implementations are in López de Prado's `mlfinlab` package and many open-source repos.

Rule of thumb mapping from [[backtesting-discipline|backtest hygiene]]: try 100 specifications → demand observed Sharpe ≥ 2.5; 10 trials → 1.8; 1 pre-registered hypothesis → 1.0. The DSR formalises this rule.

## Probability of Backtest Overfitting (PBO)

Bailey, Borwein, López de Prado & Zhu (2017) introduced PBO as a probabilistic counterpart to DSR. The setup: you have N candidate strategies and a fixed time series. Split the timeline combinatorially into many train/test partitions (CSCV — Combinatorially Symmetric Cross-Validation). For each partition:

1. Pick the best strategy on the train half.
2. Check its rank on the test half.

```
PBO = Prob[ selected strategy's OOS rank is below median ]
```

A PBO above ~0.5 means your selection procedure is essentially random — the strategy that looked best in-sample is no better than a coin flip out-of-sample. Anything below 0.2 is plausibly genuine. Quants at AQR and Two Sigma routinely report PBO alongside DSR.

CSCV is closely related to the combinatorial purged CV (CPCV) discussed under [[walk-forward-validation|walk-forward validation]]; both generate distributions of OOS performance rather than point estimates.

## What to track per strategy

Before any optimisation, log:

- **Trials taken.** Number of distinct specifications evaluated.
- **Universe of trials.** Range of every hyperparameter that was searched.
- **Selection rule.** Was it max Sharpe, max Calmar, min drawdown? Different rules inflate differently.
- **Correlation among trials.** Are these 200 truly distinct strategies or are they 5 ideas tested with 40 minor variations each? Effective N is closer to 5.

The number to deflate by is *effective* N, not nominal. Harvey & Liu (2014) propose adjustments that account for correlation among test statistics; in practice, principal-component analysis on the trial-return matrix gives a workable estimate of effective N.

## Pre-registration as the cleanest fix

The cheapest multiple-testing correction is not running multiple tests. Specify your hypothesis, parameters, universe and exit rules *before* touching the data — see [[hypothesis-first|hypothesis-first research]]. A pre-registered single hypothesis carries N = 1 and earns its full nominal t-statistic.

In practice, real research mixes pre-registered tests with exploratory ones. The right discipline is to label each test as confirmatory or exploratory at the moment you run it, and apply deflation only to the exploratory pool.

## Common mistakes

- **Counting only "serious" trials.** "I tried 50 lookbacks but only 3 looked promising — so N = 3." No. N is every specification you evaluated, full stop.
- **Re-running the same strategy with cosmetic changes.** Volatility-target 10% vs 12% vs 15% is three trials, not three deeply different strategies.
- **Deflating after the fact.** Honest accounting requires logging trials as you go; reconstructing N from memory always understates.
- **Confusing FDR with FWER.** BH-adjusted p < 0.05 does NOT mean the strategy passes Bonferroni — different guarantees.

## Where to do this in the terminal

The **AI Quant Lab** logs every backtest invocation against a strategy spec and reports a running DSR and PBO based on the trial count. The **Backtesting** screen shows a "trials taken" counter and refuses to publish a strategy without an explicit deflation calculation.

## See also

- [[backtesting-discipline|Backtest hygiene]] — multiple-testing is sin #4
- [[walk-forward-validation|Walk-forward validation]] — CPCV generates the OOS distribution that PBO sits on top of
- [[hypothesis-first|Hypothesis-first research]] — the cheapest correction is not running multiple tests
- [[ml-pipeline-pitfalls|ML pipeline pitfalls]] — every hyperparameter search is implicit multiple testing

## External references

- Bailey & López de Prado (2014). "The Deflated Sharpe Ratio." *Journal of Portfolio Management* 40(5).
- Bailey, Borwein, López de Prado & Zhu (2017). "The Probability of Backtest Overfitting." *Journal of Computational Finance* 20(4).
- Harvey, Liu & Zhu (2016). "...and the Cross-Section of Expected Returns." *Review of Financial Studies* 29(1).
- Harvey & Liu (2014). "Multiple Testing in Economics." Duke working paper.
- Benjamini & Hochberg (1995). "Controlling the False Discovery Rate." *JRSS Series B*.
