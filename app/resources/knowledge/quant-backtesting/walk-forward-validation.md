# Walk-Forward Validation

The single most reliable antidote to in-sample overfitting is the **walk-forward**: re-fit your model on a rolling window, evaluate on the immediately following window, slide forward, repeat. Stitch the out-of-sample (OOS) pieces together into one continuous equity curve. That stitched curve is the only Sharpe number worth quoting at a research meeting.

Robert Pardo formalised the technique for systematic traders in *The Evaluation and Optimization of Trading Strategies* (2008). Marcos López de Prado extended it for the ML era in *Advances in Financial Machine Learning* (2018), particularly with **purged k-fold** and **combinatorial purged cross-validation (CPCV)** for cases where the simple time slide doesn't isolate train from test.

## Why this exists

A backtest run once on all available history will hide overfitting because parameter choices made today have implicitly seen all of history. Holding out one fixed OOS block (see [[backtesting-discipline|backtest hygiene]]) is better but still wastes data — only a thin tail of history validates the model. Walk-forward uses the whole timeline for OOS without ever letting a parameter peek forward.

The mental model: every test bar is reached using only information that genuinely pre-dated it. The stitched OOS curve is what a disciplined researcher *would have* traded had they been running the procedure live since day one.

## Rolling vs anchored windows

There are two main flavours:

- **Rolling (sliding) window.** Train on the most recent N years; drop the oldest year as the window advances. Adapts to regime shifts. Appropriate when the data-generating process changes (most strategies on most assets).
- **Anchored (expanding) window.** Always train from day one to the current point; the train set grows. Statistically more efficient when the DGP is stationary, since each refit uses more data.

Practical choice: rolling is the default for trading strategies because financial time series are not stationary at multi-year horizons (a 2005-trained mean-reversion model on US single names will not survive 2017 onward, regardless of how good it was). Anchored is appropriate only when you have a strong prior that the regression is structural — e.g., the CAPM relationship between beta and expected return.

## Parameter optimisation inside the loop

This is where most walk-forwards quietly fail. Done correctly:

```
for each window (t_train_start, t_train_end, t_test_start, t_test_end):
    1. Pull data only up to t_train_end.
    2. Run parameter search on the train slice. Pick best parameter set.
    3. Lock those parameters. Run on test slice.
    4. Record test-slice returns.
slide window forward, repeat.
concatenate test slices → single OOS return series.
```

Done incorrectly: pick parameters globally (looking across all of history), then "walk-forward" by re-running with those fixed parameters. That is not walk-forward — it is in-sample fit in disguise. The parameter selection step itself is where lookahead enters.

A subtler variant: hyperparameter selection inside each train window must include its own internal validation (nested cross-validation). Otherwise you've overfit the training window to itself. López de Prado is explicit on this in Chapter 12 of *AFML*.

## The stitched OOS curve

The headline output is a single equity curve assembled from concatenated test slices. From it, compute:

- **Stitched Sharpe** — the OOS Sharpe of the full assembled series. The number that goes into the deck.
- **Window-by-window Sharpe distribution** — how much does performance vary across windows? A strategy with stitched Sharpe 1.4 and per-window Sharpes ranging [−0.5, +3.0] is a regime trade, not a robust edge.
- **Parameter drift.** Plot the chosen parameter at each window. If the "best" lookback ranges from 5 to 250 bars across windows, the optimisation is fitting noise — there is no stable optimum.
- **Train-vs-test Sharpe ratio.** López de Prado's deflation: if train Sharpe averages 2.5 and test averages 0.3, you're overfitting inside every window.

## When walk-forward isn't enough

Walk-forward is the practical workhorse but it does not solve everything. Three failure modes that survive a clean walk-forward:

- **Multiple-testing inflation across strategies.** Walk-forward validates one specification. If you try 200 specifications, the best stitched OOS Sharpe is still inflated. See [[multiple-testing-correction|multiple-testing correction]] for the deflation.
- **Backtest churn.** Looking at the stitched OOS, tweaking the spec, re-running. After three iterations the OOS has become training data. Cap the number of touches; pre-register where possible — see [[hypothesis-first|hypothesis-first research]].
- **Information leakage at the boundary.** Features built with rolling windows that straddle the train/test boundary leak future information. López de Prado's **purging** removes overlapping labels; **embargo** removes the few bars after the train cutoff to prevent serial-correlation leakage.

## Combinatorial Purged Cross-Validation (CPCV)

For ML strategies with long-horizon labels (e.g., triple-barrier labels with multi-day holding periods), the single-path walk-forward gives one OOS trajectory. CPCV (López de Prado 2018, ch. 12) generates many OOS paths by recombining purged folds, yielding a distribution of stitched Sharpes rather than a point estimate. Use it when the question is "could my single OOS path have been lucky?" — the answer comes from the percentile of the observed Sharpe within the CPCV distribution. This is also the foundation of the **Probability of Backtest Overfitting (PBO)** metric (see [[multiple-testing-correction|multiple-testing correction]]).

## Common mistakes

- **Refitting too often.** Daily refit on a 5-year window introduces enormous parameter churn and transaction costs. Refit cadence should match the half-life of the signal, typically monthly to quarterly.
- **Refitting too rarely.** A model fit once in 2010 and walked forward through 2024 with frozen parameters is not walk-forward — it is a single OOS test on 14 years.
- **Train window too short.** Less than ~3× the longest feature lookback or label horizon and your parameter estimates are unstable. Sharpe variance across windows balloons.
- **Confusing walk-forward with cross-validation.** k-fold CV shuffles folds, breaking time order. Never apply standard k-fold to a time series — see [[ml-pipeline-pitfalls|ML pipeline pitfalls]].

## Where to do this in the terminal

The **Backtesting** screen exposes rolling and anchored walk-forward modes with configurable train/test/step sizes. The **AI Quant Lab** runs CPCV for ML-style pipelines and reports the PBO and stitched-Sharpe distribution alongside the point estimate.

## See also

- [[backtesting-discipline|Backtest hygiene]] — the seven sins, of which not walk-forwarding is sin #3
- [[multiple-testing-correction|Multiple-testing correction]] — what to deflate the stitched Sharpe by
- [[ml-pipeline-pitfalls|ML pipeline pitfalls]] — purging, embargo, why k-fold breaks
- [[hypothesis-first|Hypothesis-first research]] — pre-registering the spec before the walk-forward starts

## External references

- Pardo (2008). *The Evaluation and Optimization of Trading Strategies* (2nd ed). Wiley. The original practitioner reference for walk-forward.
- López de Prado (2018). *Advances in Financial Machine Learning*. Wiley. Chapters 7 (cross-validation), 11 (backtest statistics), 12 (combinatorial purged CV).
- Bailey, Borwein, López de Prado & Zhu (2017). "The Probability of Backtest Overfitting." *Journal of Computational Finance*.
- Arnott, Harvey & Markowitz (2019). "A Backtesting Protocol in the Era of Machine Learning." *JFDS*.
