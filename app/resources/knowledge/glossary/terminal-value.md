**Terminal value** is the estimated value of a business (or asset) at the end of an explicit forecast period, representing all future cash flows beyond that horizon in a single discounted number.

In most DCF models, the terminal value accounts for 60–80% of total estimated enterprise value. It is simultaneously the most important and most uncertain component of any DCF.

## Two methods

### Method 1: Gordon Growth Model (Perpetuity Growth)

```
Terminal Value = FCF_{n+1} / (WACC − g)
             = FCF_n × (1 + g) / (WACC − g)

g = perpetuity growth rate (long-run GDP growth, typically 2–3%)
```

### Method 2: Exit Multiple

```
Terminal Value = EBITDA_n × EV/EBITDA exit multiple
```

Exit multiple anchors the terminal value to market comparables rather than a perpetuity assumption.

## Worked example

```
Year 5 FCF:  $200M
Year 5 EBITDA: $350M
WACC: 9%
Assumed perpetuity growth: 2.5%
Assumed EV/EBITDA exit multiple: 12×

Gordon Growth TV:   $200M × 1.025 / (0.09 − 0.025) = $3,154M
Exit Multiple TV:   $350M × 12 = $4,200M
Average:            $3,677M

PV of TV (5 years at 9%):
  Gordon Growth PV:  $3,154M / (1.09)^5 = $2,051M
  Exit Multiple PV:  $4,200M / (1.09)^5 = $2,730M
```

The wide range ($2B vs $2.7B) illustrates why sensitivity analysis is mandatory.

## Sensitivity to key inputs

```
WACC = 9%, g = 2.5%:  TV = $200M × 1.025 / 0.065 = $3,154M
WACC = 10%, g = 2.5%:  TV = $200M × 1.025 / 0.075 = $2,733M   (−13%)
WACC = 9%, g = 3.0%:   TV = $200M × 1.030 / 0.060 = $3,433M   (+9%)
WACC = 9%, g = 3.5%:   TV = $200M × 1.035 / 0.055 = $3,764M   (+19%)
```

A 50 bps change in g or WACC moves terminal value 10–20%.

## Terminal value as % of enterprise value

| Company Type | TV as % of EV | Reason |
|---|---|---|
| Mature, slow growth | 50–65% | Near-term cash flows are meaningful |
| High-growth tech | 70–85%+ | Most value is in the distant future |
| Early-stage startup | 90%+ | Minimal near-term cash flows |
| Declining industry | 30–50% | Value concentrated in current operations |

## Normalizing for the terminal year

The terminal year must be "normalized" — it should reflect a steady state, not cyclical peak or trough:
- Capex should approximate depreciation (maintenance capex only).
- Working capital changes should be zero (stable revenue).
- Tax rate should reflect long-run effective rate.

Failing to normalize inflates or deflates terminal value.

## Pitfalls

- A terminal growth rate above WACC creates a mathematical impossibility (negative denominator) or implies the company grows larger than the global economy.
- Exit multiples embed all current market biases — using "current market multiples" in a downturn produces artificially low terminal values.
- The perpetuity growth formula assumes constant growth forever — a strong assumption even for well-established businesses.

See also: [[dcf|DCF]], [[wacc|WACC]], [[fcf|Free Cash Flow]], [[ev-ebitda|EV/EBITDA]], [[enterprise-value|Enterprise Value]].
