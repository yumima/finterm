**Return on Equity (ROE)** measures how efficiently a company generates profit from shareholders' capital — it is the single most-cited measure of management's ability to allocate equity.

```
ROE = Net Income / Average Shareholders' Equity

Average Shareholders' Equity = (Beginning Equity + Ending Equity) / 2
```

## DuPont Decomposition

The original DuPont formula decomposes ROE into three drivers:
```
ROE = Net Profit Margin × Asset Turnover × Equity Multiplier
    = (Net Income / Revenue) × (Revenue / Assets) × (Assets / Equity)
```

Extended 5-factor DuPont:
```
ROE = Tax Burden × Interest Burden × EBIT Margin × Asset Turnover × Leverage

Tax Burden       = Net Income / Pre-tax Income    (= 1 − effective tax rate)
Interest Burden  = Pre-tax Income / EBIT
EBIT Margin      = EBIT / Revenue
Asset Turnover   = Revenue / Average Assets
Leverage         = Average Assets / Average Equity
```

## Worked example — Apple vs Meta, 2025

```
                Apple           Meta
Net income      $93.7 bn        $62.4 bn
Avg equity      $71.6 bn        $153.0 bn

ROE             93.7 / 71.6     62.4 / 153.0
                = 130.9 %       = 40.8 %
```

Apple's extraordinary ROE reflects a combination of high net margins, aggressive share buybacks (which reduce equity), and strong asset efficiency. Meta's ROE is still excellent by any normal standard.

DuPont for Apple (approximate):
```
Net margin     25.3 %  (93.7 / 370)
Asset turnover  0.98×  (370 / 376)
Equity mult.    5.25×  (376 / 71.6)

ROE = 0.253 × 0.98 × 5.25 ≈ 130 %
```

The high equity multiplier (leverage) is the product of Apple's debt issuance and buybacks — not financial distress.

## What the result means

ROE benchmarks vary by industry:

| Sector              | Typical ROE  |
|---|---|
| Technology          | 20 – 50 %+   |
| Consumer staples    | 20 – 35 %    |
| Healthcare          | 15 – 25 %    |
| Industrials         | 12 – 20 %    |
| Utilities           | 8 – 13 %     |
| Banks               | 10 – 15 %    |
| Capital-intensive   | 5 – 12 %     |

Sustainable competitive advantage (moat) often shows up as consistently above-average ROE over 5–10 years.

## Variants

- **ROCE (Return on Capital Employed)** — uses EBIT / (Total Assets − Current Liabilities); ignores capital structure; better for comparing firms with different leverage.
- **ROIC (Return on Invested Capital)** — NOPAT / Invested Capital; the cleanest measure of value creation; compare to [[wacc|WACC]] to determine if the company creates or destroys value.
- **ROA (Return on Assets)** — Net Income / Average Total Assets; eliminates leverage from the picture.

## Common mistakes

- High ROE from high leverage is not the same as high ROE from high margins — DuPont decomposition is essential to distinguish quality ROE from levered ROE.
- Negative equity (common after aggressive buybacks like Apple) makes ROE meaningless or misleading — use ROIC instead.
- Comparing ROE across sectors without adjustment — a 12 % ROE is excellent for a utility and poor for a software company.
- Not averaging equity — using ending equity instead of average equity overstates ROE when the company grew its equity significantly during the year.

See also: [[wacc|WACC]] (compare ROIC to WACC), [[free-cash-flow-formula|FCF]], [[pe-formula|P/E]], [[roe-formula|ROE in DuPont]].
