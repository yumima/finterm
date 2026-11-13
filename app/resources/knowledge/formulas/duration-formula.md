**Duration** measures the sensitivity of a bond's price to changes in interest rates — Macaulay Duration gives the weighted-average time to receive cash flows; Modified Duration converts that to a price-change estimate.

```
Macaulay Duration:
D_mac = [ Σ t × PV(CF_t) ] / Bond Price

  t      = time period (in years)
  CF_t   = cash flow at time t (coupon or principal)
  PV(CF_t) = CF_t / (1 + y)^t
  y      = yield to maturity (per period)

Modified Duration:
D_mod = D_mac / (1 + y/m)

  m = number of coupon periods per year (2 for semiannual)

Price change approximation:
ΔP/P ≈ −D_mod × Δy
```

## Worked example — 5-year Treasury, 4.5 % coupon

```
Face value:  $1,000
Coupon:      $45 per year (semiannual: $22.50 every 6 months)
YTM:         4.5 %  → price ≈ $1,000 (at-par bond)
Maturity:    5 years (10 semiannual periods)

Period  Cash flow  PV factor (@ 2.25%)   PV       t × PV
  1       22.50    1/1.0225^1 = 0.9780   22.01     22.01
  2       22.50    1/1.0225^2 = 0.9564   21.52     43.04
  3       22.50    1/1.0225^3 = 0.9352   21.04     63.12
  4       22.50    1/1.0225^4 = 0.9145   20.58     82.31
  ...
  10    1022.50    1/1.0225^10 = 0.8003  818.57  8185.72

D_mac (semiannual periods) ≈ 9.01 periods
D_mac (years) = 9.01 / 2 = 4.51 years

D_mod = 4.51 / (1 + 0.045/2) = 4.51 / 1.0225 = 4.41 years

If rates rise 100 bps (Δy = +0.01):
ΔP/P ≈ −4.41 × 0.01 = −4.41 %
ΔP   ≈ −$44.10 on a $1,000 par bond
```

## What the result means

Modified Duration tells you the **% price change per 1 % (100 bps) parallel shift in rates**:

- Duration 2: bond falls ~2 % if rates rise 1 %.
- Duration 7: bond falls ~7 % if rates rise 1 %.
- Duration 20: long-dated zero-coupon bond; extremely rate-sensitive.

Duration is additive at the portfolio level: portfolio duration = weighted average of individual bond durations (by market value weight).

## Variants

- **Effective Duration** — handles embedded options (callable bonds, MBS); computed by shocking rates up and down rather than analytically.
- **Dollar Duration (DV01)** — the dollar price change for a 1 basis point (0.01 %) move in rates.
  ```
  DV01 = D_mod × Price × 0.0001
  ```
- **Convexity** — second-order rate sensitivity; long bonds have positive convexity (price rises more than duration predicts when rates fall).
- **Key Rate Duration** — sensitivity to each point on the yield curve independently; used for non-parallel shifts.

## Common mistakes

- Confusing Macaulay Duration (years of cash-flow timing) with Modified Duration (price sensitivity) — they are related but different quantities.
- Ignoring convexity for large rate moves — duration is a linear approximation; actual price changes are curved; convexity correction matters for Δy > 50 bps.
- Using Modified Duration for callable or puttable bonds — the optionality changes price behavior; use Effective Duration instead.
- Mismatching the yield convention — Macaulay Duration must be divided by (1 + y/m) where m matches the coupon frequency.

See also: [[black-scholes|Black-Scholes]] (for options sensitivity), [[compound-interest|Compound Interest]] (time-value foundations), [[dcf|DCF]].
