# Backtesting Discipline

A backtest is the simulation of a strategy on historical data. Done well, it tells you whether an idea has merit and what its risk profile looks like. Done badly — which is the default — it produces a Sharpe-2 number that doesn't survive the first month of live trading.

The single most common failure in quantitative research is **overfitting**: finding patterns in historical data that were noise, not signal. Every published "factor" began as a backtest; many of them haven't replicated out-of-sample.

This entry covers the failure modes and the methodology that survives them.

## The seven sins of bad backtests

### 1. Lookahead bias

Using information at time *t* that wouldn't have been available until *t+k*. Examples:

- Using the *final* restated earnings number when the original release (later restated) was what the market traded on.
- Computing a moving average that includes the close of today's bar, then "trading at today's close based on a signal that included today's close" — a degenerate form of crystal-ball.
- Using closing prices to compute a signal but executing at the *same* close.

Fix: build a strict point-in-time database. Lag every fundamental data point by its reporting delay. Sign every signal at *t*, execute no earlier than *t+1*.

### 2. Survivorship bias

Backtesting on the list of stocks that currently exist excludes everything that went bankrupt, was delisted, or was acquired below the buyer's cost. The pre-1990 academic factor literature was riddled with this; modern providers (CRSP, Compustat) now publish survivorship-bias-free panels but cheaper data still has the problem.

Fix: use a "live" universe — every stock that traded on each date, regardless of whether it survives to today. Include delisting events and apply the appropriate delisting return.

### 3. Look-back optimism (in-sample fit)

Choosing parameters (e.g. moving-average length, factor weights, vol-targeting threshold) on the same data you measure performance on. Of course it looks good — you optimised it to look good.

Fix: split your data. Hold out an **out-of-sample (OOS)** period that you do not touch during optimisation. Only at the very end, after the strategy is fully specified, run it on the OOS period. If it underperforms there, the in-sample result was overfit. Lopez de Prado recommends keeping ≥ 30% of the timeline as OOS.

### 4. Multiple-testing inflation

Try 100 specifications, report the best one. With pure noise, the expected best-of-100 t-statistic is ~2.5 — high enough to publish but meaningless.

Fix: deflate the reported t-statistic by the number of trials. The **deflated Sharpe ratio** (Bailey & Lopez de Prado 2014) is the explicit correction. Better: pre-register the hypothesis before you look at the data.

### 5. Unrealistic execution

Assuming you traded at the exact close, with zero spread, zero impact, zero exchange fees. Quant strategies that look strong on these assumptions typically lose all their alpha to costs when implemented.

Fix: model the full cost stack. See [[transaction-costs|transaction-cost modelling]] for the breakdown. The first hour of testing a new strategy should always include a "worst-case-costs" stress: 2× your nominal slippage, full bid-ask, no rebates, no internalisation.

### 6. Regime-blindness

Strategy worked great 2010–2019 (low vol, QE). Was it the strategy or the regime? Test across regime breaks: pre-2008, 2008 crisis, 2010–2019 QE, 2020 COVID, 2022 rate hikes. A strategy that works in only one regime should be labelled as a regime-conditional strategy, not a universal one.

Fix: report Sharpe / Sortino / Calmar separately for each major regime period. Plot the cumulative equity curve and inspect it visually for one-regime dominance.

### 7. Backtest churn

Iterating on the same backtest in response to disappointing results, tweaking parameters until performance improves. Each iteration is implicitly fitting to the test set — your "out-of-sample" becomes in-sample by attrition.

Fix: cap the number of times you look at OOS results. Lopez de Prado's *combinatorial purged cross-validation* (PCV) is the formal version. Practical version: write down the strategy spec, run OOS, accept the result.

## The walk-forward methodology

The strongest single defense against overfitting is **walk-forward** validation:

1. Train (or fit parameters) on data *t₀ → t₁*.
2. Test on the next out-of-sample chunk *t₁ → t₂*.
3. Slide the window: train on *t₀+Δ → t₁+Δ*, test on *t₁+Δ → t₂+Δ*.
4. Concatenate all OOS chunks into a single OOS performance series.

This simulates "in real time, would I have committed to this strategy?" — each test chunk's parameters are fit on data that existed before the test. Sharpe of the concatenated OOS series is the headline number; if it's substantially lower than in-sample, the strategy was overfit.

```
                              walk-forward windows
time ──>
[train ─────┤ test ──┤
            [train ──┤ test ──┤
                     [train ──┤ test ──┤
                              [train ──┤ test ──┤
                                       [...
                              concat → single OOS series
```

## The Sharpe ratio you should believe

Given *n* trials (specifications tested), the **deflated Sharpe** by Bailey & Lopez de Prado adjusts the observed Sharpe for the multiple-testing inflation:

```
DSR = Prob[true Sharpe > 0 | observed Sharpe, n trials]
```

In practice: an observed Sharpe of 1.5 from 1 trial deflates differently than an observed Sharpe of 1.5 chosen from 100 trials. The latter is essentially indistinguishable from noise.

Rule of thumb: if you tried 100 strategies, demand observed Sharpe ≥ 2.5 to claim a real edge; 50 trials demands 2.2; 10 trials demands 1.8; 1 (pre-registered) demands 1.0.

## What a defensible backtest report contains

- **Strategy spec.** Frozen before any look at the data. Includes signal definition, universe, entry/exit rules, position-sizing, stop-loss, rebalance frequency.
- **Data audit.** Source, vintage, point-in-time treatment, survivorship handling, delisting handling.
- **In-sample / OOS split.** Dates, ratio, methodology (single split / walk-forward / PCV).
- **Performance metrics.** Sharpe, Sortino, Calmar, max drawdown, turnover, average holding period, hit rate, profit factor — for both IS and OOS, and for each regime.
- **Cost model.** Commission, spread, market impact, financing, taxes. Worst-case stress.
- **Capacity estimate.** At what AUM does the strategy break? See [[transaction-costs|transaction-cost modelling]].
- **Multiple-testing accounting.** How many specifications were tried; deflated Sharpe.
- **Failure mode analysis.** Explicit list of regimes / events where the strategy is expected to underperform.

## Where to do this in the terminal

The **Backtesting** screen runs strategies with IS/OOS splits and walk-forward windowing. The **AI Quant Lab** lets you compose strategy modules with explicit signal-by-signal performance attribution. The **Node Editor** is useful for visual decomposition of complex multi-leg strategies before backtesting.

## See also

- [[risk-adjusted-returns|Risk-adjusted return measures]]
- [[transaction-costs|Transaction-cost modelling]]
- [[quant-research-workflow|Quant research workflow]] — the broader process that prevents most backtest sins
- [[factor-investing|Factor investing]] — every factor was once a backtest; many didn't survive replication

## External references

- Bailey & Lopez de Prado (2014). "The Deflated Sharpe Ratio." *Journal of Portfolio Management*.
- Lopez de Prado (2018). *Advances in Financial Machine Learning*. Wiley. Chapters 6–7 on backtesting and 11–13 on cross-validation.
- Harvey, Liu, & Zhu (2016). "...and the Cross-Section of Expected Returns." *Review of Financial Studies*.
- Bailey, Borwein, Lopez de Prado, & Zhu (2014). "Pseudo-Mathematics and Financial Charlatanism." *Notices of the AMS*.
