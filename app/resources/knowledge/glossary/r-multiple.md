# R-Multiple

A trade outcome **expressed as multiples of the original capital risked** (the "R"). Standardizes results across different position sizes and stock prices, exposing whether you actually have an edge.

```
1R = the dollar amount you risked entering the trade
   = (Entry price − Stop price) × shares  (for a long)

R-multiple = (Realized P&L) / 1R
```

A trade where you risked $500 and made $1,500 = **+3R**. Risked $500, lost the stop = **−1R**.

## Worked example — same trade, different R-multiples

```
Account: $50,000
Risk per trade: 1% = $500

Trade A:
  AAPL @ $175, stop at $170, target $185
  1R = $5/share × 100 shares = $500
  Exit at $185 → P&L = $1,000 = +2R

Trade B:
  GME @ $200, stop at $180, target $260
  1R = $20/share × 25 shares = $500  (same dollar risk!)
  Exit at $260 → P&L = $1,500 = +3R

Both same dollar risk; different R outcomes. Comparing in R lets you see
that Trade B was actually higher quality.
```

## Why R-multiples matter

Comparing trades by **dollar P&L is misleading** because position sizes differ. A $1,000 win on a $10,000 trade looks bigger than a $500 win on a $1,000 trade — but the latter is a much higher-quality outcome.

R-multiples normalize away position size, exposing the underlying trade quality.

## Building expectancy from R

Track all your trades in R. Compute:

```
Expectancy (in R) = avg(R per trade)
                  = (P_win × Avg_R_win) − (P_loss × |Avg_R_loss|)
```

Sustainable strategies have expectancy > 0.3R after costs.

```
Example after 100 trades:
  Win rate:        45%
  Avg win:         +1.8R
  Avg loss:        -1.0R  (you honored your stops)

Expectancy = 0.45 × 1.8 − 0.55 × 1.0
           = 0.81 − 0.55
           = +0.26R per trade

If you risk 1% per trade and take 200 trades/year:
  Expected return = 200 × 0.26% = +52% gross (before drawdowns)
```

## R distribution

A healthy strategy has:
- **Winners > 1R consistently** (cut early = R distribution skewed left)
- **Losers ≤ −1R** (you honored stops; no over-runs)
- **Right-skewed distribution** (occasional big winners > 5R)
- **Avg R-win / avg R-loss > 1.5** (good R/R ratio)

Red flags:
- Winners systematically under 1R → cutting early (FOMO sells)
- Losers regularly > 1R → not honoring stops; "hope" trades
- Few trades > 2R → strategy can't compound enough to overcome losses

## Pitfalls

- **Reporting raw $ P&L** without R-normalization hides whether you have an edge
- **Calculating R after the fact** with hindsight stops is meaningless
- **Doubling down on losers** breaks R discipline; -1R becomes -3R
- **Strategies that cap winners but let losers run** have systematically negative R-distribution
- **Tiny sample size** (< 30 trades) → R-stats are noise

## Decision rules

- **Track R per trade in a journal** — non-negotiable for serious trading
- **Cut size if expectancy < 0.2R** sustained over 50+ trades
- **Cut a strategy entirely** if expectancy negative over 100+ trades
- **Aim for R-win:R-loss > 1.5** as a sanity check on edge
- **Maximize R, not $** — dollar focus encourages over-sizing

## Related

- [[expected-value]] — the more general probability-weighted concept
- [[kelly-criterion]] — sizing based on observed win rate and R-ratio
- [[expectancy]] — the same concept measured in dollars instead of R

## In finterm

Portfolio trade journal supports R-multiple tracking when you record entry, stop, and exit prices. Backtesting reports R distribution alongside win rate and Sharpe.
