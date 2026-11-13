# Beta

A measure of how much a stock moves **relative to the broader market**.

```
Beta = Covariance(stock returns, market returns) / Variance(market returns)
```

In practice, it's the slope of the regression line when you plot the stock's daily returns against the market's daily returns over a rolling window (commonly 1–5 years).

## Worked example

Suppose you regress AAPL's daily returns against SPY's daily returns over the past 3 years and get a slope of **1.2**. That means: on average, when SPY moves +1%, AAPL moves +1.2%. On a day SPY is down 2%, AAPL is expected to be down ~2.4%.

For a $1M portfolio entirely in AAPL, the dollar exposure to a 1% market move is **$12,000**. To neutralize this market risk, you'd short ~$1.2M of SPY (assuming SPY beta is 1.0).

## How to read it

- **β = 1.0** → moves in line with the market on average
- **β = 1.5** → tends to move 1.5× as much as the market (both up and down)
- **β = 0.5** → tends to move half as much
- **β = 0** → uncorrelated with the market (rare, often spurious)
- **β < 0** → tends to move *opposite* (gold sometimes; very few stocks)

## Sector-typical betas

| Sector | Typical beta |
|---|---|
| Utilities | 0.4–0.7 |
| Consumer staples | 0.5–0.8 |
| Healthcare | 0.7–1.0 |
| Industrials | 0.9–1.2 |
| Tech (mature) | 1.0–1.4 |
| Tech (growth) | 1.3–1.8 |
| Financials | 1.1–1.5 |
| Energy | 0.9–1.4 (oil dependent) |
| Biotech / speculative | 1.5–2.5+ |

## Variants worth knowing

- **Levered beta** — observed market beta (default)
- **Unlevered beta (asset beta)** — strips out leverage; useful for cross-comparison and DCFs
- **Adjusted beta** — Bloomberg-style: `0.67 × raw + 0.33 × 1.0` (mean-reverts toward 1.0)
- **Downside beta** — beta computed only on down days; useful for tail-risk thinking

## Where you'll use it

- **Portfolio risk**: a portfolio's overall beta tells you sensitivity to market swings.
- **Hedging**: shorting an index proxy in proportion to your beta neutralizes market exposure.
- **Cost of equity (CAPM)**: `Re = Rf + β × (Rm − Rf)` — beta drives the equity discount rate
- **Position sizing**: high-beta names get smaller weight to equalize dollar risk

## Decision rules

- **Defensive portfolio** target beta: 0.6–0.8
- **Balanced portfolio** target beta: 0.9–1.1
- **Aggressive portfolio** target beta: 1.2–1.4
- **Beta drift > 0.3 across regime change** → re-estimate

## Pitfalls

- Beta is **a backward-looking statistic**. Past beta is not future beta — particularly across regime changes (interest rates, business model shifts).
- A short estimation window is noisy; a long window may include regimes that no longer apply.
- Beta tells you about *systematic* risk only. A stock with low beta can still blow up on company-specific news.
- Low-volume stocks have noisy beta estimates — large standard errors.

## Beta ≠ volatility

A high-volatility stock can have low beta if its movements are uncorrelated with the market. Beta tells you co-movement direction; [[volatility]] tells you absolute swing size. Both matter for portfolio construction.
