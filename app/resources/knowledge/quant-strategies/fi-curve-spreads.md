# Yield Curve Spreads — Flatteners and Steepeners

A yield-curve spread trade is a **directional bet on the slope of the curve**: long one maturity, short another from the same issuer, sized to express a view on whether the curve will steepen or flatten. It is the second principal component of bond returns (after level), the most-watched indicator of central-bank policy expectations, and historically one of the most reliable predictors of US recessions.

Kakushadze & Serur (2018) §5.13 give the rule compactly. This entry expands the macro context, the curve-shape drivers, and the standard sizing and risk-management practices.

## The basic trade

The yield-curve spread is the difference between two yields on the same issuer (typically Treasuries) at two maturities:

```
Spread  =  R(t, T_long) − R(t, T_short)
```

The classic spreads watched by the market:

| Spread | Watched for |
|---|---|
| **2s10s** (10y − 2y) | Recession indicator; standard yield-curve slope |
| **5s30s** | Long-end shape, term-premium dynamics |
| **3m10s** | Cleaner recession signal (Fed-controlled short rate vs. long market rate) |
| **2s5s** | Belly-of-curve action; sometimes used in butterflies |

The rule from the book (§5.13):

```
Flattener:  short the spread  →  short the back leg, long the front leg
            (use when rates are expected to rise / curve to flatten)

Steepener:  long the spread   →  long the back leg, short the front leg
            (use when rates are expected to fall / curve to steepen)   (Eq. 5.43)
```

## Why short-the-spread = expecting flattening

If the curve flattens, the long-end yield falls (or rises less) relative to the short-end yield. Long bond prices rise relative to short bond prices. A **short spread position** (short long bond + long short bond, sized appropriately) profits when the long bond outperforms.

But raw "short the long bond" loses money if rates fall in *parallel*. That's why the book notes: **match dollar durations of the front and back legs to remove parallel-shift exposure**.

## Duration-matched sizing

The book is brief here, but this is critical. If you short a 10y note and long a 2y note with the **same notional**, you have far more duration exposure on the 10y side. A parallel rate rise hurts the 10y much more, so your "spread trade" is mostly a directional level bet in disguise.

To isolate slope:

```
Notional_short × ModD_short  =  Notional_long × ModD_long
```

Example: if 10y duration is 8 years and 2y duration is 1.9 years, the 10y notional should be roughly `1.9 / 8 ≈ 0.24` times the 2y notional. So for every $100 of 2y, short $24 of 10y. The trade is now **dollar-duration-neutral** and isolates slope.

This is identical to the construction of a [[fi-butterflies|fifty-fifty butterfly]] without the middle leg.

## The macro drivers — what actually moves the curve

The curve does not move randomly. Its shape is dominated by **monetary policy expectations** and **term premium**:

### Front end (2y and below)

Pinned by Fed policy expectations. The 2y yield is essentially "where the market thinks the Fed funds rate will average over 2 years."

- When the market prices Fed cuts → 2y falls fast.
- When the market prices Fed hikes → 2y rises fast.

### Back end (10y and beyond)

Driven by long-run inflation expectations + term premium + supply/demand for safe duration.

- Term premium is what investors demand to bear duration risk; it has structurally declined since the 1980s (Adrian-Crump-Moench).
- Long yields move slower than short yields because long-run expectations move slower than near-term policy expectations.

### So the curve flattens when…

- The Fed is hiking and the market doesn't fully price the implied future inflation reduction → front end up faster than back end.
- A late-cycle recession scare → front end stays high (Fed slow to ease), back end falls on growth fears.
- The classic **bear-flattener** (front up, back up but less) and **bull-flattener** (front flat, back down) patterns.

### And steepens when…

- Fed starts cutting aggressively → front end falls fast.
- Massive fiscal expansion → long end rises on supply.
- Inflation expectations un-anchor → long end rises.
- Classic **bull-steepener** (front down, back flat) and **bear-steepener** (front flat, back up).

## The recession-indicator angle

The 3m–10y curve has inverted before every US recession since 1969. The 2s10s has the same record, with one questionable signal in the 1960s.

The mechanism (Estrella & Hardouvelis 1991; Estrella & Mishkin 1998): when the Fed tightens enough to invert the curve, monetary conditions are tight enough that growth eventually weakens. The lag from inversion to recession is **typically 12–18 months**.

This makes curve trades **macro instruments** as much as relative-value trades. A steepener position right before an inversion-driven recession can pay enormously when the Fed eventually cuts.

