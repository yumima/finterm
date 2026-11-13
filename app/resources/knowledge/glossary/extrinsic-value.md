**Extrinsic value** (also called time value or premium) is the portion of an option's price that exceeds its intrinsic value — the extra amount buyers pay for the possibility that the option will move further into the money before expiration.

Every option's price has two components: what it's worth right now if exercised (intrinsic) and what it might be worth in the future given time and uncertainty (extrinsic). Extrinsic value decays to exactly zero at expiration.

## Formula

```
Extrinsic Value = Option Premium − Intrinsic Value
```

## Worked example

```
SPY trading at $520
SPY 530 call, 45 DTE, implied vol = 18%:
  Option price:      $8.20
  Intrinsic value:   max(0, 520−530) = $0.00 (OTM)
  Extrinsic value:   $8.20 − $0 = $8.20

SPY 510 call, 45 DTE:
  Option price:      $18.50
  Intrinsic value:   max(0, 520−510) = $10.00
  Extrinsic value:   $18.50 − $10.00 = $8.50
```

ATM options typically carry the highest extrinsic value in dollar terms.

## What drives extrinsic value

Three main inputs:

| Factor | Effect on Extrinsic Value | Greek |
|---|---|---|
| Time to expiration | More time → more extrinsic | Theta (decay rate) |
| Implied volatility | Higher IV → more extrinsic | Vega |
| Proximity to ATM | Closer to ATM → more extrinsic | Gamma |

## Theta decay

Extrinsic value erodes every day. The rate of erosion accelerates as expiration approaches — this is **theta decay**.

```
Theta example: Option with $8.20 extrinsic, 45 DTE
  Theta = −$0.12/day (approximately linear until ~30 DTE)
  At 21 DTE: accelerating decay
  At 7 DTE:  decay very rapid
  At expiry: extrinsic = $0.00
```

Options sellers collect extrinsic value as their profit if the option expires OTM. Options buyers pay extrinsic value as their "rent" — they need the underlying to move enough to overcome it.

## Breakeven for option buyers

```
Call buyer breakeven at expiry = Strike + Premium Paid
Put  buyer breakeven at expiry = Strike − Premium Paid

Example: Buy SPY 530 call for $8.20
Breakeven: $530 + $8.20 = $538.20
SPY must be above $538.20 at expiry to profit
```

## Implied volatility and extrinsic

Extrinsic value is essentially a market bet on volatility. When IV is high:
- Options are expensive (more extrinsic per dollar of underlying).
- Buyers overpay if realized volatility is lower than implied.
- Sellers collect rich premium but face higher risk.

## Pitfalls

- Buying options with high IV means paying lots of extrinsic value that may never be recovered.
- ATM options near earnings have inflated extrinsic (IV crush risk) — extrinsic can drop 30–50% overnight post-announcement even if the stock moves.
- Confusing intrinsic and extrinsic leads to poor exercise decisions; never exercise when you could sell the option and capture remaining extrinsic value.

See also: [[intrinsic-value|Intrinsic Value]], [[theta|Theta]], [[implied-volatility|Implied Volatility]], [[vega|Vega]], [[moneyness|Moneyness]].
