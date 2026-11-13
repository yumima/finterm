# Vega (ν)

The Greek that measures **sensitivity to implied volatility** — how much the option's price changes per 1 percentage point change in IV.

```
Vega = ∂(option price) / ∂(IV)
```

A vega of $0.10 means: a 1-point IV move (e.g., 25% → 26%) changes the option's value by $0.10.

## Worked example — earnings vega play

```
AAPL @ $175
30-day $175 call (ATM):
  Premium:  $5.00
  Vega:     $0.20
  IV:       25%

Earnings expected → IV climbs to 38% (+13 points):
  Premium gain ≈ $0.20 × 13 = $2.60
  New premium ~$7.60

Earnings prints, IV crushes to 22% (−16 points from peak):
  Premium drop ≈ $0.20 × −16 = −$3.20
  Premium drops back to ~$4.40
```

If you bought the call hoping for direction but the stock barely moved, **vega crush after earnings ate $3.20 of your $7.60 mid-event peak**. Net P&L: −$0.60 even though IV at entry climbed.

## Vega profile

| Moneyness × DTE | Vega |
|---|---|
| ATM, long-dated | **Maximum** |
| ATM, short-dated | Lower (less time for vol to matter) |
| Deep OTM | Low |
| Deep ITM | Low |

**Long-dated options carry the most vega.** LEAPS are essentially long-vol bets on a multi-year horizon.

## Vega and IV relationship

- **Long options** = +vega = profit when IV rises (uncertainty climbs)
- **Short options** = −vega = profit when IV falls (uncertainty resolves)
- **Vega cluster across portfolio** = total vol exposure ($ change per 1 IV point)

## Common vega trades

| Structure | Vega profile | When to use |
|---|---|---|
| Long straddle | Strong +vega | Big IV expansion expected |
| Iron condor | −vega | IV elevated, expect mean reversion |
| Calendar spread | +vega (back month dominates) | Long-term IV cheap |
| Covered call | Slight −vega | Income with mild vol view |

## Decision rules

- **Buying premium pre-earnings** → only if you expect IV expansion *more* than is priced in
- **Selling premium pre-earnings** → captures vega crush; size for direction risk
- **Term structure inverted** (front IV > back IV) → buy back vega, sell front; calendar spread
- **VIX percentile rank > 70** → consider net-short vega trades

## Pitfalls

- Buying vega just because IV "looks low" — it can stay low or get lower
- Selling vega on a known catalyst expecting crush — the catalyst can be a real move
- Forgetting that vega scales with sqrt(time) — long-dated vol is much "stickier"
- IV surface doesn't shift uniformly — front-end and back-end can move opposite directions

## In finterm

Surface Analytics shows the IV surface and vega contours. Watch the spread between front-month and back-month IV to time vega trades.
