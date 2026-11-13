**Put-call parity** states that for European options on the same underlying, the prices of a call and a put with identical strike and expiry are linked by a no-arbitrage relationship — if one price diverges, a risk-free profit is available.

```
C − P = S − PV(K)
C − P = S − K × e^(−rT)

C    = call option price
P    = put option price
S    = current stock price
K    = strike price
r    = continuously compounded risk-free rate
T    = time to expiration (years)
PV(K)= present value of the strike = K × e^(−rT)
```

Rearrangements:
```
Synthetic long stock:    S = C − P + PV(K)
Synthetic call:          C = P + S − PV(K)
Synthetic put:           P = C − S + PV(K)
```

## Worked example — AAPL options

```
S  = $175.00   (AAPL spot price)
K  = $175.00   (at-the-money)
T  = 0.25      (3 months)
r  = 4.5 %

PV(K) = 175 × e^(−0.045 × 0.25) = 175 × 0.9889 = $173.06

Put-call parity: C − P = S − PV(K) = 175 − 173.06 = $1.94

If the market shows:
  Call price C = $10.73   (from Black-Scholes example)
  Then put price must be: P = C − 1.94 = 10.73 − 1.94 = $8.79

  Or equivalently: P = C + PV(K) − S = 10.73 + 173.06 − 175 = $8.79
```

If the put trades at $9.50 instead of $8.79, the 71-cent discrepancy is an arbitrage:

```
Arbitrage (put overpriced):
  Buy call (−$10.73)
  Sell put (+$9.50)
  Sell stock (+$175.00)
  Invest PV(K) in risk-free bonds (−$173.06)
  Net initial cash flow: +$0.71

At expiry: every payoff scenario nets to zero, locking in the $0.71 profit.
```

## What the result means

Put-call parity is a **no-arbitrage constraint** — it must hold for European options or arbitrageurs will exploit the violation. Key implications:

1. You can **synthesize any position** from the other three (call, put, stock, bond).
2. Implied volatility is the same for calls and puts at the same strike and expiry (for European options without dividends) — if not, there is a vol arbitrage.
3. Deep in-the-money puts and calls are priced relative to each other through the parity relationship; one can always be inferred from the other.

## Variants

- **American options** — put-call parity becomes a pair of inequalities rather than an equality, because early exercise has value (especially for puts on high-dividend stocks or calls on deep ITM options near dividends).
- **Dividend-adjusted** — if S pays a discrete dividend D before expiry: C − P = S − D × e^(−r × t_D) − K × e^(−rT).
- **Forward-based form** — C − P = (F − K) × e^(−rT), where F is the forward price of the underlying; useful for futures options.

## Common mistakes

- Applying put-call parity to American options as if it were an equality — early exercise rights break the exact relationship.
- Forgetting dividends — for stocks paying discrete dividends before expiry, the parity formula must be adjusted; ignoring dividends will produce a mismatch.
- Treating the put-call parity spread as a vol signal — deviations in liquid markets are almost always explained by dividends, borrow costs, or bid-ask spreads, not mispricing.
- Using discrete discounting (1+r)^T instead of continuous compounding e^(rT) in the same formula — be consistent with the convention.

See also: [[black-scholes|Black-Scholes]], [[compound-interest|Compound Interest]], [[duration-formula|Duration]].
