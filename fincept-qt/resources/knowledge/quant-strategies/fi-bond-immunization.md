# Bond Immunization

Bond immunization is the strategy of **constructing a bond portfolio whose value moves in lockstep with a future liability**, so that interest-rate shifts hurt the asset side and the liability side equally and the funded status is preserved. It is the foundational technique of pension and insurance asset-liability management (ALM) and the original "duration matching" strategy.

Kakushadze & Serur (2018) §5.5 give the math compactly. The strategy is older than the book by decades — **Redington (1952)** introduced it; **Fisher and Weil (1971)** extended it to non-flat term structures.

## The setting

You owe a fixed amount `F` at some future date `T*` — a pension payment, an annuity, a structured product redemption. You want to invest the present value today such that, **no matter how yields move**, you can meet the obligation when it comes due.

The simplest solution: buy a zero-coupon bond maturing exactly at `T*` with face value `F`. Done. The cash flow is perfectly hedged because the bond's only cash flow occurs exactly when the liability is due.

The problem: **a zero-coupon bond at exactly the right maturity is often not available**, especially for very long liabilities (30+ years) or in markets without deep zero-coupon stripped issuance. So you must build a portfolio from available coupon bonds, and that portfolio is **not** automatically immunized — it has reinvestment risk (if rates fall, intermediate coupons reinvest at low rates) and price risk (if rates rise, final-period bond prices fall).

The Redington insight: **match the portfolio's modified duration to the liability's time horizon**, and the two risks cancel — at least for parallel yield-curve shifts.

## The math — two-bond and three-bond immunization

With two bonds (maturities `T₁` and `T₂`, durations `D₁` and `D₂`), three constraints determine the position:

```
P₁ + P₂  =  P                                   (Eq. 5.24)  total budget
P₁·D₁ + P₂·D₂  =  P·D                           (Eq. 5.25)  duration match

where  D = T*/(1 + Y·δ)                         (Eq. 5.26)
       P = F / (1 + Y·δ)^(T*/δ)                 (Eq. 5.23)
```

`P` is the present value of the liability (so you fund exactly enough), `D` is the liability's modified duration, `T*` is the liability maturity, `Y` is the (assumed flat) yield, `δ` is the compounding period (e.g., 1 year).

This is a two-equation, two-unknown system. Solve for `P₁` and `P₂`.

With three bonds, you can also **match convexity**:

```
P₁ + P₂ + P₃  =  P                              (Eq. 5.27)
P₁·D₁ + P₂·D₂ + P₃·D₃  =  P·D                   (Eq. 5.28)
P₁·C₁ + P₂·C₂ + P₃·C₃  =  P·C                   (Eq. 5.29)

where  C = T*·(T* + δ) / (1 + Y·δ)²              (Eq. 5.30)
```

Matching convexity protects against larger yield shifts (the curvature of the price-yield relationship), not just first-order changes.

## Why duration matching works — the intuition

When yields rise, two things happen to a bond portfolio:

1. **Price drops.** Existing bond holdings are marked down.
2. **Reinvestment yields rise.** Future coupons (and any maturing principal) reinvest at higher rates.

For a portfolio held to a specific horizon `T*`:

- If portfolio duration `D < T*` (too short), then when yields rise, the price drop is small but the reinvestment gain is small *and* you'll have to reinvest at the new high rates for years before `T*` — you actually benefit on net from a yield rise.
- If portfolio duration `D > T*` (too long), then the price drop dominates — you suffer.
- If portfolio duration `D = T*`, the two effects cancel at first order.

This is Redington's result. The economic interpretation: **duration is the horizon at which a bond's price risk and reinvestment risk offset each other**.

## What can go wrong

The math is exact only for **parallel, instantaneous, infinitesimal** yield curve shifts. Each of those words is a real-world failure mode:

### Non-parallel shifts

In reality, the short end and long end of the curve move by different amounts. Steepening, flattening, twists, butterfly shifts — all of these break duration matching. A bond portfolio with duration 10y might consist of a 30y bond and cash; if only the long end rises, the long bond suffers far more than the duration formula predicts.

**Solution**: include convexity matching, multiple key-rate durations, or full curve immunization (Fong & Vasicek 1983, 1984).

### Large yield shifts (non-infinitesimal)

The first-order approximation `ΔP/P ≈ −D·ΔY` only holds for small `ΔY`. For 100+ bps moves, you need the convexity term, and even with convexity, very large shifts (300+ bps) require higher-order matching.

**Solution**: convexity matching, plus stress-testing for large parallel shifts.

### Time

Duration changes as time passes (the bonds age, their remaining cash flows shrink). The matched duration today is *not* the matched duration in three months. Without rebalancing, the immunization drifts.

