# Commodity Hedging Pressure

The **hedging-pressure** strategy uses CFTC's weekly **Commitments of Traders (COT)** data to identify commodities where commercial hedgers are heavily short relative to speculators, and trades the prediction that those commodities offer a positive risk premium to the speculator who takes the other side. It is the most direct empirical test of **Keynes's "normal backwardation"** hypothesis ([[commodity-roll-yield|see roll yield]]) using actual position data.

Kakushadze & Serur (2018) §9.2 describe the construction in two paragraphs. The intellectual lineage runs from **Cootner (1960)** through **Hirshleifer (1990)** to modern implementations like **Basu & Miffre (2013)** and **de Roon, Nijman, Veld (2000)**.

## The intuition — Keynes restated with position data

Keynes's normal-backwardation hypothesis says producers are systematic shorts in their commodity's futures, so they pay a premium to speculators (long futures) to take that risk. **The CFTC publishes who is on each side**. The COT data lets us check the hypothesis directly and use it as a real-time signal.

If commercial hedgers are heavily short (which is when they're paying the largest premium), the **opposite** long position should earn the most. If commercials are net long (perhaps a consumer hedging input costs), speculators short the contract should earn the premium instead.

## The signal — hedging pressure index

For each commodity, the CFTC reports weekly:

- Number of long and short contracts held by **commercial hedgers** (producers and consumers using futures to hedge physical business).
- Number of long and short contracts held by **speculators** (everyone else).

Define **hedging pressure** for each group as:

```
HP_hedger      = (Long_hedger) / (Long_hedger + Short_hedger)
HP_speculator  = (Long_speculator) / (Long_speculator + Short_speculator)
```

Both lie between 0 and 1.

- `HP_hedger = 0` means hedgers are 100% short (commodity producers fully hedged).
- `HP_hedger = 1` means hedgers are 100% long (commodity consumers fully hedged).
- The two are roughly mirror images (futures are zero-sum) — when hedgers are net short, speculators are net long, and vice versa.

The book's interpretation of HP signal:

```
High hedgers' HP  →  hedgers are net long  →  consumers hedging  →  expect CONTANGO,  negative roll
Low  hedgers' HP  →  hedgers are net short →  producers hedging →  expect BACKWARDATION, positive roll

High speculators' HP →  speculators net long →  consumer-hedged commodity →  weaker premium
Low  speculators' HP →  speculators net short →  producer-hedged commodity →  stronger premium
```

So **low hedgers' HP and high speculators' HP both flag backwardation candidates** — commodities where producers are paying for hedging, speculators are absorbing it long, and the roll premium should be high.

## The construction

The book's two-stage cross-sectional construction (§9.2):

```
1. Divide the universe of commodity futures into upper half and lower half
   by the SPECULATORS' HP.

2. Within the upper half (speculators relatively long), select the BOTTOM quintile
   by the HEDGERS' HP. Long these commodities.

3. Within the lower half (speculators relatively short), select the TOP quintile
   by the HEDGERS' HP. Short these commodities.

4. Equal-weight each leg. Hold for 6 months. Re-screen.
```

The reasoning: the long leg captures commodities where speculators are clearly absorbing producer hedging pressure (Keynesian premium). The short leg captures commodities where the opposite is happening — consumers are hedging, speculators are providing short liquidity, and the roll premium runs the *other* way.

## Empirical record

**Cootner (1960)** showed that long positions in producer-hedged commodities earned positive returns historically. **de Roon, Nijman, Veld (2000)** documented the HP premium across 20 commodity futures over 1986–1994 with Sharpe ratios in the 0.3–0.6 range. **Basu & Miffre (2013)** and **Fuertes, Miffre, Fernandez-Perez (2015)** updated to more recent samples and added refinements:

- The HP signal is **complementary to the term-structure (roll-yield) signal**. Combined Sharpes are higher than either alone.
- The HP signal is **slower-moving** than the roll-yield signal, with rebalancing horizons of 6 months reasonable (versus monthly for roll-yield).
- The signal **decayed but did not vanish** after the 2004–2008 commodity financialisation.

## How HP relates to roll yield

[[commodity-roll-yield|Roll yield]] uses price data (term-structure slope). HP uses position data (COT). They are **correlated but not identical**:

- A backwardated curve (positive roll yield) usually coincides with low hedgers' HP (producers short).
- But not always: a commodity can have low hedgers' HP and be in contango if the convenience-yield is low despite producer hedging (gold is a typical example — producers do hedge, but the cost-of-carry exceeds the convenience yield).

Combining the two signals improves selection:

| Roll-yield signal | HP signal | Interpretation | Trade |
|---|---|---|---|
| Backwardated | Producers short | Classic Keynesian setup | Strong long |
| Backwardated | Producers long | Curve says backwardation but data says consumers hedged | Weak long |
| Contangoed | Producers long | Consumer hedging dominates | Short |
| Contangoed | Producers short | Producers short but curve still contango — convenience yield low | Avoid or neutral |

## What HP captures that price alone misses

- **Forward-looking information.** Hedger positions reflect operational commitments (planned production hedged, expected consumption covered). They lead curve shape changes.
- **Aggregate flow.** Hedging volume is observable. A spike in producer hedging is a leading indicator of either rising production or falling expected prices — both push down the futures curve.
- **Sentiment signal.** Extreme speculator positioning (`HP_speculator > 0.9` or `< 0.1`) often marks turning points, similar to extreme positioning in other markets.

## Practical issues

- **Reporting lag.** COT data is released Fridays for the prior Tuesday — 3-day delay. Position-based strategies must account for this when backtesting.
- **Disaggregated vs. legacy reports.** The CFTC publishes both a legacy COT (commercial / non-commercial / non-reportable) and a disaggregated COT (producer / merchant / swap dealer / managed money / other reportable). Disaggregated is more accurate; legacy is longer history. Most academic work uses the legacy report.
- **Commodity coverage.** COT data covers all major US-traded commodities. International futures (LME copper, ICE Brent) are not in CFTC reports; their analogous data (LME COTR, ICE) is different in structure.
- **Position-data quality post-2007.** The Dodd-Frank classification revisions changed how some categories are reported. Time-series breaks must be handled.

## Variants

- **Net positioning normalisation.** Use `(Long_hedger − Short_hedger) / (Long_hedger + Short_hedger)` as a `[-1, +1]` signal instead of the `[0, 1]` HP. Same information, different scaling.
- **HP momentum.** The *change* in hedger positioning over a window is itself a signal — a sharp increase in producer shorts often precedes price weakness.
- **Combined with futures basis.** Multiple papers (Fuertes et al.) document that combining HP with the term-structure slope and with price momentum produces a triple-signal that materially outperforms any single signal.

## When this strategy has worked

- **Pre-2004 commodity markets.** Classic Keynesian premia were available at scale. The HP signal produced steady 5–8% per year excess returns on the long leg.
- **Post-2009 recovery.** As index flow normalised, HP signals re-strengthened.
- **2020–2022.** Producer hedging in oil and gas during the supply shocks was extreme; the long-backwardated leg earned spectacular returns.

## When it has failed

- **2004–2008 financialisation.** Index investors (treated as speculators in the COT) flooded the long side. The "speculator long" signal was no longer specifically directional — it was just passive index flow.
- **2014–2016 oil collapse.** Producer hedging surged as prices fell, but the HP signal correctly flagged producer shorting; the issue was the magnitude of the spot move, not the signal direction.
- **2020 March COVID.** Position data went stale fast as markets dislocated; the lag in COT data made the signal less responsive than needed in a fast-moving market.

## Common mistakes

- **Treating COT as a market-timing signal in isolation.** HP works as a cross-sectional ranking, not as a "load up when speculators are extreme" signal. Mean-reversion of speculator positioning is overstated in popular trading lore.
- **Ignoring the 3-day report lag.** A backtest that uses Tuesday's positions to trade Tuesday's prices is forward-looking; you can only act on the Friday release.
- **Conflating "swap dealers" with "speculators."** In the disaggregated report, swap dealers are large index-tracking entities, not directional speculators. Their flows are mechanical and shouldn't be treated as sentiment.
- **Equal-weighting across very different commodities.** A 1% position in palladium has 10x the vol of a 1% position in corn. Vol-scale.

## Risk management essentials

- **Vol-target each leg.** Equal-vol weighting smooths out the differential volatility across commodities (cattle is very different from natural gas).
- **Cap roll-yield drag.** If the long leg ends up dominated by contangoed commodities (signal mismatch), force a roll-yield filter.
- **Limit single-commodity exposure.** Some commodities (palladium, lean hogs) are thinly traded; cap positions to a percentage of open interest.
- **Pay attention to delivery month.** Strategy is built around continuous front-month exposure; avoid getting caught in delivery on physically-settled commodities.

## Where to do this in the terminal

- **COT screen** — weekly COT releases visualised; HP indices plotted; cross-sectional ranking displayed.
- **AI Quant Lab** — HP-based commodity strategy template, combinable with roll-yield and momentum signals.
- **Backtesting** — runs the HP strategy with the realistic 3-day reporting lag and per-commodity costs.

## See also

- [[commodity-roll-yield|Commodity Roll Yield]] — the term-structure cousin signal; often combined with HP
- [[commodity-value-momentum|Commodity Value and Momentum]] — the value and trend factors in commodities
- [[fx-carry-trade|FX Carry Trade]] — the same risk-premium logic in currencies
- [[strategy-patterns|Strategy Patterns]] — carry as an archetype

## External references

- Cootner, P. (1960). "Returns to Speculators: Telser versus Keynes." *Journal of Political Economy* 68(4), 396–404.
- Hirshleifer, D. (1990). "Hedging Pressure and Futures Price Movements in a General Equilibrium Model." *Econometrica* 58(2), 411–428.
- de Roon, F., Nijman, T., Veld, C. (2000). "Hedging Pressure Effects in Futures Markets." *Journal of Finance* 55(3), 1437–1456.
- Basu, D., Miffre, J. (2013). "Capturing the Risk Premium of Commodity Futures: The Role of Hedging Pressure." *Journal of Banking & Finance* 37(7), 2652–2664.
- Bessembinder, H. (1992). "Systematic Risk, Hedging Pressure, and Risk Premiums in Futures Markets." *Review of Financial Studies* 5(4), 637–667.
- Fuertes, A., Miffre, J., Fernandez-Perez, A. (2015). "Commodity Strategies Based on Momentum, Term Structure, and Idiosyncratic Volatility." *Journal of Futures Markets* 35(3), 274–297.
- Dewally, M., Ederington, L., Fernando, C. (2013). "Determinants of Trader Profits in Commodity Futures Markets." *Review of Financial Studies* 26(10), 2648–2683.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §9.2. https://doi.org/10.1007/978-3-030-02792-6
