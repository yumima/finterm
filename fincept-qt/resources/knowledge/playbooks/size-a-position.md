# How to Size a Position

The hardest decision in investing isn't *what* to buy — it's *how much*. Get sizing wrong and even great ideas blow up your account.

## The two questions sizing answers

1. **How much pain can I tolerate if I'm wrong?**
2. **How much do I want to make if I'm right?**

Most retail investors only ask the second. Pros lead with the first.

## The simplest framework: fixed risk per trade

```
Position Size (shares) = Risk Capital / (Entry − Stop Price)
```

Pick a maximum loss per idea — typically **0.5–2% of total portfolio**. Set a stop-loss level you actually intend to honor. Solve for shares.

### Worked example — fixed 1% risk

```
Account size:       $100,000
Risk per trade:     1% = $1,000
Stock entry:        $50
Stop-loss:          $45  (thesis-based; e.g., breaks support)
Risk per share:     $5
Position size:      $1,000 / $5 = 200 shares
Position value:     200 × $50 = $10,000   (10% of portfolio)
```

If the stop hits, you lose $1,000 (1% of account) — survivable. If you're right and price doubles, you make $50/share × 200 = $10,000 (10% of account) — meaningful.

This automatically scales position size *down* for volatile stocks (wider stops) and *up* for stable ones — preserving constant dollar risk per idea.

## Volatility-adjusted sizing

A more sophisticated version: target equal **dollar volatility** per position.

```
Position Size = Risk Capital / (Stock's Daily $ Volatility)
Daily $ vol  = Price × Annual Vol / √252
```

### Worked example — vol parity

```
Target daily $ vol per position: $500

Position A — JNJ
  Price: $160, annual vol: 18%
  Daily vol = $160 × 0.18 / √252 = $1.81/share
  Size: $500 / $1.81 ≈ 276 shares ≈ $44k position

Position B — TSLA
  Price: $250, annual vol: 55%
  Daily vol = $250 × 0.55 / √252 = $8.66/share
  Size: $500 / $8.66 ≈ 58 shares ≈ $14.5k position
```

Both positions contribute roughly $500 of daily P&L noise. Without vol parity, a $44k TSLA position would dominate $44k JNJ in risk by ~5×.

## Kelly criterion (with caveats)

Mathematical optimum for compounding when you know your edge:

```
f = (p × b − q) / b

where:
  p = probability of winning
  q = 1 − p
  b = win/loss ratio
```

### Kelly worked example

```
Strategy: 60% win rate, 1.5:1 average W/L ratio
  f = (0.6 × 1.5 − 0.4) / 1.5
  f = (0.9 − 0.4) / 1.5
  f = 0.333  →  bet 33.3% of capital per trade (full Kelly)

Half Kelly (recommended): 16.7% per trade
Quarter Kelly: 8.3%
```

Even at the optimum, **most practitioners use ¼ or ½ Kelly** because:
- You don't actually know `p` precisely
- Drawdowns at full Kelly are brutal (50%+ common)
- Real returns are non-stationary

If you're not measuring your hit rate and average W/L from at least 50 trades, you're guessing — and Kelly amplifies guessing into account destruction.

## Concentration limits

Even with good per-trade sizing, total portfolio rules matter:

| Style | Max single | Max sector | Max factor exposure |
|---|---|---|---|
| Diversified retail | 5% | 20% | 30% (e.g., growth) |
| Active retail | 8–10% | 25% | 40% |
| Concentrated active | 15–20% | 30% | 50% |
| Buffett-style | up to 40% | uncapped | concentrated by design |

**Correlation gotcha**: 5 different software stocks isn't 5 ideas — it's one idea, 5 ways. Compute pairwise correlation; positions correlated >0.7 should count as one bet.

## Sizing across strategies

| Strategy type | Position size | Stop discipline |
|---|---|---|
| Conviction long-term hold | 5–10% | Wide stop or no stop; periodic re-evaluation |
| Swing trade (weeks-months) | 2–5% | Hard stop at thesis violation |
| Day trade | 0.5–2% | Tight stop, defined R-multiple |
| Hedge / pair trade | varies | Beta-balanced |
| Options (defined risk) | 0.5–2% of account at risk | Premium = max loss |

## A realistic process

1. Decide max risk per trade (1% is a reasonable default)
2. Decide stop-loss based on the **technical or fundamental thesis** — *not* arbitrary percentages
3. Compute share count
4. Check against max-position and max-sector limits
5. Cut size if either limit binds
6. Re-check correlation with existing positions

## Position sizing during drawdowns

After a meaningful drawdown (>10% portfolio), most should reduce sizing:

```
Drawdown    Recommended sizing scale
0–10%       100% (normal)
10–20%      75%
20–30%      50%
> 30%       25% — also stop adding new ideas; review process
```

This protects from compound errors when judgment may be impaired.

## The most expensive sizing mistake

**Doubling down on a loser** ("averaging down") without re-evaluating the thesis. Falling price is *information* — if your thesis was right, price would be rising. Re-do the [[evaluate-a-stock|evaluation playbook]] before adding capital, not just because it's "cheaper now."

## Decision rules

- **New trade size > pre-defined max** → reduce; don't override
- **Position grew > 2× max via gains** → trim back to max; rebalance
- **Stop hit** → exit fully; never widen the stop
- **Correlated cluster > 25% of portfolio** → reduce; don't add more
- **In active drawdown > 15%** → cut new-trade sizing in half

## In finterm

Portfolio risk panel shows current per-position sizing alongside [[volatility|stock vol]]. Use it before adding to or trimming positions, not after.
