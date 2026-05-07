An **iron condor** is a four-legged options strategy that profits when the underlying stays within a defined range — combining a bull put spread and a bear call spread on the same underlying and expiration.

The iron condor is a market-neutral premium collection strategy. You sell options to collect time value, expecting low realized volatility and no large directional move.

## Structure

```
Bull put spread (lower wing):
  + Buy 1 OTM put  (lower strike, e.g., $490)
  − Sell 1 OTM put (higher strike, e.g., $500)

Bear call spread (upper wing):
  − Sell 1 OTM call (lower strike, e.g., $540)
  + Buy 1 OTM call  (higher strike, e.g., $550)
```

The strikes form a "condor" shape on the profit diagram.

## Worked example

```
SPY at $520
Sell 540/550 bear call spread for $1.20 credit
Sell 490/500 bull put spread for $1.30 credit
Total credit received: $2.50

Maximum profit:  $2.50 (if SPY stays between $500–$540 at expiry)
Maximum loss:    $10.00 (wing width) − $2.50 (credit) = $7.50
Breakeven:       Upper: $540 + $2.50 = $542.50
                 Lower: $500 − $2.50 = $497.50

Win if SPY between $497.50 – $542.50 at expiry
```

## Profit / loss diagram

```
P&L ($)
 +2.50 |....___________....
       |   /           \
  0.00 |__/             \__
       |                   \
 -7.50 |                    \_____
       490 500    520   540 550
```

## Greeks profile

| Greek | Iron Condor Position |
|---|---|
| Delta | Near zero (market neutral) |
| Theta | Positive (profits from time decay) |
| Vega | Negative (hurt by rising IV) |
| Gamma | Negative (hurt by large moves) |

## When to use

- Low-volatility, range-bound environment.
- After an IV spike (high IV → fat premium → wider breakevens).
- When you expect a stock or index to stay near current levels through expiration.

## Adjustments and management

- If one side is threatened, traders often close the threatened spread and leave the other, or roll the threatened spread to a farther strike.
- Many traders close at 50% of max profit rather than holding to expiry, reducing gamma risk late in the cycle.

## Pitfalls

- Iron condors have limited upside ($2.50) and meaningful downside ($7.50) — the risk/reward ratio is unfavorable; you need a high win rate.
- A single gap move (earnings, macro shock) can wipe out multiple months of premium.
- Negative vega means rising implied volatility hurts even if the stock hasn't moved — IV expansion alone can cause mark-to-market losses.

See also: [[covered-call|Covered Call]], [[extrinsic-value|Extrinsic Value]], [[volatility-skew|Volatility Skew]], [[implied-volatility|Implied Volatility]], [[theta|Theta]].
