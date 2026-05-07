**Compound interest** is the process by which a principal amount earns returns not only on itself but also on previously accumulated interest — it is the fundamental time-value relationship underlying all of finance.

```
FV = PV × (1 + r)^n

FV  = future value
PV  = present value (initial principal)
r   = periodic interest rate (as a decimal)
n   = number of compounding periods

Solving for PV (discounting):
PV = FV / (1 + r)^n

Solving for r (implied return):
r = (FV / PV)^(1/n) − 1
```

For continuous compounding:
```
FV = PV × e^(rn)
PV = FV × e^(−rn)
```

## Worked example — long-term investment in SPY

```
Initial investment (PV)    $10,000
Annual return (r)          10 %   (S&P 500 long-run nominal avg)
Holding period (n)         30 years

FV = 10,000 × (1 + 0.10)^30
   = 10,000 × 17.449
   = $174,494

After 10 years:  10,000 × (1.10)^10  = $25,937
After 20 years:  10,000 × (1.10)^20  = $67,275
After 30 years:  10,000 × (1.10)^30  = $174,494
```

Simple interest over 30 years at 10 % would yield only $40,000 ($10k principal + $30k interest). Compounding adds an extra **$134,494** — the difference is the power of reinvested returns.

## What the result means

The Rule of 72: divide 72 by the annual rate to estimate the doubling time.
```
At 6 %:  72 / 6  = 12 years to double
At 10 %: 72 / 10 = 7.2 years to double
At 12 %: 72 / 12 = 6 years to double
```

Compounding frequency also matters for a given nominal rate:

| Compounding    | Effective annual rate (at 10 % nominal) |
|---|---|
| Annual         | 10.000 % |
| Semiannual     | 10.250 % |
| Monthly        | 10.471 % |
| Daily          | 10.516 % |
| Continuous     | 10.517 % |

## Variants

- **[[cagr|CAGR]]** — the compound annual growth rate is the constant annual return that produces the same FV/PV ratio over n years; compound interest applied to observed investment returns.
- **Annuity formula** — extends compounding to regular periodic payments (mortgages, pensions).
  ```
  FV_annuity = PMT × [(1+r)^n − 1] / r
  ```
- **Continuous compounding** — used in Black-Scholes, bond pricing, and instantaneous rate calculations; the limiting case as compounding frequency → infinity.

## Common mistakes

- Confusing nominal rate with the periodic rate — a 12 % annual rate compounded monthly has r = 1 % per month, n = 12 months per year.
- Confusing real and nominal returns — compound a nominal return at a nominal rate; compound real wealth at the real return (nominal − inflation).
- Treating compound interest as additive — many investors mentally add returns year by year, systematically underestimating ending wealth for long horizons.
- Ignoring tax drag on compounding — taxes paid on dividends and realized gains each year reduce the effective compounding rate and dramatically lower terminal wealth.

See also: [[cagr|CAGR]], [[dcf|DCF]], [[gordon-growth|Gordon Growth Model]], [[compound-interest|this formula in DCF context]].
