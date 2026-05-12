# Statistical Arbitrage

Statistical arbitrage ("stat-arb") is the cross-sectional generalisation of [[pairs-trading|pairs trading]]: instead of two cointegrated names, you decompose every name's return into a **factor component** and a **residual**, then mean-revert the residual. The strategy is market-neutral, sector-neutral by construction, and runs on hundreds to thousands of names simultaneously. The Sharpe of any single residual reversion is modest; the diversified book historically delivers Sharpe 2+ before costs.

Marco Avellaneda and Jeong-Hyun Lee's "Statistical Arbitrage in the U.S. Equities Market" (2010, *Quantitative Finance*) is the cleanest open description of how a modern stat-arb book is built. The framework predates them — D.E. Shaw, Renaissance, PDT Partners and Tartaglia's Morgan Stanley group all ran versions in the late 1980s — but Avellaneda-Lee is the canonical academic write-up.

## The residual construction

The starting recipe (Avellaneda-Lee, slightly modernised):

```
1. Estimate factor exposures.
   For each stock i, regress its returns on a factor set F:
     r_i,t = α_i + Σ_k β_{i,k} · f_{k,t} + ε_i,t
   F is typically:
     - ETF-based: sector ETFs (XLF, XLK, ...) + SPY for market.
     - PCA-based: top K principal components of the return correlation matrix.
     - Risk-model: Barra USE4 / Axioma worldwide medium-horizon model.
2. Build the residual series.
   For each name, accumulate ε_i,t into a "residual price" R_i,t = Σ ε_i,τ.
3. Calibrate Ornstein-Uhlenbeck on R_i,t.
   d R = −θ_i · (R − μ_i) · dt + σ_i · dW
   half_life_i = ln(2) / θ_i
   z_i,t = (R_i,t − μ_i) / σ_i
4. Trade z_i.
   Short the name when z_i > +1.25 (residual rich)
   Long  the name when z_i < −1.25 (residual cheap)
   Exit  when z_i crosses ±0.5
```

Avellaneda-Lee report Sharpe in the 1.5–2.0 range out-of-sample from 1997 to 2007 for ETF-based factors, falling to ~0.5 in 2007–2009 as crowding and the August 2007 quant meltdown hit the cohort.

## Why this scales when single-name reversion doesn't

A single name's residual reversion has Sharpe ~0.3 — usually not worth the costs. Diversification across 500 names with idiosyncratic residuals reduces noise much faster than expected return:

```
Sharpe_book ≈ Sharpe_single · √(N_effective)
```

N_effective is well below the nominal N because residuals are not fully independent (sector residuals correlate within sector after a generic factor model). In practice the lift is √100 ≈ 10, taking a 0.3 single-name Sharpe to ~3 book Sharpe. Costs and turnover eat much of that, leaving the historically reported Sharpe 1.5–2.0 net.

This diversification math is the entire economic moat of stat-arb relative to single-name strategies. Run the same idea on 5 names, the Sharpe is unworkable. On 500 with controlled factor exposure, it's a strategy.

## Factor / sector / beta neutrality

The cleanliness of the residual depends entirely on the factor model. Three approaches in production:

- **ETF hedging (Avellaneda-Lee).** Beta to SPY and beta to sector ETF together absorb most systematic exposure. Simple, robust, cheap to estimate. Default for new books.
- **PCA on returns.** Extract the top K principal components of the rolling 252-day return correlation matrix. K = 15–25 typically captures 60–70% of variance. More adaptive than ETF hedging; can over-fit the noisy tail of the eigenspectrum (use Marchenko-Pastur or random-matrix filters).
- **Commercial risk model (Barra/Axioma).** Country, sector, style (size, value, momentum, growth, quality, volatility, leverage). Most institutional shops use this. Better residuals, expensive licence, slower to react to regime changes.

After residual construction, the book should be:

- **Dollar-neutral** at the book level (longs notional ≈ shorts notional).
- **Beta-neutral** to SPY and to each sector ETF.
- **Style-neutral** to size, value, momentum (otherwise you're paying carry to be long a factor you're not getting compensated for).

If a stat-arb book has consistently positive exposure to the momentum factor, the right interpretation is "this is a momentum strategy with stat-arb dressing" — and you should compare its Sharpe to the [[momentum-factor|momentum factor]] alone before claiming any alpha.

## Crowding, capacity, and the August 2007 event

