**WACC** is the blended rate of return a company must earn on its existing assets to satisfy all its capital providers — debt holders and equity holders alike.

```
WACC = (E/V) × Re  +  (D/V) × Rd × (1 − Tc)

E   = market value of equity
D   = market value of debt
V   = E + D  (total capital)
Re  = cost of equity (e.g., from CAPM)
Rd  = cost of debt (yield on outstanding bonds or bank debt)
Tc  = corporate tax rate
```

## Worked example — Apple, May 2026

```
Market cap (E)          $2,700 bn
Net debt (D)            $  85 bn
Total capital (V)       $2,785 bn

E/V                     2700 / 2785 = 96.9 %
D/V                     85 / 2785   =  3.1 %

Cost of equity (CAPM)
  Rf = 4.5 %,  β = 1.2,  ERP = 5.5 %
  Re = 4.5 + 1.2 × 5.5  = 11.1 %

Cost of debt (pre-tax)  Rd = 4.8 %
Tax rate                Tc = 21 %
After-tax cost of debt  4.8 × (1 − 0.21) = 3.79 %

WACC = 0.969 × 11.1 + 0.031 × 3.79
     = 10.76 + 0.12
     ≈ 10.9 %
```

Apple's investment projects must return at least **10.9 %** to create value for shareholders.

## What the result means

WACC is the hurdle rate inside a [[dcf|DCF]]: future cash flows are discounted at this rate to produce today's intrinsic value. A lower WACC raises valuation; a higher WACC lowers it.

- **ROIC > WACC** — the company creates economic value (spread is positive).
- **ROIC < WACC** — the company destroys value even while posting positive GAAP profits.
- **ROIC = WACC** — break-even; management earns exactly what was required.

## Variants

- **Levered vs unlevered WACC** — strip out the debt shield to get the unlevered (all-equity) cost; useful for comparing capital structures.
- **Divisional WACC** — conglomerates should use a division-specific beta rather than the corporate beta.
- **Adjusted Present Value (APV)** — an alternative to WACC that explicitly separates the unlevered value from the tax shield value.

## Common mistakes

- Using book-value weights instead of market-value weights — for leveraged firms this overstates the debt proportion and understates WACC.
- Forgetting the tax shield: after-tax cost of debt = Rd × (1 − Tc). Ignoring it artificially inflates the debt component.
- Applying one corporate WACC to every division — a high-growth tech division and a mature industrial division have very different risk profiles.
- Holding WACC constant across all projection years in a DCF — capital structure and rates change; a terminal WACC that differs from the near-term WACC is often more realistic.

See also: [[capm|CAPM]] for deriving Re, [[dcf|DCF]] for how WACC is applied, [[beta-formula|Beta]] for the β input.
