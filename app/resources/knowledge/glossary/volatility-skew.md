**Volatility skew** is the pattern in which options at different strikes but the same expiration have different implied volatilities — most commonly, lower-strike (OTM put) options carry higher IV than higher-strike (OTM call) options in equity markets.

Skew reflects market participants' demand for protection against large down moves. It encodes the market's fear asymmetry: crashes happen more violently than melt-ups, so downside options are priced more richly.

## The equity skew pattern

In equity indexes (SPX, SPY), the volatility surface typically shows:

```
IV (%)
 25% |*
     | *
 20% |   *
     |      *
 15% |            *   *   *
     |
     +--OTM put--ATM--OTM call---->
          (lower strikes)  (higher strikes)
```

OTM puts command the highest IV; OTM calls are cheapest. This "smirk" pattern has persisted since the 1987 crash.

## Measuring skew

**25-delta risk reversal (most common):**
```
Skew = IV of 25-delta put − IV of 25-delta call
```

A more negative number means more skew (puts more expensive vs calls).

**Skew index (SKEW, published by CBOE):**
Measures the risk of outlier (tail) returns; higher SKEW = more skew, more tail concern.

## Worked example

```
SPX at 5,200
30-day ATM IV:          18%
30-day 25-delta put IV: 22%  (5,000 strike area)
30-day 25-delta call IV: 16%  (5,400 strike area)

25-delta risk reversal: 22% − 16% = 6% (puts 6 vol points richer)
```

## Skew by asset class

| Asset Class | Typical Skew Pattern | Reason |
|---|---|---|
| Equity indexes | Negative (put skew) | Crash demand, portfolio hedging |
| Individual stocks | Negative, less steep | Less systemic crash demand |
| Commodities (oil) | Positive or mixed | Supply shocks push prices up violently |
| FX | Bilateral, symmetric | Currencies can crash both ways |
| Crypto | Steep negative near major events | High perceived tail risk |

## Term structure of skew

Skew is not constant across time. Near-dated options often have steeper skew than longer-dated options because near-term crash risk is more immediately priced.

## Trading skew

- **Buy skew** (buy OTM puts, sell OTM calls): costs money; profits if market drops or fear rises.
- **Sell skew** (sell OTM puts, buy OTM calls): receives premium; profits if market stays calm.
- **Skew trade** (risk reversal): sell OTM put, buy OTM call; directionally bullish, monetizes expensive put premium.

## Pitfalls

- High skew does not mean a crash is imminent — it means downside options are expensive, which can persist indefinitely.
- Low skew (complacency) can reverse violently; SKEW was low before multiple market dislocations.
- Single-stock skew is harder to interpret than index skew, because event-driven (earnings, M&A) factors dominate.

See also: [[implied-volatility|Implied Volatility]], [[vix|VIX]], [[put-call-parity|Put-Call Parity]], [[moneyness|Moneyness]], [[iron-condor|Iron Condor]].
