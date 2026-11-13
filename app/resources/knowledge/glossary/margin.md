**Margin** in trading refers to the funds deposited with a broker as collateral to support leveraged positions — borrowing money to control a larger position than cash alone would allow.

Margin enables both amplified returns and amplified losses. It is a tool for professional investors and a frequent cause of retail account destruction when used without discipline.

## Equity margin (stock trading)

The Federal Reserve's Regulation T governs margin in U.S. equity markets:

```
Initial margin requirement: 50% of purchase price
Maintenance margin (minimum): 25% (FINRA requirement; brokers often higher)

Purchase $10,000 of stock:
  Equity contributed: $5,000 (50%)
  Borrowed from broker: $5,000

If stock falls to $6,000:
  Loan remains: $5,000
  Equity: $6,000 − $5,000 = $1,000
  Margin ratio: $1,000 / $6,000 = 16.7% → below 25% maintenance
  → Margin call triggered
```

## Futures margin (very different concept)

Futures margin is a **performance bond**, not a loan:

```
E-mini S&P 500 futures (ES):
  Notional value: $5,000 × 5,200 = $26M per contract
  Initial margin: ~$15,000 per contract
  Maintenance margin: ~$12,000 per contract

Leverage: $26M / $15,000 ≈ 1,733× (the account, not position to margin)
More practically: $26M / account size
```

No interest is charged on futures margin because no money is actually borrowed; the position is marked-to-market daily.

## Options margin (selling options)

Writing (selling) uncovered (naked) options requires margin to cover potential obligations:

```
Naked short put, $5,000 strike:
  Margin requirement: roughly 20% of underlying − OTM amount + premium
  = 20% × $500,000 + premium
  = $100,000 + premium (approximate)
```

Covered calls require no margin (the stock covers the obligation).

## Margin terminology

| Term | Definition |
|---|---|
| Initial margin | Deposit required to open position |
| Maintenance margin | Minimum equity to keep position open |
| Margin call | Demand to deposit more funds when equity falls below maintenance |
| Reg-T margin | Fed-regulated equity margin (50% initial) |
| Portfolio margin | Risk-based margin; available to qualified investors; often lower than Reg-T |
| Cross margin | Full account balance backs all positions collectively |
| Isolated margin | Each position has its own separate margin allotment |

## Interest on margin loans

Equity margin borrowing accrues interest (typical rates in 2024–2025: 6–12%+ depending on broker and balance):

```
Borrow $50,000 on margin at 8% annual interest
Annual interest cost: $4,000
The position must outperform by $4,000+ to break even vs. unleveraged
```

## Pitfalls

- Margin amplifies losses exactly as much as gains; a 50% leveraged position loses 100% equity on a 33% adverse move.
- Margin calls can force liquidation at the worst possible time — during panics when prices are most depressed.
- Interest costs compound silently; a marginally profitable trade held for months can become a loss after interest.
- Volatility triggers margin calls even in positions that eventually recover — forced selling at lows is a common margin casualty.

See also: [[margin-call|Margin Call]], [[leverage|Leverage]], [[short-selling|Short Selling]], [[liquidation-price|Liquidation Price]], [[position-sizing|Position Sizing]].
