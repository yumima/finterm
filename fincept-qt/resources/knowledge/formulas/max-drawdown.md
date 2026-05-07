**Maximum Drawdown (MDD)** is the largest peak-to-trough decline in portfolio value over a specified period — it is the most intuitive measure of the downside risk an investor would have experienced.

```
MDD = (Trough Value − Peak Value) / Peak Value

Expressed as a percentage (always negative or zero):
MDD = (V_trough − V_peak) / V_peak × 100
```

A related metric is **Calmar Ratio**, which divides annualized return by maximum drawdown magnitude:
```
Calmar = Annualized Return / |MDD|
```

## Worked example — SPY 2022 bear market

```
SPY peak (Jan 3, 2022)   $479.98
SPY trough (Oct 12, 2022) $348.11
Duration                 ~9 months

MDD = (348.11 − 479.98) / 479.98
    = −131.87 / 479.98
    = −27.5 %
```

An investor who bought at the January 2022 peak saw their portfolio decline **27.5 %** before any recovery. Notably, the investor needed a subsequent gain of **37.9 %** just to break even (recovery math: 1 / (1 − 0.275) − 1 = 37.9 %).

## What the result means

MDD captures the *worst emotional and financial moment* an investor would have faced — which is often more behaviorally relevant than volatility.

| MDD range   | Context |
|---|---|
| 0 – 10 %    | Conservative; short-duration bonds, low-volatility equity |
| 10 – 20 %   | Moderate; diversified equity portfolios in mild corrections |
| 20 – 35 %   | Significant; typical for broad equity in a bear market |
| 35 – 50 %   | Severe; sector funds, high-beta equity, some hedge funds |
| > 50 %      | Catastrophic; levered strategies, single-stock concentration |

The recovery ratio is asymmetric: a 50 % drawdown requires a 100 % gain to recover.

## Variants

- **Calmar Ratio** — annualized return ÷ |MDD|; a single number combining return and worst-case risk.
- **Sterling Ratio** — similar to Calmar but uses the average of the n largest drawdowns rather than just the maximum.
- **Ulcer Index** — RMS of all percentage drawdowns from the running peak; penalizes both depth and duration.
- **Time to Recovery (TTR)** — the number of days from trough to a new peak; MDD captures depth, TTR captures duration.

## Common mistakes

- Measuring MDD over a short history that excludes known crisis periods — a 3-year track record ending in 2021 misses the 2022 bear market entirely.
- Treating MDD as complete downside risk without considering [[cvar|CVaR]] — MDD is a realized historical metric; tail risk measures quantify forward-looking probabilities.
- Comparing MDD across strategies with different time periods — a 10-year MDD and a 2-year MDD are not comparable.
- Ignoring the time to recovery — a 30 % drawdown recovered in 3 months is very different from one that takes 3 years.

See also: [[var-parametric|VaR]], [[cvar|CVaR]], [[sharpe-ratio|Sharpe]], [[kelly-criterion|Kelly Criterion]].
