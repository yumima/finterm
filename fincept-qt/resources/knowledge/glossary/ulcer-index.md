The **Ulcer Index** is a risk measure that quantifies the depth and duration of drawdowns from peak equity — designed to measure the "stress" experienced by holding an investment through its losses, not merely the volatility of returns.

Created by Peter Martin in 1987, the Ulcer Index captures what investors actually feel: prolonged drawdowns are more psychologically damaging than brief sharp drops. A portfolio that drops 20% and recovers in two weeks is better than one that lingers 15% below peak for two years.

## Formula

```
Ulcer Index = sqrt( (1/N) × Σ Drawdown_i² )

Where:
Drawdown_i = (Price_i − MaxPrice_i) / MaxPrice_i × 100
MaxPrice_i = highest price over the preceding period up to i
N = number of periods measured
```

## Worked example

```
Monthly peak-to-current drawdowns over 6 months:
Month 1:  0%   (at peak)
Month 2: -5%
Month 3: -12%
Month 4: -8%
Month 5: -3%
Month 6:  0%   (new peak)

Squared drawdowns:  0, 25, 144, 64, 9, 0  (sum = 242)
Mean of squares:    242 / 6 = 40.33
Ulcer Index:        √40.33 = 6.35
```

A higher Ulcer Index means more pain from drawdowns.

## Interpretation

| Ulcer Index | Assessment |
|---|---|
| 0–5 | Low stress; shallow or brief drawdowns |
| 5–10 | Moderate; noticeable drawdown periods |
| 10–20 | High; significant prolonged drawdowns |
| > 20 | Very high; deep extended drawdowns |

Typical benchmarks:
- S&P 500 (long-run): UI ≈ 5–7
- 60/40 portfolio: UI ≈ 3–5
- High-yield bond fund: UI ≈ 7–12
- Technology sector (volatile periods): UI ≈ 15–25

## Ulcer Performance Index (Martin Ratio)

The Ulcer Index pairs with a performance metric:

```
Martin Ratio (Ulcer Performance Index) = (Portfolio Return − Risk-free Rate) / Ulcer Index
```

Analogous to the Sharpe Ratio but using Ulcer Index instead of standard deviation. Higher is better.

```
Portfolio return 10%, Rf 4.5%, Ulcer Index 6:
Martin Ratio = (10% − 4.5%) / 6 = 0.92
```

## Comparison to other drawdown metrics

| Metric | Measures | Limitation |
|---|---|---|
| Maximum drawdown | Worst single drawdown | One-time event; ignores duration |
| Calmar ratio | Return / max drawdown | Same limitation |
| Ulcer Index | Depth AND duration combined | Less intuitive than max drawdown |
| Recovery time | Time from peak to new peak | Directional; ignores depth |

The Ulcer Index is superior to maximum drawdown alone because it captures **persistent** drawdown — it squares and averages all drawdowns across time, so a 10% drawdown lasting 12 months scores much worse than a brief 10% dip.

## Pitfalls

- Ulcer Index depends heavily on the measurement period; a long period with one severe bear market dominates the score.
- Rising markets artificially suppress the Ulcer Index even for risky strategies that haven't yet been tested.
- Ulcer Index is less widely reported than Sharpe or max drawdown — must be calculated from raw data.

See also: [[drawdown|Drawdown]], [[sortino-ratio|Sortino Ratio]], [[sharpe-ratio|Sharpe Ratio]], [[cvar|CVaR]], [[volatility|Volatility]].
