# Stop-Loss Order

A conditional order that **triggers a market order when the price hits a defined level** — designed to cap losses on an existing position.

```
Long position:  stop sells when price drops to stop level
Short position: stop buys when price rises to stop level
```

The stop **does nothing until triggered** — it's a sleeping order in the broker's system, not the exchange's order book.

## Worked example

```
Buy AAPL @ $175
Set stop-loss at $170 (−2.86% below entry)

Scenario A — orderly decline:
  Stock drops to $169.50, triggers stop.
  Stop becomes a market sell.
  Fill: $169.45 (slight slippage from spread)
  Loss: $5.55/share, ~3.2% of cost

Scenario B — gap down on bad news:
  Stock closes $172. Pre-market, news hits.
  Stock opens at $158 (gap below stop).
  Stop triggers at $158 → market sell at $158.
  Loss: $17/share, ~9.7% of cost — much worse than the $5 you "limited."
```

The gap risk is real. Stops protect against orderly declines, not against discrete gap events.

## Stop placement methodologies

| Method | Calc | Best for |
|---|---|---|
| **Fixed %** | 5–10% below entry | Simple; arbitrary |
| **Fixed $** | $5 below entry | Same as % but in dollars |
| **ATR-based** | 2–3× Average True Range | Adapts to volatility; preferred |
| **Technical level** | Below support/SMA | Thesis-aligned |
| **Volatility-adjusted** | 3 × stdev daily | Statistical; accounts for noise |

## Stop distance and trade frequency

```
Tight stop  (1–2% from entry)  → frequent stop-outs (whipsaws)
Medium stop (3–5%)             → balanced
Wide stop   (10%+)             → fewer stop-outs, larger losses
```

Tight stops + high win rate is hard. Most pros use medium-to-wide stops with smaller position sizes (constant $ risk).

## Stop variants

- **Hard stop** (default) — triggers at the exact level
- **Mental stop** — you intend to exit but no actual order; relies on discipline (often fails under pressure)
- **Time stop** — exit if thesis hasn't played out by a certain date
- **Volatility stop** — recalculated based on recent vol; e.g., 2 × 14-day ATR

## Pitfalls

- **Gap risk**: stops execute as market orders; a gap can fill far below
- **Round numbers cluster**: $50 stops cluster at exactly $50; algos hunt them
- **Stop-running** by market makers: known stop levels get tested intraday
- **Stops tighten through bear markets**: traders set new stops at recent lows that are themselves declining
- **Mental stops fail** in real losses: emotional override is the rule, not exception

## Stop-loss alternatives

- **Stop-limit** — same trigger, but converts to limit not market (avoid bad fills, risk no fill — see [[stop-limit-order]])
- **Trailing stop** — adjusts upward as price rises ([[trailing-stop]])
- **Position sizing** — accept full max loss with no stop; only viable for small position
- **Hedging with options** — buy a put to define max loss without execution risk

## Decision rules

- **Always set a hard stop** for trades where you'll be away from the market
- **Tighten stops only on adds**, not on existing positions (chasing)
- **Wide stop + small size** = same $ risk as tight stop + big size; prefer the wide-stop version for less noise
- **Reset stops after major moves** in your favor — don't let winners turn into losers
- **Never widen a stop** — that's how losses become catastrophes

## In finterm

Equity Trading order ticket has stop-loss option. Set the trigger price above (for shorts) or below (for longs) current market.
