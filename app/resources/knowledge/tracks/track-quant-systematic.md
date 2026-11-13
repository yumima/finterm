# Path: Quant & Systematic Trading

Factor models, backtesting rigor, execution quality, and building strategies that hold up out of sample.

Estimated time: **8–12 hours of focused reading**, plus substantial hands-on coding and testing.

## Who This Track Is For

You think in numbers and want your trading decisions driven by rules, not discretion. You may already know some programming and are looking to build your first systematic strategy, or you're a discretionary trader trying to quantify your intuitions. This track covers the intellectual framework: how to think about factors, how to test a strategy without fooling yourself, and how to execute without giving back your edge.

---

## Foundation — the quantitative building blocks (1–3)

1. [[expected-value|Expected Value]] — the bedrock of any systematic strategy: edge is positive EV consistently expressed
2. [[r-multiple|R-Multiple]] — standardizing trade outcomes as multiples of risk; the unit of measurement for systematic strategy analysis
3. [[beta|Beta]] — factor exposure; understanding what systematic risk your strategy is actually taking

## Strategy Design — building with intellectual honesty (4–5)

4. [[drawdown|Drawdown]] — the primary metric for evaluating strategy risk; maximum drawdown determines sizing and survivability
5. [[correlation|Correlation]] — why combining uncorrelated strategies is more valuable than improving any single strategy

## Execution — where edge leaks out (6–7)

6. [[slippage|Slippage]] — the invisible tax on systematic trading; how to measure it and why it dominates small-edge strategies
7. [[bid-ask-spread|Bid-Ask Spread]] — transaction cost modeling in backtests; the most commonly understated expense

---

## What "Done" Looks Like

You can construct a hypothesis about a market inefficiency, test it on historical data with correct methodology (out-of-sample holdout, walk-forward, realistic transaction costs), interpret the results honestly (including what the Sharpe and max drawdown actually tell you), and size the strategy appropriately for your live account.

## Key Mindset Shift

**Most backtests overfit.** The central challenge of systematic trading is not finding patterns — it's finding patterns that will persist out of sample. Every additional parameter you add to a strategy (this entry rule, that exit rule, this filter) increases in-sample fit and decreases out-of-sample probability. The discipline is to do less: find the simplest expression of a real structural edge and accept that it won't look as impressive on historical data.

**Execution is the third dimension.** Most systematic traders obsess over entry and exit signals and underweight execution. A strategy with a theoretical Sharpe of 0.8 and average slippage of 0.3% per trade may have a live Sharpe near zero in small/mid-cap names. Building realistic transaction cost models — and sizing strategies only as large as liquidity supports — separates professionals from hobbyists.

**Correlation of strategies, not strategies in isolation.** A portfolio of three uncorrelated strategies with Sharpe 0.5 each produces a combined Sharpe of ~0.87. A portfolio of three strategies with 0.8 correlation produces ~0.56. The math of diversification is more powerful than marginal improvement in any single strategy.

## Suggested Follow-Ups

- [[kelly-criterion|Kelly Criterion]] — optimal sizing with known edge probabilities
- [[var|Value at Risk]] — portfolio-level risk measurement
- [[track-microstructure-execution|Path: Execution & Microstructure]] — going deep on execution quality
- [[case-volmageddon-2018|Volmageddon 2018]] — what happens when a systematic strategy (short vol) gets too crowded
