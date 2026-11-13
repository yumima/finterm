A **margin call** is a broker's demand that an investor deposit additional funds or securities to bring a margin account back above the required minimum level — triggered when the value of the account falls below the maintenance margin threshold.

If the investor cannot meet the margin call within the required timeframe, the broker has the right to liquidate positions — often at the worst possible time.

## How it triggers

```
Initial setup:
  Account value:      $100,000
  Leveraged position: $200,000 (2× leverage, $100,000 borrowed)
  Maintenance margin: 25% of position value

Trigger threshold:
  Position value must be ≥ $50,000 (25% × $200,000)
  Which means equity can't fall below $50,000 − $100,000 borrowed = $0? 
  
More precisely:
  Equity / Position Value ≥ 25%
  $100,000 equity / $200,000 position = 50% ✓ (above 25%)
  
  Position falls 30%: Position = $140,000
  Equity = $140,000 − $100,000 loan = $40,000
  Ratio = $40,000 / $140,000 = 28.6% ✓ (above 25%)
  
  Position falls 37.5%: Position = $125,000
  Equity = $125,000 − $100,000 = $25,000
  Ratio = $25,000 / $125,000 = 20% → MARGIN CALL
```

## Margin call mathematics

```
Margin call price = Loan Value / (1 − Maintenance Margin %)
                  = $100,000 / (1 − 0.25) = $133,333

If position falls below $133,333 → margin call triggered
Original $200,000 position → margin call at −33.3%
```

## Broker response timeline

| Stage | Action |
|---|---|
| Margin call issued | Investor notified (email, phone, app) |
| 24–72 hours | Investor must deposit cash, sell assets, or add securities |
| Deadline missed | Broker liquidates positions at market, no approval needed |
| Post-liquidation | Investor responsible for any remaining deficiency |

In volatile markets, brokers may skip the waiting period and liquidate immediately.

## Cascade mechanics

Margin calls during market panics create cascading forced selling:

```
Asset falls 15% → margin call → forced selling
→ Asset falls another 5% (forced selling pressure)
→ More margin calls triggered → more forced selling
→ Cascade
```

This dynamic amplified the 2008 crisis, COVID crash (March 2020), and numerous other dislocations.

## Types of accounts that can margin call

| Account Type | Margin Call Risk |
|---|---|
| Margin brokerage account | Yes — Reg T margin |
| Futures account | Yes — variation margin (daily) |
| Leveraged crypto exchange | Yes — often automated and instant |
| CFD account | Yes — often aggressive margin requirements |
| Short positions | Yes — stock rises trigger covering requirements |

## Portfolio margin

Some sophisticated investors qualify for **portfolio margin** (Rule 2520), which uses risk-based margin:
- Lower initial requirements if positions are hedged.
- Can still result in margin calls if correlation assumptions break down.

## Pitfalls

- Brokers can increase margin requirements without notice ("house calls"), especially during volatile periods — the 25% minimum is a floor, not the maximum requirement.
- Meeting a margin call by selling *other* positions to raise cash extends the portfolio's pain in a downturn.
- Crypto exchanges often have automated instant liquidation with no grace period.
- Short-term volatility can trigger margin calls on positions that would ultimately recover — forced selling locks in losses.

See also: [[margin|Margin]], [[leverage|Leverage]], [[liquidation-price|Liquidation Price]], [[short-selling|Short Selling]], [[var|VaR]].
