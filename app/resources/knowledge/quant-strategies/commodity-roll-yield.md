# Commodity Roll Yield

The roll yield is the **single most distinctive return source in commodities**, and it has no equity analogue. It comes from the *shape* of the futures curve — specifically whether longer-dated contracts trade above (contango) or below (backwardation) the front month. A long position in a backwardated future earns positive roll yield; a long position in a contangoed future *bleeds* roll yield. This single fact has driven decades of commodity-investor performance dispersion.

Kakushadze & Serur (2018) §9.1 give the one-paragraph mechanic. The deeper theory — **Keynes's "normal backwardation"** and the **theory of storage** (Working 1933, Kaldor 1939) — is what makes this a real economic phenomenon, not just a quirk of futures pricing.

## The mechanic — what "rolling" actually does

A futures contract has a fixed expiration. To maintain continuous exposure (which a buy-and-hold commodity investor wants), you must **periodically sell the expiring contract and buy a deferred contract**. This is the "roll."

Define:
```
P₁ = front-month futures price
P₂ = second-month (deferred) futures price
φ = P₁ / P₂                                       (Eq. 9.1)
```

- **Backwardation**: `P₁ > P₂` (`φ > 1`). Front month is more expensive than deferred. **Rolling a long position pays you** — you sell high, buy low.
- **Contango**: `P₁ < P₂` (`φ < 1`). Front month is cheaper. **Rolling a long position costs you** — you sell low, buy high.

For a continuously-rolled long position, the **expected return** decomposes as:

```
Total return = Spot price change + Roll yield + Collateral return
```

The roll yield is positive in backwardation, negative in contango. Across many commodities, **the magnitude of the roll yield is comparable to or exceeds the spot price change** over multi-year horizons.

## The strategy

```
1. Across a universe of commodity futures, compute φ_i for each.
2. Long the top quantile by φ_i (most backwardated).
3. Short the bottom quantile by φ_i (most contangoed).
4. Equal-weight or risk-weight each leg.
5. Roll futures monthly (or before expiry).
```

Zero-cost, cross-sectional, market-neutral commodity strategy.

This is essentially the **carry factor in commodities**, the analogue of [[fx-carry-trade|FX carry]] and the [[fi-bond-factors|bond carry factor]]. Erb & Harvey (2006) is the canonical paper documenting this premium.

## Why the curve shape exists — two theories

The shape of a commodity futures curve is not arbitrary. It reflects real economic forces.

### Theory 1 — The theory of storage (Working, Kaldor)

Forward prices reflect spot price plus carrying costs minus convenience yield:

```
F(t,T) = S(t) · (1 + r·(T−t)) + Storage − Convenience yield
```

