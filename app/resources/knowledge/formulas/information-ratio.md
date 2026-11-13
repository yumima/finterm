**The Information Ratio (IR)** measures the consistency with which a portfolio manager adds alpha above a benchmark — it is the primary metric for evaluating active management skill.

```
IR = (Rp − Rb) / TE

Rp  = portfolio return (annualized)
Rb  = benchmark return (annualized)
TE  = tracking error — the standard deviation of (Rp − Rb),
      the active return, annualized
```

The numerator is **active return** (alpha); the denominator is **tracking error** (the volatility of that alpha). IR asks: "Are you generating consistent excess return, or just making big bets?"

## Worked example — active equity fund vs S&P 500

```
Fund annual return (Rp)         14.5 %
S&P 500 return (Rb)             12.0 %
Active return (Rp − Rb)          2.5 %
Tracking error (TE)              4.0 %  (annualized std dev of monthly active returns)

IR = 2.5 / 4.0 = 0.63
```

An IR of **0.63** is in the "good active manager" range. The fund earns 63 basis points of active return for every unit of benchmark-relative risk it takes.

## What the result means

| IR range    | Interpretation |
|---|---|
| < 0         | Underperforms benchmark on a risk-adjusted basis |
| 0 – 0.30    | Weak; very few active managers clear this consistently |
| 0.30 – 0.50 | Acceptable; median active fund over full cycles |
| 0.50 – 0.75 | Good; top-quartile active management |
| > 0.75      | Excellent; top-decile; rare over multi-year periods |

A famous rule of thumb: an IR of 0.5 over a 5-year period is exceptional for most liquid equity strategies.

## Variants

- **Generalized Information Ratio** — divides by a risk measure other than tracking error (e.g., semivariance of active returns).
- **Appraisal Ratio** — similar concept but uses alpha from a regression (Jensen's alpha) divided by the residual standard deviation.
- **IR vs Sharpe** — Sharpe measures absolute risk-adjusted return (using Rf); IR measures active management skill (using the benchmark). A high Sharpe fund may have a low IR if the benchmark also performed well.

## Common mistakes

- Confusing a high IR driven by low tracking error with skill — a fund that hugs the benchmark tightly can have a respectable IR on a small positive active return.
- Using a mismatched benchmark — an equity fund benchmarked to a bond index will show a misleadingly high or low IR.
- Treating IR as stationary — it degrades rapidly if the manager's edge becomes crowded or the market regime changes.
- Annualizing tracking error incorrectly — multiply monthly TE by sqrt(12) only when returns are approximately independent.

See also: [[sharpe-ratio|Sharpe Ratio]], [[sortino-ratio|Sortino]], [[r-squared|R-Squared]], [[beta-formula|Beta]].
