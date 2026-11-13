# Hedging Risk with Futures

The original economic purpose of futures markets is **hedging** — transferring price risk from someone who has it (a farmer with growing crop, an airline with fuel exposure, a pension fund with duration exposure) to someone willing to take it (a speculator). This entry covers the three canonical futures-hedging strategies: **direct hedge**, **cross-hedge** (when a futures contract for the exact asset doesn't exist), and **interest-rate hedging** for bond portfolios.

Kakushadze & Serur (2018) §10.1 cover these compactly. The strategies are operational and risk-management oriented rather than alpha-generating, but understanding them is essential for any institutional finance practitioner.

## Strategy 1 — Direct hedge

The simplest case: a grain trader who needs to **buy `X` tons of soy at time `T`** can hedge by **buying soy futures today** with delivery `T`. If soy prices rise between now and `T`, the futures gain offsets the higher physical price. If prices fall, the futures loss is offset by the cheaper physical.

```
At time t: anticipate need to buy X tons at time T.
Action: Buy X tons worth of soy futures with delivery T.

At time T: 
  Pay S(T) for the physical.
  Settle the futures at S(T) − F(t,T).
  
Net cost: F(t,T) — locked in at time t.
```

This is **perfect hedging**: if the futures contract exactly matches the underlying asset and delivery date, the hedge has zero basis risk.

## Strategy 2 — Cross-hedging

Often, no futures contract exists for the exact asset being hedged. A jet fuel buyer can't hedge with a jet fuel futures contract (there isn't one); they use heating oil or crude oil futures as a proxy.

This introduces **basis risk** — the gap between the hedged asset and the proxy.

Following the book's formulation (§10.1, Eq. 10.1):

```
S(T) − F(T,T) + F(t,T)  =  [S*(T) − F(T,T)] + [S(T) − S*(T)] + F(t,T)   (Eq. 10.1)
```

Where `S*` is the spot price of the **underlying of the futures contract** (e.g., heating oil) and `S` is the spot of the **asset being hedged** (e.g., jet fuel). The terms decompose into:

- `F(t,T)` — the locked-in futures price (no risk).
- `S*(T) − F(T,T)` — **convergence basis risk**: the gap between futures and its own underlying at delivery. Usually small.
- `S(T) − S*(T)` — **cross-asset basis risk**: the gap between jet fuel and heating oil prices at delivery. Can be large.

The strategy works to the extent that `S − S*` is stable. Heating oil and jet fuel move together (95%+ correlation), but not identically. The residual is the basis risk you accept.

### Optimal hedge ratio

Instead of 1-to-1 hedging (`h = 1`), use a **regression-based hedge ratio**:

```
h*  =  ρ · σ_S / σ_F
```

Where `ρ` is the correlation between spot returns and futures returns, and `σ_S`, `σ_F` are the respective volatilities. This is the **minimum-variance hedge ratio** (Ederington 1979).

For jet fuel hedged with heating oil, `h*` is typically 0.85–0.95 (correlation < 1, jet fuel slightly more volatile than heating oil).

### Common cross-hedge applications

| Hedge target | Futures used | Why |
|---|---|---|
| Jet fuel | Heating oil (HO) | Closest existing futures contract |
| Diesel | Heating oil (HO) | Refining proximity |
| Coal | Natural gas (NG) | Power-generation substitute |
| Aluminium-can manufacturer | Aluminium (LME) | Industrial input cost |
| Bond portfolio | Treasury futures (TY, US) | Duration matching |
| Single-currency exposure | Major FX futures | Liquidity |

## Strategy 3 — Interest Rate Risk Hedging

A bond portfolio is exposed to rate moves. Futures hedging uses interest-rate futures (Treasury bond futures, Eurodollar futures, etc.) to neutralise the duration exposure.

For a long position in bonds (you suffer when rates rise), **sell** interest-rate futures (which gain when rates rise) to hedge.

```
P_L(t,T)  =  B(0,T) − B(t,T)                                    (Eq. 10.2)
P_S(t,T)  =  B(t,T) − B(0,T)                                    (Eq. 10.3)

Where B(t,T) = S(t) − F(t,T) is the futures basis.            (Eq. 10.4)
```

`P_L` and `P_S` are the P&L on long and short hedges respectively (with unit hedge ratio).

### Hedge ratios for bond portfolios

Two standard approaches:

