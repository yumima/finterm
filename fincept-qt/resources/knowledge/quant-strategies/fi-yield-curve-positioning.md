# Yield Curve Positioning — Bullets, Barbells, Ladders, and Rolling Down

Four closely-related strategies for **structuring a bond portfolio across maturities**: bullets, barbells, ladders, and rolling down the yield curve. They look nearly identical on a spreadsheet — same instruments, similar yields — but they bet on very different things about the curve's shape and motion.

This entry covers Kakushadze & Serur (2018) §5.2–5.4 and §5.12 together, because in practice these four are choices on the same dial: *how concentrated should my maturities be, and where should they sit on the curve?*

## Prerequisite — what duration and convexity actually measure

Skip this if you have it. Two quantities run through everything below:

**Modified duration** `D`: the percentage price change of a bond for a 1 percentage-point parallel shift in yields.
```
ΔP/P  ≈  −D · ΔY                         (first-order approximation)
```

**Convexity** `C`: the second-order correction.
```
ΔP/P  ≈  −D · ΔY  +  (1/2) · C · (ΔY)²   (Eq. 5.16)
```

Convexity is *always positive* for plain bonds, so its contribution is *always positive*. A higher-convexity portfolio outperforms a lower-convexity one for the same duration, after any large yield move in either direction. **Higher convexity is unambiguously good** — which is why traders pay for it.

For a parallel shift, modified duration scales roughly **linearly** with maturity, but convexity scales roughly **quadratically**. This single fact drives most of what follows.

## Bullets

A **bullet portfolio** holds all bonds at the same maturity `T`. It's the simplest possible structure.

When to use it:
- You have a **directional view** on rates at that specific point on the curve.
- You expect parallel curve shifts (or you're willing to bear the basis risk if you're wrong).
- You want minimum complexity.

When not to use it:
- You have **no view** on rates — a bullet concentrates risk at one maturity for no reason.
- You expect the curve to **twist or flatten** — a bullet bets on one point, ignoring the rest of the curve.

Practical note from the book: bullets are typically built up over time (dollar-cost-averaging into the target maturity), which softens timing risk on the entry yield.

## Barbells

A **barbell portfolio** holds bonds at two maturities, `T₁` short and `T₂` long, with no middle exposure. It is the opposite of a bullet structurally.

The book derives the key result in equations (5.17)–(5.21). Take a bullet at the mid-maturity `T* = (w₁·T₁ + w₂·T₂) / (w₁ + w₂)` with the same modified duration as the barbell. Then:

```
C_barbell − C_bullet  =  [w̃₁ · w̃₂ / (w̃₁ + w̃₂)²] · (T₂ − T₁)²   (Eq. 5.21)
```

This is **strictly positive**. **A barbell always has higher convexity than the duration-matched bullet**. The wider the maturity gap `(T₂ − T₁)`, the larger the convexity advantage.

**What you give up**: the barbell has lower yield than the duration-matched bullet. In a normal upward-sloping curve, the bullet sits on the steepest part of the curve where yields are highest; the barbell averages a high long yield and a low short yield, and the average is lower than the mid-point yield.

**The trade**: pay yield, get convexity. Profitable if rate volatility is high or curve shifts are large; loses to bullet if yields drift smoothly without much volatility.

A heuristic from rates desks: **barbell when implied rate vol is high or curves are steep; bullet when vol is compressed and curves are flat**.

## Ladders

A **ladder portfolio** holds equal-weighted positions in bonds at `n` equally-spaced maturities `T_i = T_0 + i·δ`. Typical ladder: 10 bonds, one per year out to year 10.

The book frames it as "duration-targeting" — as bonds age and shorten, you sell them and buy new long-maturity bonds, keeping the **average maturity roughly constant**:

```
T_ladder  =  (1/n) · Σᵢ Tᵢ              (Eq. 5.22)
```

What a ladder gives you:

- **Steady reinvestment cadence.** Every year (or whatever the rung spacing is), one bond matures and gets re-invested at the prevailing long-rate. This **averages your entry yields over time**, smoothing reinvestment risk.
- **Cash-flow predictability.** Useful for liability matching when the liabilities are smooth.
- **Reduced concentration risk** compared to bullets.

What a ladder doesn't give you:

- **Convexity advantage.** A ladder's convexity is between bullet and barbell — better than bullet, worse than barbell, for the same duration.
- **Pure curve view.** A ladder essentially has no directional curve view; it's a hold-to-maturity-style structure.

Ladders are the go-to for endowments, individuals, and corporate treasuries — anyone who needs a steady income stream and doesn't want to take tactical curve views.

## Rolling down the yield curve

This is **not a portfolio structure but a return-generating mechanism** that all three of the above benefit from when the curve is upward-sloping.

The idea: a bond's yield depends on its time-to-maturity. As time passes and the bond ages, its remaining time-to-maturity shortens. If the curve is **upward-sloping and unchanged**, the bond's yield decreases (it moves down the curve), and **decreasing yield means rising price**. This price gain is the "roll-down."

The book formalises it in §5.11–§5.12. With constant term-structure of yields `R(t,τ) = f(T − t)`, total carry between `t` and `t + Δt` decomposes as:

