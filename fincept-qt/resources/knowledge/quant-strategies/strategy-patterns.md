# Strategy Patterns

Most production quant strategies fall into one of seven structural patterns. Knowing the pattern matters because each has a characteristic risk profile, capacity ceiling, and failure mode. A new "alpha idea" almost always reduces to a variant of one of these.

The catalog below draws on Kakushadze & Serur's *151 Trading Strategies* (2018), AQR's working papers, and Lopez de Prado's *Advances in Financial Machine Learning*.

## 1. Trend / momentum

**Idea.** Prices that have moved in one direction over a lookback window are more likely to continue than reverse.

**Construction.** Sort assets by past-N-month return; long the top quantile, short the bottom. The canonical equity momentum: 12-month return excluding the most recent month (the "12-2" specification). For CTA-style trend: cross-asset trend signals across stocks, bonds, commodities, FX.

**Where it lives in *151 Strategies*.** §3.1 (price momentum), §3.7 (residual momentum), §10.4 (futures trend), §4.6 (multi-asset trend).

**Sharpe expectation.** Historical 0.5–1.0 for diversified cross-asset; 0.7–1.2 for equity momentum; long-horizon CTA Sharpe 0.5 with occasional 1.5 years.

**Failure mode.** **Momentum crashes** — sharp bear-to-bull reversals (1932, 2009, 2020) when prior losers explode upward. Equity momentum lost 80% in 2009 over a few months.

**Capacity.** High in liquid markets ($100B+ for AQR-style); limited in small-cap.

### In this terminal
- **algo_trading** — trend strategy templates
- **alpha_arena** — momentum competition environment
- **node_editor** — visual trend signal construction

## 2. Mean reversion / pairs

**Idea.** Prices that have moved sharply revert to a fair value or to a co-integrated peer.

**Construction.**
- **Single-asset.** Compute z-score of price vs N-day moving average; long when z < −2, short when z > +2, exit at z = 0.
- **Pairs.** Find two co-integrated stocks (e.g. KO and PEP, or two regional banks). Trade the spread between them: long the cheaper, short the richer, exit when spread closes.
- **Stat-arb.** Generalisation to baskets — score every stock by deviation from a multi-factor expected value, trade the deviations.

**Where it lives in *151 Strategies*.** §3.8 (pairs trading), §3.9 (mean-reversion clusters), §3.18 (stat-arb optimization).

**Sharpe expectation.** Stat-arb at Renaissance / D.E. Shaw scale historically 4–10 net (with significant capacity constraints); naive single-asset z-score 0.3–0.8.

**Failure mode.** **Trend regimes break the strategy** — a stock that "should mean-revert" instead trends to zero (bankruptcy) or to a new permanent level. Pairs that lose co-integration (one company changes business model) bleed continuously.

**Capacity.** Capacity-limited. Renaissance's Medallion is the canonical example — capped at single-digit billions, the rest of the firm runs lower-Sharpe higher-capacity strategies.

## 3. Carry

**Idea.** Pure compensation for taking the other side of risk someone wants to lay off.

**Construction.**
- **FX.** Buy high-rate currencies, sell low-rate (the AUD/JPY canonical pair).
- **Fixed income.** Buy higher-yielding bonds, sell lower-yielding peers.
- **Commodities.** Capture roll yields by buying backwardated curves (where futures < spot).
- **Equities.** Buy high-dividend-yield stocks vs low.

**Where it lives in *151 Strategies*.** §5.11 (fixed-income carry), §7.4 (vol risk premium), §8.2 (FX carry), §9.1 (commodity roll yields).

**Sharpe expectation.** 0.5–0.8 in calm regimes, with sharp drawdowns in stress periods.

**Failure mode.** **Carry crashes.** When funding currencies appreciate (yen 2008, 2020) or risk-off events occur, carry positions liquidate fast. The classic "picking up pennies in front of a steamroller" — high Sharpe in calm regimes, fat left tail.

**Capacity.** Generally high; FX carry trades at $100B+.

## 4. Dispersion / relative value

**Idea.** Mispricings between correlated instruments — the index vs its constituents, options vs underlying, futures vs spot, on-shore vs off-shore listing.

**Construction.** Trade the spread, not the level. Long-short by construction. Examples:
- **Index dispersion.** Sell index volatility, buy single-stock volatility (correlation collapsed → makes money on correlation decline).
- **Cash-and-carry.** Long spot, short futures when futures-implied financing exceeds actual cost.
- **Triangular FX.** Three FX pairs whose cross-rates imply an arbitrage; capture before mid-price recovers.

