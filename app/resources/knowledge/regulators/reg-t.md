# Regulation T — Federal Reserve Margin Requirements

**Regulation T** is a Federal Reserve rule (12 CFR Part 220) that governs the extension of credit by brokers and dealers. Its most consequential provision: when buying securities on margin, you must deposit at least **50% of the purchase price in cash**. The other 50% can be borrowed from your broker.

Reg T was enacted as part of the Securities Exchange Act of 1934, giving the Federal Reserve Board the authority to limit speculation in securities markets — a direct response to the leveraged buying that amplified the 1929 crash.

## Who it covers

Regulation T applies to:

- All U.S. broker-dealers extending credit to customers
- Equity securities (stocks, ETFs)
- Corporate and convertible bonds
- Options (special rules apply — see below)

It does **not** directly apply to:
- Futures (covered by exchange performance bond rules)
- Government securities and agency bonds (subject to separate Reg T provisions with different requirements)
- Certain exempted securities

## Initial margin vs. maintenance margin

Reg T sets the **initial margin** — the minimum equity required when first entering a position. FINRA and individual brokers then set **maintenance margin** — the ongoing minimum equity to hold an existing position.

| Margin Type | Rate | Set By |
|-------------|------|--------|
| Initial (equity purchase) | 50% | Federal Reserve (Reg T) |
| Maintenance (ongoing) | Typically 25–30% | FINRA + broker |
| Day trading (PDT accounts) | 25% of full position | FINRA |

**Example:** You want to buy $10,000 of AAPL stock.
- Reg T initial margin: $5,000 cash required, $5,000 borrowed from broker
- If AAPL drops: once your equity falls below 25–30% of the position value, a maintenance margin call is triggered

## Short selling margin

Short selling has different Reg T requirements. Initially, you must deposit 150% of the short sale value (the proceeds remain in the account, plus an additional 50% margin). Effectively, shorting requires 50% margin above the proceeds.

## Options

Options are not purchased on margin in the same sense — you generally must pay the full premium for long options. However:

- **Covered calls** written against long stock: the stock serves as collateral; no additional margin required
- **Naked puts and calls**: substantial margin requirements (the OCC sets portfolio margin requirements for sophisticated traders)
- **Portfolio margin** (available to accounts with $100K+ equity): uses risk-based margin rather than Reg T's fixed percentages, typically allowing 6:1 leverage for hedged portfolios

## Margin calls

When a Reg T call is issued (buying more than Reg T allows without sufficient cash), the customer has **5 business days** to deposit the required funds. Failure to meet the call results in the broker liquidating enough securities to bring the account into compliance.

A **maintenance margin call** is separate and generally requires same-day or next-day resolution, with the broker having the right to liquidate without notice in many account agreements.

## Why traders care

1. **Leverage limit** — 50% initial margin = maximum 2:1 leverage on equity positions in a margin account.
2. **Margin interest** — borrowed funds accrue interest (typically SOFR + spread); for positions held longer than a day, this is a real cost.
3. **Forced liquidation** — brokers liquidate positions to meet margin calls, often at the worst possible time. The broker chooses which positions to sell.
4. **Portfolio margin** — sophisticated traders with large hedged books can access portfolio margin, reducing capital requirements substantially.
5. **Cross-margining** — professional accounts can cross-margin futures and options positions, with equity in one offsetting margin requirements in the other.

## Common violations and gotchas

- **"Freeriding"** — in a cash account, buying a security and selling it before paying for the purchase is prohibited. The account is frozen for 90 days.
- **Margin call cascade** — using maximum Reg T leverage means a 20% adverse move triggers a maintenance margin call; a 50% adverse move wipes out all equity.
- **Overnight vs. day trade margin** — day trading margin (via FINRA's PDT rule) allows 4× leverage intraday, but if a position is held overnight, it reverts to Reg T (50% initial).
- **Interest accrual** — many traders forget margin interest compounds daily; a long-held leveraged position bleeds steadily even without price movement.
