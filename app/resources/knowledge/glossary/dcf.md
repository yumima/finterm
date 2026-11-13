A **DCF (Discounted Cash Flow)** analysis estimates the intrinsic value of an asset by projecting its future free cash flows and discounting them back to present value at a rate that reflects the riskiness of those cash flows.

The core logic: a dollar of cash flow in the future is worth less than a dollar today. DCF quantifies exactly how much less, using a discount rate ([[wacc|WACC]] for businesses, required return for projects).

## Formula

```
Intrinsic Value = Σ [FCFₜ / (1 + r)^t] + Terminal Value / (1 + r)^n

FCFₜ = Free cash flow in year t
r    = Discount rate (WACC)
n    = Final explicit forecast year
```

## Worked example — simplified 5-year DCF

```
Company: $500M revenue, growing 10%/year, 20% FCF margin
WACC: 10%

Year 1: FCF = $100M / (1.10)^1 = $90.9M PV
Year 2: FCF = $110M / (1.10)^2 = $90.9M PV
Year 3: FCF = $121M / (1.10)^3 = $90.9M PV
Year 4: FCF = $133M / (1.10)^4 = $90.8M PV
Year 5: FCF = $146M / (1.10)^5 = $90.7M PV

Sum of FCF PVs: ~$454M

Terminal Value (Gordon Growth, 3% perpetuity):
  TV = FCF₅ × (1 + g) / (WACC − g) = $146M × 1.03 / (0.10 − 0.03) = $2,149M
  PV of TV = $2,149M / (1.10)^5 = $1,334M

Enterprise Value = $454M + $1,334M = $1,788M
```

## Key DCF levers

The terminal value typically drives 60–80% of the total value in a growth company DCF — making it simultaneously the most important and most uncertain component.

| Assumption | 1% Change Effect on Value |
|---|---|
| WACC | −15 to −25% value |
| Terminal growth rate | +15 to +25% value |
| Near-term FCF margins | +3 to +5% value per year |

## Two DCF approaches

**FCFF (Free Cash Flow to Firm):** Discount at WACC → Enterprise Value → subtract net debt → Equity Value

**FCFE (Free Cash Flow to Equity):** Discount at cost of equity → Equity Value directly. Better for capital-heavy companies with stable leverage.

## Terminal value methods

1. **Perpetuity growth (Gordon Growth):** TV = FCF × (1+g) / (r−g). Assumes growth forever.
2. **Exit multiple:** TV = EBITDA × Multiple. Anchors value to market comparables.

Using both and triangulating provides a more robust estimate.

## Common DCF mistakes

- **Circular logic**: Using market price to calibrate inputs defeats the purpose of independent valuation.
- **Hockey stick projections**: Near-term optimism that mean-reverts before the terminal year understates dilution.
- **Inconsistent WACC**: Using equity beta for a leveraged company without un-levering/re-levering.
- **Ignoring working capital**: FCF ≠ EBITDA; capex and working capital changes matter enormously.

## Pitfalls

- DCF is highly sensitive to terminal value assumptions — small changes in g or WACC radically change the output.
- "Garbage in, garbage out" — a DCF is only as good as the underlying business assumptions.
- DCFs work best for stable, cash-generative businesses; they are unreliable for early-stage companies with no current cash flows.
- Scenario/sensitivity analysis is essential — present a range, not a single point estimate.

See also: [[wacc|WACC]], [[terminal-value|Terminal Value]], [[fcf|Free Cash Flow]], [[ev-ebitda|EV/EBITDA]], [[free-cash-flow-yield|FCF Yield]].
