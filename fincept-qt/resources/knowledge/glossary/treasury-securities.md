# Treasury Securities

US government debt instruments — the bedrock of the global financial system. Considered "risk-free" for default purposes (the US can print dollars to pay them), but **not** risk-free for inflation, reinvestment, or duration.

## The four flavors

| Type | Maturity | Coupon | How it pays |
|---|---|---|---|
| **T-bill** | 4w / 8w / 13w / 26w / 52w | None | Sold at discount; difference = return |
| **T-note** | 2y / 3y / 5y / 7y / 10y | Yes | Semi-annual coupons + face at maturity |
| **T-bond** | 20y / 30y | Yes | Semi-annual coupons + face at maturity |
| **TIPS** | 5y / 10y / 30y | Yes (real) | Principal indexed to CPI; coupon × adjusted principal |

## Worked example — T-bill yield

```
13-week T-bill:
  Face value: $10,000
  Auction price: $9,872
  Discount: $128
  Days to maturity: 91

Discount yield (banker's rate):
  ($128 / $10,000) × (360 / 91)  ≈  5.06%

Bond-equivalent yield (more comparable to coupon bonds):
  ($128 / $9,872) × (365 / 91)  ≈  5.21%
```

The market quotes T-bills in discount yield, but BEY is what an apples-to-apples comparison uses.

## Where they trade

- **Primary market**: Treasury auctions (TreasuryDirect.gov for retail, primary dealers for institutions)
- **Secondary market**: deepest, most liquid market in the world. Bid-ask spreads in microns.
- **ETFs**: SHV (T-bills), SHY (1-3y), IEF (7-10y), TLT (20y+), SCHP (TIPS)

## The role in markets

Treasuries are the **risk-free benchmark** against which everything else is priced:
- Stock cost of equity = T-bond yield + equity risk premium
- Corporate bond yields = T-yield + credit spread
- Mortgage rates = ~10y Treasury + spread
- DCF valuations discount future cash flows at a Treasury-anchored rate

## Risks (yes, even Treasuries have risks)

| Risk | Detail |
|---|---|
| **Inflation** | A 4% T-bond yields negative real return at 6% inflation |
| **Duration** | A 30-year T-bond falls ~17% per 100bp yield rise |
| **Reinvestment** | Coupons must be reinvested at prevailing rates (could be lower) |
| **Currency** | For non-USD holders, FX swings dominate |
| **(Theoretical) default** | US has never defaulted; 2011 debt-ceiling crisis showed it's not zero |

## TIPS (Treasury Inflation-Protected Securities)

Principal adjusts with CPI. Coupon is fixed real rate × adjusted principal.

```
10y TIPS coupon: 1.5%
Year 1 inflation: 3%
Adjusted principal: $1,030
Year 1 coupon: 1.5% × $1,030 = $15.45
```

At maturity, you get max(adjusted principal, original principal) — small deflation protection.

## Decision rules

- **Cash for spending in 1–2 years** → T-bills only; never long bonds
- **Long-term retirement (10y+)** → modest long Treasury exposure for diversification
- **Inflation hedge** → TIPS or short-term bills (which roll up with rising rates)
- **Curve trade** → see [[yield-curve]]
- **Watch the auctions** → tail (tail-of-the-tail) events at Treasury auctions move the curve

## Tax treatment (US)

- T-bill, T-note, T-bond interest: **federal taxable, state-exempt**
- Capital gain on sale: ordinary cap gains
- TIPS principal accrual: **taxed annually as income** even though not received in cash (the "phantom income" issue) — best held in tax-advantaged accounts

## In finterm

Gov Data screen plots Treasury yields by maturity (the [[yield-curve]]). Economics screen tracks Treasury spreads and TIPS breakevens (implied inflation expectations).
