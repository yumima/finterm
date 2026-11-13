# Return on Equity (ROE)

```
ROE = Net Income / Shareholder Equity
```

The percentage return a company generates on the equity capital its shareholders have invested.

If a company earned $20M on $100M of equity, its ROE is 20% — every dollar of equity produced 20¢ of profit last year.

## Worked example — Apple

```
Net income (TTM):       $99.8B
Shareholder equity:     $63.0B   (after years of buybacks)
ROE:                    99.8 / 63.0  ≈  158% (!!)

This number is artificially high because Apple has bought back so much
stock that book equity is tiny relative to earnings. ROE is real, but
unhelpful for comparison.

Use return on assets (ROA) or return on invested capital (ROIC) for a
fairer cross-company picture:
  ROA  = $99.8B / $352B total assets  ≈  28.4%
  ROIC = $99.8B / $215B (debt + equity)  ≈  46.4%
```

## How to read it

- **< 5%** → poor; capital may be better deployed elsewhere
- **10–15%** → solid for most industries
- **15–25%** → very good; a meaningful competitive advantage usually present
- **> 25% sustainable** → outstanding (think wide-moat consumer or software)
- **> 50% sustainable** → suspicious — usually leverage, buybacks, or accounting

## Sector-typical ROE

| Sector | Typical ROE |
|---|---|
| Banks | 10–15% |
| Utilities | 8–12% |
| Industrials | 12–20% |
| Consumer staples (quality) | 20–35% |
| Big tech | 25–40% (some buyback-distorted, like AAPL) |
| Insurance | 8–15% |
| Pharma (large) | 15–25% |
| REITs | use FFO/equity instead |

## Why high ROE matters

A company that consistently earns 20% on equity can compound retained earnings at 20% — without raising new capital. Over 20 years, that's a ~38× growth in book value (and usually share price). This is the engine behind long-term compounders.

## DuPont decomposition — where ROE comes from

```
ROE = (Net Income / Sales) × (Sales / Assets) × (Assets / Equity)
    =     Net Margin       ×   Asset Turnover  ×    Leverage
```

Same 18% ROE can come from very different sources:

| Company | Margin | Asset turnover | Leverage | ROE |
|---|---|---|---|---|
| Wal-Mart | 3% | 2.5× | 2.4× | 18% |
| Coca-Cola | 22% | 0.5× | 1.6× | 18% |
| US bank | 25% | 0.05× | 14× | 18% |

All three have 18% ROE but completely different businesses. Use DuPont to know which.

## Where ROE misleads

- **Leverage inflates ROE.** A company that funds itself with debt has less equity, mechanically raising ROE — without improving the business.
- **Buybacks reduce equity** → ROE rises even with flat earnings.
- **Goodwill** can swamp tangible equity. Some analysts prefer **return on tangible equity (ROTE)** for banks and acquisitive firms.
- **Negative equity** (after huge buybacks or losses) makes ROE undefined or backwards.

## Variants worth knowing

- **ROA** — Return on Assets; strips out leverage effect
- **ROIC** — Return on Invested Capital (debt + equity); the cleanest cross-company metric
- **ROCE** — Return on Capital Employed; UK/European cousin of ROIC
- **ROTE** — Return on Tangible Equity; excludes goodwill/intangibles, used for banks
- **CROIC** — Cash ROIC; uses FCF instead of net income

## Decision rules

- **ROE > 15% sustained for 5+ years + low leverage** → quality compounder
- **ROE > 20% with leverage > 4×** → bank or financial; check loan-loss reserves
- **ROE > 30% with goodwill > tangible book** → look at ROTE for the real picture
- **ROE declining year-over-year + revenue rising** → margin compression; investigate
- **Negative equity + positive ROE math** → undefined; ignore the number

## Pair with

- [[pb-ratio]] — high ROE justifies a higher P/B (`Justified P/B ≈ ROE / cost of equity`)
- [[ev-ebitda]] — for capital-intensive comparisons
- ROIC is generally a more honest cross-company metric than ROE