**Where it lives in *151 Strategies*.** §6.2 (cash-and-carry), §6.3 (index dispersion), §6.4 (intraday ETF arb), §8.5 (FX triangular arb).

**Sharpe expectation.** When the spread is real and persistent, very high (8+); when arbitraged out, near zero. Hyper-competitive.

**Failure mode.** Mostly **competition** — these are zero-sum trades and the spread narrows as more capital enters.

**Capacity.** Usually very low — true arbitrages are tiny in size and disappear within seconds to days.

## 5. Volatility risk premium

**Idea.** Implied volatility on average exceeds realised volatility. Selling vol (via options or VIX futures) earns the difference.

**Construction.**
- Sell index puts; hedge with smaller index puts further out-of-the-money.
- Short VIX futures, calibrated for the volatility-of-volatility.
- Variance swaps: sell implied variance, hedge dynamically.

**Where it lives in *151 Strategies*.** §2.6–2.57 (option spreads), §7.4 (VRP), §7.6 (variance swaps).

**Sharpe expectation.** 0.5–1.0 with severely negative skew.

**Failure mode.** **The crash trade.** 2018 "Volmageddon" took XIV to zero in one session. 2020 March wiped years of carry. The strategy has positive expected value but a left tail that occasionally erases years.

**Capacity.** High in listed index vol; bounded in single-stock vol.

## 6. Market-making

**Idea.** Quote both sides of the bid-ask spread; earn the spread on flow.

**Construction.** Continuously post bid + ask near mid-price; manage inventory delta. Profit per round-trip is small (a fraction of a basis point); volume is the multiplier.

**Where it lives in *151 Strategies*.** §3.19 (single-stock market making).

**Sharpe expectation.** When done right, 5–10. Capacity-constrained at the firm scale.

**Failure mode.** **Adverse selection.** When informed traders take only one side of your quote, you accumulate losing positions. Sophisticated market-makers move quote prices the moment they detect informed flow.

**Capacity.** Limited per venue / asset; can be enormous in aggregate (Citadel Securities, Virtu).

## 7. Event-driven / multi-leg

**Idea.** Trade around discrete corporate events — M&A, earnings, index inclusions/exclusions, spinoffs, restructurings.

**Construction.**
- **M&A arbitrage.** Long the target / short the acquirer in a stock deal; long the target / short cash position in a cash deal. Earn the spread between deal price and current market price.
- **Earnings drift.** Buy stocks with positive earnings surprises, sell those with negative — the "post-earnings announcement drift" (PEAD).
- **Index rebalancing.** Front-run anticipated additions, back-run deletions.

**Where it lives in *151 Strategies*.** §3.16 (M&A), §3.2 (earnings momentum).

**Sharpe expectation.** PEAD 0.7–1.0; M&A arb 0.5–1.0 over long horizons.

**Failure mode.** **Deal breaks** (M&A) wipe out years of carry. **Earnings revisions** can re-rate stocks against the PEAD signal.

**Capacity.** High in PEAD; modest in M&A arb (constrained by deal flow).

## Combining patterns

The most robust strategies are **combinations** of uncorrelated patterns:

- Trend (40%) + carry (30%) + mean-reversion (30%) gives smoother drawdowns than any single style.
- Equity factor portfolios (value + momentum + quality + low-vol) typically Sharpe 1.0–1.5 net of costs, vs 0.5–0.8 for any single factor.

The diversification works because the patterns fail at different times — trend crashes don't coincide with carry crashes don't coincide with stat-arb regime shifts. Renaissance reportedly runs hundreds of uncorrelated signals; AQR runs dozens; most retail strategies stop at one or two.

## What pattern is your idea?

Whenever you have a new strategy hypothesis, the first question is: **which pattern does it fit?** This anchors your expectations for Sharpe, capacity, drawdown profile, and failure mode. It also flags whether the "edge" is real (new variation on a known pattern with new data) or illusory (same pattern in a market where everyone knows it).

## See also

- [[factor-investing|Factor investing]] — the academic literature on the persistent patterns
- [[backtesting-discipline|Backtesting discipline]]
- [[transaction-costs|Transaction-cost modelling]]
- [[quant-research-workflow|Quant research workflow]]

## External references

- Kakushadze & Serur (2018). *151 Trading Strategies*. SSRN id 3247865 — the canonical strategy catalog this entry draws from.
- Chan (2009). *Quantitative Trading*. Wiley. Practitioner-oriented walk-throughs of patterns 1–3.
- Pedersen (2015). *Efficiently Inefficient*. Princeton. Asset-manager interviews + the academic synthesis behind patterns 1, 2, 3, 5.
- Asness, Moskowitz, Pedersen (2013). "Value and Momentum Everywhere." *Journal of Finance*.
