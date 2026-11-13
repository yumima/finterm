The **price-to-earnings ratio** answers one question: how many dollars are investors paying for one dollar of this company's profit?

A stock at `$50` with `$2` of EPS over the last year has a trailing P/E of `25` — investors pay `$25` today for `$1` of last year's earnings. That's the whole formula:

```
P/E = Share Price / Earnings Per Share
```

The number on its own means nothing. P/E is only useful in three comparisons: vs the company's own history, vs its peers, and vs the broad market.

## Trailing vs forward

Both are P/Es; the difference is which earnings number sits in the denominator.

- **Trailing P/E** — uses the last 12 months of EPS. Concrete, but backward-looking; punishes one-time charges and rewards windfalls.
- **Forward P/E** — uses analyst estimates for the next year. Useful when estimates are reasonable; misleading at cyclical turning points where analysts lag.
- **Adjusted P/E** — non-GAAP earnings. Always check what management adjusted out.

Forward is the default Wall Street uses; trailing is what shows up on free quote pages.

## Worked example — Apple, May 2026

```
Share price        $175.00
TTM EPS (diluted)  $6.30
Trailing P/E       175.00 / 6.30  ≈  27.8×
Forward P/E        175.00 / 7.10  ≈  24.6×    (FY26 EPS estimate)
```

An investor today pays `$27.80` for every `$1` of Apple's *past-year* profit, or `$24.60` for every `$1` of *next-year* expected profit. Versus the long-run S&P 500 average near `16×`, Apple trades at a premium — defensible only if growth and quality are above average.

## Sector-typical ranges

Multiples cluster by industry economics. A 40× software stock and a 12× bank are both "normal" inside their own neighborhoods.

| Sector | Typical trailing P/E | Why |
|---|---|---|
| Banks | 8–14× | Regulated returns, leverage caps multiples |
| Energy | 8–15× | Commodity-cycle dependent |
| Utilities | 15–22× | Stable cash flows, dividend support |
| Industrials | 15–22× | Cyclical — depends on cycle position |
| Consumer staples | 18–25× | Defensive earnings, slow growth |
| Mature tech | 20–30× | Growth + buybacks |
| High-growth SaaS | 30–60×+ | Future profits dominate; trailing often N/M |
| Biotech (pre-earnings) | N/M | Use revenue or cash burn instead |

## Decision rules

- **Above 5y own history + 2σ** — expensive vs itself; need a thesis why this time is different.
- **In line with peers, growth justifies the multiple** — fairly priced.
- **Below 5y own history − 2σ, business intact** — potential value; verify it's not a value trap.
- **Negative or `N/M`** — fall back to `EV/EBITDA`, `P/S`, or FCF yield.

## Common gotchas

- Negative earnings make P/E meaningless — usually displayed as `N/M`.
- One-time charges crater EPS, which can make a healthy business look "expensive" on trailing P/E.
- Forward P/E assumes analysts are right — they often aren't at turning points.
- Share buybacks shrink the denominator and inflate EPS without growing the business.
- Post-spinoff and post-restructuring entities often have arithmetically meaningless P/Es for a year.

## Variants worth knowing

- **`Shiller P/E (CAPE)`** — uses 10-year average inflation-adjusted earnings; smooths cyclicality.
- **`PEG`** — P/E divided by expected growth rate; tries to "price growth in".
- **`P/AFFO`** — REIT cousin of P/E; AFFO replaces accounting earnings.
- **`P/TBV`** — bank cousin; tangible book replaces earnings.

P/E ignores debt, cash, and earnings quality. Pair it with [[ev-ebitda|EV/EBITDA]], [[fcf|free cash flow]], and revenue growth before drawing conclusions.
