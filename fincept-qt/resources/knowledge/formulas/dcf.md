**DCF** converts a stream of future cash flows into a single present value today by discounting each cash flow at the required rate of return — it is the theoretical foundation for all intrinsic-value investing.

```
PV = CF₁/(1+r)¹  +  CF₂/(1+r)²  +  ...  +  CFₙ/(1+r)ⁿ  +  TV/(1+r)ⁿ

CF_t  = free cash flow in year t
r     = discount rate (usually WACC for firm value, Re for equity value)
n     = number of explicit forecast years (typically 5–10)
TV    = terminal value at end of forecast period
```

Terminal value (Gordon Growth):
```
TV = CFₙ × (1 + g) / (r − g)

g  = perpetuity growth rate (typically 2–3 %, near long-run GDP)
```

## Worked example — simplified 5-year DCF

```
Assumptions:
  WACC (r)         = 10 %
  FCF Year 1–5     = 10, 11, 12, 13, 14  ($ billions)
  Terminal growth  = 2.5 %
  Shares out       = 15.5 bn

Year  CF ($bn)   Discount factor    PV ($bn)
  1     10.0      1/(1.10)^1 = 0.909   9.09
  2     11.0      1/(1.10)^2 = 0.826   9.09
  3     12.0      1/(1.10)^3 = 0.751   9.01
  4     13.0      1/(1.10)^4 = 0.683   8.88
  5     14.0      1/(1.10)^5 = 0.621   8.69

Sum of PV (years 1–5)                44.76

Terminal value   = 14.0 × 1.025 / (0.10 − 0.025)
                 = 14.35 / 0.075
                 = $191.3 bn

PV of TV         = 191.3 / (1.10)^5  = $118.8 bn

Enterprise value = 44.76 + 118.8  = $163.6 bn
Equity value     = EV − net debt = 163.6 − 5.0  = $158.6 bn
Intrinsic value  = 158.6 / 15.5  ≈  $10.23 per share
```

## What the result means

If the current stock price exceeds the intrinsic value, the stock is expensive on a DCF basis; if lower, it offers a margin of safety. However, DCF is extraordinarily sensitive to the discount rate and terminal growth assumption — a 1 % change in either can move intrinsic value by 20–30 %.

## Variants

- **Equity DCF** — discounts free cash flow to equity (FCFE) at the cost of equity (Re); directly produces equity value.
- **Firm DCF** — discounts FCFF at WACC; produces enterprise value; subtract net debt for equity value.
- **Dividend Discount Model (DDM)** — special case where dividends are the cash flow; see [[gordon-growth|Gordon Growth Model]].
- **Adjusted Present Value (APV)** — separates unlevered value from tax shield; preferred when capital structure changes materially.

## Common mistakes

- Terminal value often represents 70–80 % of total DCF value — a small change in `g` or `r` dominates the result; always run a sensitivity table.
- Using nominal cash flows with a real discount rate (or vice versa) — keep them consistent.
- Projecting FCF as a percentage of revenue without checking that capex and working capital assumptions are internally consistent.
- Anchoring the terminal growth rate above long-run GDP — implies the company eventually becomes larger than the global economy.

See also: [[wacc|WACC]] for the discount rate, [[free-cash-flow-formula|FCF]] for the cash flow input, [[gordon-growth|Gordon Growth Model]] for terminal value derivation.
