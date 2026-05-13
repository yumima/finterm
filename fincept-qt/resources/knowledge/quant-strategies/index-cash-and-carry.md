# Index Cash-and-Carry Arbitrage (and Intraday ETF Arb)

**Cash-and-carry** is the canonical example of how derivatives markets discipline cash markets. The futures price of an index is **mathematically pinned** to the spot price by the no-arbitrage relationship: futures = spot + cost-of-carry. When the relationship breaks, an arbitrageur can lock in a risk-free profit by going long one side and short the other.

In practice the trade is the domain of **high-frequency desks at major banks and proprietary firms**. Retail/manual implementation is essentially impossible. Kakushadze & Serur (2018) §6.2 give the math in one page. The closely-related **intraday arbitrage between two ETFs tracking the same index** (§6.4) operates on the same principle at higher frequency.

## The math — the no-arbitrage identity

For an index spot value `S(t)` at time `t`, the fair futures price `F*(t,T)` for delivery at time `T` is:

```
F*(t,T) = [S(t) − D(t,T)] · exp(r · (T − t))            (Eq. 6.1)
```

Where:
- `S(t)` = current spot value of the index basket.
- `D(t,T)` = sum of (discounted values of) dividends paid by the index constituents between `t` and `T`.
- `r` = risk-free rate (assumed constant from `t` to `T`).

The **basis** is the deviation of the observed futures price from the fair price:

```
B(t,T) = [F(t,T) − F*(t,T)] / S(t)                      (Eq. 6.2)
```

Where `F(t,T)` is the actually-traded futures price.

### The strategy rule

```
If B > 0 (futures rich vs. fair):
  Sell the futures, buy the basket (cash).
  Hold until the basis converges to zero.

If B < 0 (futures cheap vs. fair):
  Buy the futures, sell the basket (cash).
  Hold until convergence.
```

This is **textbook arbitrage**: a riskless profit if the trade can be executed and held to convergence.

## Why does this exist at all?

In an idealised continuous-time market with zero costs, this arbitrage cannot exist. In reality it does, but for short periods and at small magnitudes, because:

### 1. Execution latency

Spot and futures prices update at slightly different speeds. Spot value of the S&P 500 is a synthetic — it must be computed from 500 constituent prices, each updating asynchronously. The futures price (E-mini ES) updates instantly on every tick.

In the milliseconds between a synchronised market move and the basket "catching up," the basis temporarily widens. HFT firms catch these windows.

### 2. Trading costs and market impact

Selling 500 stocks simultaneously to "execute the basket" carries real costs:
- Bid-ask spreads on illiquid components.
- Market impact (large orders move price).
- Settlement timing differences.

So `|B|` must exceed transaction-cost magnitude (10–30 bps on the S&P 500 basket) for the arbitrage to be profitable.

### 3. Funding and short-selling constraints

The arbitrage requires either borrowing money to buy the basket or borrowing stock to short it. Hard-to-borrow names (after corporate events, in distressed sectors) make the short leg expensive or impossible.

### 4. Dividend uncertainty

`D(t,T)` is assumed known. In practice, dividend amounts and timing can change. Estimation error in `D(t,T)` shows up as basis volatility that's not pure arbitrage.

## Empirical record

Index futures arbitrage was a major source of profit for HFT desks in the 1990s and early 2000s:

- **MacKinlay & Ramaswamy (1988)**: documented persistent S&P 500 futures mispricings before the S&P/CME systems fully integrated.
- **Cornell & French (1983)**: foundational paper on stock index futures pricing.
- **Modern era (2010s+)**: opportunities are now in the **microsecond regime**. Cross-venue execution gaps between CME (futures) and NYSE/NASDAQ (cash) generate fleeting basis dislocations.

Median lifetime of an exploitable basis dislocation in S&P 500 futures: **under 100 milliseconds**. This is HFT territory.

## The closely-related strategy — intraday arbitrage between two ETFs (§6.4)

The book covers an analogous strategy at §6.4: two ETFs tracking the **same** underlying index occasionally diverge in price intraday.

Example: SPY and IVV both track the S&P 500. Both should trade at the same price (give or take the small expense ratio differential). They mostly do — but intraday, brief 1–3 bps dislocations occur as one ETF leads price discovery.

```
Rule:
  Buy ETF2, short ETF1  if  P^Bid_2 ≥ P^Ask_1 × κ
  Liquidate                if  P^Bid_2 ≥ P^Ask_1
  Buy ETF1, short ETF2  if  P^Bid_1 ≥ P^Ask_2 × κ
  Liquidate                if  P^Bid_1 ≥ P^Ask_2          (Eq. 6.8)
```

Where `κ` is a profit threshold (e.g., 1.002 = require 20 bps gap to enter).

### Why it works

The two ETFs have slightly different authorised-participant pools, different mechanisms for creation/redemption, different listing venues. These differences create temporary dislocations.