The defining stat-arb event is **August 6–9, 2007**: a sudden, simultaneous unwind of crowded long-short equity quant books. Goldman's Global Equity Opportunities fund lost ~30% in three days; many smaller shops blew up. Khandani & Lo (2007) reconstructed the mechanism: a single fund deleveraging a "standard" stat-arb book forced losses on identical positions held by every other fund running the same recipe, triggering further deleveraging. The unwind reversed within a week — but anyone forced to liquidate at the trough realised permanent losses.

Lessons that shaped subsequent practice:

- **Capacity awareness.** A stat-arb book at $5B AUM has different liquidity dynamics than at $500M. Forced unwinds at $5B move the residual itself. See [[transaction-costs|transaction costs]] and capacity modelling.
- **Avoid identical recipes.** Differentiating signal construction — alternative factor models, custom universes, novel features — reduces co-positioning with the cohort.
- **Liquidity-aware position sizing.** Limit single-name positions to N days of ADV (typical: 5–10% of 20-day ADV per name).
- **Stress test the unwind.** Simulate "what if I had to liquidate the book in 3 days." If the cost of unwind exceeds expected alpha for the next 6 months, you're over-sized.

## Refinements that earn marginal Sharpe

- **Residual-of-residual.** Factor out a second pass on industry sub-groupings (e.g., big-tech as a sub-sector within XLK).
- **Conditional reversion.** Some residual signals are stronger conditional on volume / news / borrow scarcity. Avellaneda's later papers extend the framework to these.
- **Short-horizon ML overlays.** Use gradient-boosted trees on order-flow features to weight which residual signals to take. Modest Sharpe lift, large infrastructure cost; reserved for the largest shops.
- **Borrow-cost integration.** Hard-to-borrow names are priced into the residual; explicitly modelling borrow rate as a factor reduces false signals on shorted names.

## Common mistakes

- **Over-fitting the factor model.** Using 50 PCA components captures noise as much as signal. Marchenko-Pastur filtering or cross-validated K selection helps.
- **Treating the in-sample Sharpe as live.** Stat-arb has historically halved post-publication on every documented strategy. Always deflate; see [[multiple-testing-correction|multiple-testing correction]].
- **Ignoring small-cap microstructure.** Stat-arb's gross Sharpe often comes from the lowest-liquidity quartile, where costs eat the entire alpha. Trade what you can execute.
- **Static parameters.** Half-lives, vol estimates and betas drift; refit at least monthly within a [[walk-forward-validation|walk-forward]].

## Risk management essentials

- **Concentration limits.** No name > X% of book gross; no sector > Y% of book gross.
- **Drawdown circuit-breakers.** If realised volatility of the book triples and PnL drawdown exceeds 5%, deleverage to half book size mechanically. Discretion costs you the second leg of unwinds.
- **Daily borrow re-check.** Borrow rates change overnight; a name that was short-friendly Monday may not be Tuesday.
- **Earnings exclusions.** Drop names within ±2 days of earnings — single-name binary risk overwhelms the residual reversion signal.

## Where to do this in the terminal

The **AI Quant Lab** has an end-to-end stat-arb pipeline: factor selection (ETF / PCA / Barra), residual construction, OU calibration per name, basket sizing, and book-level neutrality reporting. The **Backtesting** screen runs the full universe with realistic borrow and slippage models; outputs include per-sector PnL attribution and crowding-stress scenarios.

## See also

- [[pairs-trading|Pairs trading]] — the two-name special case; stat-arb is its scaled generalisation
- [[mean-reversion|Mean reversion]] — the underlying empirical fact about residual returns
- [[factor-investing|Factor investing]] — the factor models used to define the residual
- [[time-series-basics|Time-series basics]] — Ornstein-Uhlenbeck, half-life, stationarity
- [[multiple-testing-correction|Multiple-testing correction]] — every parameter choice is implicit multiple testing
- [[transaction-costs|Transaction costs]] — capacity and slippage dominate the live Sharpe

## External references

- Avellaneda & Lee (2010). "Statistical Arbitrage in the U.S. Equities Market." *Quantitative Finance* 10(7).
- Khandani & Lo (2007). "What Happened to the Quants in August 2007?" *Journal of Investment Management* 5(4).
- Khandani & Lo (2011). "What Happened to the Quants in August 2007? Evidence from Factors and Transactions Data." *Journal of Financial Markets* 14(1).
- Pole (2007). *Statistical Arbitrage: Algorithmic Trading Insights and Techniques*. Wiley.
- Laloux, Cizeau, Bouchaud & Potters (1999). "Noise Dressing of Financial Correlation Matrices." *Physical Review Letters* — random-matrix filtering for PCA factor models.
