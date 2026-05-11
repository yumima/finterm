# What Quant Trading Is

Quantitative trading is the systematic application of statistical models, programmed rules, and large-scale data analysis to capture returns from financial markets. It is distinguished from discretionary trading not by the asset class or holding period, but by the **commitment to repeatable, testable, encoded decisions** — a model decides, not a human in the moment.

The quant approach treats the market as a noisy data-generating process. Most of what looks like signal is randomness; the job is to identify the small fraction that is genuinely repeatable, prove it survives out-of-sample testing, and harvest it before it decays.

## What quants actually do

Roughly five things, in iterative order:

1. **Form a hypothesis.** "Stocks with high accruals underperform low-accrual peers by ~2%/yr." "FX carry returns are positive in low-volatility regimes." "Index futures roll yields predict next-quarter spot returns." Hypotheses come from academic papers, market structure observations, anomaly catalogues, or recombinations of known factors.

2. **Build the data.** Clean, point-in-time aligned, survivorship-bias-free panels of prices, fundamentals, alternative data. The bulk of quant work is here — most apparent signals dissolve when the data is cleaned properly.

3. **Backtest.** Implement the rule, simulate it on historical data, measure risk-adjusted returns. Critical: hold out unseen data before final evaluation (out-of-sample test), and model realistic transaction costs.

4. **Stress, refine, walk forward.** Robustness across regimes, parameter sensitivity, drawdown behaviour. Walk-forward re-fitting at scheduled intervals to detect signal decay.

5. **Deploy and monitor.** Live trading is its own discipline — slippage, market impact, exchange outages, risk limits. Performance attribution against the backtest tells you whether the model is working or has broken.

The cycle never stops. A signal that worked last year may be arbitraged away this year; a signal that has worked for decades (price momentum) may suddenly enter a multi-year drawdown.

## Where quant differs from discretionary

| Dimension | Discretionary | Quantitative |
|---|---|---|
| Decision unit | Per trade, human judgement | Per rule, applied to every name |
| Universe | 10–50 names tracked deeply | Thousands of names with shallow but consistent coverage |
| Edge source | Insight, relationships, narrative | Statistical regularities, mispricings at scale |
| Failure mode | Cognitive bias, position emotion | Overfitting, regime change |
| Verification | P&L only | P&L + attribution + drift monitoring |

Neither is universally better. Discretionary managers can act on context the model never sees ("the CEO just resigned mid-quarter"); quants extract small edges from thousands of positions simultaneously. The most enduring track records — Renaissance Technologies, AQR, Two Sigma — are quant. The most enduring single-investor records — Buffett, Soros, Druckenmiller — are discretionary.

## The signal-vs-noise problem

A backtest with a Sharpe of 2.0 on five years of data sounds impressive. It is also exactly what you get on a randomly generated indicator if you try enough specifications — the [Lopez de Prado] result on the [[backtesting-discipline|deflated Sharpe]] showed that researchers exploring 100 strategies should expect a "winner" with Sharpe ≈ 2 purely from luck.

The quant discipline is built around **not fooling yourself**:

- Pre-register the hypothesis before you look at the data.
- Reserve a true out-of-sample period that you only touch once, at the very end.
- Estimate the multiple-testing penalty for how many specifications you actually tried.
- Demand an economic story for why the signal should exist — pure data-mined patterns without a mechanism rarely persist.

## Quant horizons and styles

Quant is not synonymous with high-frequency trading. The horizons span:

- **Microseconds–seconds (HFT, market-making).** Spread capture, queue position, latency arbitrage. Capital-intensive; capacity-limited.
- **Minutes–hours (intraday).** Order-book signals, news-reaction microstructure, intraday momentum.
- **Days–weeks (stat-arb).** Mean-reversion baskets, pairs, dispersion. Renaissance's Medallion operates here.
- **Weeks–months (factor / risk-premia).** Value, momentum, quality, low-vol, carry. AQR's bread and butter. Highest capacity.
- **Months–years (macro, long-term factor).** Multi-asset trend, business-cycle tilts, structural carry.

Holding period drives everything else: data requirements, infrastructure, capacity, regulatory burden, and what looks like "the same strategy" at one horizon (mean-reversion) is the opposite at another (momentum).

## What you can do in this terminal

This knowledge tree has cross-linked entries that walk through each part of the discipline. Where a concept maps to an in-app tool, the entry's "Try it" button jumps you there:

- **AI Quant Lab** — module composer with backtesting integration.
- **QuantLib** — reusable indicator and metric library.
- **Backtesting** — historical strategy simulation, walk-forward, IS/OOS splits.
- **Algo Trading** — strategy builder, scanner, deployment.
- **Node Editor** — visual signal composition.
- **Alpha Arena** — competition / paper-trading environment.

## What to read first

In order:

1. [[risk-adjusted-returns|Risk-adjusted return measures]] — the language of the discipline (Sharpe, Sortino, Calmar, information ratio).
2. [[factor-investing|Factor investing]] — the academic catalogue of repeatable risk premia.
3. [[backtesting-discipline|Backtesting discipline]] — the failure modes and the methodology that survives them.
4. [[transaction-costs|Transaction-cost modelling]] — why a Sharpe-2 backtest can be a Sharpe-0 live strategy.
5. [[quant-research-workflow|Quant research workflow]] — how to do the work without fooling yourself.
