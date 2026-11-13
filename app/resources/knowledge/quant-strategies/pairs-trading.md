# Pairs Trading

Pairs trading is the original statistical-arbitrage strategy: identify two related securities whose prices co-move historically, build a stationary spread between them, and trade the spread's reversion to its mean. Long the cheap leg, short the rich leg, exit at convergence. The framework dates to Nunzio Tartaglia's Morgan Stanley group in the mid-1980s; the canonical academic study is Gatev, Goetzmann & Rouwenhorst (2006), which documented Sharpe ratios near 1.4 for US equity pairs over 1962–2002 — most of the profit concentrated before 2003.

The strategy is also the cleanest single illustration of [[time-series-basics|cointegration]] in practice: each leg is non-stationary (a random walk in log-price), but a specific linear combination is stationary.

## The setup

The two-leg pair is the simplest case of a cointegrated system. Engle & Granger (1987) introduced the formal econometric apparatus and the two-step test that bears their name:

```
Step 1: regress log-price of asset Y on log-price of asset X:
        log(Y_t) = α + β · log(X_t) + ε_t
Step 2: run ADF test on residuals ε_t.
        If ADF rejects unit root at 5% → the pair is cointegrated.
```

β is the **hedge ratio**: how many units of X to short against one unit of Y to construct a market-neutral spread. The residual ε_t is the **spread**. If cointegration holds, the spread is stationary and mean-reverts to zero.

For three or more assets, use the Johansen (1991) test, which yields all cointegrating vectors simultaneously. Most pair shops still use Engle-Granger on two-name pairs because the additional complexity of Johansen rarely buys edge.

## Building the trade

The mechanical recipe used by most cash-equity pair desks:

```
1. Universe selection. Same sector / same industry / same supply chain.
   Pre-screen by economic story, not by data mining.
   Examples: KO/PEP, HD/LOW, V/MA, COP/CVX, EWA/EWC.

2. Cointegration test. Engle-Granger over a formation period (1–2 years).
   Require ADF p < 0.05; reject pairs that just look correlated.

3. Spread construction.
   spread_t = log(Y_t) − β · log(X_t) − α
   z_t = (spread_t − rolling_mean) / rolling_std

4. Entry / exit.
   Enter when |z| > 2.0 (long the cheap leg, short the rich leg)
   Exit when |z| < 0.5
   Stop when |z| > 3.5 (cointegration broke)

5. Position sizing.
   Dollar-neutral: equal notional on each leg.
   Beta-neutral: adjust by β so portfolio beta ≈ 0.
   Vol-neutral: scale each leg so risk contributions match.
```

The Gatev-Goetzmann-Rouwenhorst implementation is even simpler: form pairs by minimising sum of squared deviations of normalised prices over a 12-month formation period; trade pair when normalised spread exceeds 2σ; exit on crossing. That brute simplicity is why their results are robust — no parameter tuning, no curve fit.

## Half-life as a trade-quality filter

After computing the spread, calibrate an Ornstein-Uhlenbeck on it (see [[mean-reversion|mean reversion]] for the recipe):

```
Δspread_t = α + β · spread_{t−1} + ε_t
half_life = ln(2) / (−β) · Δt
```

Half-life buckets the trade:

- **< 5 days** — fast reversion, scalable but heavily eaten by costs. HFT regime mostly.
- **5–30 days** — sweet spot for cash equity pairs.
- **30–100 days** — slow reversion, capital lock-up, big drawdowns are normal.
- **> 100 days** — abandon. Either cointegration is weak or the relationship is structural drift, not reversion.

Set the maximum holding period to ~3× the half-life. If the trade hasn't converged by then, exit anyway — the cointegration probably broke.

## The Gatev-Goetzmann-Rouwenhorst evidence

GGR (2006, *Review of Financial Studies*) ran the simplest possible pair-trading rule on all US equity pairs 1962–2002 and reported:

- **Average excess return** ~11% annualised for the top 5 / top 20 pairs.
- **Sharpe** ~1.4 before costs.
- **Low market beta** (~0.04) — genuinely market-neutral.
- **Profits concentrated in early decades.** The Sharpe in 1962–1988 was ~1.6; in 1989–2002 it was ~0.9.

Do, Faff & Hamza (2010) and Do & Faff (2010) updated the dataset through 2009 and found pair-trading Sharpes had fallen to ~0.4 post-2002. The decay is real and is attributed to:

