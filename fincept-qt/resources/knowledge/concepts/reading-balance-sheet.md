# Reading the Balance Sheet

The balance sheet is a **snapshot of what a company owns, what it owes, and what's left over for shareholders** at a specific point in time. While the income statement tells you about profitability over a period, the balance sheet tells you about financial position and durability at that moment.

The fundamental identity never changes:

```
Assets = Liabilities + Shareholders' Equity
```

Everything on the right side either belongs to creditors (liabilities) or to owners (equity). Everything on the left side is what the company deployed that capital into.

## Assets

Assets are divided by liquidity — how quickly they can be converted to cash.

**Current assets** (due or convertible within 12 months):

| Line item | What to look for |
|---|---|
| **Cash & equivalents** | Core liquidity; high is protective, very high may signal no investment opportunities |
| **Short-term investments** | Near-cash; Treasuries, money markets |
| **Accounts receivable (AR)** | Revenue billed but not yet collected; watch Days Sales Outstanding (DSO) |
| **Inventories** | Goods on hand; watch Days Inventory Outstanding (DIO); rising inventory is often a warning |
| **Prepaid expenses** | Prepayments to suppliers; usually small |

**Non-current (long-term) assets**:

| Line item | What to look for |
|---|---|
| **PP&E** (net) | Physical assets minus accumulated depreciation; capex-heavy businesses live here |
| **Intangibles & goodwill** | From acquisitions; goodwill impairments are a red flag that a deal overpaid |
| **Long-term investments** | Stakes in other companies, held-to-maturity securities |
| **Deferred tax assets** | Tax losses that can offset future income |

## Liabilities

**Current liabilities** (due within 12 months):

| Line item | What to look for |
|---|---|
| **Accounts payable (AP)** | Amounts owed to suppliers; rising AP can be a cash management tool or a stress sign |
| **Short-term debt** | Debt coming due soon; a refinancing need in a credit crunch is dangerous |
| **Accrued expenses** | Wages, interest, taxes owed but not yet paid |
| **Deferred revenue** | Cash received but service not yet delivered (a quality metric — future revenue already paid) |

**Non-current liabilities**:

| Line item | What to look for |
|---|---|
| **Long-term debt** | The dominant source of financial risk; watch maturities and covenants |
| **Lease obligations** | Post-2019, operating leases are on-balance-sheet; major item for retailers, airlines |
| **Pension obligations** | Underfunded pensions are a hidden liability; actuarial assumptions can obscure the true gap |
| **Deferred tax liabilities** | Taxes owed in future; often from accelerated depreciation |

## Shareholders' equity

```
Equity = Paid-in capital + Retained earnings − Treasury stock
```

- **Common stock + APIC** (Additional Paid-In Capital): what investors paid in when shares were issued
- **Retained earnings**: accumulated profits not paid as dividends; the scoreboard of historical profitability
- **Treasury stock**: shares repurchased — reduces equity (deducted)
- **Other comprehensive income (OCI)**: unrealized gains/losses on investments, FX translation, pension adjustments

Negative retained earnings (accumulated deficit) is common in early-stage companies but is a red flag in mature businesses. Deeply negative total equity (often from heavy buybacks or losses) means liabilities exceed assets — technically insolvent by book.

## Key ratios derived from the balance sheet

**Liquidity**:

| Ratio | Formula | What it means | Healthy range |
|---|---|---|---|
| **Current ratio** | Current assets / Current liabilities | Ability to cover near-term obligations | > 1.5× for most industries |
| **Quick ratio** | (Cash + AR) / Current liabilities | More conservative; strips out inventory | > 1.0× |
| **Cash ratio** | Cash / Current liabilities | Strictest liquidity test | > 0.2× |

**Leverage**:

| Ratio | Formula | What it means |
|---|---|---|
| **Debt-to-equity** | Total debt / Total equity | Capital structure leverage; > 2× is elevated for most industries |
| **Debt-to-assets** | Total debt / Total assets | What fraction of assets are debt-financed |
| **Net debt** | Debt − Cash | The "real" debt burden after deducting cash the company could use |
| **Net debt / EBITDA** | Net debt / EBITDA (from income statement) | How many years of operating earnings to repay debt; > 4× is stressed |

**Efficiency** (bridge to income statement):

| Ratio | Formula | What it means |
|---|---|---|
| **Days Sales Outstanding (DSO)** | AR / (Revenue / 365) | How long to collect receivables; rising DSO = customers stretching |
| **Days Inventory Outstanding (DIO)** | Inventory / (COGS / 365) | How long goods sit; rising DIO = demand weakness or over-ordering |
| **Days Payable Outstanding (DPO)** | AP / (COGS / 365) | How long the company takes to pay suppliers |
| **Cash Conversion Cycle** | DSO + DIO − DPO | The cash tied up in operations; lower is better |

**Return**:

| Ratio | Formula | Notes |
|---|---|---|
| **Return on Assets (ROA)** | Net income / Total assets | Capital efficiency regardless of leverage |
| **Return on Equity (ROE)** | Net income / Total equity | Shareholder return; inflated by high leverage |
| **Return on Invested Capital (ROIC)** | NOPAT / Invested capital | Best single return metric; measures true capital productivity |

## What to look for — balance sheet quality checklist

- **Cash is building**: the company generates more than it consumes
- **AR growing faster than revenue**: customers are slow to pay or revenue is being stuffed
- **Goodwill large relative to equity**: acquisitive company; impairment risk
- **Pension underfunded by > 10% of market cap**: material hidden liability
- **Short-term debt exceeds cash**: refinancing risk
- **Deferred revenue growing**: customers prepaying; favorable signal (SaaS, subscription businesses)

## How the three statements connect

The balance sheet **connects** to the income statement and cash flow statement. Net income from the income statement flows into retained earnings on the balance sheet. Changes in working capital accounts (AR, inventory, AP) explain why net income and operating cash flow diverge. See [[reading-financials]] for the full picture.

## In finterm

The Equity Research screen shows the balance sheet by quarter. Key derived ratios (net debt/EBITDA, current ratio) appear in the fundamentals summary. Watch how leverage changes over time — not just the current snapshot.
