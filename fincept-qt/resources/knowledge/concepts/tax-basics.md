# Tax Basics for Traders & Investors (US)

> *General educational guidance, not tax advice. Consult a CPA for your specific situation. Tax law changes annually; verify current rules before relying.*

After-tax return is what you actually keep. A 10% pre-tax return at 35% effective tax rate is 6.5% net. **Tax efficiency is one of the highest-leverage decisions in investing**, often worth more than picking the right strategy.

## The capital gains framework

| Holding period | Tax rate (US, 2024 brackets) |
|---|---|
| **Short-term** (≤1 year) | Ordinary income: 10–37% |
| **Long-term** (>1 year) | 0% / 15% / 20% by income bracket |
| **Net Investment Income Tax** | Additional 3.8% above $200k single / $250k joint |
| **State** | Add 0–13% depending on state (CA worst, FL/TX/WA none) |

The **365-day cliff** for short-term vs long-term is enormous — selling on day 364 vs 366 can change your tax by 15+ percentage points.

## Worked example — the holding-period decision

```
You bought AAPL 11 months ago at $100. Now $130 (+30% gain).

Sell now (short-term, 32% federal bracket):
  Tax: 30% × 32% = 9.6% of cost
  After-tax gain: 20.4%

Wait 1 month, sell at $130 (long-term, 15% federal bracket):
  Tax: 30% × 15% = 4.5% of cost
  After-tax gain: 25.5%

Difference: 5.1% of cost = $510 on a $10,000 position
For a 1-month wait — almost always worth it if thesis intact.
```

## Wash sale rule

If you sell at a loss and **buy substantially identical security within 30 days before or after**, the loss is disallowed. The disallowed loss is added to the cost basis of the new purchase.

```
Day 0:   Sell AAPL at $90 (cost $100, $10 loss)
Day 15:  Re-buy AAPL at $92
  ↑ Wash sale! $10 loss disallowed
  New cost basis: $92 + $10 = $102

Tax effect:
  No deduction now; the loss is deferred until you eventually sell the new shares.
```

**Watch out**:
- Spans calendar year-end — December selling for tax loss + January buyback
- Spousal accounts are aggregated
- IRAs trigger wash sales without ever recovering the loss (worst case)
- "Substantially identical" includes options and ETFs that closely track

## Section 1256 contracts (futures + index options)

Most regulated futures and broad-based index options get **60/40 treatment**:

```
60% of gain treated as long-term cap gain (lower rate)
40% treated as short-term (higher rate)
Regardless of holding period
```

This is **structurally better than equity short-term** and is one reason active traders prefer futures for index exposure. Section 1256 contracts also use **mark-to-market** at year-end (gains/losses realized for tax even if position open).

## Tax-advantaged accounts

| Account | What's special |
|---|---|
| **401(k) / Traditional IRA** | Pre-tax contributions; tax-deferred growth; ordinary income at withdrawal |
| **Roth IRA / Roth 401(k)** | After-tax contributions; tax-free growth and withdrawal |
| **HSA** | Triple-tax-advantaged for medical |
| **529** | State tax deduction; tax-free if used for education |
| **Brokerage (taxable)** | No tax shield; cap gains and dividend rules apply |

**Rule of thumb**: tax-inefficient assets (HY bonds, REITs, active funds) belong in tax-advantaged accounts. Tax-efficient (broad-market index ETFs, individual stocks held long-term) can go in taxable.

## Dividends

| Type | Tax rate |
|---|---|
| **Qualified dividends** (most US corps held >60 days) | LT cap gains rates (0/15/20%) |
| **Ordinary (non-qualified) dividends** | Ordinary income rates |
| **REIT dividends** | Mostly ordinary; some return-of-capital + 20% QBI deduction |
| **MLP distributions** | Mostly ROC; deferred until you sell; K-1 reporting headache |
| **Foreign dividends** | Foreign withholding (claim FTC); domestic tax also |

## Tax-loss harvesting

Sell losing positions to realize losses, offsetting gains and (up to $3k/year) ordinary income. **Re-buy a similar but not "substantially identical" replacement** to maintain market exposure.

```
Year-end positions:
  AAPL: +$10,000 LT gain
  TSLA: −$8,000 LT loss

Without harvest: $10k gain × 15% = $1,500 tax
With harvest:    $2k net gain × 15% = $300 tax
Tax savings:     $1,200

(Re-buy QQQ instead of TSLA to maintain similar exposure without wash sale)
```

## Special situations

- **Short selling** — losses on shorts treated as ordinary income; opaque rules; talk to CPA
- **Options exercised** — basis adjustments; complex
- **Crypto** — taxed as property; every trade is a taxable event
- **Foreign income** — FTC and PFIC rules; very complex for US persons abroad
- **Day trader status** — IRS has specific rules; allows mark-to-market election (Section 475)

## Form 8949 + Schedule D

Capital gains/losses report on these forms. Most brokerages provide a 1099-B that you transcribe (or import). Watch for:
- Wash sale adjustments
- Cost basis errors (especially after broker transfers, ESPP exercises)
- Box 1B "covered" vs "non-covered" lots

## Decision rules

- **Sell at <12 months only with strong reason** — the tax differential is large
- **Harvest losses every December** in taxable accounts (avoid wash sales)
- **Maximize tax-advantaged contributions first** before adding to taxable
- **Hold tax-inefficient strategies in tax-advantaged accounts** (active trading, HY bonds, REITs)
- **Section 1256 for index exposure** if active — lower effective tax rate
- **Document everything** — the IRS audits cost basis discrepancies

## In finterm

Portfolio tax-lot view shows holding period and unrealized gain/loss per lot. Use it to identify tax-loss harvesting candidates and to confirm long-term status before selling.
