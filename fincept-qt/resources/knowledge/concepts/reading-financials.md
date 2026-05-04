# Reading Company Financial Statements

Public companies file three core statements quarterly (10-Q) and annually (10-K). Each tells you something the others can't.

## 1. Income Statement — "Did the business make money this period?"

```
Revenue
−  COGS
=  Gross Profit
−  Operating Expenses (R&D, SG&A)
=  Operating Income (EBIT)
−  Interest, Taxes
=  Net Income     →     EPS
```

What to look for:
- **Revenue trend** — is the top line growing? Year-over-year is more revealing than quarter-over-quarter (seasonality).
- **Gross margin** — pricing power and unit economics
- **Operating margin** — efficiency at scale
- **Below-the-line surprises** — one-time gains/losses, tax benefits

## 2. Balance Sheet — "What does the business own and owe right now?"

```
Assets    =    Liabilities    +    Equity
```

A snapshot at a point in time. Key questions:
- **Cash and equivalents** — runway
- **Debt vs. equity** — leverage
- **Working capital** (current assets − current liabilities) — short-term liquidity
- **Goodwill** — accumulated overpayment for acquisitions; can be written down
- **Share count change** — buybacks shrink, dilution grows

## 3. Cash Flow Statement — "Did real cash actually arrive?"

```
Operating Cash Flow      ← from running the business
+ Investing Cash Flow    ← buying/selling assets
+ Financing Cash Flow    ← raising/returning capital
= Change in Cash
```

The most important number for most analyses: **Operating Cash Flow − CapEx = [[fcf|Free Cash Flow]]**. This tells you what's actually available to shareholders after the business reinvests in itself.

## Worked example — bridging the three statements

```
Income Statement (Q1):
  Revenue:               $100M
  Net income:             $10M

Balance Sheet movement:
  Receivables (start):    $30M
  Receivables (end):      $42M    → +$12M (customers haven't paid yet)
  Inventory (start):      $20M
  Inventory (end):        $25M    → +$5M (built inventory)

Cash Flow Statement:
  Net income:             $10M
  + D&A (non-cash):        $4M
  − Change in WC:        −$17M    (+$12M AR, +$5M inventory)
  = Operating cash flow:  −$3M    ← BURNED CASH despite "profit"!
```

If this happens for several quarters: red flag. Revenue is being booked but cash isn't arriving — possibly aggressive credit terms or channel-stuffing.

## Common red flags by statement

### Income statement
- **Revenue growing, gross margin shrinking** → losing pricing power
- **"One-time" charges that recur** → not really one-time
- **Adjusted earnings >> GAAP** consistently → aggressive add-backs
- **Tax rate jumping around** → may be massaging EPS

### Balance sheet
- **Receivables growing faster than revenue** → channel-stuffing or credit-term aggression
- **Inventory rising while sales flat** → demand softening, possible writedown coming
- **Goodwill > tangible book** → impairment risk on next downturn
- **Net debt growing while EBITDA flat** → leverage rising silently
- **Sudden auditor change** → investigate why

### Cash flow statement
- **Op cash flow << net income** for multiple quarters → quality concern
- **CapEx persistently below depreciation** → asset base shrinking; sustainability concern
- **Most cash "generated" comes from issuing debt** → not real
- **Stock buybacks > free cash flow** → funded by debt; check sustainability

## Quick ratios to compute

```
Current ratio:    Current Assets / Current Liabilities         (>1.5 healthy)
Quick ratio:      (Current Assets − Inventory) / Curr Liab     (>1.0 healthy)
Debt/EBITDA:      Total Debt / EBITDA                          (<3× for non-utility)
Interest coverage: EBIT / Interest Expense                     (>5× safe; <2× distressed)
Cash conversion:  Operating Cash Flow / Net Income             (>0.9 high quality)
Days sales outstanding: AR × 365 / Revenue                    (sector-dependent)
```

## How they tie together

A profitable income statement (positive EPS) can hide a cash-burning business (negative FCF), if revenue is growing through aggressive payment terms or inventory build. Always cross-check: net income vs. operating cash flow. Big persistent gaps are a yellow flag.

Conversely, GAAP losses can mask strong businesses with heavy non-cash depreciation. Cash flow tells the truer story.

## Where to read them

- **SEC EDGAR** — primary source, free. Look for 10-K (annual) and 10-Q (quarterly).
- **Investor Relations site** — usually has nicer-formatted versions plus earnings call transcripts.
- **In finterm** — Equity Research → Financials tab.

## Practical advice

- Read the **Management Discussion & Analysis (MD&A)** section of the 10-K — it's where management explains the numbers in plain English (and where they often reveal forward concerns).
- Read the **footnotes**. Bad news lives in footnotes.
- Compare 3–5 years of trends, not single quarters.
- For competitor comparison, normalize to common-size statements (% of revenue) — then differences in cost structure jump out.
- The earnings call transcript answers questions the filings don't.

## Famous accounting blowups

| Company | Year | What was hidden |
|---|---|---|
| Enron | 2001 | Off-balance-sheet SPVs; revenue inflation |
| WorldCom | 2002 | $11B operating expenses capitalized as assets |
| Lehman Brothers | 2008 | "Repo 105" deals to manipulate quarter-end leverage |
| Wirecard | 2020 | Fictitious cash balances |
| Luckin Coffee | 2020 | Fabricated sales |

Pattern: when revenue, AR, and inventory growth diverge from cash flow, look harder.
