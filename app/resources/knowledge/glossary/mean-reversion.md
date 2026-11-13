**Mean reversion** is the tendency of an asset price, ratio, or spread to return toward its historical average after deviating from it — the principle that extreme values in financial data are often temporary and followed by a move back toward the mean.

Mean reversion and [[momentum|momentum]] represent opposing forces in markets. At short time horizons (minutes to days) and long time horizons (multi-year), prices tend to mean-revert; at intermediate horizons (months), momentum tends to dominate.

## Where mean reversion applies

| Domain | Reversion Period | Examples |
|---|---|---|
| Intraday prices | Minutes to hours | Pairs trading, stat arb |
| Volatility | Days to weeks | Short VIX strategies |
| Valuations | Years to decades | CAPE, P/B ratio reversion |
| Credit spreads | Months | HY spread mean reversion |
| Macro variables | Years | CPI, unemployment, GDP gap |
| Interest rates | Long cycles | Rate normalization |

## Statistical framework

Mean reversion is characterized by an **Ornstein-Uhlenbeck (OU) process**:

```
dX_t = κ(μ − X_t) dt + σ dW_t

κ = speed of mean reversion
μ = long-run mean
σ = volatility
```

Higher κ means faster mean reversion. Financial analysts estimate κ and μ from historical data to model expected reversion.

## Worked example — pairs trading

```
Two highly correlated stocks: JPM and BAC
Spread = ln(JPM price) − ln(BAC price)
Historical spread mean: 0.15
Historical spread std dev: 0.05

Current spread: 0.28 → 2.6 standard deviations above mean

Trade: Short JPM, Long BAC
Thesis: Spread will revert to 0.15 → profit from convergence
Stop loss: Spread > 0.35 (reversion thesis broken)
```

## Valuation mean reversion

Shiller CAPE ratio shows long-run mean reversion in valuations:

```
US CAPE long-run average: ~17×
2000 peak CAPE: ~44× → 10yr subsequent returns near zero
2009 trough CAPE: ~13× → 10yr subsequent returns excellent
2024 CAPE: ~35× → implies below-average future returns on historical patterns
```

Valuations mean-revert, but on very long timescales (5–20 years) — not useful for short-term trading.

## Mean reversion in volatility

Implied volatility (VIX) is highly mean-reverting:

```
VIX long-run average: ~19
VIX during COVID (March 2020): 82.69 → reverted to ~15 within 6 months
VIX during calm (2017): ~10 → reverted upward over time
```

Short volatility strategies (selling options) exploit this reversion. The risk: reversion can take time, and during drawdown periods, the VIX can stay elevated for months.

## Half-life of mean reversion

```
Half-life = ln(2) / κ

For a half-life of 30 days: κ = ln(2)/30 ≈ 0.023
Meaning: the spread closes half the gap vs. the mean in 30 days on average
```

## Pitfalls

- Mean reversion strategies suffer catastrophic losses when the "mean" itself shifts permanently — regime changes, structural breaks, or paradigm shifts cause long-term divergences.
- "This time it's different" is often wrong; occasionally it's right. The challenge is distinguishing temporary deviation from permanent structural change.
- Averaging down in stocks assuming mean reversion can lead to large losses if the company is structurally deteriorating.
- Mean reversion in individual stocks is much weaker than in spreads, ratios, and aggregate market data.

See also: [[momentum|Momentum]], [[volatility|Volatility]], [[vix|VIX]], [[correlation|Correlation]], [[sharpe-ratio|Sharpe Ratio]].
