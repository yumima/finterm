The **risk/reward ratio** (also called risk-to-reward or R:R) compares the potential profit of a trade to its potential loss, establishing a minimum standard for which trades are worth taking.

A trader who consistently takes only trades with favorable risk/reward ratios can be profitable even with a win rate below 50%.

## Formula

```
Risk/Reward Ratio = Distance to Stop (risk) / Distance to Target (reward)

Expressed as 1:X (1 unit of risk for X units of reward)
```

## Worked example

```
Stock entry: $50.00
Stop loss:   $47.50  → risk = $2.50/share
Price target: $57.00 → reward = $7.00/share

Risk:Reward = $2.50 : $7.00 = 1 : 2.8

→ For every $1 risked, $2.80 is the potential gain
```

## Win rate and minimum risk/reward

The required win rate to break even depends on risk/reward:

```
Breakeven win rate = 1 / (1 + Reward/Risk)
```

| Risk:Reward | Breakeven Win Rate | Interpretation |
|---|---|---|
| 1:1 | 50.0% | Must win half to break even |
| 1:2 | 33.3% | Win 1 in 3 to break even |
| 1:3 | 25.0% | Win 1 in 4 to break even |
| 1:5 | 16.7% | Win 1 in 6 to break even |

A trader with 40% win rate at 1:2 R:R:
```
Expected value per $100 risked:
  40% × $200 (reward) − 60% × $100 (risk) = $80 − $60 = +$20 EV
  System is profitable despite losing 60% of trades
```

## Setting targets and stops

**Stop placement principles:**
- Stop below technical support (not arbitrary dollar amount).
- Outside recent volatility range (ATR-based).
- Below the level that invalidates the trade thesis.

**Target placement principles:**
- Prior resistance levels.
- Fibonacci extensions.
- Risk/reward minimum threshold (don't take trades unless target is at least 2:1 reward-to-risk).

## Adjusting for probability

[[expected-value|Expected value]] combines both win rate and risk/reward:

```
EV = (P(win) × Reward) − (P(loss) × Risk)

A 60% win rate at 1:1 R:R:
EV = (0.60 × 1) − (0.40 × 1) = +$0.20 per $1 risked

A 30% win rate at 1:3 R:R:
EV = (0.30 × 3) − (0.70 × 1) = +$0.20 per $1 risked

Both have identical EV — different styles, same profitability
```

## Risk/reward in portfolio management

At the portfolio level, risk/reward applies to entire positions:
- Position entry at $50 with stop $47 and target $57 = 2.8:1 R/R
- Allocate more capital to higher R/R setups (position sizing correlation)

## Pitfalls

- Risk/reward analysis is only as good as the stop and target placements — arbitrary stops lead to meaningless ratios.
- A 1:5 R:R trade with a 5% win rate is a losing system; R:R must be evaluated alongside realistic win rate expectations.
- Target prices are often aspirational; actual exit levels in practice are affected by fear, overconfidence, and changing conditions.
- Most trades are not binary (stop vs. target) — partial profits, trailing stops, and position adjustments create more complex outcomes.

See also: [[expected-value|Expected Value]], [[r-multiple|R-Multiple]], [[position-sizing|Position Sizing]], [[stop-loss-order|Stop-Loss Order]], [[kelly-criterion|Kelly Criterion]].
