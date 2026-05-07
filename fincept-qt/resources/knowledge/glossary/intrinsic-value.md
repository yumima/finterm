**Intrinsic value** (of an option) is the amount of money an option holder would receive if they exercised the option immediately — the difference between the underlying price and the strike price, if that difference is positive.

Intrinsic value can only be zero or positive. An option cannot have negative intrinsic value because no rational holder would exercise at a loss.

## Formula

```
Call intrinsic value = max(0,  Underlying Price − Strike Price)
Put  intrinsic value = max(0,  Strike Price    − Underlying Price)
```

## Worked example

```
AAPL trading at $185

Call, strike $180:  max(0, 185 − 180) = $5.00  intrinsic
Call, strike $190:  max(0, 185 − 190) = $0.00  intrinsic (out of the money)

Put,  strike $190:  max(0, 190 − 185) = $5.00  intrinsic
Put,  strike $180:  max(0, 180 − 185) = $0.00  intrinsic (out of the money)
```

An option with positive intrinsic value is **in the money (ITM)**. An option with zero intrinsic value is either **at the money (ATM)** or **out of the money (OTM)**.

## Total option price = intrinsic value + extrinsic value

```
Option Premium = Intrinsic Value + Extrinsic Value (time value)

Example: AAPL $180 call, 30 DTE
  Option price:     $8.50
  Intrinsic value:  $5.00
  Extrinsic value:  $3.50
```

The extrinsic portion reflects time value and implied volatility premium — it decays to zero at expiration. At expiration, an option's value equals its intrinsic value only.

## Deep ITM options

Deeply in-the-money options have very high intrinsic value relative to total premium. Their delta approaches 1.0 (calls) or −1.0 (puts), behaving much like the underlying itself.

| Moneyness | Intrinsic | Extrinsic | Delta |
|---|---|---|---|
| Deep ITM | High | Small | ~0.90–1.00 |
| ATM | Zero | Maximum | ~0.50 |
| OTM | Zero | All extrinsic | ~0.10–0.40 |
| Deep OTM | Zero | Very small | ~0.00–0.10 |

## Why it matters for option strategies

- **Exercising early**: For American options, early exercise is rational only when intrinsic value exceeds the extrinsic value you'd forfeit. This almost never applies to calls (except pre-dividend) but can apply to deep ITM puts.
- **Covered calls**: Intrinsic value is the minimum loss if a short call is exercised against you.
- **Assignment risk**: Any ITM option at expiration may be exercised, even by a small amount.

## Pitfalls

- Intrinsic value is not "fair value" of the option — it ignores time and volatility.
- A stock can move against you and erode intrinsic value just as fast as it created it.
- Deep ITM options can have very wide bid-ask spreads despite large intrinsic value, making execution costly.

See also: [[extrinsic-value|Extrinsic Value]], [[moneyness|Moneyness]], [[delta|Delta]], [[put-call-parity|Put-Call Parity]].
