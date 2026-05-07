**SOFR (Secured Overnight Financing Rate)** is the benchmark interest rate that replaced LIBOR for USD-denominated loans, derivatives, and financial contracts.

SOFR measures the cost of overnight borrowing collateralized by U.S. Treasury securities in the repo market. Unlike LIBOR, it is based on actual transactions — roughly $1 trillion in daily volume — rather than bank submissions, making it nearly impossible to manipulate.

```
SOFR = Volume-weighted median rate of overnight Treasury repo transactions
```

## Why it replaced LIBOR

LIBOR was phased out after the 2012 manipulation scandal revealed that banks were submitting false rates. SOFR was chosen by the Alternative Reference Rates Committee (ARRC) as the preferred USD replacement. The transition completed June 30, 2023.

Key differences:

| Feature | LIBOR | SOFR |
|---|---|---|
| Credit risk | Yes (bank credit) | Nearly zero (Treasury-backed) |
| Basis | Survey / submissions | Actual transactions |
| Term structure | 1w, 1m, 3m, 6m, 12m | Overnight (Term SOFR available) |
| Liquidity | Low post-2021 | Deep ($1T+/day) |

## Worked example — floating rate note

A corporate bond pays SOFR + 120 bps. If overnight SOFR is 4.30%, the coupon for that period is:

```
4.30% + 1.20% = 5.50% annualized
```

Most loans reference **Term SOFR** (1-month or 3-month), which is set at the start of the period rather than compounding overnight rates.

## Term SOFR vs overnight SOFR

- **Overnight SOFR** — published daily by the NY Fed; resets every business day.
- **Term SOFR (1m, 3m, 6m)** — forward-looking; set at period start; published by CME Group. Used in most new loans and CLOs.
- **SOFR averages (30/90/180-day)** — compounded backward-looking; used in some bonds.

## Rules of thumb

- SOFR typically trades 5–10 bps below the Fed Funds effective rate in calm markets.
- During repo market stress (like Sept 2019 with LIBOR's predecessor), overnight rates can spike 200–400 bps intraday.
- A LIBOR-to-SOFR basis swap spread was standardized at +26.161 bps (1-month) and +26.161 bps (3-month) for most legacy contract transitions.

## Pitfalls

- SOFR has no credit risk premium — spreads on bank loans built around SOFR need to embed credit risk separately, unlike old LIBOR loans.
- Term SOFR and compounded SOFR differ; mixing the two in contract documentation causes disputes.
- SOFR can spike at quarter-end when balance-sheet constraints reduce Treasury repo supply.

See also: [[federal-funds-rate|Federal Funds Rate]], [[yield-to-maturity|Yield to Maturity]], [[duration|Duration]], [[tips|TIPS]].
