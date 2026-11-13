# Bond Butterflies — Curve Curvature Trades

A bond butterfly is a **three-maturity position that bets on the *curvature* of the yield curve**, not its level or slope. The portfolio is constructed long–short–long across short, medium, and long maturities, typically engineered to be duration-neutral so that **parallel curve shifts wash out** and only changes in the curve's shape matter.

There are four practical variants — dollar-duration-neutral, fifty-fifty, regression-weighted, and maturity-weighted — distinguished by what additional curve-motion they neutralise (or deliberately expose to). Kakushadze & Serur (2018) §5.6–§5.8 lay out all four.

## The setup

Three bonds at maturities `T₁ < T₂ < T₃`, with modified durations `D₁`, `D₂`, `D₃`, and prices `P₁`, `P₂`, `P₃`. The trade is: **long the wings (T₁ and T₃), short the body (T₂)**, or vice versa.

Why this shape? Because if the curve gets *more curved* (the middle of the curve rises relative to the wings, like a hump growing), the body sells off more than the wings — bad for a long-body, good for a short-body. The butterfly isolates that shape change.

## Variant 1 — Dollar-Duration-Neutral Butterfly (§5.6)

The standard textbook butterfly. Two constraints:

```
P₁ + P₃  =  P₂                         (Eq. 5.31)  dollar-neutral (zero cost)
P₁·D₁ + P₃·D₃  =  P₂·D₂                (Eq. 5.32)  dollar-duration-neutral
```

The first equation says the total dollar long equals the total dollar short — the trade is self-financing.
The second says the duration-weighted dollar exposure of the wings equals that of the body — so a small parallel shift in yields doesn't move the position's P&L.

What's left? **Sensitivity to curve curvature** (and second-order effects from large parallel shifts via differential convexity). The trade pays when the body cheapens relative to the wings (curve becomes more bowed) and loses when the body richens.

**Profile**: zero-cost, duration-neutral, **exposed to slope and curvature changes**.

## Variant 2 — Fifty-Fifty Butterfly (§5.7)

The fifty-fifty (a.k.a. "neutral-curve" or "balanced") butterfly differs by **balancing the dollar-duration on each wing**:

```
P₁·D₁  =  P₃·D₃  =  (1/2) · P₂·D₂        (Eq. 5.33)
```

This means each wing carries half the body's dollar duration. Consequences:

- Still dollar-duration-neutral (sum of wings' duration = body's).
- **Not** dollar-neutral — it's not a zero-cost trade; one side has more notional.
- **Approximately neutral to small slope changes** of the curve.

Why is it slope-neutral? If the spread `(R₂ − R₁)` and `(R₃ − R₂)` change by the same amount (a uniform steepening or flattening), the equal dollar-duration on each wing means the P&L from the short-wing leg cancels the P&L from the long-wing leg. Only **non-uniform changes** in the spreads — i.e., changes in the *relative* spread between the two halves of the curve, which is curvature — move the position.

**Profile**: not zero-cost, duration-neutral, slope-neutral, **purely exposed to curvature**.

## Variant 3 — Regression-Weighted Butterfly (§5.8)

The fifty-fifty assumes that the short-wing spread and long-wing spread move by the *same* amount. Empirically, **short-end yield spreads are more volatile than long-end spreads** (Mankiw–Summers 1984; Shiller 1979). So the equal-dollar-duration weighting over-weights the short wing in volatility-adjusted terms.

The fix: estimate the empirical sensitivity `β` from a regression of `Δ(R₂ − R₁)` on `Δ(R₃ − R₂)`. Typically `β > 1`.

Then:
```
P₁·D₁ + P₃·D₃  =  P₂·D₂                  (Eq. 5.34)  duration-neutral
P₁·D₁  =  β · P₃·D₃                      (Eq. 5.35)  curve-neutrality
```

The wings now have *unequal* dollar-duration, with the short wing carrying `β` times more than the long wing. This is meant to **truly isolate the curvature signal** by stripping out the empirically-typical slope-change pattern.

**Profile**: not zero-cost, duration-neutral, **empirically slope-neutral**, exposed to curvature deviations from the typical pattern.

## Variant 4 — Maturity-Weighted Butterfly

Instead of regressing to estimate `β`, this variant uses a **deterministic** rule based on the maturities themselves:

```
β  =  (T₂ − T₁) / (T₃ − T₂)              (Eq. 5.36)
```

This is essentially saying: weight by the maturity gap, on the assumption that yield-spread volatility scales linearly with the maturity gap. It's a structural shortcut that avoids the noise of estimating `β` from data — useful when data is scarce or the time series is short.

**Profile**: similar to regression-weighted but using a maturity-distance heuristic instead of an empirical regression.

## Which variant to use?

| Variant | Slope-neutral? | Zero-cost? | When to choose |
|---|---|---|---|
| Dollar-duration-neutral | No | Yes | View on both slope and curvature; want simplicity |
| Fifty-fifty | Approximately | No | Pure curvature view, simple constant assumption |
| Regression-weighted | Empirically | No | Pure curvature view, willing to estimate `β` |
| Maturity-weighted | Approximately | No | Pure curvature view, can't estimate `β` reliably |

