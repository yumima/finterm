# How to Use Stop-Losses Without Sabotaging Yourself

Stop-losses are simultaneously the most useful and the most misused risk-management tool in retail trading. The difference is in the placement, the discipline, and knowing when *not* to use them.

## The two purposes of stops

1. **Capping max loss** on a position-by-position basis (sizing tool)
2. **Forcing exit** when thesis is violated (decision tool)

Different stops serve different purposes. Confusing them leads to over-stopping or under-stopping.

## Where to place a stop

### Method A — Thesis-based (preferred)

Stop where the **thesis would be violated**, not where you're "comfortable losing."

```
Stock: $100
Bullish thesis: it will hold the 50-day moving average at $94 and rally
Stop: $93 (just below the level that would invalidate the thesis)
Risk per share: $7
```

If $94 breaks, your thesis is wrong and the loss is the cost of being wrong.

### Method B — Volatility-based

Use ATR (Average True Range) to set stop in proportion to normal noise:

```
Stock: $100
14-day ATR: $2.50
Stop distance: 2 × ATR = $5
Stop: $95
```

This adapts to the stock's natural movement. Tighter than 2× ATR → frequent whipsaws.

### Method C — Fixed percentage (worst)

```
Stop = entry × (1 − 0.05)  = $95
```

Simple but ignores volatility. A 5% stop on a 10%-vol stock is too tight; on a 50%-vol stock is too loose.

## Position sizing comes FIRST

Stop placement determines position size, not the other way:

```
Account: $100,000
Risk per trade: 1% = $1,000
Stop distance: $5/share
Position size: $1,000 / $5 = 200 shares
Position dollar value: 200 × $100 = $20,000
```

If the stop hits, you lose $1,000 (1% account). The 20% of account in this position doesn't matter — what matters is the dollar risk.

## When stops fail (and what to do)

### Failure 1 — Gap through the stop

Stock gaps from $96 close → $86 open. Your $95 stop triggers, fills at $86. You lose 14% on a "stop-loss" you set at 5%.

**Mitigation**: 
- Use position sizing that accounts for gap risk (smaller positions in high-gap-risk names)
- Hedge with options if held through binary events
- Don't hold concentrated positions through earnings without hedges

### Failure 2 — Whipsaw (stopped out at the noise low)

Stock dips $94.80 (your stop is $95), triggers, then rebounds to $108 over the next two weeks.

**Mitigation**: 
- Wider stops (2.5–3× ATR vs 1.5×)
- Smaller position sizes to make wider stops palatable
- Time-of-day filter (don't have stops active during opening 30 min)

### Failure 3 — Ignoring your own stop

You set $95, stock dips to $94, you say "the thesis is still intact" and let it run to $80 before capitulating.

**Mitigation**:
- Use **hard** stops in the broker, not mental stops
- Pre-commit publicly (write thesis + stop in journal before entering)
- Reduce position size next time so you can stomach the actual stop

## When NOT to use stops

- **Long-term core holdings** with multi-year thesis — quarterly review > daily stop
- **Highly volatile small-caps** where stops are noise more than signal — use small position sizing instead
- **Positions you would re-buy lower anyway** — use limit-add orders, not stops
- **Options positions** with defined max loss already (long calls/puts/spreads)
- **In retirement accounts** for long-term passive holdings (taxes don't apply, so no harvesting concern)

## Stop variants

| Type | Use case |
|---|---|
| [[stop-loss-order|Stop-loss (market)]] | Default for active positions |
| [[stop-limit-order|Stop-limit]] | Slippage protection in normal markets only |
| [[trailing-stop|Trailing stop]] | Lock in gains on trends |
| **Time stop** | Exit after N days regardless of price |
| **Mental stop** | For experienced traders who actually honor them (rare) |
| **Volatility stop** | Recalculated from rolling ATR |

## Worked example — A swing trade with proper stops

```
Trade: Long XYZ
Entry: $100.00
Reasoning: Breakout from $98 base, target $115
Stop placement: $96.50 (just below base support)
Stop distance: $3.50

Position sizing:
  Account: $50,000
  Max risk: 1% = $500
  Shares: $500 / $3.50 = 143 shares
  Position: 143 × $100 = $14,300 (28% of account)

Plan:
  Day 1: Place hard stop at $96.50 (broker)
  Set price alerts at $98.50 (early warning) and $103 (move stop?)
  Stock hits $108 → trail stop to $103 (lock in profit if reverses)
  Stock hits $115 (target) → consider trim half, trail rest
```

## Decision rules

- **Always place hard stop** at trade entry; mental stops fail
- **Stop based on thesis**, not arbitrary % from entry
- **Position size = max risk / stop distance** — sizing comes from stop, not from $ position size
- **Never widen a stop** — that's how losses become catastrophes
- **Trim, don't widen** — if thesis is intact at -2R, you sized too big
- **Honor stops on the day they hit** — don't "wait for the close to confirm"

## In finterm

Equity Trading order ticket supports stop, stop-limit, and trailing stop orders. Set them at order entry, not after — the discipline of pre-committing matters.
