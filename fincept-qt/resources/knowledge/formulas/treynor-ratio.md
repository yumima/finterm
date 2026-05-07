**The Treynor ratio** measures risk-adjusted return using beta (systematic risk) as the denominator instead of total volatility — it is the appropriate metric for evaluating a single fund that is one component of a diversified portfolio.

```
Treynor = (Rp − Rf) / βp

Rp  = portfolio return (annualized)
Rf  = risk-free rate (annualized)
βp  = beta of the portfolio vs the market benchmark
```

The logic: if a portfolio is fully diversified, the only risk that matters (and that gets compensated) is market risk (beta). Total volatility includes idiosyncratic risk that can be diversified away at no cost.

## Worked example — two funds in a broader portfolio

```
                Fund A      Fund B
Annual return   13.0 %      11.0 %
Rf              4.5 %        4.5 %
Beta (βp)       0.80         0.50
Sharpe          0.94         0.85

Treynor:
Fund A:  (13.0 − 4.5) / 0.80  = 8.5 / 0.80  = 10.6
Fund B:  (11.0 − 4.5) / 0.50  = 6.5 / 0.50  = 13.0
```

Fund B has a lower Sharpe but a higher Treynor — it earns more excess return per unit of systematic risk. If you hold Fund A or B *inside* a diversified portfolio, Fund B is the better choice.

## What the result means

Treynor is best used to compare funds that will be combined in a larger portfolio. When comparing standalone investments (the portfolio *is* the investment), use [[sharpe-ratio|Sharpe]] instead.

A higher Treynor indicates superior risk-adjusted performance per unit of market exposure. Like Sharpe, Treynor values are only interpretable relative to each other or the market Treynor (which equals ERP by definition).

## Variants

- **Jensen's Alpha** — related concept: regress portfolio returns on the market and measure the intercept. Alpha > 0 means the manager beat the market on a risk-adjusted basis after accounting for beta.
- **M² (Modigliani-Modigliani)** — converts Sharpe into a return figure for easier communication: "What return would this portfolio have earned if it had the same volatility as the market?"

## Common mistakes

- Using Treynor for a portfolio that is the investor's *only* investment — in that case, total volatility matters and Sharpe is the right tool.
- Using a beta estimated over too short a period — 60-month betas are standard; shorter windows produce unstable estimates.
- Comparing Treynor across portfolios benchmarked to different indices — ensure beta is measured against the same reference market.
- A positive Treynor in a bear market may simply reflect a low (or negative) beta, not genuine skill.

See also: [[sharpe-ratio|Sharpe Ratio]], [[sortino-ratio|Sortino]], [[beta-formula|Beta]], [[capm|CAPM]].