## Historical context — when curve trades worked spectacularly

- **2000–2002.** Inversion to bull-steepening. Long-2y / short-10y steepener returned multi-hundred basis points.
- **2007–2008.** Pre-GFC inversion to bull-steepening. Steepeners shone.
- **2018–2020.** Multiple inversions, COVID steepening — steepeners worked.
- **2022–2023.** Curve inverted aggressively, then began re-steepening in 2024. Bear-steepener (front down, back up on supply) was the dominant trade.

When they failed:

- **2004–2006.** "Conundrum" period — Fed hiking but long yields stuck. Flatteners worked steadily but the bear-flattener bet via short-back was muted because back rates barely moved.
- **2014–2015.** Long-end pinned by ECB QE and global term-premium compression. Curve trades had less variance to work with.

## Variants

- **Box trade.** Long 2s5s flattener + short 10s30s flattener = a "curve box." A bet on whether front-curve flattens *more* than back-curve.
- **Conditional steepener via options.** Buy short-end calls, sell long-end calls, structured to pay only if curve steepens. Risk-defined version of the trade.
- **Cross-currency curve trades.** Long US 2s10s steepener, short EUR 2s10s steepener — a bet on relative central-bank divergence.

## Common mistakes

- **Trading raw notionals.** Mismatched dollar-duration makes it a level trade, not a slope trade. Match durations.
- **Ignoring carry.** A steepener (long long-end, short short-end) typically earns positive carry from the higher long-end yield minus the lower short-end yield. The trade pays you to wait — usually. In inverted-curve environments, carry is *negative* and the trade costs you to hold.
- **Holding through Fed surprises.** Curve trades are most volatile around FOMC meetings, CPI prints, and unemployment reports. Size with that volatility in mind.
- **Conflating shape with level.** A curve "flattening" can happen via either the long end falling (bull-flat, often risk-off) or the short end rising (bear-flat, often Fed-driven). The trade has the same P&L but very different macro context.

## Risk management essentials

- **Sizing**: scale to a fixed DV01 (dollar value of a 1bp slope move), not to fixed notional.
- **Stop on slope dislocation, not level.** If 2s10s moves 25 bps against you (slope move), exit. If 2s10s is unchanged but both yields drop 100bps in parallel, that's not your trade — don't react.
- **Watch the rebalancing calendar.** Off-the-run vs. on-the-run rolls create temporary distortions that can be confused with slope signal.
- **Carry budget.** In inverted curves, a steepener bleeds carry. Have a time stop, not just a level stop.

## Where to do this in the terminal

- **Bond screen** — live 2s10s, 5s30s, 3m10s plots with historical inversion zones shaded.
- **AI Quant Lab** — curve-spread strategy template with auto duration-matching, FOMC-event flags, and PCA-decomposition of historical curve moves.
- **Backtesting** — P&L decomposition into level / slope / curvature contributions to diagnose what actually drove returns.

## See also

- [[fi-yield-curve-positioning|Yield Curve Positioning]] — bullets, barbells, ladders (the directional version)
- [[fi-butterflies|Bond Butterflies]] — curvature trades (the 3rd principal component)
- [[fi-bond-factors|Bond Factor Investing]] — carry, value, low-risk in bonds
- [[strategy-patterns|Strategy Patterns]] — where curve trades fit among the archetypes

## External references

- Litterman, R., Scheinkman, J. (1991). "Common Factors Affecting Bond Returns." *Journal of Fixed Income* 1(1), 54–61.
- Estrella, A., Hardouvelis, G. (1991). "The Term Structure as a Predictor of Real Economic Activity." *Journal of Finance* 46(2), 555–576.
- Estrella, A., Mishkin, F. (1998). "Predicting U.S. Recessions: Financial Variables as Leading Indicators." *Review of Economics and Statistics* 80(1), 45–61.
- Adrian, T., Crump, R., Moench, E. (2013). "Pricing the Term Structure with Linear Regressions." *Journal of Financial Economics* 110(1), 110–138.
- Diebold, F., Li, C. (2002). "Forecasting the Term Structure of Government Bond Yields." *Journal of Econometrics* 130(2), 337–364.
- Chua, C., Koh, W., Ramaswamy, K. (2006). "Profiting from Mean-Reverting Yield Curve Trading Strategies." *Journal of Fixed Income* 15(4), 20–33.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.13. https://doi.org/10.1007/978-3-030-02792-6
