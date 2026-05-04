# P/E Ratio

The **price-to-earnings ratio** answers a single question: how many dollars are investors paying for one dollar of this company's profit?

```
P/E = Share Price / Earnings Per Share
```

A stock at $50 with $2 of EPS over the last year has a trailing P/E of 25 — investors pay $25 today for $1 of last year's earnings.

## Worked example — Apple, May 2026

```
Share price:        $175.00
TTM EPS (diluted):  $6.30
Trailing P/E:       175.00 / 6.30  ≈  27.8×
Forward P/E:        175.00 / 7.10  ≈  24.6×    (analyst FY26 EPS estimate)
```

How to read it: an investor today pays **$27.80** for every $1 of Apple's *past-year* profit, or **$24.60** for every $1 of *next-year* expected profit. Versus the S&P 500 long-run average of ~16, Apple is priced at a premium — justifiable if growth and quality are above average.

## How to read it

- **Trailing P/E** uses the last 12 months of EPS. Concrete but backward-looking — penalizes one-time charges and rewards windfalls.
- **Forward P/E** uses analyst estimates. Useful if the estimate is right; misleading at cyclical inflection points where analyst lag is worst.
- **A "high" P/E means nothing in isolation.** A 40× growth software stock may be cheap if it grows 30%/year for the next decade. A 40× no-growth utility is almost certainly expensive.

## Sector-typical ranges

| Sector | Typical trailing P/E | Why |
|---|---|---|
| Utilities | 15–22× | Stable cash flows, dividend support |
| Consumer staples | 18–25× | Defensive earnings, slow growth |
| Industrials | 15–22× | Cyclical — depends on cycle position |
| Financials (banks) | 8–14× | Regulated returns, leverage caps multiples |
| Mature tech | 20–30× | Growth + buybacks |
| High-growth software / SaaS | 30–60×+ | Future profits dominate; trailing often N/M |
| Energy | 8–15× | Commodity-cycle dependent |
| Biotech (with no earnings) | N/M | Use revenue or cash burn metrics instead |

## Historical context

| Era | S&P 500 P/E | Notes |
|---|---|---|
| 1970s post-Nixon | ~7× | Stagflation, sub-historical multiple |
| 1999 dot-com peak | ~33× (Shiller CAPE > 44) | Bubble |
| 2008 trough | ~10× | GFC capitulation |
| 2021 post-COVID | ~28× | Free money + meme rally |
| Long-run average | ~16× | Use as a rough anchor |

Multiples mean-revert over decades, not quarters. Today's level matters less than direction relative to expectations.

## Variants worth knowing

- **Shiller P/E (CAPE)** — uses 10-year average inflation-adjusted earnings; smooths cyclicality
- **PEG** — P/E divided by expected growth rate; tries to "price growth in"
- **Forward P/E** vs **trailing P/E** — already discussed
- **Adjusted P/E** — non-GAAP earnings; always check what management adjusted out
- **Industry-specific cousins** — P/AFFO for REITs, P/TBV for banks

## Decision rules

- **P/E > 5y own history + 2σ** → expensive vs its own history; need a thesis why this time is different
- **P/E in line with peers and growth justifies multiple** → fairly priced
- **P/E < 5y own history − 2σ + business intact** → potential value; verify it's not a value trap
- **Negative or N/M** → fall back to EV/EBITDA, P/S, or FCF yield

## What it doesn't tell you

P/E ignores debt, cash on the balance sheet, and earnings quality. A company can boost EPS by buying back shares without growing the business. Pair P/E with [[ev-ebitda|EV/EBITDA]], [[fcf|free cash flow]], and revenue growth.

## Common gotchas

- Negative earnings → P/E is meaningless (often shown as "N/M").
- One-time charges can collapse EPS, making a healthy business look "expensive" on trailing P/E.
- Forward P/E assumes analysts are right — they often aren't at turning points.
- Buybacks shrink share count and inflate EPS without business improvement.
- Companies in transition (post-spinoff, post-restructuring) often have P/Es that are arithmetically meaningless.