```
C(t, t+Δt, T)  =  R(t,T) · Δt  +  C_roll(t, t+Δt, T)         (Eq. 5.41)

where  C_roll(t, t+Δt, T)  ≈  −ModD(t,T) · [R(t, T−Δt) − R(t,T)]   (Eq. 5.42)
```

Reading this in English: your total return over the period is (current yield) × (time) **plus** (modified duration) × (the yield difference between your maturity-bucket and the slightly-shorter bucket you'll be in next period).

In a steep curve, the second term is large and positive — you earn the coupon **plus** an extra capital gain from rolling down. In a flat curve, the second term is near zero.

**Rolling-down strategy**: buy bonds in the **steepest segment** of the curve (where the roll-down is largest), hold them while they roll, then sell and replace with new long-end bonds when the steep section has been captured.

Historical context: the rolling-down trade was big in the 1990s and 2000s when curves were reliably positive. Post-2008 ZIRP made the trade harder — there was nothing to roll *to* at the short end. Curve flattening or inversion (2019, 2022–2023) flips the sign — rolling *up* a flat-to-inverted curve costs you money.

## How they relate — one picture

| Structure | Duration | Convexity | Yield | Best when |
|---|---|---|---|---|
| Bullet at mid `T*` | D | Lowest | Highest | Vol low, curve flat, directional view |
| Ladder around `T*` | D | Medium | Medium | No view, want smooth cash flows |
| Barbell with mid-D `T*` | D | Highest | Lowest | Vol high, curve steep, expect twists |

For the **same duration**, you can express different views by choosing how concentrated vs. dispersed your maturities are. That's the whole point.

## Where they break

**Bullets break** when the curve twists against you. A bullet at 5y can underperform a barbell at (2y + 10y) by 100+ bps over a year if the curve flattens — even though duration matches.

**Barbells break** when the curve steepens dramatically and the long end sells off harder than the short end rallies. The 2022 episode (long yields up >250 bps, short up a similar amount but starting from zero) hurt barbells with very long back legs.

**Ladders break** in inverted curves. The reinvestment of maturing short bonds happens at *lower* rates than the bonds you're selling at the long end. A ladder built in 1980 and run through 1985 underperformed almost everything.

**Rolling-down breaks** when curves flatten or invert. Empirically, the strategy's Sharpe collapsed in 2007–2008 and 2019–2020 because the assumption of a "stable, upward-sloping curve" failed.

## Common mistakes

- **Treating all four as interchangeable.** They have very different convexity and curve-view content. A ladder is not a bullet with extra bonds.
- **Ignoring credit when maturities span issuers.** A 30-year corporate vs. a 30-year Treasury are not the same risk. The curve view assumes you're working within one issuer or one credit quality.
- **Assuming yields stay constant.** Equation 5.41 derives roll-down under that assumption. In practice yields move; the actual return is roll-down *plus* the duration-times-yield-change term, which can dominate.
- **Over-leveraging the convexity advantage.** The convexity gain only shows up when yields move *a lot*. In quiet markets, the barbell-vs-bullet trade is mostly yield drag.

## Risk management essentials

- **Match duration honestly.** If you're comparing a barbell to a bullet, match modified duration to two decimals before declaring a "convexity advantage." Small duration mismatches dominate convexity in any realistic move.
- **Account for credit dispersion.** If your ladder spans 1y to 10y in corporates, the 10y has both more duration risk *and* more credit-curve risk. Treat them differently in sizing.
- **Stress test for non-parallel shifts.** The whole framework assumes parallel shifts. Run separate scenarios for bull-steepening, bear-flattening, and twists.

## Where to do this in the terminal

- **Bond screen** — view the live yield curve for governments, agencies, and corporates with maturity-bucketed positions overlaid.
- **Portfolio** — duration and convexity diagnostics for arbitrary bond holdings; switches between bullet / barbell / ladder visualisations.
- **AI Quant Lab** — yield-curve-positioning template; backtest bullet vs barbell vs ladder over historical curve regimes.

## See also

- [[fi-butterflies|Bond Butterflies]] — duration-neutral curve trades (the active version of these positioning ideas)
- [[fi-curve-spreads|Yield Curve Spreads — Flatteners and Steepeners]] — directional curve trades
- [[fi-bond-immunization|Bond Immunization]] — duration-targeting for liability matching
- [[fi-bond-factors|Bond Factor Investing]] — carry, value, low-risk in bonds

## External references

- Fabozzi, F. (2012). *Bond Markets, Analysis, and Strategies.* Prentice Hall. — the standard reference for bullet/barbell/ladder construction.
- Grantier, B. (1988). "Convexity and Bond Performance: The Benter The Better." *Financial Analysts Journal* 44(6), 79–81.
- Dyl, E., Joehnk, M. (1981). "Riding the Yield Curve: Does It Work?" *Journal of Portfolio Management* 7(3), 13–17.
- Ang, S., Alles, L., Allen, D. (1998). "Riding the Yield Curve: An Analysis of International Evidence." *Journal of Fixed Income* 8(3), 57–74.
- Grieves, R., Marcus, A. (1992). "Riding the Yield Curve: Reprise." *Journal of Portfolio Management* 18(4), 67–76.
- Pelaez, R. (1997). "Riding the Yield Curve: Term Premiums and Excess Returns." *Review of Financial Economics* 6(1), 113–119.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.2–5.4 and §5.12. https://doi.org/10.1007/978-3-030-02792-6