### Why it's hard

Same as index futures arbitrage: the dislocations are **sub-second**, requiring fast execution. Marshall, Nguyen, Visaltanachoti (2013) documented these dislocations and the trading mechanics in detail.

## Variants of cash-and-carry

### Cash-and-carry on commodities

The same principle applies to commodity futures (oil, gold, copper). The fair price involves:
- Spot price + financing cost − convenience yield.

Mispricings in commodity term structure are typically larger and more persistent than equity index basis — but trading them requires physical storage capability or warehouse-receipt logistics. Still a real strategy for commodity traders.

### Cash-and-carry on Treasuries

The Treasury basis (cash bond price vs. futures price) is one of the most-traded fixed-income relative value trades. Hedge funds (Citadel, Millennium, others) historically had large books on it — including the famous Long-Term Capital Management 1998 trades.

Notably: the Treasury basis blew out in March 2020 as funding stresses forced unwinds; one of the most dramatic basis dislocations on record.

### SPDR vs. iShares ETF arbitrage

The §6.4 trade applied to SPY vs IVV (or any pair). Brown, Davies, Ringgenberg (2018) studied ETF arbitrage and found significant predictability around ETF flows.

## What this section is useful for

Most readers won't run cash-and-carry directly — it requires sub-millisecond infrastructure. But understanding it matters:

1. **It anchors what a "real" arbitrage looks like** — a mathematical no-arb identity that breaks down at the margin.
2. **It explains why index futures prices stay so close to fair value** — HFT firms enforce the relationship continuously.
3. **It calibrates execution expectations** — if HFTs can't keep the basis tight, they're either offline or the move is huge.

## Common mistakes

- **Confusing static arbitrage opportunity with executable arbitrage.** The math says profit; reality includes slippage, fees, financing costs, dividend estimation error.
- **Trying it at retail latency.** A 1-second-old quote is useless. You can't compete with HFT desks colocated at CME.
- **Assuming `D(t,T)` is known.** Dividend estimates carry uncertainty. Treat them as random variables with confidence intervals.
- **Ignoring funding costs.** Borrowing to fund the cash leg has a real cost; assuming "frictionless" overstates returns.

## Risk management essentials

- **Counterparty risk on stock loan**: short legs require borrowing; lenders can recall, blowing up the trade.
- **Margin shocks**: in a stress event, futures margin requirements can jump 50%+ overnight.
- **Tracking error on the basket replicator**: using fewer than 500 stocks to replicate the S&P is fast but introduces basis risk.
- **Settlement timing**: T+2 settlement on the cash side vs. cash-settled futures creates a small mismatch period.

## Where to do this in the terminal

- **Futures screen** — live futures vs. spot basis monitor; flags significant deviations.
- **ETF screen** — intraday ETF spread monitor for SPY/IVV/VOO and other tracking pairs.
- **Backtesting** — historical cash-and-carry P&L with realistic tick-level execution modelling (limited; full HFT backtesting requires specialised data).

## See also

- [[fx-triangular-arbitrage|FX Triangular Arbitrage]] — the FX analogue
- [[fi-credit-arbitrage|Credit Arbitrage]] — CDS-bond basis, similar funding-risk dynamics
- [[index-dispersion-trading|Dispersion Trading]] — index vol vs. constituent vol
- [[strategy-patterns|Strategy Patterns]] — arbitrage as a strategy archetype

## External references

- Cornell, B., French, K. (1983). "The Pricing of Stock Index Futures." *Journal of Futures Markets* 3(1), 1–14.
- MacKinlay, A., Ramaswamy, K. (1988). "Index-Futures Arbitrage and the Behavior of Stock Index Futures Prices." *Review of Financial Studies* 1(2), 137–158.
- Yadav, P., Pope, P. (1990). "Stock Index Futures Arbitrage: International Evidence." *Journal of Futures Markets* 10(6), 573–603.
- Yadav, P., Pope, P. (1994). "Stock Index Futures Mispricing: Profit Opportunities or Risk Premia?" *Journal of Banking & Finance* 18(5), 921–953.
- Marshall, B., Nguyen, N., Visaltanachoti, N. (2013). "ETF Arbitrage: Intraday Evidence." *Journal of Banking & Finance* 37(9), 3486–3498.
- Brown, D., Davies, S., Ringgenberg, M. (2018). "ETF Arbitrage and Return Predictability." Working paper, SSRN 2872414.
- Ben-David, I., Franzoni, F., Moussawi, R. (2017). "Do ETFs Increase Volatility?" Working paper, SSRN 1967599.
- Dwyer, G., Locke, P., Yu, W. (1996). "Index Arbitrage and Nonlinear Dynamics Between the S&P 500 Futures and Cash." *Review of Financial Studies* 9(1), 301–332.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §6.2 and §6.4. https://doi.org/10.1007/978-3-030-02792-6
