# Yield to Maturity (YTM)

The **total annualized return** an investor would earn from holding a bond until maturity, accounting for:
1. All future coupon payments
2. The price paid today
3. The face value received at maturity
4. The implicit reinvestment of coupons at YTM

It's the bond market's equivalent of **internal rate of return (IRR)**.

```
Bond Price = Σ [ Coupon / (1+y)^t ]  +  [ Face / (1+y)^T ]

Solve iteratively for y = YTM
```

## Worked example — discount bond

```
10-year Treasury, 4% coupon (paid semi-annually = $20 per period)
Current price: $94.00 (per $100 face)

Solving:
  $94.00 = Σ [$20 / (1+y/2)^t]  +  [$1000 / (1+y/2)^20]
  YTM ≈ 4.78%

Components of return:
  Coupon income:     ~4.25%/yr (4% on $94 cost, lower than 4% on par)
  Pull to par:       ~0.53%/yr ($6 capital gain over 10 years)
  Total YTM:         ~4.78%
```

## YTM vs other yields

| Yield | Formula | When it matters |
|---|---|---|
| **Coupon rate** | $coupon ÷ face | Stays constant; useful for cash flow planning |
| **Current yield** | $coupon ÷ price | Income only; ignores capital gain/loss to maturity |
| **YTM** | IRR to maturity | Total return if held + coupons reinvested at YTM |
| **YTC (yield-to-call)** | YTM but to first call date | Important for callable bonds |
| **YTW (yield-to-worst)** | min(YTM, YTC) | Conservative number; common in HY |

## Reading YTM

- **Bond at par** ($100) → YTM ≈ coupon
- **Bond at premium** (>$100) → YTM < coupon (you overpaid; capital loss to par)
- **Bond at discount** (<$100) → YTM > coupon (capital gain to par)

## YTM and price relationship

```
Yields ↑  →  Bond prices ↓
Yields ↓  →  Bond prices ↑
```

A 100 bp rise in yields on a 10-year bond drops price ~8% (duration effect). On a 30-year, ~17%.

## Pitfalls

- **Reinvestment risk**: YTM assumes you reinvest each coupon at YTM. If rates drop, real return is lower.
- **Default risk** (corporates): YTM is "promised" — actual return is YTM − credit losses.
- **Callable bonds**: YTM is misleading; use YTC or YTW.
- **Floating-rate notes**: YTM is undefined; use spread to benchmark.

## Decision rules

- Compare bonds of **similar duration and credit quality** by YTM
- For **callable bonds**, always quote YTW
- Curve shape signals: **YTM(10y) − YTM(2y)** = 2s10s spread (recession indicator)
- **Real YTM** = YTM − expected inflation; the most meaningful number for buy-and-hold

## In finterm

The Gov Data screen plots Treasury YTM by maturity (the yield curve). The Economics screen tracks credit spreads (corporate YTM − Treasury YTM).
