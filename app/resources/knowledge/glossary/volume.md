# Volume

The **number of shares (or contracts) traded** in a given period — typically reported per day. Volume is the most reliable confirmation signal for price moves.

```
Volume(day) = sum of all shares that changed hands today
```

A breakout on **2× average volume** is more credible than the same breakout on average volume.

## Worked example — confirmation signal

```
AAPL closes Friday at $175 on a downtrend, then:

Monday: opens $176, closes $182 (+4%)
  Volume: 80M shares (vs ADV 50M = 1.6× average)
  Conviction: real buying; institutional rotation

vs

Monday: opens $176, closes $182 (+4%)
  Volume: 35M shares (vs ADV 50M = 0.7× average)
  Conviction: short-covering or thin trading; less reliable
```

Same price action, very different signal value.

## Volume × price patterns

| Price | Volume | Interpretation |
|---|---|---|
| Up | High | Conviction buying; trend confirmation |
| Up | Low | Suspect rally; maybe short-cover only |
| Down | High | Distribution / panic; conviction selling |
| Down | Low | Drift; often retraces |
| Flat | High | Battle at level; consolidation |
| Flat | Low | Indecision |
| Breakout above resistance + 2× volume | — | Strong technical signal |
| Breakdown below support + 2× volume | — | Strong technical signal |

## Useful volume metrics

- **ADV (Average Daily Volume)** — typically 30-day or 60-day average
- **Volume ratio** — today's volume / ADV; >1.5 = elevated
- **VWAP (Volume-Weighted Average Price)** — fair-value benchmark used by algos
- **OBV (On-Balance Volume)** — cumulative volume signed by price direction
- **Volume profile** — histogram of volume by price level over a period

## Where volume signals shine

- **Earnings reactions**: heavy down-volume on a miss = capitulation
- **52-week high breakouts**: need volume confirmation
- **Failed breakouts**: low-volume break that fails fast → fade signal
- **Distribution days**: 4–5 high-volume down days in a month = institutional selling

## Where volume can mislead

- **Index-rebalance days** (S&P, MSCI quarterly) — volume spikes are mechanical, not informational
- **Options expiration weeks** (especially monthly) — volume distorted by hedging
- **Pre-market and after-hours** — small volumes magnified into "moves"
- **Dark pool activity** — doesn't show on lit-tape; total volume picture incomplete
- **Low-float stocks** — small absolute trades cause huge % moves and misleading volume

## Pitfalls

- **Single high-volume day is noise** until confirmed by next session
- **Backtesting strategies on close prices** ignores volume profile entirely
- **Crypto reported volume often inflated** (wash trading on exchanges)
- **ETF volume ≠ underlying basket volume** — creation/redemption arbitrage
- **Holding through high-volume reversal day** is one of the fastest ways to lose money

## Decision rules

- **Breakout signal** → require >1.5× ADV to consider it valid
- **Position with declining volume** despite rising price → trim; weakening signal
- **High-volume reversal day at extreme** → exit or reduce; capitulation
- **Volume contracting before breakout** → bullish (compressed energy)
- **Low-float stock + 5× volume + trending news** → squeeze risk; size down

## In finterm

Markets shows daily volume vs ADV. Watchlist volume column highlights names with elevated activity. The volume-by-price profile (when available) reveals where the heavy positioning is.
