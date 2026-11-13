# ML Pipeline Pitfalls in Finance

The same neural-network architecture that wins ImageNet competitions will, naively applied to financial returns, produce a backtest with Sharpe 4 and live performance near zero. Financial data has properties that violate the standard ML assumptions, and most off-the-shelf pipelines (scikit-learn's `cross_val_score`, PyTorch's default training loop) get them wrong by default.

This entry walks through the most common pitfalls and the corrections. The single best practitioner reference is **López de Prado's *Advances in Financial Machine Learning*** (2018); chapters 6–13 cover everything below in detail.

## Pitfall 1 — IID assumption breaks

Standard ML assumes training examples are independent and identically distributed (IID). Financial returns are neither:

- **Not independent**: adjacent observations share information (overlapping windows; today's price affects tomorrow's features). A naive 80/20 train/test split bleeds future information into past.
- **Not identical**: distributions shift over regimes. A model trained 2010–2019 is being asked to predict 2020–present from a different distribution.

**Consequence.** Standard k-fold cross-validation gives wildly optimistic CV scores. Models that look good in CV collapse in production because CV used future data to evaluate past predictions.

**Fix.** Use **walk-forward** or **purged k-fold cross-validation** (next section).

## Pitfall 2 — Wrong cross-validation

### Walk-forward CV

Train on data up to time t, test on t+1:t+k, slide forward, repeat. Strictly causal — at every test fold, the model has only seen data that was available before the test period.

Pros: simple, intuitive, mirrors live deployment.
Cons: fewer test folds, larger CV variance, no use of "future" training data even for the early test folds.

### Purged k-fold (López de Prado)

Standard k-fold partitions the data into k chunks. The bug: if features at time t depend on the label at time t+h (which is typical — labels are forward returns), then training on chunk 1 leaks information into chunk 2's test set even with non-overlapping chunks.

Purged k-fold removes (purges) any training observation whose feature window overlaps with the test window's label horizon. Optionally also adds an embargo period after each test fold so the next training fold doesn't immediately follow stale data.

Effect: CV scores drop dramatically vs naive k-fold (typically 30–60%). The lower number is the honest one.

### Combinatorial Purged CV (CPCV)

López de Prado's extension: take k splits, evaluate all `C(k, m)` combinations of m test splits, average. Produces an empirical distribution of CV scores from which you can compute a CV-Sharpe distribution and a deflated CV statistic.

For most practical work, walk-forward + a final OOS hold-out is enough. For research-grade work where you're testing many hypotheses, CPCV's distribution-of-CV-Sharpes lets you formally control multiple-testing inflation.

## Pitfall 3 — Lookahead in features

Standard feature engineering on a Pandas DataFrame routinely leaks future information. Common offenders:

- **Z-scores using the whole-sample mean and std.** Train using a z-score normalized over the full dataset → the test set "knows" the mean of the future. Fix: rolling z-score with a lookback window that ends *before* the prediction time.
- **Target encoding.** Replace categorical features with average target — the average must be computed only from training rows.
- **Imputing missing values with future data.** Forward-fill is OK; mean-imputation across the whole sample leaks.
- **Indicator computed on future bar's close.** Moving average of 20 bars "centered" on current bar uses 10 future bars.

**Fix.** Compute every feature with strictly point-in-time inputs. Test by shifting features forward by 1 period — if the score doesn't change much, you have a lookahead bug.

## Pitfall 4 — Stationarity of features

Most ML models assume features are stationary. Raw prices are not. Naive feature: "log(price)" — non-stationary, model overfits to the level rather than the structure.

Standard fix: take returns (first difference of log price). But naive differencing destroys all the long-memory structure — the model loses any signal from price levels relative to historical bands, the position of the price in its 52-week range, etc.

**Fractional differencing** (López de Prado): a continuous parameter d ∈ (0, 1) — d=0 is no diff (preserves everything, fully non-stationary), d=1 is standard diff (stationary but no memory), intermediate d preserves memory while passing ADF stationarity. Practical d is typically 0.3–0.6.

Few practitioners get this right; most either don't difference (over-fit non-stationarity) or fully difference (throw away signal).

## Pitfall 5 — Labels are noisy and lookahead-adjacent

In finance, labels are typically "did the stock go up by more than X% within the next N periods?" — a function of future prices. Three corrections:

- **Triple-barrier method** (López de Prado). Define three barriers from each bar: profit target (up X%), stop loss (down Y%), time barrier (N periods). Label = which barrier hits first. Captures realistic trade outcomes.
- **Meta-labeling.** A primary model predicts the direction; a secondary "meta-label" model predicts the *probability the primary model is right*. The meta-label trains on the residuals of the primary, often higher signal-to-noise than direct return prediction.
- **Label uniqueness weighting.** Two overlapping label windows (bars t and t+1, both predicting next 5-day return) share most of their information — they're nearly the same data point. Downweight overlapping samples in training.

