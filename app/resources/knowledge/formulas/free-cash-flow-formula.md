**Free Cash Flow (FCF)** measures the cash a business generates after funding its operating needs and capital investments — it is the cash available to pay dividends, repurchase shares, reduce debt, or make acquisitions.

```
FCF = Operating Cash Flow (CFO) − Capital Expenditures (CapEx)

Alternatively, from net income:
FCF = Net Income + D&A − ΔWorking Capital − CapEx

D&A           = Depreciation & Amortization (non-cash add-back)
ΔWorking Capital = Change in (Current Assets − Current Liabilities)
                   Positive change = cash use; Negative change = cash source
CapEx         = Purchases of property, plant, and equipment
```

Two useful variants:
```
FCFF (Free Cash Flow to Firm):
FCFF = EBIT × (1 − Tax Rate) + D&A − ΔWorking Capital − CapEx

FCFE (Free Cash Flow to Equity):
FCFE = Net Income + D&A − ΔWorking Capital − CapEx + Net Borrowing
```

## Worked example — Apple, FY2025 (approximate)

```
Operating Cash Flow            $118.0 bn
Capital Expenditures           $ 10.7 bn   (PP&E purchases, net)

FCF = 118.0 − 10.7 = $107.3 bn

Cross-check from net income:
Net Income                     $ 93.7 bn
+ D&A                          $ 12.5 bn
− ΔWorking Capital             +$ 3.2 bn   (WC increased, cash use)
− CapEx                        $ 10.7 bn

FCF = 93.7 + 12.5 − 3.2 − 10.7 = $92.3 bn  (minor differences due to rounding)
```

Apple's FCF yield: FCF / Market Cap = 107.3 / 2,700 ≈ **4.0 %** — what a dollar invested in Apple generates in free cash annually.

## What the result means

FCF is the "quality check" on earnings. A company with strong EPS but weak FCF is likely experiencing one or more of: aggressive revenue recognition, working capital deterioration, or high capex intensity. Companies with FCF > Net Income are typically high-quality businesses.

**FCF Yield** = FCF / Market Cap — the equity market's equivalent of a bond yield:
- Above 5 %: potentially undervalued or mature cash cow.
- 3–5 %: fairly priced for low-growth, stable businesses.
- Below 2 %: growth is being priced in; needs high CAGR to justify.

## Variants

- **Owner Earnings** (Buffett) — net income + D&A − maintenance capex only (not growth capex); approximates distributable cash.
- **Unlevered FCF (FCFF)** — removes financing effects; used in [[dcf|DCF]] with [[wacc|WACC]] as the discount rate.
- **FCFE** — levered free cash flow available specifically to equity holders; discounted at cost of equity.
- **Cash Conversion Ratio** = FCF / EBITDA; ratios near or above 1.0 indicate high-quality earnings with minimal cash traps.

## Common mistakes

- Using reported CapEx without distinguishing maintenance from growth capex — maintenance capex sustains the business (a cost); growth capex creates future returns (an investment); lumping them together understates true free cash generation.
- Ignoring lease payments under IFRS 16 / ASC 842 — lease payments reduce operating cash flow; comparisons across pre- and post-adoption periods are distorted.
- Treating a short period of negative FCF as terminal — capex-heavy growth phases (cloud build-out, semiconductor fab) temporarily depress FCF; normalize over a cycle.
- Comparing FCF without adjusting for working capital seasonality — retailers build inventory before the holiday quarter; Q3 FCF looks artificially depressed.

See also: [[dcf|DCF]], [[wacc|WACC]], [[ev-ebitda-formula|EV/EBITDA]], [[roe-formula|ROE]].
