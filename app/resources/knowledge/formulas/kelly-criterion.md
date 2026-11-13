**The Kelly Criterion** calculates the theoretically optimal fraction of capital to risk on a single bet in order to maximize long-run compounded wealth — it is the bridge between probability theory and position sizing.

```
f* = (bp − q) / b

f*  = fraction of capital to wager (0 to 1)
b   = net odds (profit per unit risked; e.g., b = 2 means win $2 per $1 risked)
p   = probability of winning
q   = probability of losing  =  1 − p
```

Equivalently, for continuous returns:
```
f* = (μ − Rf) / σ²

μ   = expected return of the asset
Rf  = risk-free rate
σ²  = variance of returns
```

## Worked example — a trade with defined risk/reward

```
Setup: long AAPL calls
Win probability (p)   = 0.55
Lose probability (q)  = 0.45
Net odds (b)          = 1.5   (win $1.50 per $1 risked)

f* = (1.5 × 0.55 − 0.45) / 1.5
   = (0.825 − 0.45) / 1.5
   = 0.375 / 1.5
   = 0.25  (25 % of capital)
```

Kelly says to risk **25 %** of capital on this trade. However, practitioners nearly always use a fraction of Kelly (commonly half-Kelly: 12.5 %) to reduce volatility and protect against estimation error.

## What the result means

Kelly maximizes the expected value of log(wealth), which maximizes long-run compounded growth. Key properties:

- **f* > 0** — positive edge; bet this fraction.
- **f* = 0** — no edge; do not bet.
- **f* < 0** — negative expected value; bet is unfavorable.
- **f* > 1** — leverage is required; very rare and dangerous in practice.

Overbetting Kelly (using f > f*) produces lower long-run returns than Kelly and increases ruin probability. Underbetting is suboptimal but safe.

## Variants

- **Fractional Kelly** — use f* × k where k = 0.25 to 0.5; trades lower expected growth for much lower volatility and sensitivity to parameter estimation error.
- **[[position-size-formula|Fixed fractional sizing]]** — simpler cousin; risk a fixed percentage of capital per trade regardless of edge (e.g., always 1–2 %).
- **Continuous-time Kelly** — the continuous extension produces the same f* = (μ − Rf) / σ² result from Modern Portfolio Theory.

## Common mistakes

- Treating estimated probabilities as exact — Kelly is extremely sensitive to p and b inputs; a small overestimate of edge causes significant overbetting.
- Using full Kelly in leveraged or illiquid instruments — even correct parameters produce terrifying drawdowns at full Kelly.
- Ignoring correlation across simultaneous positions — multi-asset Kelly requires a covariance matrix, not independent application to each position.
- Applying Kelly to strategies with non-stationary edge — an edge that degrades over time does not have a fixed p to plug in.

See also: [[position-size-formula|Position Sizing]], [[var-parametric|VaR]], [[max-drawdown|Max Drawdown]], [[sharpe-ratio|Sharpe Ratio]].