- **`r`** is the risk-free rate.
- **Storage** is the cost of physically warehousing the commodity (real for oil, grain; near-zero for gold).
- **Convenience yield** is the value of holding the physical asset (oil refineries need physical oil to operate; a futures contract doesn't run a refinery).

When inventories are tight, **convenience yield is high** and can exceed `r + Storage`, pushing futures below spot — *backwardation*. When inventories are abundant, convenience yield is low; the curve is in *contango*.

So backwardation signals **tight current supply** and contango signals **abundant current supply**. The roll yield is real compensation for bearing inventory risk.

### Theory 2 — Normal backwardation (Keynes 1930)

Producers (e.g., oil drillers, farmers) are *natural shorts* — they own the physical commodity and want to lock in future selling prices. To do that, they sell forwards. Speculators take the other side, buying forwards, but demand a **risk premium** to do so.

The result: forwards trade *below* expected future spot. The roll-up return as the contract approaches expiry is the speculator's risk premium.

This is "normal backwardation" — it predicts that long commodity-futures positions should earn a premium **on average**, even in commodities that spend most of their time in contango.

### What does the data say?

Gorton & Rouwenhorst (2006) "Facts and Fantasies about Commodity Futures" provided the empirical landmark study: from 1959–2004, an equal-weighted commodity-futures index earned roughly 11% nominal per year, comparable to equities and with low correlation to them.

The crucial finding: **the bulk of the return came from collateral interest and roll yield, not spot price appreciation**. Across many commodities, spot prices barely outpaced inflation, but the roll-yield premium in backwardated commodities was substantial.

After 2004, the picture changed:

- **2004–2014 commodity boom + financialisation.** Index investors (pension funds via GSCI/BCOM) flooded into the front-month long position, pushing curves into persistent contango. Roll yields turned negative across the board.
- **2014–2020 commodity bust.** Many commodities stayed in contango through the bust.
- **2020–2023 supply-shock era.** Strong backwardation returned, especially in energy. Roll yields were spectacular in 2022 (crude was backwardated by $5–10/bbl, generating 30%+ annualised roll yields on a long-only position).

The lesson: **the roll-yield premium is real but time-varying with structural supply/demand conditions and investor flows**.

## What the trade actually exposes you to

A long-short backwardation-vs-contango portfolio is:

- **Approximately commodity-spot-neutral** (long and short legs span the spot complex).
- **Net long convenience yield** — you're paid for bearing the implicit "physical-storage" risk.
- **Highly exposed to inventory shocks.** If a backwardated commodity (tight inventory) sees inventory build, the curve flips to contango and your long position pays the contango roll.

## Specific commodities — typical curve behaviour

| Commodity | Typical state | Why |
|---|---|---|
| **WTI crude oil** | Variable; contango during gluts, backwardation during shortages | Storage at Cushing, OK is the binding constraint |
| **Natural gas (Henry Hub)** | Seasonal contango — futures rise into winter | Heating demand cycle; storage is seasonal |
| **Gasoline (RBOB)** | Seasonal — backwardation in summer | Driving-season demand |
| **Gold** | Persistent contango at storage + rate cost | No convenience yield; financial asset behaviour |
| **Silver** | Mild contango | Some industrial demand creates intermittent backwardation |
| **Copper** | Variable; backwardation common in tight markets | Industrial-input convenience yield |
| **Corn / wheat / soybeans** | Seasonal — contango at harvest, backwardation pre-harvest | Crop cycle |
| **Live cattle** | Variable | Slow biological supply response |

Knowing these patterns matters because **a "high `φ`" signal at a structurally-backwardated commodity is less informative than the same signal at a structurally-contangoed one**. Some constructions normalise `φ` by each commodity's own history.

## Variants and extensions

- **Term-structure slope.** Instead of `P₁/P₂`, use the slope between, e.g., 1st and 12th contract. Reduces noise.
- **Roll-yield * momentum.** Combine the curve signal with price momentum on the spot. Fuertes, Miffre, Fernandez-Perez (2015) and others find combining curve and momentum signals materially improves Sharpe.
- **Active roll timing.** Instead of mechanical month-end rolling, choose the deferred contract and roll date to minimise carrying cost. The **enhanced commodity indices** (DJ-UBS-CI vs DJ-AIG-CI; S&P GSCI Dynamic; Bloomberg Roll Select) do exactly this.
- **Hedging-pressure overlay.** Combine `φ` with the [[commodity-hedging-pressure|COT-based hedging-pressure signal]] to identify which side speculators are taking and which is structurally short.

## Where the strategy has failed

- **2004–2008 financialisation.** Index flow pushed most commodities into deep contango. A long-only contango position lost 5–10% per year *just on roll*, even before any spot move.
- **2012–2015 oil glut.** WTI deep contango. Long-oil positions via ETFs (USO most famously) underperformed crude spot by 40+% over the period.
- **April 2020 negative oil.** Front-month WTI traded at -$37/bbl as storage at Cushing filled. ETF rolls were catastrophic. The roll-yield strategy, if scaled aggressively into the long side, had its worst single week in the asset class's history.

The third example deserves special note: **rolling a long position when storage physically runs out** can lose far more than the entire investment. ETFs like USO had to restructure mid-crisis. The strategy is not "free roll yield" — it's compensation for absorbing exactly the kind of inventory-tail risk that 2020 represented.

## Common mistakes

- **Treating roll yield as risk-free carry.** It's not. It's compensation for inventory risk, which is real and occasionally enormous.
- **Mechanical month-end rolling.** Predictable rolls get front-run by HFTs. Spread the roll over multiple days (the "TWAP roll") or use enhanced-roll methodologies.
- **Ignoring seasonality.** A "contango" signal in nat gas in October is meaningless — every nat-gas curve is in contango then. Normalise by seasonal pattern.
- **Comparing index returns to spot returns.** Most commodity indices report total return (including roll); spot indices report price changes only. Conflating them produces wildly wrong return expectations.

## Risk management essentials

- **Liquidity by contract.** Front-month majors (CL, NG, GC, SI) are deeply liquid; less-traded commodities and deferred months are thin. Cap positions by ADV per contract.
- **Exchange margin shocks.** During volatile periods, exchanges raise margin requirements unilaterally; this is a real funding cost that backtest models often miss.
- **Inventory tail risk.** Especially in oil. Build position limits that account for storage constraints in the underlying physical market.
- **Roll timing.** Spread roll over 5–10 days using the exchange "roll calendar" to avoid HFT front-running and reduce slippage.

## Where to do this in the terminal

- **Futures screen** — live term-structure curves for all liquid commodities; `φ` and slope-percentile flagged.
- **AI Quant Lab** — roll-yield strategy template with universe selection (energy / metals / softs / all) and per-commodity vol-targeting.
- **Backtesting** — historical roll-yield strategy P&L with proper rolling-window modelling and per-commodity cost assumptions.

## See also

- [[commodity-hedging-pressure|Commodity Hedging Pressure]] — the COT-data signal that complements curve shape
- [[commodity-value-momentum|Commodity Value and Momentum]] — the value and trend factors in commodities
- [[fx-carry-trade|FX Carry Trade]] — the same carry premium in currencies
- [[fi-bond-factors|Bond Factor Investing]] — the carry factor in fixed income
- [[strategy-patterns|Strategy Patterns]] — carry as one of the five archetypes across asset classes

## External references

- Erb, C., Harvey, C. (2006). "The Strategic and Tactical Value of Commodity Futures." *Financial Analysts Journal* 62(2), 69–97.
- Gorton, G., Rouwenhorst, K. (2006). "Facts and Fantasies About Commodity Futures." *Financial Analysts Journal* 62(2), 47–68.
- Gorton, G., Hayashi, F., Rouwenhorst, K. (2013). "The Fundamentals of Commodity Futures Returns." *Review of Finance* 17(1), 35–105.
- Fama, E., French, K. (1987). "Commodity Futures Prices: Some Evidence on Forecast Power, Premiums, and the Theory of Storage." *Journal of Business* 60(1), 55–73.
- Fuertes, A., Miffre, J., Fernandez-Perez, A. (2015). "Commodity Strategies Based on Momentum, Term Structure, and Idiosyncratic Volatility." *Journal of Futures Markets* 35(3), 274–297.
- Bessembinder, H. (1992). "Systematic Risk, Hedging Pressure, and Risk Premiums in Futures Markets." *Review of Financial Studies* 5(4), 637–667.
- Working, H. (1933). "Price Relations Between July and September Wheat Futures at Chicago since 1885." *Wheat Studies* 9(6), 187–238.
- Kaldor, N. (1939). "Speculation and Economic Stability." *Review of Economic Studies* 7(1), 1–27.
- Keynes, J. M. (1930). *A Treatise on Money*, Volume 2.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §9.1. https://doi.org/10.1007/978-3-030-02792-6
