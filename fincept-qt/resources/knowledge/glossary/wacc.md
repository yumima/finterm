**WACC (Weighted Average Cost of Capital)** is the blended rate of return a company must earn on its assets to satisfy both debt and equity investors — the "hurdle rate" that a project must beat to create value.

It weights the cost of each capital source (equity, debt) by its proportion of the total capital structure. It is the discount rate used in DCF valuations and capital allocation decisions.

## Formula

```
WACC = (E/V) × Re + (D/V) × Rd × (1 − Tax Rate)

Where:
E = Market value of equity
D = Market value of debt
V = E + D (total capital)
Re = Cost of equity
Rd = Cost of debt (pre-tax)
(1 − Tax Rate) = Tax shield on interest (debt is tax-deductible)
```

## Cost of equity (CAPM)

```
Re = Rf + β × (Rm − Rf)

Rf = Risk-free rate (10yr Treasury)
β  = Equity beta (market sensitivity)
Rm − Rf = Equity risk premium (ERP, typically 4–6%)
```

## Worked example — AAPL

```
Market cap (E):         $2.8T
Total debt (D):         $95B
V = E + D:              $2.895T
E/V:                    96.7%
D/V:                    3.3%

Cost of equity (Re):    Rf (4.5%) + 1.2 (β) × 5.0% (ERP) = 10.5%
Cost of debt (Rd):      3.8% (weighted average of bonds outstanding)
Tax rate:               15%

WACC = (0.967 × 10.5%) + (0.033 × 3.8% × 0.85)
     = 10.16% + 0.11%
     = 10.27%
```

Apple must earn at least 10.27% on its invested capital to create value for shareholders.

## Capital structure effect on WACC

Debt is cheaper than equity (interest < equity return expectations), and interest is tax-deductible. Adding debt (up to a point) lowers WACC:

| Leverage | Effect |
|---|---|
| Zero debt | Higher WACC (all expensive equity) |
| Moderate debt | Lower WACC (tax shield benefit) |
| Excessive debt | Higher WACC (financial distress premium outweighs tax shield) |

The "optimal" capital structure minimizes WACC and maximizes firm value — this is Modigliani-Miller with taxes.

## WACC in DCF valuation

In a DCF model, WACC is the discount rate applied to **free cash flows to the firm (FCFF)**:

```
Enterprise Value = Σ FCFFₜ / (1 + WACC)^t + Terminal Value / (1 + WACC)^T
```

A 1% change in WACC can shift enterprise value by 15–25% — making WACC the most sensitive assumption in a DCF.

## Pitfalls

- WACC uses current market weights, not book values — using book debt/equity ratios is incorrect.
- Beta is backward-looking; future risk may differ from historical sensitivity.
- Equity risk premium (ERP) is debated; estimates range from 4% to 6%; a 1% difference changes WACC by ~0.7% for equity-heavy companies.
- WACC assumes constant capital structure; in LBOs and leveraged companies, the structure changes significantly over time — APV (Adjusted Present Value) is more appropriate.

See also: [[dcf|DCF]], [[beta|Beta]], [[terminal-value|Terminal Value]], [[fcf|Free Cash Flow]], [[lbo|LBO]].