**Solution**: periodic rebalancing (typically quarterly or semi-annually). This brings transaction costs that real-world ALM programmes have to budget for.

### Credit risk

If your "bonds" are corporates or non-Treasury, you're matching duration against the rate component but the credit-spread component is unhedged. A widening of credit spreads hurts the asset side without affecting the (typically Treasury-discounted) liability.

**Solution**: immunize with Treasuries only, or explicitly hedge spread risk separately.

## Where it's used in practice

- **Pension funds.** Defined-benefit pensions have long-dated liabilities (20–60 years) and use **liability-driven investing (LDI)**, the institutional implementation of duration matching with key-rate durations across the curve.
- **Insurance companies.** Life insurers and annuity providers have predictable long-dated cash flows; they immunize against rate moves to keep statutory reserves stable.
- **Structured products.** A bond-backed CD or structured note can immunize its payoff with a duration-matched bond portfolio.
- **Defeasance.** A corporate or municipal issuer wanting to remove a bond from its balance sheet can buy a duration-matched Treasury portfolio held in escrow; rating agencies treat the original debt as defeased.

## A historical lesson — Orange County 1994

Orange County, California ran an aggressively *un-immunized* bond portfolio in 1994: long duration, leveraged via repo, betting on rates falling. When Greenspan raised rates 300 bps over the year, the portfolio lost $1.7 billion. The county filed bankruptcy.

The lesson: **duration mismatch is a directional rate bet, even if the position is "just bonds."** Immunization is the discipline of *not* taking that bet when the mandate doesn't support it.

## Common mistakes

- **Matching duration once and forgetting.** Duration drifts. Without rebalancing, the immunization decays over time.
- **Ignoring convexity for long horizons.** A 30-year liability needs convexity matching; first-order duration matching alone fails badly under large yield shifts.
- **Assuming a flat yield curve.** The two-bond formula (Eq. 5.24–5.26) assumes constant yield `Y`. Real curves are sloped. Use Fisher-Weil duration (cash-flow-weighted, present-valued at the actual term-structure) instead of textbook Macaulay duration for non-flat curves.
- **Hedging with off-the-run bonds.** Some bonds have low liquidity premia and behave differently from the on-the-run curve. Stick to liquid issues or run a separate basis hedge.

## Risk management essentials

- **Rebalance on a schedule, not on a whim.** Time-based rebalancing (quarterly) is more disciplined than event-driven; reduces over-trading.
- **Use key-rate durations for long-dated liabilities.** A single duration number hides the actual sensitivity to different parts of the curve. Decompose into 2y / 5y / 10y / 30y key rates.
- **Stress-test for non-parallel shifts.** Run +/−100 bp parallel, +50 bp steepener, +50 bp flattener, 1994-style 300 bp rise scenarios.
- **Track credit-spread risk separately.** If the portfolio holds non-Treasuries, the spread component is a separate book.

## Where to do this in the terminal

- **Portfolio** — duration / convexity / key-rate-duration diagnostics for arbitrary bond holdings, against a user-defined liability stream.
- **Bond screen** — yield curve visualization with the portfolio's duration plotted as a vertical line; helps eyeball mismatch.
- **AI Quant Lab** — immunization template with two-bond and three-bond solvers.

## See also

- [[fi-yield-curve-positioning|Yield Curve Positioning]] — bullets, barbells, ladders as immunization building blocks
- [[fi-butterflies|Bond Butterflies]] — duration-neutral curve trades
- [[portfolio-construction|Portfolio Construction]] — the broader optimisation context (Markowitz, risk parity)

## External references

- Redington, F. (1952). "Review of the Principles of Life Office Valuations." *Journal of the Institute of Actuaries* 78(3), 286–340.
- Fisher, L., Weil, R. (1971). "Coping with the Risk of Interest-Rate Fluctuations: Returns to Bondholders from Naïve and Optimal Strategies." *Journal of Business* 44(4), 408–431.
- Fong, H., Vasicek, O. (1983). "The Tradeoff Between Return and Risk in Immunized Portfolios." *Financial Analysts Journal* 39(5), 73–78.
- Fong, H., Vasicek, O. (1984). "A Risk Minimizing Strategy for Portfolio Immunization." *Journal of Finance* 39(5), 1541–1546.
- Bierwag, G., Kaufman, G. (1978). "Bond Portfolio Strategy Simulations: A Critique." *Journal of Financial and Quantitative Analysis* 13(3), 519–525.
- Albrecht, P. (1985). "A Note on Immunization Under a General Stochastic Equilibrium Model of the Term Structure." *Insurance: Mathematics and Economics* 4(4), 239–244.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.5. https://doi.org/10.1007/978-3-030-02792-6
