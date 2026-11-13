**Average True Range (ATR)** is a technical indicator that measures the average magnitude of price movement over a specified period, accounting for overnight gaps — a volatility measure used for setting stops, sizing positions, and identifying volatility regimes.

Created by J. Welles Wilder (1978), ATR answers "how much does this instrument typically move per day?" — essential for setting intelligent stop-loss distances.

## Formula

**True Range (TR) for each period:**

```
True Range = max of:
  1. Current High − Current Low
  2. |Current High − Previous Close|
  3. |Current Low  − Previous Close|
```

The second and third terms capture overnight gaps — gaps where the market opens far above or below the previous close.

**ATR (14-period, standard):**

```
ATR₁₄ = Simple average of last 14 True Range values
(or EMA-style: ATR = (Prior ATR × 13 + Current TR) / 14)
```

## Worked example

```
AAPL over 5 days:
Day 1: H=186, L=183, Close=184, gap=0 → TR = max(3, 0, 0) = 3
Day 2: H=185, L=181, Close=183, gap=0 → TR = max(4, 0, 1) = 4
Day 3: H=188, L=182, Close=187, prev=183 → TR = max(6, 5, 1) = 6 (gap up)
Day 4: H=189, L=185, Close=186 → TR = max(4, 2, 2) = 4
Day 5: H=187, L=183, Close=185 → TR = max(4, 1, 3) = 4

5-day simple ATR = (3+4+6+4+4)/5 = 4.2 points
```

AAPL "typically" moves $4.20/day — useful for setting stops and sizing positions.

## ATR applications

**1. Stop-loss placement:**
```
Volatility stop = Entry price − (ATR × multiplier)
Common multiplier: 1.5× to 3× ATR

AAPL entry at $186, ATR = $4.20, 2× multiplier:
Stop = $186 − (2 × $4.20) = $186 − $8.40 = $177.60
```

This stop is placed outside normal daily noise — not easily hit by random fluctuation.

**2. Position sizing:**
```
Risk $ per trade: $500
ATR (14-day): $4.20
Stop: 2× ATR = $8.40

Position size = $500 / $8.40 = 59 shares
```

**3. Volatility regime detection:**
```
If ATR(14) > 2 × ATR(50):  high volatility regime → reduce size
If ATR(14) < 0.5 × ATR(50): low volatility → normal size or larger
```

## ATR across timeframes

ATR scales roughly with the square root of time:

```
1-day ATR: $4.20
Weekly ATR ≈ $4.20 × √5 ≈ $9.40
Monthly ATR ≈ $4.20 × √21 ≈ $19.24
```

## Wilder's other indicators

ATR was developed alongside the Relative Strength Index (RSI), Parabolic SAR, and the Directional Movement Index (ADX) — all by Wilder.

## Pitfalls

- ATR is a backward-looking average — it reflects recent volatility, not future volatility. ATR can spike quickly and then mean-revert.
- ATR doesn't give a direction — only magnitude. A high ATR stock can be trending steadily in one direction or whipsawing randomly; the ATR looks the same.
- Using fixed-point stops instead of ATR-based stops in changing volatility regimes leads to inconsistent risk.
- Short-period ATR (5 or fewer bars) is noisy; 14 periods is standard but 20+ periods can be more stable.

See also: [[position-sizing|Position Sizing]], [[volatility|Volatility]], [[stop-loss-order|Stop-Loss Order]], [[risk-reward-ratio|Risk/Reward Ratio]], [[drawdown|Drawdown]].
