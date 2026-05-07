**Moneyness** describes the relationship between an option's strike price and the current price of the underlying asset — whether exercising the option immediately would be profitable (in the money), breakeven (at the money), or unprofitable (out of the money).

Moneyness determines an option's intrinsic value, delta, and behavior. It is one of the most fundamental classification tools in options trading.

## The three states

| State | Call | Put | Intrinsic Value | Typical Delta |
|---|---|---|---|---|
| **In the money (ITM)** | Strike < Underlying | Strike > Underlying | Positive | > 0.50 (call) / < −0.50 (put) |
| **At the money (ATM)** | Strike ≈ Underlying | Strike ≈ Underlying | Zero | ~0.50 / ~−0.50 |
| **Out of the money (OTM)** | Strike > Underlying | Strike < Underlying | Zero | < 0.50 (call) / > −0.50 (put) |

## Worked example

```
Underlying: TSLA at $250

Option         Type   Strike   Moneyness   Approx. Delta
TSLA 230 call  Call   $230     Deep ITM    0.85
TSLA 245 call  Call   $245     Slightly ITM 0.60
TSLA 250 call  Call   $250     ATM          0.50
TSLA 260 call  Call   $260     OTM          0.35
TSLA 280 call  Call   $280     Deep OTM     0.12

TSLA 270 put   Put    $270     ITM          -0.65
TSLA 250 put   Put    $250     ATM          -0.50
TSLA 230 put   Put    $230     OTM          -0.25
```

## Quantitative moneyness measures

Beyond the categorical labels, traders use numerical measures:

**Simple moneyness:**
```
M = Underlying Price / Strike Price
M > 1 → call is ITM; M < 1 → call is OTM
```

**Log-moneyness (used in volatility surface construction):**
```
m = ln(F / K)
F = forward price; K = strike
m > 0 → ITM call; m = 0 → ATM; m < 0 → OTM call
```

**Delta-based moneyness:** Options are often referred to by their delta — a "25-delta put" is a moderately OTM put with ~0.25 delta. Used in FX and institutional options markets.

## Why moneyness matters for strategy selection

- **ITM options**: higher cost, higher delta, lower extrinsic value decay risk. Used when high probability of finishing ITM is desired.
- **ATM options**: maximum extrinsic value. Used in straddles; theta decay is highest here.
- **OTM options**: cheap in dollar terms but expensive in implied probability terms. Used for tail protection (puts) or leveraged directional bets (calls).

## "Moneyness" in volatility skew

The volatility skew plots implied volatility by moneyness — OTM puts typically have higher IV than OTM calls in equities, reflecting demand for downside protection.

## Pitfalls

- "In the money" only means profitable *if exercised immediately* — an ITM option can still expire worthless if the stock reverses.
- Deep OTM options have very low delta but can deliver large percentage returns on small premium outlay; they can also expire worthless at high frequency.
- ATM is an approximation — the true "at the money forward" uses the forward price of the underlying (adjusting for cost of carry), not the spot.

See also: [[intrinsic-value|Intrinsic Value]], [[extrinsic-value|Extrinsic Value]], [[delta|Delta]], [[volatility-skew|Volatility Skew]], [[put-call-parity|Put-Call Parity]].
