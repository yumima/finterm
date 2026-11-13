# Drawdown

The percentage decline from a portfolio's prior **high-water mark** to its subsequent low.

```
Drawdown(t) = (Equity(t) − Peak Equity up to t) / Peak Equity up to t
Max Drawdown = the worst (most negative) drawdown over the period
```

If your portfolio went from $100k → $130k → $85k → $110k, the max drawdown was **−34.6%** (from $130k to $85k), even though you ended up with a positive return.

## Worked example — equity curve

```
Month    Equity     Peak       Drawdown
Jan      100,000    100,000     0.0%
Feb      115,000    115,000     0.0%
Mar      130,000    130,000     0.0%   ← high-water mark
Apr      120,000    130,000    −7.7%
May      105,000    130,000   −19.2%
Jun       85,000    130,000   −34.6%   ← max drawdown
Jul      100,000    130,000   −23.1%
Aug      110,000    130,000   −15.4%   ← still underwater
```

You're "underwater" until equity recovers to the prior peak. The **time underwater** matters as much as depth — a 30% drawdown that recovers in 3 months feels different from one that takes 5 years.

## Historical max drawdowns (S&P 500 reference)

| Event | Max DD | Recovery time |
|---|---|---|
| Great Depression (1929–32) | −86% | 25 years |
| 1973–74 oil crisis | −48% | 7 years |
| Dot-com bust (2000–02) | −49% | 7 years |
| Global Financial Crisis (2008) | −57% | 5 years |
| COVID crash (Mar 2020) | −34% | 5 months |
| 2022 inflation cycle | −25% | ~2 years |

## Why it matters more than volatility

[[volatility|Volatility]] tells you the *typical* swing. Drawdown tells you the *worst* you actually felt. A retiree who needs to draw income doesn't care that the long-run mean return is 8% if a 50% drawdown forced them to sell at the bottom.

A simple test: would you have stuck with the strategy through its historical max drawdown? If not, the realistic return for *you* is lower than the backtest implies.

## Recovery math is brutal

| Drawdown | Gain needed to recover |
|---|---|
| −10% | +11% |
| −20% | +25% |
| −33% | +50% |
| −50% | +100% |
| −80% | +400% |

This asymmetry is why **avoiding large drawdowns** matters more than capturing the last 10% of upside.

## Variants worth knowing

- **Max Drawdown (MDD)** — worst peak-to-trough loss in the sample
- **Average Drawdown** — mean of all drawdown values; smoother view
- **Underwater duration** — how long equity stays below peak
- **Calmar ratio** — annualized return ÷ MDD; rewards smooth equity curves
- **Conditional Drawdown at Risk (CDaR)** — average of the worst N% drawdowns

## Decision rules

- **Backtest MDD > your psychological tolerance** → realistic future MDD will likely be worse; don't trade it
- **MDD recovery time > 3 years** → check whether your liquidity needs allow that
- **Live MDD exceeds backtest MDD by >20%** → strategy may be broken; investigate
- **Calmar > 0.5** → reasonable risk-adjusted return given drawdown profile

## In finterm

The Portfolio risk panel shows current drawdown and historical max. Backtesting results report max drawdown alongside [[sharpe-ratio]] — never look at one without the other.
