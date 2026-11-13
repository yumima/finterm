**Par value** (also called face value or principal) is the nominal value of a bond as stated at issuance — the amount the issuer promises to repay at maturity and the basis on which coupon interest is calculated.

For bonds, par is almost always $1,000 per bond (or $100 per bond in the UK). For stocks, par value is a legal artifact (often $0.01 or $0.0001) with no meaningful economic significance.

## Bond par value mechanics

```
Par value:      $1,000
Coupon rate:    5%
Annual coupon:  $1,000 × 5% = $50
Maturity:       10 years

Cash flows to investor:
  Year 1–10: $50/year (or $25 semiannually)
  Year 10:   $1,000 (par repayment)
```

## Premium, par, and discount bonds

A bond's market price is quoted as a percentage of par:

| Price Quote | Dollar Price | Price vs. Par |
|---|---|---|
| 105.00 | $1,050 | Premium |
| 100.00 | $1,000 | At par |
| 97.50 | $975 | Discount |
| 85.00 | $850 | Deep discount |

Bonds trade above par when their coupon rate exceeds current market rates; they trade below par when the coupon is below market rates.

## Why par value matters

1. **Coupon calculation**: $1,000 par × 4.5% = $45/year — always a fixed dollar amount.
2. **Redemption amount**: At maturity, you receive $1,000 regardless of what you paid.
3. **Capital gain/loss**: If you buy at $900, you receive $1,000 at maturity = $100 capital gain.
4. **YTM calculation**: The gap between purchase price and par is part of total return.

## Worked example — premium vs. discount bond

```
Scenario: Interest rates rise from 4% to 6% after issuance

Bond: 4% coupon, $1,000 par, 5 years remaining
Required yield: 6%

New price = PV of coupons + PV of par
  = $40 × PVIFA(6%,5) + $1,000 × PVIF(6%,5)
  = $40 × 4.212 + $1,000 × 0.747
  = $168.48 + $747.00
  = $915.48

Trading at $915 vs $1,000 par = 8.5% discount
```

## Par value in distressed debt

In distressed investing, bonds may trade at 30–60 cents on the dollar (30–60% of par). Recovery analysis focuses on what bondholders will receive in restructuring versus par — "recovery to par" is the potential upside.

## Pitfalls

- Par value is not "fair value" or "intrinsic value" for a bond — bonds regularly trade above and below par.
- For callable bonds, the call price is typically above par (e.g., $1,050 "call premium") to compensate investors for early redemption.
- Floating rate notes often stay close to par because coupon adjusts to market rates — duration is low.
- Inflation erodes the real value of par repayment — a 10-year bond returning $1,000 at maturity is worth less in purchasing power than $1,000 today.

See also: [[coupon-rate|Coupon Rate]], [[yield-to-maturity|Yield to Maturity]], [[duration|Duration]], [[yield-spread|Yield Spread]], [[treasury-securities|Treasury Securities]].
