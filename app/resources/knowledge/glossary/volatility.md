# Volatility

The standard deviation of an asset's returns — a measure of how much prices vary from their average.

```
Daily vol  = stdev of daily log returns
Annual vol = daily vol × √252   (252 trading days/year)
```

A stock with **20% annual volatility** typically moves ±1.26% per day (1σ), and ±2.5% per day on rough days (2σ).

## Worked example — back-of-envelope

If AAPL has 25% annual volatility, what's a typical day?

```
Daily vol  = 25% / √252  ≈  1.57%
On any given day, expect ±1.57% (1σ).
~5% of days will move >2.5% (2σ).
~0.3% of days will move >4.7% (3σ).
```

For a 1-day VaR at 95% confidence on a $100k AAPL position:
```
VaR ≈ 1.65 × 1.57% × $100k ≈ $2,590
```

## Two flavors

- **Historical / realized volatility (HV)**: computed from past prices. Backward-looking and concrete.
- **[[implied-volatility|Implied volatility (IV)]]**: extracted from option prices. Forward-looking — what the market *expects* future volatility to be.

When IV > HV, options are pricing in *more* movement than has recently occurred — often because something is coming up (earnings, a Fed decision, an FDA ruling).

## Typical annual volatility by asset

| Asset class | Annual vol |
|---|---|
| 10-year US Treasury | 5–8% |
| Investment-grade bonds | 4–7% |
| S&P 500 (long-run) | 15–18% |
| Single large-cap stock | 20–35% |
| Single mid-cap | 30–45% |
| Single small-cap | 40–60% |
| EM equities | 22–30% |
| Gold | 14–18% |
| Crude oil | 30–45% |
| Bitcoin | 60–80% |
| Penny stocks / micro-caps | 70%+ |

## Variants worth knowing

- **Realized volatility (RV)** — computed from observed price history
- **Implied volatility (IV)** — extracted from option prices
- **Conditional / GARCH volatility** — model that adjusts for vol clustering
- **Parkinson / Garman-Klass** — uses daily high/low/open/close, more efficient than close-to-close
- **Vol-of-vol** — volatility of volatility itself; matters for VIX derivatives

## Vol ≠ risk, but they're cousins

Volatility is one *component* of risk:
- It measures dispersion, not direction.
- Big upside days count as "vol" too.
- It assumes returns are roughly normal — they aren't (fat tails).

For risks like permanent capital loss, [[drawdown]], [[var|Value at Risk]], and stress tests give you views volatility doesn't.

## Volatility regimes (S&P 500 / VIX)

- **Low vol** (VIX 10–15): complacent markets, options cheap to buy, often precedes regime change
- **Normal** (VIX 15–25): typical
- **Elevated** (VIX 25–40): something is happening
- **Crisis** (VIX 40+): GFC peak ~80, COVID peak ~85

## Decision rules

- **Position sized to dollar vol**: target equal $-risk per name (size ∝ 1/vol)
- **Vol expanding rapidly + correlation rising** → de-risk; tail-event setup
- **HV << IV before catalyst** → option premium is rich for sellers; vol crush risk for buyers
- **HV >> IV without catalyst** → buy-side opportunity in options if you have a directional view

See [[vix|VIX]] for the standard equity-market vol benchmark.
