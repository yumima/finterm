**The P/E formula** quantifies how many dollars investors pay for one dollar of a company's earnings — it is the single most referenced valuation multiple in equity markets.

```
P/E = Share Price / Earnings Per Share (EPS)

Trailing P/E:  use TTM (trailing twelve months) EPS
Forward P/E:   use next-twelve-months consensus EPS estimate
```

EPS is calculated as:
```
EPS = (Net Income − Preferred Dividends) / Diluted Shares Outstanding
```

## Worked example — Apple, May 2026

```
Share price (market)      $175.00
TTM diluted EPS           $6.30    (reported, GAAP)
FY+1 EPS consensus est.   $7.10    (analyst mean estimate)

Trailing P/E  = 175.00 / 6.30  = 27.8×
Forward P/E   = 175.00 / 7.10  = 24.6×
```

An investor today pays **$27.80 for each $1 of Apple's past-year profit**. Against the long-run S&P 500 average of ~16×, this premium reflects Apple's brand, margins, and capital return.

Reverse the formula to find the implied share price at a given multiple:
```
Target price = target P/E × forward EPS
             = 26 × $7.10 = $184.60
```

## What the result means

P/E has no universal "right" value — context is everything. Compare against:

1. **Own history** — is the stock expensive vs its 5-year or 10-year average P/E?
2. **Peers** — is it at a premium or discount to comparable companies?
3. **Market** — is it above or below the index P/E?
4. **Growth-adjusted** — use [[peg-formula|PEG]] to check whether the multiple is justified by growth.

## Variants

- **Trailing P/E** — historical, concrete; not affected by analyst optimism.
- **Forward P/E** — forward-looking; reflects expectations; subject to estimate revisions.
- **Adjusted P/E** — uses non-GAAP (adjusted) EPS; useful but requires scrutiny of what was excluded.
- **CAPE (Shiller P/E)** — uses 10-year inflation-adjusted earnings; removes cyclicality; best for macro valuation.
- **[[peg-formula|PEG ratio]]** — P/E divided by expected growth rate; penalizes expensive multiples without growth.

## Common mistakes

- Comparing P/E across sectors without adjustment — a 12× bank and a 40× SaaS company may both be fairly priced given their economics.
- Using trailing P/E at cyclical troughs — EPS is depressed, making the multiple look artificially high ("expensive") when the business is actually cyclically cheap.
- Ignoring leverage — two companies with the same P/E can have very different risk profiles if one is highly levered; [[ev-ebitda-formula|EV/EBITDA]] controls for capital structure.
- Trusting adjusted EPS without checking what management excluded — stock-based compensation, restructuring charges, and M&A costs are often excluded but are real economic costs.

See also: [[ev-ebitda-formula|EV/EBITDA]], [[peg-formula|PEG]], [[roe-formula|ROE]], [[free-cash-flow-formula|FCF]].
