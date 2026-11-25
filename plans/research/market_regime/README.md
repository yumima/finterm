# The weekly brief's market stance (2026-11-25)

`regime_backtest.py` → `app/scripts/regime_evidence.json`. Definition: `regime_state` in
`app/scripts/weekly_brief_core.py` (the backtest imports it). Inputs: the S&P 500 against its 200-day average, the
VIX against 20 and 30. The levels are conventional, set before the test, not tuned; no other input was tried.

S&P 500, weekly, 1990-03 to 2026-08, the next 63 sessions (total return):

| State | Weeks | Higher | Average | 10% lower at some point | Average minus all weeks, 95% | 10% fall minus all weeks, 95% |
|---|---:|---:|---:|---:|---|---|
| Bullish (above, VIX < 20) | 1,132 | 73% | +2.4% | 8% | −1.5 to +0.5 pt | −10 to −2 pt |
| Bullish, with nerves (above, VIX ≥ 20) | 298 | 85% | +5.6% | 7% | **+1.3 to +4.0 pt** | −12 to −1 pt |
| Bearish (below, VIX < 30) | 346 | 58% | +0.7% | 32% | −4.6 to +0.6 pt | **+8 to +28 pt** |
| High fear (below, VIX ≥ 30) | 128 | 80% | +7.3% | 27% | −0.3 to +8.8 pt | −4 to +34 pt |
| All | 1,904 | 73% | +2.9% | 14% | | |

Intervals: moving-block bootstrap, 26-week blocks (the windows overlap).

The index is with dividends; the 10% fall is on closes and from the starting close.

## The stance

Full above the 200-day average, half below it, the rest in 13-week T-bills; weekly, 5 bp a unit traded, next week's
return (worst fall on weekly closes):

| Rule | A year | Volatility | Worst fall | First half | Second half |
|---|---:|---:|---:|---:|---:|
| **Full above, half below (the stance)** | 9.8% | 12.8% | −35% | 9.1% | 10.6% |
| Always invested | 10.8% | 16.7% | −55% | 9.6% | 12.0% |
| Half when bearish only | 10.5% | 15.1% | −52% | 10.2% | 10.8% |
| None when bearish only | 10.0% | 14.6% | −50% | 10.7% | 9.4% |
| None below the average | 8.4% | 11.2% | −29% | 8.2% | 8.6% |

The stance gives up a point a year for a worst fall a third smaller: a risk rule, not a return rule. It changes about
3.4 times a year. "Half" was picked from this table, so its record is in-sample; the 200-day average and the VIX levels
were not.

## What else it supports

- **Risk is what the state tells, direction hardly.** Below the average a 10% fall within a quarter is about twice as
  likely (bearish: interval clear of zero; high fear: not). The lower average of bearish weeks is inside noise.
- **Above the average with the VIX at 20 or more**, the next quarter was better than usual in both halves. That is the
  state, not "weakness": the index was up over the month before in two thirds of those weeks.
- **High fear**: the average quarter was strong, the interval includes zero, and in the second half it was higher 71%
  of the time, under the 73% of all weeks. 128 weeks are a handful of episodes (1990-91, 1998, 2000-03, 2008-09, 2010-11,
  2018, 2020, 2022, 2025-26).

Not used: valuation, positioning, seasonality, the yield curve, sentiment surveys.