## Pitfall 6 — Signal-to-noise ratio is brutal

Image recognition has SNR > 1; the digit "8" is clearly distinguishable from random noise. Daily stock return prediction has SNR ~ 0.05 — 95% of variance is noise. Standard ML methods that work great with SNR > 1 fail or barely work at SNR < 0.1.

**Implications:**

- **Sample size matters more.** A neural net needs much more data to learn a faint signal than a clear one. With 5 years of daily data (~1250 points) you cannot train deep architectures responsibly; with 25 years × 5000 stocks (~6M points), you can.
- **Simpler models win.** Logistic regression and gradient-boosted trees with conservative hyperparameters often beat deep learning out-of-sample in finance — there isn't enough signal to support the complexity of a deep network.
- **Ensembling helps disproportionately.** Bagging and boosting average out noise; a single tree learns the noise.
- **Don't expect "interpretable" features.** Feature importance from a noisy fit is itself noisy.

Israel, Kelly, & Moskowitz (2020) — "Can Machines 'Learn' Finance?" — give the careful empirical evaluation.

## Pitfall 7 — Mistaking validation Sharpe for live Sharpe

A walk-forward CV Sharpe of 1.5 does NOT mean live Sharpe will be 1.5. Reasons:

- **Cost drag.** CV typically assumes zero transaction costs. See [[transaction-costs|transaction-cost modelling]].
- **Capacity decay.** A model whose CV uses prices implicitly assumes zero market impact. At any capital, your trades move the market against you.
- **Distributional drift.** The market regime in the next year is not the average of past regimes.
- **Multiple-testing inflation.** If you tried 100 feature sets and reported the best, the deflated CV Sharpe is much lower than the reported.

A rule of thumb: **divide CV Sharpe by 2** for a realistic live expectation. If the CV Sharpe is below 1.0 net of costs, the strategy is unlikely to produce meaningful alpha live.

## Where ML genuinely helps in finance

Despite the pitfalls, ML adds value where:

1. **Non-linearity matters.** Decision trees, gradient boosting, and neural nets handle interactions between features that linear models miss.
2. **High-dimensional features.** Quant fundamentals work has 50–500 features per stock; ridge / lasso / random forests scale better than hand-engineered linear models.
3. **NLP and alternative data.** Sentiment from news, satellite imagery of parking lots, credit-card transaction data — these are domains where ML is the only viable approach.
4. **Execution.** Reinforcement learning for order placement (TWAP vs VWAP vs adaptive) has shown real gains over rule-based execution.

Where ML hurts:

1. **Trying to predict daily returns from price-only features.** Decades of literature say this is barely possible; throwing a neural net at it doesn't change the SNR.
2. **Small samples.** Pre-2000 emerging market data, single-stock alpha hunting on 5 years of data. Use simpler methods.
3. **Anywhere the goal is interpretability for a stakeholder.** Auditors and PMs trust regression coefficients more than SHAP values.

## A defensible ML quant pipeline

1. **Define the prediction target** with a triple-barrier or other realistic label.
2. **Engineer features** point-in-time, stationarity-aware (fractional differencing where relevant), normalized with rolling statistics.
3. **Split data** with walk-forward or purged k-fold; reserve a true OOS hold-out.
4. **Train conservatively**: shallow models, strong regularization, early stopping.
5. **Cross-validate honestly**: measure CV Sharpe, drawdown, hit rate.
6. **Stress test**: regime breakouts, parameter sensitivity, multiple-testing correction.
7. **Cost-model the live deployment**: include [[transaction-costs|spread + impact]] in the strategy's projected Sharpe.
8. **Paper-trade before live**: 3–6 months of shadow with the production code.

If the pipeline produces a CV Sharpe of 0.6 after honest cost modeling, that's a real strategy. If it produces 4.0, something is broken — find it before you trade it.

## See also

- [[backtesting-discipline|Backtesting discipline]] — the seven sins beyond ML
- [[time-series-basics|Time-series basics]]
- [[transaction-costs|Transaction-cost modelling]]
- [[quant-research-workflow|Quant research workflow]]

## External references

- López de Prado (2018). *Advances in Financial Machine Learning*. Wiley. Chapters 6–13 on cross-validation; 17–22 on backtesting; 4 on labeling.
- Israel, Kelly, & Moskowitz (2020). "Can Machines 'Learn' Finance?" *Journal of Investment Management*. AQR working paper version freely available.
- Gu, Kelly, & Xiu (2020). "Empirical Asset Pricing via Machine Learning." *Review of Financial Studies*. Large-scale benchmark of ML methods on US equity returns.
- Bailey & López de Prado (2014). "The Deflated Sharpe Ratio." *Journal of Portfolio Management*.
- López de Prado (2020). *Machine Learning for Asset Managers*. Cambridge. Shorter follow-up focused on PMs.
