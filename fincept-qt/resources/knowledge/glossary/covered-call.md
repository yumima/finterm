A **covered call** is an options strategy in which an investor who owns shares of a stock sells (writes) a call option on those same shares, collecting premium in exchange for capping their upside.

The long stock position "covers" the short call — if the option is exercised, the investor already owns the shares to deliver. This makes it one of the most conservative options strategies and is approved for most brokerage accounts.

## Structure

```
Position = Long 100 shares + Short 1 call option (same underlying)
Net premium received = call premium collected
```

## Worked example

```
Buy AAPL at $180.00
Sell AAPL $185 call, 30 DTE, for $3.20 premium

Maximum profit:    ($185 − $180) + $3.20 = $8.20 per share
Maximum loss:      $180 − $3.20 = $176.80 (stock goes to zero, keep premium)
Breakeven:         $180 − $3.20 = $176.80

At expiry scenarios:
AAPL at $175:  −$5.00 + $3.20 = −$1.80  (loss, but less than holding stock)
AAPL at $180:  $0.00 + $3.20 = +$3.20   (full premium, no stock gain)
AAPL at $185:  $5.00 + $3.20 = +$8.20   (maximum profit)
AAPL at $195:  $5.00 + $3.20 = +$8.20   (capped — shares called away at $185)
```

## Risk/reward profile

- **Upside is capped** at the strike price; all gains above the strike belong to the call buyer.
- **Downside is reduced** (not eliminated) by the amount of premium received.
- The strategy is mildly bullish to neutral — ideal for range-bound markets.

## When covered calls make sense

| Market View | Covered Call Suitability |
|---|---|
| Neutral / flat | Excellent — collect premium in stagnant market |
| Mildly bullish | Good — profit if stock rises to strike |
| Strongly bullish | Poor — you'll be called away before capturing full upside |
| Bearish | Poor — premium doesn't adequately offset a large decline |

## Assignment and tax considerations

If the stock closes above the strike at expiration, shares may be **called away** — you must sell them at the strike price. In taxable accounts, this triggers a capital gain event. If shares were held long-term but the covered call converts the holding period, tax impact can be significant.

## Covered calls on ETFs

QYLD, XYLD, and JEPI are ETF products that systematically sell covered calls. They generate high income (dividend-like distributions) at the cost of capping appreciation. Appropriate for income seekers; not for those expecting strong bull markets.

## Pitfalls

- Covered calls do not protect against large drawdowns — they only reduce loss by the premium received.
- Writing calls on a stock you hold for fundamental reasons can force you to sell at the wrong time.
- Rolling a short call higher when the stock surges costs money and does not recover all forgone upside.
- Opportunity cost: the biggest risk is a stock that doubles after you've capped it at 5% gain.

See also: [[extrinsic-value|Extrinsic Value]], [[moneyness|Moneyness]], [[theta|Theta]], [[iron-condor|Iron Condor]], [[put-call-parity|Put-Call Parity]].
