A **calendar spread** (also called a time spread or horizontal spread) is an options strategy that simultaneously buys a longer-dated option and sells a shorter-dated option at the same strike on the same underlying, profiting from the difference in time decay rates.

The core insight: near-term options decay faster than longer-term options. By selling the fast-decaying front-month option and owning the slower-decaying back-month option, a trader can profit from the passage of time if the underlying stays near the strike.

## Structure

```
Buy  1 call (or put), longer expiry (e.g., 60 DTE)
Sell 1 call (or put), shorter expiry (e.g., 30 DTE)
Same strike, same underlying

Net debit = premium paid for long − premium received for short
```

## Worked example

```
AAPL at $185, ATM strike = $185

Buy  AAPL $185 call, 60 DTE:  −$7.50
Sell AAPL $185 call, 30 DTE:  +$4.80
Net debit:                     −$2.70

Maximum profit: Realized if AAPL is near $185 at 30-day expiry
  (front month expires worthless, back month retains ~$5.50 of value → ~$2.80 gain)
Maximum loss: $2.70 (net debit paid, if AAPL moves far from $185)
```

## How profit is generated

When the short option expires (30 DTE → 0), the long option still has 30 days of time value. If the underlying is near the strike at the short expiry, the short leg expires worthless and the remaining long option retains significant time value.

## Theta and vega dynamics

- **Theta**: Positive in ideal conditions — the short leg decays faster, net theta is positive near the strike.
- **Vega**: Positive — longer-dated options have more vega. If IV rises, the back-month option gains more than the front-month option loses.

This makes calendar spreads appealing before events where IV is expected to rise (though you want IV to rise on the back month, not just the front month).

## Double calendar

A variation using two strikes (one above, one below) to widen the profit zone while paying slightly more debit.

## Forward term structure matters

In contango (longer-dated IV > short-dated IV), calendars are cheaper. In backwardation (IV term structure inverted, common around earnings), calendars can be expensive.

## Pitfalls

- A large move away from the strike destroys both legs, resulting in a total loss of the debit paid.
- At earnings, IV crush can hurt the back month more than expected if term structure flattens sharply.
- You cannot achieve unlimited profit — maximum gain is bounded by the spread between the two options' decay paths.
- Pin risk: if the stock pins the strike exactly at short expiry, the short leg assignment and remaining long position can create unintended delta exposure.

See also: [[extrinsic-value|Extrinsic Value]], [[theta|Theta]], [[implied-volatility|Implied Volatility]], [[volatility-skew|Volatility Skew]], [[iron-condor|Iron Condor]].
