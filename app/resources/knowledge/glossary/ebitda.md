# EBITDA

**Earnings Before Interest, Taxes, Depreciation, and Amortization.**

A measure of operating profitability that strips out:
- **Interest** (a financing decision, not operating)
- **Taxes** (vary by jurisdiction and structure)
- **Depreciation & Amortization** (non-cash accounting allocations)

The point: get a comparable view of how much cash the *core operations* throw off, regardless of how the business is financed or where it's incorporated.

## Worked example — bridge from net income

```
Net income:          $50M
+ Interest expense:  $12M
+ Taxes:             $14M
+ Depreciation:      $18M
+ Amortization:       $6M
= EBITDA:           $100M
EBITDA margin (vs $400M revenue): 25%
```

If maintenance CapEx is $40M and working capital ate $5M, the **real cash flow is closer to $55M** — about half of EBITDA. That's why "EBITDA = cash flow" is a dangerous shortcut.

## Why investors love it

- Easy to compute from any income statement
- Makes companies of different leverage and tax situations comparable
- Approximates operating cash before reinvestment
- Standard input for [[ev-ebitda|EV/EBITDA multiples]]

## Why "EBITDA is bullshit" (as Munger put it)

Stripping out depreciation pretends physical assets don't wear out. But they *do* — and replacing them costs real cash (CapEx). A trucking company with $100M of EBITDA but $90M of annual CapEx is generating $10M of [[fcf|FCF]], not $100M. Using EBITDA to value it as if it generated $100M is how money gets lost.

## EBITDA margins by industry (rough)

| Industry | Typical EBITDA margin |
|---|---|
| Software / SaaS (mature) | 25–45% |
| Consumer brands | 18–30% |
| Healthcare services | 12–22% |
| Retail | 5–12% |
| Telecom | 30–45% (but huge CapEx) |
| Airlines | 12–20% (cyclical) |
| Big banks | N/A — don't use EBITDA |

## Variants worth knowing

- **EBITDAR** — adds back Rent (used in retail / restaurants where leased real estate dominates)
- **EBITDAX** — adds back Exploration costs (oil & gas)
- **Adjusted EBITDA** — management-defined, often strips stock-based comp; read the footnotes
- **EBITDA − maintenance CapEx** — closer to true cash generation; sometimes called "EBITDA minus M-CapEx"

## Decision rules

- **Capital-intensive business** → don't trust EBITDA alone; subtract maintenance CapEx
- **Adjusted EBITDA gap > 15% from GAAP** → understand every line item being adjusted
- **EBITDA growing faster than FCF over multiple years** → CapEx is climbing or working capital is bleeding; investigate
- **Net debt / EBITDA > 4× for non-utility business** → leverage warning

## Use it carefully

- **OK**: Comparing operating margins across peers in the same industry.
- **OK**: Quick screen for "businesses that produce cash before investment."
- **Dangerous**: Valuing a capital-intensive business on EBITDA alone.
- **Dangerous**: Trusting "Adjusted EBITDA" without reading what management adjusted out — sometimes everything bad gets called "non-recurring."
