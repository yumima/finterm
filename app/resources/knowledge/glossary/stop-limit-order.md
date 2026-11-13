# Stop-Limit Order

Combines a stop trigger with a limit order. Once the stop price is hit, the order becomes a **limit order** at a specified limit price — not a market order.

```
Stop price (trigger):  $170
Limit price:           $169.50

Behavior:
  Stock drops to $170 → order activates as a limit-sell at $169.50
  Sells if price stays at or above $169.50; doesn't fill if below
```

## Why use it

Stop-loss orders execute as **market orders** at trigger — vulnerable to slippage and gaps. Stop-limit orders **protect against bad fills** by capping how far below your stop you'll accept.

## Worked example — gap protection

```
Buy AAPL @ $175
Set stop-limit:
  Stop:  $170
  Limit: $169

Scenario A — orderly decline:
  Stock drops to $169.95 — triggers stop, becomes limit-sell at $169
  Fill: $169.50 (at or above your $169 limit) ✓

Scenario B — bad gap:
  Stock gaps from $172 close to $158 open
  Stop triggers (price < $170), order becomes limit at $169
  Price is at $158 — no one will buy from you at $169
  Order doesn't fill; you remain long while stock keeps falling
```

This is the **trade-off**: stop-limit protects price but creates risk of no fill.

## When stop-limit is better than stop-loss

- Liquid stocks during normal hours where a 0.5% slip is unwanted
- Limit set just below stop (e.g., 0.1–0.5% below) — fills almost always
- After-hours holds where slippage on stop-loss could be large but a real gap is unlikely

## When stop-limit is worse than stop-loss

- During genuine gaps (earnings, news shocks) — exactly when you most need to exit
- Illiquid names where execution is uncertain even in normal conditions
- Highly volatile assets (small caps, crypto) where the limit can be skipped

## Worked example — limit too tight

```
Set stop-limit:  stop $170, limit $169.99 (0.005% spread)

Stock drops fast: $170 → $169.50 in 30 seconds
Stop triggers, limit at $169.99
Best bid is $169.40 — your $169.99 sell never crosses
You're stuck long as it slides further
```

The narrower the limit-to-stop band, the more likely you don't fill in stress.

## Sizing the gap

Common configurations for stop-limit:

| Stock vol | Stop → limit gap | Reason |
|---|---|---|
| Low (utilities, staples) | 0.1–0.3% | Slippage rare |
| Normal (mega-cap) | 0.3–0.5% | Most fills succeed |
| High (small-cap, growth) | 1–2% | Account for normal range |
| Crypto / penny stocks | Don't use stop-limit; use stop-loss + small size |

## Pitfalls

- **Treating stop-limit as risk management** when it's actually risk *avoidance* (you can't be stopped out at all in big drops)
- **Forgetting the limit doesn't follow** as price keeps falling
- **GTC stop-limits expire** — check broker policy
- **Confusing stop trigger with limit price** — they're two different inputs

## Decision rules

- **Hard exit needed regardless of price** → stop-loss (market)
- **Mild slippage protection in normal markets** → stop-limit with 0.3–0.5% gap
- **Black swan / overnight risk** → use puts or position size, not stop-limit
- **Day trade with live monitoring** → either type works; check intraday liquidity

## In finterm

Equity Trading order ticket supports stop-limit. Set both the trigger (stop) price and the maximum acceptable fill (limit) price. Verify they're configured correctly before submitting.
