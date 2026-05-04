# Expectancy

The **average expected dollar return per trade** given a strategy's win rate, average winner, and average loser. The single most important number for evaluating whether a trading approach has an edge.

```
Expectancy = (P_win × Avg_win) − (P_loss × |Avg_loss|)
```

If your expectancy is positive after costs, the strategy makes money in the long run. If negative, it bleeds — no matter how many "feel like winners" you have.

## Worked example — strategy A

```
After 100 trades:
  Win rate (P_win):      55%
  Average winner:        $300
  Average loser:         $200

Expectancy = (0.55 × $300) − (0.45 × $200)
           = $165 − $90
           = +$75 per trade

Over 200 trades/year:
  Expected gross profit ≈ $15,000 (before costs)
  
Subtract typical commissions ($1/trade): -$200
Subtract typical slippage ($5/trade):    -$1,000
Net expected profit:                     ~$13,800
```

Sustainable. Now check:

## Worked example — strategy B (the trap)

```
After 100 trades:
  Win rate:        80%   "I win most of the time!"
  Avg winner:      $50
  Avg loser:       $400  ("I let losers run hoping...")

Expectancy = (0.80 × $50) − (0.20 × $400)
           = $40 − $80
           = -$40 per trade

200 trades/year = -$8,000 per year
```

High win rate ≠ profitable. **The R/R ratio matters as much as win rate.**

## Expectancy = Edge × Opportunity

```
Annual return ≈ Expectancy × Number of trades / Capital
```

Three levers:
1. **Increase expectancy** (better entries, exits, or risk control)
2. **Increase number of trades** (more opportunities; risk overtrading)
3. **Same expectancy with bigger size** (Kelly suggests how much)

## Reference expectancies

| Strategy quality | Expectancy in R |
|---|---|
| Negative (most retail traders) | < 0R |
| Break-even after costs | 0R |
| Marginal edge | 0.05–0.20R |
| Solid retail edge | 0.20–0.50R |
| Strong systematic strategy | 0.50–1.0R |
| Suspicious / data-mined | > 2R (over-fit?) |

## What kills expectancy

- **Slippage** in live vs backtest
- **Commissions** in active strategies
- **Strategy decay** as edges become known
- **Discretionary deviations** from system rules
- **Drawdown-induced sizing-down** (cuts winners short)

## How many trades to trust the number

Expectancy is a noisy statistic. Sample size matters:

| Trades | Expectancy reliability |
|---|---|
| 10 | Pure noise; ignore |
| 30 | Still very noisy |
| 50 | Some signal emerging |
| 100 | Reasonable confidence |
| 250 | Solid; trust within 95% CI |
| 1,000+ | High confidence |

For low-frequency strategies (5–10 trades/year), it takes years to build statistical confidence.

## Pitfalls

- **Computing expectancy from cherry-picked period** — use full sample
- **Ignoring transaction costs** — turns positive expectancy negative
- **Survivorship bias** in self-evaluation (forgetting losing trades)
- **Mixing strategy types** (swing + day + scalp) into one expectancy number
- **Treating expectancy as future-guaranteed** — it's a historical average, not a forecast

## Decision rules

- **Compute expectancy after every 20 trades** to catch degradation early
- **Cut sizing in half** if expectancy turns negative for 30+ trades
- **Stop the strategy** if expectancy stays negative for 50+ trades
- **Recompute quarterly** for systematic strategies
- **Use R-multiples** instead of dollars for cross-strategy comparison

## Related

- [[r-multiple]] — same concept in normalized risk units
- [[expected-value]] — the broader probability concept
- [[kelly-criterion]] — sizing once you know expectancy and variance

## In finterm

Backtesting reports expectancy alongside Sharpe ratio and max drawdown. Trade journals in Portfolio compute live expectancy as you accumulate trade history — invaluable for catching strategy decay early.
