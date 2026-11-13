**CAGR** (Compound Annual Growth Rate) expresses the smoothed annual return rate that would grow a starting value to an ending value over a specified number of years — it is the standard way to compare investments with different holding periods.

```
CAGR = (End Value / Start Value)^(1/n) − 1

End Value   = final value of the investment
Start Value = initial value of the investment
n           = number of years in the holding period
```

Equivalently: `CAGR = (FV/PV)^(1/n) − 1`

## Worked example — two investments compared

```
Investment A: $10,000 → $18,500 over 7 years
Investment B: $10,000 → $22,000 over 10 years

CAGR_A = (18,500 / 10,000)^(1/7) − 1
        = (1.85)^(0.1429) − 1
        = 1.0922 − 1
        = 9.22 %

CAGR_B = (22,000 / 10,000)^(1/10) − 1
        = (2.20)^(0.10) − 1
        = 1.0820 − 1
        = 8.20 %

Investment A has a higher CAGR despite lower absolute gain — it
achieved more return per unit of time.
```

## What the result means

CAGR is a **geometric mean return** — it accounts for compounding, unlike arithmetic mean return which simply averages annual percentage changes. This distinction matters enormously over multi-year periods:

```
Year 1: +50 %,  Year 2: −33 %
Arithmetic mean = (50 − 33) / 2 = +8.5 %  (misleading)
Geometric mean (CAGR) = (1.50 × 0.67)^(1/2) − 1 = (1.005)^0.5 − 1 ≈ 0.25 %

Actual wealth after 2 years: $1.00 × 1.50 × 0.67 = $1.005
→ barely broke even despite an "average" of +8.5 % per year
```

Benchmarks for CAGR context (US, long-run):

| Asset class         | Approximate CAGR (nominal) |
|---|---|
| US large-cap equity | 9–11 % |
| US small-cap        | 11–13 % |
| Bonds (agg)         | 4–6 % |
| Real estate (REIT)  | 8–10 % |
| Cash/T-bills        | 3–4 % |

## Variants

- **Revenue CAGR / Earnings CAGR** — applied to company financials rather than investment returns; fundamental analysts use 3- and 5-year EPS CAGR as growth inputs to [[peg-formula|PEG]].
- **Inflation-adjusted (real) CAGR** — subtract CPI to get purchasing-power growth.
  ```
  Real CAGR ≈ Nominal CAGR − Inflation rate
  ```
- **CAGR vs IRR** — for investments with interim cash flows (dividends, reinvestment), IRR is more precise than CAGR; CAGR implicitly assumes all returns are realized at the end.

## Common mistakes

- Using CAGR to describe a volatile investment as if the smooth line represents reality — a fund with 9 % CAGR could have had years of −40 % followed by massive recoveries; CAGR hides the journey.
- Comparing CAGRs over different periods without adjusting for starting conditions — a 10-year CAGR starting in 2009 (market bottom) and a 10-year CAGR starting in 2000 (peak) are not comparable.
- Annualizing returns from less than 1 year of data — CAGR technically requires n ≥ 1; annualizing a 3-month return to a CAGR introduces extreme noise.
- Ignoring reinvestment assumption — CAGR assumes all intermediate cash flows are reinvested at the same rate.

See also: [[compound-interest|Compound Interest]], [[peg-formula|PEG]], [[dcf|DCF]], [[roe-formula|ROE]].
