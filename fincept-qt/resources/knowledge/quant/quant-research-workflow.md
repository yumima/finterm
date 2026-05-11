# Quant Research Workflow

A productive quant research practice is not about being smarter than the market. It is about avoiding the systematic mistakes that destroy most retail and discretionary alpha. The workflow is a discipline — a sequence of habits that catches errors before they become deployed bad bets.

## The five stages of a strategy idea

Every project should pass through these in order. Skipping stages is how 95% of "promising signals" become losses.

### 1. Hypothesis (before any data)

Write down — in words, not code — what you believe and why. Two sentences:

- **What pattern do you expect?** "Stocks with sharply rising short interest underperform peers over the next 3 months."
- **Why does it exist?** "Short sellers are systematically smarter than average on information about near-term earnings risk; their bets reveal future stock weakness."

If you cannot articulate the "why," you do not have a hypothesis — you have a candidate for [[backtesting-discipline|data mining]]. Stop and find a mechanism before proceeding.

The "why" is your **economic story**. The Fama-French value premium has one: cheap stocks bear distress risk. AQR's momentum has one: behavioural underreaction. Both have survived 30+ years of out-of-sample testing partly because the mechanism is identifiable. Pure "we tried 1000 things and this worked" patterns rarely persist.

### 2. Data

Build the data **before** you write strategy code. The data step is 60–70% of real quant work.

Required steps:

- **Source.** Where the data comes from, who owns it, what the licence permits.
- **Vintage.** Was the data published at the date you're using it? Re-stated fundamentals (e.g., restatements months later) are common gotchas; use point-in-time, not as-currently-known.
- **Coverage.** Is it survivorship-bias-free? Does it include delisted stocks and the delisting returns?
- **Lag.** Reported earnings hit the tape ~30–90 days after period-end. SEC filings have their own delays. Lag every fundamental field by its real release date.
- **Cleaning.** Splits, dividends, ticker changes, corporate actions. Use total-return prices, not raw closes, for return calculations.
- **Sanity check.** Plot histograms of every input. Outliers usually reveal a data bug (an index where every stock returned 100% on one day = a corporate action that wasn't handled).

Do not skip cleaning. The most consistent reason early-career quants find Sharpe-5 strategies is that the data is dirty in their favour.

### 3. Backtest (in-sample only)

Run the strategy on the in-sample data. Measure:

- Sharpe / Sortino / Calmar — see [[risk-adjusted-returns|risk-adjusted measures]]
- Hit rate, profit factor, average win / average loss
- Turnover (trades per year)
- Maximum drawdown and time-to-recovery
- Performance per regime (pre-2008, 2008 crisis, 2010s QE, 2020 COVID, 2022 rates)

At this point you are allowed to iterate freely. Adjust parameters, add filters, refine the universe. Keep an explicit count of how many variations you tried — you'll need it for the multiple-testing correction.

### 4. Out-of-sample test (one shot)

Once the strategy is fully specified, run it on the held-out OOS period. Do NOT iterate after seeing OOS results.

Compare IS and OOS performance:

- Sharpe drop of 30–50% is normal — backtests routinely overstate by this amount.
- Sharpe drop of > 70% suggests overfitting; the strategy may not have a real edge.
- OOS Sharpe < 0 — the in-sample result was noise. Stop.

Apply the [[backtesting-discipline|deflated Sharpe]] correction using the count of in-sample variations. The deflated number is your credible OOS Sharpe.

### 5. Paper trade (final reality check)

Before live deployment, run the strategy in paper-trading or shadow mode for at least 3–6 months. This catches:

- Implementation bugs (the live system isn't running the same logic as the backtest)
- Cost overruns (real slippage is worse than modelled)
- Regime mismatch (the OOS period was non-representative)

Paper trading is the cheapest insurance you'll buy on a strategy. Skipping it because "the OOS test was great" is one of the more reliable ways to lose money.

## Tooling and hygiene

### Reproducibility

- Every backtest result should be regeneratable from the data + code repository. No notebooks with manually-edited intermediate cells; no "I think this is the latest version" parameter files.
- Pin data snapshots by date. A backtest run today should produce the same number a year from now from the same underlying data.
- Version-control every change to the strategy code, with commit messages that note **what changed and why**.

### Research logging

Keep a hypotheses log. Each entry:

- Date
- Hypothesis statement
- IS test result
- OOS test result (when run)
- Decision (deploy / shelve / reject)
- Lessons learned

This becomes the most valuable artifact in your research practice. The log shows you which categories of ideas have repeatedly worked and which have repeatedly failed — far more valuable than any single strategy.

### Failure post-mortems

When a deployed strategy underperforms:

1. **Reproduce in shadow.** Re-run the strategy on the live-period data; confirm the backtest formula gives the same result as the live P&L. If not, you have an implementation bug.
2. **Attribute the loss.** Is it from one decision (a single big trade), a regime (this whole month was wrong), or a slow drift (signal decay)?
3. **Decide.** Pause, refine, or sunset. Most live strategies that drift > 3 standard deviations below backtest expectations are broken, not just unlucky.

## What separates productive quants from unproductive ones

Productive practices share these habits:

- **Few projects in flight.** 2–3 hypotheses being actively tested, not 20.
- **High kill rate.** 80%+ of hypotheses fail at backtest or OOS; that is correct, not a sign of failure.
- **Slow before fast.** A week building proper point-in-time data + cost modelling, then a day to test, beats a day on data and a week on backtest tweaks.
- **Anchored economic story.** "This pattern exists because X" — refusable, testable, falsifiable.
- **Honest documentation.** Backtests reported include the number of variations tried and the worst-case-cost stress.
- **Strategy diversity.** No single strategy is the firm. A research practice that has run 30 strategies and kept 3 is healthier than one that has run 3 strategies and kept all 3.

## In this terminal

- **AI Quant Lab** — module composer, supports hypothesis-to-backtest workflow with module-level attribution.
- **Backtesting** — historical simulation with IS/OOS, walk-forward, regime breakdowns.
- **Algo Trading** — strategy builder, paper-trade deployment, performance attribution.
- **Notes** — durable research log; keep hypotheses + decisions here.
- **Power Trader** — congressional disclosure dataset, useful for backtests of signal-from-disclosure strategies.

## See also

- [[quant-overview|What quant trading is]]
- [[backtesting-discipline|Backtesting discipline]]
- [[transaction-costs|Transaction-cost modelling]]
- [[factor-investing|Factor investing]]
- [[build-a-thesis|How to build a thesis]] — the discretionary-side analogue, in playbooks

## External references

- Lopez de Prado (2018). *Advances in Financial Machine Learning*. Wiley. Chapters 1–3 on workflow + data; 6–7 on backtesting.
- Chan (2008, 2017). *Quantitative Trading* and *Machine Trading*. Wiley.
- Narang (2013). *Inside the Black Box*. Wiley. Practitioner overview.
- Knight (2018). "The Five Skills of a Good Quant" — Quantopian blog (archived). Practical research-process essay.