**Conversion factor model** (for bonds in the futures' delivery basket):

```
h_C  =  C · (M_B / M_F)                                         (Eq. 10.5)
```

Where `M_B` is the bond nominal value, `M_F` is the futures nominal value, and `C` is the conversion factor used by the exchange.

**Modified duration model** (more general, works for non-deliverable bonds):

```
h_D  =  β · (D_B / D_F)                                         (Eq. 10.6)
```

Where `D_B` and `D_F` are the dollar durations of bond and futures respectively, and `β` is the change in bond yield relative to futures yield (often set to 1).

The duration-matched hedge is the bond-market analogue of the [[fi-bond-immunization|immunization strategies]] — neutralise the first-order rate sensitivity.

## When hedging is right and wrong

Hedging is a risk-management decision, not an alpha decision. It's **right** when:
- You have specific exposure that you don't want.
- The hedge basis risk is small.
- The cost (bid-ask, margin) is acceptable.

It's **wrong** (or at least, more nuanced) when:
- You're "hedging" because of a directional view (then it's just a speculative trade with extra steps).
- The basis risk is comparable to the underlying risk (you've changed the risk, not reduced it).
- The hedge cost (in lost upside) exceeds the protection value.

### The over-hedging trap

A common mistake: hedge **more than** the underlying exposure to "be safe." This often turns a hedge into a speculative position. If a corporate hedges 150% of its anticipated fuel needs, the 50% over-hedge is a directional bet on oil prices.

## Practical issues

### Margin and mark-to-market

Futures hedges require margin. As prices move against the hedge (or in its favour), margin calls / margin releases occur daily. The corporate hedger may face cash-flow stress even when the hedge is "working" — because the mark-to-market loss on the futures side is paid daily, while the offsetting gain on the physical is realised only at delivery.

This **funding-liquidity risk** is what made several airlines stop hedging fuel in 2008 (Southwest Airlines maintained their famous hedge, others didn't).

### Rolling

Most futures contracts have monthly or quarterly expiries. Long-horizon hedges (e.g., 5-year bond hedges) require continuously rolling forward. Rolling has costs:
- Roll yield (in contango or backwardation, you lose or gain on rolls).
- Transaction costs (bid-ask spreads on the spread trade).
- Liquidity (front-month is most liquid; far-deferred contracts can be thin).

### Tax and accounting treatment

Hedge accounting (under ASC 815 / IFRS 9) has specific requirements to qualify. If a hedge fails accounting requirements, the gains/losses are recognised through P&L rather than other comprehensive income, creating earnings volatility.

## Historical lessons

### Metallgesellschaft (1993)

Metallgesellschaft, a German oil refiner, hedged long-dated oil supply contracts with short-dated futures. When oil prices fell, the long-term contracts gained value but the short-dated futures generated massive margin calls. The company nearly went bankrupt because the **maturity mismatch** between hedge and exposure created funding stress.

The lesson: hedge with futures of matching maturity, not just matching direction.

### Long-Term Capital Management (1998)

LTCM ran hugely leveraged convergence trades using futures-like instruments. When their trades widened against them, margin demands forced unwinds at the worst time. The lesson: **leverage + hedging do not eliminate risk; they transform timing risk into funding risk**.

### 2008 oil hedging

Several airlines (Delta, United) had **mark-to-market losses** on long-oil hedges as oil collapsed from $147 to $35 in H2 2008. The hedges were "working" in the sense that fuel was now cheaper, but the hedge accounting made the losses look catastrophic. Some carriers exited hedge positions at the bottom, locking in losses.

## Common mistakes

- **Over-hedging.** Hedging more than the underlying exposure is speculation.
- **Maturity mismatch.** Hedge with futures whose expiry matches the underlying exposure timing.
- **Ignoring funding risk.** Margin calls on hedge legs can stress corporate liquidity.
- **Forgetting tax implications.** Hedge accounting rules are real; ignore them and you can have phantom earnings volatility.
- **Treating hedge ratio as static.** The optimal ratio changes with market conditions; re-estimate periodically.

## Risk management essentials

- **Maturity matching**: hedge expiry should align with underlying exposure.
- **Hedge ratio re-estimation**: monthly or quarterly, depending on volatility regime.
- **Liquidity buffer**: maintain cash reserve for margin variation.
- **Basis monitoring**: track the spot-futures basis; widening basis = hedge breaking down.
- **Net exposure reporting**: report hedged vs. unhedged exposure clearly to management.

## Where to do this in the terminal

- **Futures screen** — futures contracts with basis monitor for cross-hedge candidates.
- **Portfolio** — bond portfolio duration analyzer with futures-hedge calculator.
- **AI Quant Lab** — cross-hedging strategy template with optimal hedge ratio estimation.

## See also

- [[fi-bond-immunization|Bond Immunization]] — the bond-portfolio version of duration hedging
- [[fx-carry-trade|FX Carry Trade]] — alternative for FX exposure (carry vs. hedge)
- [[futures-calendar-spread|Calendar Spread]] — playing the term structure of futures
- [[futures-contrarian|Futures Contrarian Trading]] — mean-reversion in futures
- [[commodity-roll-yield|Commodity Roll Yield]] — what happens when you don't hedge and just hold

## External references

- Ederington, L. (1979). "The Hedging Performance of the New Futures Markets." *Journal of Finance* 34(1), 157–170.
- Anderson, R., Danthine, J.-P. (1981). "Cross Hedging." *Journal of Political Economy* 89(6), 1182–1196.
- Ahmadi, H., Sharp, P., Walther, C. (1986). "The Effectiveness of Futures and Options in Hedging Currency Risk." *Advances in Futures and Options Research* 1(B), 171–191.
- Hilliard, J. (1984). "Hedging Interest Rate Risk with Futures Portfolios under Term Structure Effects." *Journal of Finance* 39(5), 1547–1569.
- Baillie, R., Myers, R. (1991). "Bivariate GARCH Estimation of the Optimal Commodity Futures Hedge." *Journal of Applied Econometrics* 6(2), 109–124.
- Gay, G., Kolb, R. (1983). "The Management of Interest Rate Risk." *Journal of Portfolio Management* 9(2), 65–70.
- Ankirchner, S., Dimitroff, G., Heyne, G., Pigorsch, C. (2012). "Futures Cross-Hedging with a Stationary Basis." *Journal of Financial and Quantitative Analysis* 47(6), 1361–1395.
- Mun, K. (2016). *Hedging with Futures*. Wiley.
- Working, H. (1953). "Futures Trading and Hedging." *American Economic Review* 43(3), 314–343.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §10.1. https://doi.org/10.1007/978-3-030-02792-6
