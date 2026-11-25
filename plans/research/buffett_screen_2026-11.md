# Buffett Value screen (2026-11-25)

A good company at a cheaper price, as rules. Code: `buffett_facts`, `buffett_quality`, `buffett_multiple`,
`BUFFETT` in `app/scripts/screener_data.py`. The live screen and the backtest import the same functions.

## Where each rule comes from

| Rule | Source |
|---|---|
| A profit in each of 10 fiscal years | 1986 letter: "demonstrated consistent earning power" |
| Free cash flow positive in 9 of 10 | 1986 letter: owner earnings, not reported earnings |
| ROE ≥ 15% | 1987 letter: "no year worse than 15%" (there a ten-year test; the live feed has no equity history) |
| ROIC ≥ 12%, total debt ≤ 4 years of earnings | 1986 letter: "good returns on equity while employing little or no debt"; both numbers are ours |
| Earnings growth ≥ 3% a year; worst gross margin ≥ 80% of the median; TTM earnings ≥ 80% of the 3-year average | ours: growth above inflation, a stable margin as the trace of a moat, no turn-around (1986 letter) |
| Owner earnings = min(3-year FCF, 3-year net income) | 1986 letter's formula; total capex stands in for maintenance capex (strict for growers), the lower of the two keeps share-based pay out |
| Value = 10 years at the company's own growth, 0–8%, then 3%, at 10% | 1992 letter (discounted cash); 10% from the 1991 letter's example; the 8% and 3% are ours |
| Ranked by margin of safety, at most 30, none above value | 1992 letter. Buffett gives no number for the margin, so none is a cut: the list shows it |

Not measurable: understanding the business, its future, its managers.

## Measured

Point-in-time S&P 500, SEC annual filings visible 120 days after period end, monthly, 10 bp a side,
2015-03 to 2026-08 (138 months that held names; XBRL starts in 2009, so 7–10 years of history).

| | excess a year vs equal weight | t | median names |
|---|---:|---:|---:|
| Buffett Value (quality, at or below value) | −0.1% | −0.03 | 7 |
| All quality passers, any price | −0.9% | −0.52 | 30 |
| Wait list (quality, not on the list) | −0.8% | −0.35 | 25 |

No evidence that it beats the index, like every screen here. Cheaper than the estimate did better than quality
alone, inside noise. 3.1% of wait-list names were on the list a month later (others: 0.1%).

The thresholds are the first ones written and were not tuned. Two errors were corrected after the first run
(+0.4%, t 0.13): a filer that tags no debt was read as debt-free (Comcast, DaVita), and the gross-margin median of
an even count took the upper of the middle pair.

## Effect on the other records

Their rules and code are unchanged. SEC's ticker→CIK file was fetched again, which changed a few members'
fundamentals: Trending Value +0.68% → +0.79%, Magic Formula +0.21% → +0.15% (2023–26: −3.4% → −3.7%), Quality +
Momentum +1.03% → +1.02%; not shipped: Piotroski in the cheapest 30% −1.6% → −1.1%. No t moved by more than 0.1.
The setting-up lists were measured again on the same data. `fund_collect.py` now keeps tickers SEC has dropped, and
`backtest.py` drops the last month by whether the data reaches its end, not by today's date.