Practitioner default: **regression-weighted** if you have ≥ 3 years of clean spread data on the same issuer; **maturity-weighted** otherwise.

## What the trade actually bets on

In rate-derivatives speak, the butterfly is a **bet on the 2nd principal component (PC2) of the yield curve**, which is curvature.

Empirically (Litterman & Scheinkman 1991):

- PC1 ≈ level — explains ~80% of yield curve variance.
- PC2 ≈ slope — explains ~15%.
- PC3 ≈ curvature — explains ~3%.

A butterfly that's both duration-neutral and slope-neutral isolates that final ~3%. Two consequences:

1. **The signal is small**, so position sizing must be large to generate meaningful P&L.
2. **Mistakes are unforgiving** — if your "neutralisation" of slope is imperfect, slope movements (which are 5× the variance of curvature) will dominate your supposed curvature trade.

## Where the trade works

- **Rich/cheap analysis vs. fitted yield curve.** Identify maturities that are dislocated relative to a smooth fitted curve (e.g., Nelson-Siegel-Svensson), trade the butterfly to capture the dislocation.
- **Convexity arbitrage.** Long-wing convexity exceeds body convexity, all else equal. A duration-matched long-wings / short-body position captures convexity *for free* if the math is right — but this only matters in volatile rate environments.
- **Macro views on curve shape.** "I think the curve will hump-up at 5y vs 2y and 10y" → long 5y bullet, short 2y+10y wings (a sold butterfly).

## Risks

- **Curvature is the last residual after level and slope.** If your duration-neutralisation is off by 0.1y on a multi-billion-dollar portfolio, the level-move P&L can dwarf the curvature P&L.
- **Liquidity at the wings.** The 30y end of the curve is much less liquid than the 5–10y belly. Bid-ask costs eat butterfly P&L disproportionately.
- **Issuer credit drift.** If wings and body are different issuers (or different on/off-the-run statuses), credit/liquidity spread changes look like curvature changes.
- **Roll risk.** Butterflies must be rebalanced as bonds age. The structural maturity gaps shift, and so does the optimal `β`. Without disciplined rebalancing, the trade silently drifts off its intended profile.

## Historical context

Butterfly trades are a staple of the **government-bond relative-value desk** at investment banks. The strategy was originally documented at length by **Grieves (1999)** and **Fontaine & Nolin (2017)** in the context of bond portfolio management.

A particularly famous butterfly episode: the 2002–2003 "carry-trade in butterflies" where dealers loaded up on 5y vs 2y/10y butterflies betting on continued curve flattening, and got hurt when the Fed signalled tightening in mid-2003 and the curve butterfly inverted overnight.

## Common mistakes

- **Confusing dollar-neutral with duration-neutral.** They are different constraints. Equation 5.31 vs. 5.32. A trade can be one without the other.
- **Using stale durations.** Modified duration changes with yield level. After a 50 bps move, your "duration-matched" butterfly might be 5% mismatched.
- **Estimating `β` on too-short windows.** Regression-weighted butterflies need at least 36 months of spread data; less than that and your `β` estimate is dominated by noise.
- **Not netting against existing curve exposure.** Most rates books already have curve exposure from other positions. A "butterfly trade" added on top is rarely a pure curvature bet.

## Where to do this in the terminal

- **Bond screen** — fitted yield curve (Nelson-Siegel-Svensson) with maturity-by-maturity rich/cheap deviations highlighted.
- **AI Quant Lab** — butterfly construction module with all four variants; user inputs `T₁`, `T₂`, `T₃` and the system computes weights for each variant.
- **Backtesting** — historical butterfly P&L decomposition into level, slope, and curvature contributions.

## See also

- [[fi-yield-curve-positioning|Yield Curve Positioning]] — bullets/barbells/ladders, the directional cousins
- [[fi-curve-spreads|Yield Curve Spreads]] — flatteners and steepeners (PC2 trades)
- [[fi-bond-immunization|Bond Immunization]] — duration matching, the foundation for all duration-neutral construction
- [[strategy-patterns|Strategy Patterns]] — where curve trades fit in the strategy catalogue

## External references

- Grieves, R. (1999). "Butterfly Trades." *Journal of Portfolio Management* 26(1), 87–95.
- Litterman, R., Scheinkman, J. (1991). "Common Factors Affecting Bond Returns." *Journal of Fixed Income* 1(1), 54–61.
- Fontaine, J.-S., Nolin, S. (2017). "Measuring Limits of Arbitrage in Fixed-Income Markets." Staff Working Paper 2017-44, Bank of Canada.
- Bedendo, M., Cathcart, L., El-Jahel, L. (2007). "The Slope of the Term Structure of Credit Spreads: An Empirical Investigation." *Journal of Financial Research* 30(2), 237–257.
- Christiansen, C., Lund, J. (2005). "Revisiting the Shape of the Yield Curve: The Effect of Interest Rate Volatility." Working paper, SSRN 264139.
- Mankiw, N., Summers, L. (1984). "Do Long-Term Interest Rates Overreact to Short-Term Interest Rates?" *Brookings Papers on Economic Activity* 1984(1), 223–242.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.6–5.8. https://doi.org/10.1007/978-3-030-02792-6
