**Put-call parity** is a fundamental no-arbitrage relationship that defines the required price relationship between a European call option, a European put option, the underlying asset, and a risk-free bond, all sharing the same strike and expiration.

If the relationship is violated, arbitrageurs can construct a riskless profit — so in liquid markets it holds precisely. Understanding it reveals the structural equivalences between options, stock, and bonds.

## Formula

```
C − P = S − K × e^(−rT)

Where:
C = call price
P = put price
S = current spot price of underlying
K = strike price
r = risk-free rate (continuously compounded)
T = time to expiration (years)
e^(−rT) = present value discount factor
```

For simplicity at low interest rates:

```
C − P ≈ S − PV(K)
```

## Worked example

```
SPY = $520
Strike K = $520 (ATM)
Expiry = 3 months (T = 0.25)
r = 5.0%
PV(K) = 520 × e^(−0.05 × 0.25) = 520 × 0.9876 = 513.55

Put-call parity: C − P = 520 − 513.55 = $6.45

If ATM call trades at $14.80:
Required put price = 14.80 − 6.45 = $8.35

If actual put trades at $7.50 → arbitrage: sell call, buy put, buy stock
```

## Equivalent positions (synthetic relationships)

Put-call parity directly implies synthetic equivalences:

| Desired Position | Synthetic Construction |
|---|---|
| Long call | Long put + Long stock + Borrow PV(K) |
| Long put | Long call + Short stock + Lend PV(K) |
| Long stock | Long call + Short put + Lend PV(K) |
| Short stock (synthetic) | Short call + Long put − Lend PV(K) |

These synthetics underpin market-making, hedging, and arbitrage strategies.

## How dividend adjustments change parity

For dividend-paying stocks, the forward price replaces spot:

```
C − P = F × e^(−rT) − K × e^(−rT)
F = S × e^(rT) − PV(Dividends)
```

Ignoring dividends overstates calls and understates puts for dividend-paying stocks.

## Limitations: American vs European options

Put-call parity holds strictly for **European options** (exercise at expiry only). For American options, the relationship becomes an inequality because early exercise is possible:

```
S − K ≤ C − P ≤ S − K × e^(−rT)
```

This means American put-call parity only defines bounds, not exact prices.

## Practical uses

- Verify if quotes are consistent (detect data errors or stale quotes).
- Price OTC options when only one leg is observable.
- Construct synthetic positions at lower transaction costs.

## Pitfalls

- Put-call parity assumes no dividends (unless adjusted), frictionless markets, and European options.
- Apparent violations in equity options are often due to early exercise premium in American puts.
- During market stress, bid-ask spreads widen and the parity "band" can be several dollars wide — not arbitrageable.

See also: [[intrinsic-value|Intrinsic Value]], [[moneyness|Moneyness]], [[covered-call|Covered Call]], [[delta|Delta]], [[volatility-skew|Volatility Skew]].
