**Tracking error** is the divergence between an ETF's (or index fund's) returns and the returns of its benchmark index — a measure of how faithfully the fund replicates its target index.

A lower tracking error indicates the fund closely mirrors its benchmark. A higher tracking error means the fund's returns deviate meaningfully from what the index delivered.

## Two definitions in common use

**1. Tracking difference (ex-post, most practical):**
```
Tracking Difference = Fund Return − Index Return (over a period)

Negative = fund underperformed index
Positive = fund outperformed index (rare, usually from securities lending)
```

**2. Tracking error (volatility-based, risk-focused):**
```
TE = Standard Deviation of (Fund Daily Return − Index Daily Return)
```

Annualized tracking error: multiply daily standard deviation by √252.

## Worked example

```
Benchmark (S&P 500) annual return:  +12.0%
SPY ETF annual return:              +11.93%
Tracking difference:                −0.07%

IVV ETF annual return:              +11.97%
Tracking difference:                −0.03%

Daily return deviations (rolling):
SPY TE (annualized):  ~0.04%  (excellent)
Active ETF TE:        ~2.5%   (high — significant active positioning)
```

## Sources of tracking error

| Source | Impact | Explanation |
|---|---|---|
| Expense ratio | Negative (always) | Fees reduce returns vs. index |
| Cash drag | Negative | Uninvested cash earns less than index |
| Sampling | Variable | Holding only a subset of index securities |
| Dividend timing | Small | Dividends received after index ex-date |
| Rebalancing costs | Negative | Transaction costs on index changes |
| Securities lending income | Positive (sometimes) | Lending holdings offsets costs |
| Swap/derivative structure | Variable | Synthetic ETFs track swap prices |

## Why tracking error matters

- **Passive investors** want the index return, not deviations. Even small persistent underperformance compounds significantly over decades.
- **Smart beta / factor ETFs** have higher tracking error by design — they're expected to differ from the cap-weighted index.
- **Active ETFs** may have tracking errors of 2–5%+ — high error shows active management; low error shows closet indexing.

## Tracking difference vs. tracking error

These are different:
- **Tracking difference**: total return gap between fund and index (the "cost").
- **Tracking error**: volatility/consistency of that gap (the "reliability").

A fund can have good tracking error (consistent deviation) but bad tracking difference (systematic underperformance).

## Pitfalls

- Low tracking error can occur alongside large tracking difference — the fund consistently underperforms by a stable amount.
- Comparing tracking error across different time periods or market regimes is misleading; crisis periods inflate all tracking errors.
- Securities lending income can make a fund's tracking difference better than its expense ratio — the fund appears to beat the index.
- Synthetic ETFs (holding swaps not securities) can have very low tracking error under normal conditions but introduce counterparty risk.

See also: [[nav-premium-discount|NAV Premium/Discount]], [[expense-ratio|Expense Ratio]], [[authorized-participant|Authorized Participant]], [[beta|Beta]].
