# Alpha

The return a portfolio (or manager) generates **above what the market would predict** given the risk taken.

```
Alpha = Actual Return − [Risk-free Rate + Beta × (Market Return − Risk-free Rate)]
```

If the model says "given your beta, you should have earned 8%," and you earned 11%, your **alpha is +3%**.

## Worked example

```
Your portfolio return (1y):     14.0%
S&P 500 return (1y):            10.0%
Risk-free rate (1y T-bill):      4.0%
Your portfolio's beta to SPX:    1.1

Expected return (CAPM):
  4.0% + 1.1 × (10.0% − 4.0%)  =  10.6%

Alpha = 14.0% − 10.6%  =  +3.4%
```

You earned 3.4 percentage points more than your beta exposure would predict — apparent skill (or luck) of 3.4% over the year.

## What it actually measures

Alpha is the *unexplained* return — the part not accounted for by exposure to the broader market. Loosely, it's the question "did the manager add value beyond just taking on more risk?"

## Reality check — alpha through history

| Group | Annualized alpha (after fees) |
|---|---|
| Average active US large-cap fund (15y) | −1.0% to −2.0% |
| Top-quartile active fund (10y, before survivor bias) | +0.5% to +2.0% |
| Renaissance Medallion Fund (since 1988, gross) | +66% (closed to outsiders) |
| Berkshire Hathaway (1965–2024) | ~+10% (vs S&P) |
| Hedge fund index (last 10y) | ~0% to slightly negative |

True repeatable alpha is rare. Most apparent alpha is one of:

- **Hidden beta exposure** to a different factor (small-cap, value, momentum) that just hasn't been measured
- **Luck over a short window**
- **Survivorship bias** — funds that lost get closed and forgotten

## Variants worth knowing

- **Jensen's alpha** — original CAPM-based form (above)
- **Multi-factor alpha** — alpha after controlling for size, value, momentum, quality (Fama-French 3/5-factor)
- **Information Ratio** — alpha divided by tracking error; alpha "per unit of active risk"

## Decision rules

- **1-year alpha** → ignore as standalone evidence
- **5y consistent alpha across bull and bear markets** → meaningful
- **Multi-factor alpha (after Fama-French) > 0** → harder-to-fake signal
- **Alpha shrinks when scaled** → capacity issue; signal may not survive larger AUM

## Use it carefully

- A 1-year alpha number is mostly noise.
- 5+ years of consistent alpha across regimes is a much stronger signal — but still no guarantee.
- Always check what benchmark was used. A US large-cap manager benchmarked against a global index will *look* like an alpha generator if US large-caps outperformed.
- Alpha after fees vs gross — only after-fee alpha is what investors pocket.

## Related

- [[beta]] — the part of return attributed to market exposure
- [[sharpe-ratio]] — total risk-adjusted performance, not just market-adjusted