- **Crowding.** When Renaissance, D.E. Shaw, and Tartaglia's MS alumni all run the same model, the spread mean-reverts faster (great in-flight) and offers less to wait for (bad on entry).
- **Decimalisation (2001).** Tighter bid-ask spreads compressed the noise on which fast pairs depended.
- **Better hedging tools.** ETF baskets and futures allow market-makers to neutralise inventory more cheaply, eating the dislocations.

Bottom line: pure same-name pair trading is largely a museum strategy now. The technique still works for thinly-covered names, cross-listed ADR pairs, and country-ETF pairs (EWA/EWC remains a textbook live cointegration); it generally does not work for liquid same-industry US large caps.

## Cointegration breaks — the catastrophic failure mode

A cointegrating relationship that held for 5 years can break overnight. Common causes:

- **M&A.** Acquirer's price moves on deal, target jumps to deal price, the spread becomes a deal-risk arbitrage, not a reversion. KO/PEP works because neither acquires the other.
- **Business model divergence.** Sears and Walmart were cointegrated for years; Sears's decline ended that. Re-test cointegration every walk-forward window.
- **Regulatory shock.** Energy pairs broke during the 2014 oil collapse; financial pairs broke in 2008.

The discipline: re-run Engle-Granger at every rebalance; if ADF p-value of the residual stops rejecting the unit root, pause new entries on the pair. If you're already in a trade and cointegration breaks while the spread keeps widening — exit on the next bar. Do not "wait for reversion" of a relationship that no longer exists.

## Common mistakes

- **Mistaking correlation for cointegration.** Two assets can have 0.9 daily-return correlation and not be cointegrated. Run the test on log-prices, not on returns.
- **Re-estimating β too often.** A new β every day creates a "self-fulfilling" stationary spread by construction. Re-estimate quarterly or on a walk-forward schedule.
- **Skipping survivorship.** A pair list built from today's S&P 500 excludes Lehman, Bear, Enron, and many genuine 2008-era pairs. See [[data-hygiene|data hygiene]].
- **Ignoring borrow on the short leg.** Hard-to-borrow names have negative carry that easily eats the spread reversion. Pre-screen.

## Risk management essentials

- **Hard stop on |z|.** When the spread blows past 3.5σ without convergence, the cointegration likely broke. Don't pyramid into a widening spread.
- **Basket of pairs, not concentration.** Single-pair Sharpe is ~0.5–1.0; a basket of 50 uncorrelated pairs can deliver Sharpe 2+ because spread reversions are largely idiosyncratic. See [[position-sizing|position sizing]].
- **Sector / factor exposure check.** A book of "neutral" pairs can accumulate net factor exposure (size, value, momentum) inadvertently. Hedge or rebalance to control it; see [[stat-arb|stat-arb]] for the systematic version.

## Where to do this in the terminal

The **AI Quant Lab** has a cointegration screener that runs Engle-Granger and Johansen on user-defined universes and reports half-life and Hurst exponent for each candidate spread. The **Backtesting** screen supports beta-neutral and vol-neutral pair backtests with explicit borrow modelling.

## See also

- [[time-series-basics|Time-series basics]] — cointegration, ADF, Engle-Granger background
- [[mean-reversion|Mean reversion]] — the broader pattern; pairs is the cointegrated-spread special case
- [[stat-arb|Statistical arbitrage]] — the multi-leg, factor-residual generalisation
- [[data-hygiene|Data hygiene]] — survivorship matters here especially
- [[transaction-costs|Transaction costs]] — pair turnover is high; cost drag is decisive

## External references

- Engle & Granger (1987). "Co-integration and Error Correction: Representation, Estimation, and Testing." *Econometrica* 55(2).
- Johansen (1991). "Estimation and Hypothesis Testing of Cointegration Vectors." *Econometrica* 59(6).
- Gatev, Goetzmann & Rouwenhorst (2006). "Pairs Trading: Performance of a Relative-Value Arbitrage Rule." *Review of Financial Studies* 19(3).
- Do & Faff (2010). "Does Simple Pairs Trading Still Work?" *Financial Analysts Journal* 66(4).
- Vidyamurthy (2004). *Pairs Trading: Quantitative Methods and Analysis*. Wiley.
