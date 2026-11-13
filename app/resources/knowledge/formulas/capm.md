**CAPM** calculates the minimum return an investor should demand from a stock given its systematic (market) risk — it is the standard formula for the cost of equity in a [[wacc|WACC]] calculation.

```
Re = Rf + β × ERP

Re   = expected return on the equity (= cost of equity)
Rf   = risk-free rate (typically the 10-year Treasury yield)
β    = beta of the stock vs the market portfolio
ERP  = equity risk premium (expected market return − Rf)
```

## Worked example — Apple, May 2026

```
Risk-free rate (Rf)         4.5 %   (10-year UST yield)
Apple beta (β)              1.20    (5-year monthly, vs S&P 500)
Equity risk premium (ERP)   5.5 %   (Damodaran implied ERP, Jan 2026)

Re = 4.5 + 1.20 × 5.5
   = 4.5 + 6.6
   = 11.1 %
```

An investor should require at least **11.1 %** annualized return from AAPL before it clears the risk-adjusted hurdle. Anything less means they're undercompensated for taking on Apple's level of market risk.

## What the result means

CAPM produces a single expected return that serves as the cost of equity in valuation models. The key driver is beta:

- **β = 1.0** — stock moves in line with the market; Re = Rf + ERP.
- **β > 1.0** — stock amplifies market moves (more volatile); higher required return.
- **β < 1.0** — stock dampens market moves (defensive); lower required return.
- **β < 0** — stock is negatively correlated with the market (rare); acts as a hedge.

## Variants

- **Fama-French three-factor model** — adds a size premium (SMB) and value premium (HML) on top of CAPM's single market factor. More explanatory power, more complexity.
- **Carhart four-factor** — Fama-French plus a momentum factor.
- **Country risk premium** — for emerging-market equities, add a sovereign spread to ERP to capture political and currency risk.
- **Build-up method (private companies)** — replaces beta with industry risk and company-specific size premiums when market-based beta is unavailable.

## Common mistakes

- Using the 3-month T-bill as the risk-free rate in a long-horizon DCF — maturity should match the duration of the cash flows (use the 10-year Treasury).
- Taking beta from a 1-year daily regression — too noisy; 3–5 year monthly betas are standard.
- Applying a single global ERP to all geographies — country risk premiums vary meaningfully.
- Confusing expected return with actual return — CAPM gives an ex-ante expectation; realized returns will differ.

See also: [[wacc|WACC]] (where Re is used), [[beta-formula|Beta]] for calculating β, [[dcf|DCF]] for the full valuation chain.
