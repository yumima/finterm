# Options Greeks — Deep Dive

The five Greeks describe how an option's price changes with everything that can move it. Together they form a **complete sensitivity dashboard** for any options position.

| Greek | Symbol | Sensitivity to | Long option |
|---|---|---|---|
| **Delta** | Δ | Underlying price | Call: +; Put: − |
| **Gamma** | Γ | Rate of change of delta | Always + |
| **Theta** | Θ | Time | Always − (decay) |
| **Vega** | ν | Implied volatility | Always + |
| **Rho** | ρ | Interest rates | Call: +; Put: − |

See individual entries for each: [[delta]], [[gamma]], [[theta]], [[vega]], [[rho]].

## How Greeks interact

Greeks **don't move independently**. A single move in the underlying changes price (delta), changes delta itself (gamma), and may shift IV (vega impact). Time passes (theta). Rates move (rho).

Real positions live at the intersection.

## Worked example — full Greek attribution

```
Long 10 AAPL $180 calls (30-day)
Initial:  Premium $5/share, total cost $5,000
  Delta: 0.45 → portfolio delta = 450 shares
  Gamma: 0.04 → delta moves by 0.04 per $1
  Theta: -0.05/day × 10 contracts × 100 = -$50/day
  Vega:  0.20 per IV point × 10 × 100 = $200/IV pt

End of day, AAPL up $2 to $177, IV up 1 point:
  Δ-PnL:  +$2 × 450 = +$900
  Γ-PnL:  +½ × 0.04 × 10 × 100 × $2² = +$80
  Θ-PnL:  −$50 (one day)
  ν-PnL:  +$200 × 1 = +$200
  Total:  +$1,130

New delta: 0.45 + 0.04 × 2 = 0.53 → portfolio delta = 530
```

Greeks decompose your P&L into clean attribution: how much came from price, how much from time, how much from vol.

## Position aggregation

A portfolio of options has **portfolio Greeks** = sum of position Greeks. This is how dealers and hedge funds manage option books:

```
Position 1: long 10 calls (delta +500, gamma +20)
Position 2: short 5 puts (delta +250, gamma −10)
Position 3: long 100 shares (delta +100)

Portfolio:
  Delta = +850
  Gamma = +10
  Theta = whatever the long calls + short puts net to
  Vega  = ditto
```

To delta-hedge: short 850 shares. Result: portfolio delta = 0.

## Greek combinations

| Profile | Means... |
|---|---|
| Long delta + long gamma | Outright bullish |
| Short delta + long gamma | Bearish but happy with movement |
| Long gamma + short theta | Long volatility — pays for time, gains on movement |
| Short gamma + long theta | Short volatility — collects decay, loses on movement |
| Long vega | Positive on IV expansion |
| Short vega | Profits as IV drops |

A **straddle** = long gamma + short theta + long vega. Triple bet on uncertainty.
An **iron condor** = short gamma + long theta + short vega. Triple bet on calm.

## The fundamental tension

You can't be long gamma and avoid theta. You can't collect theta and not be short gamma. **Long gamma costs theta; collecting theta means short gamma.**

Successful options traders **size positions for the worst case in the Greek they're short.**

## Decision rules

- **Long premium near expiry** → max gamma + max theta cost
- **Short premium far from expiry** → less gamma + less theta + more vega
- **Calendar spreads** → trade theta vs vega; long back-month vega + short front theta
- **Earnings plays** → factor in vol crush (vega effect)
- **Always know your portfolio Greeks before entering a new position**

## Greeks across the chain (visual)

```
          Strike →
          80   90   100 (ATM)  110   120
Delta:   0.85  0.70  0.50      0.30  0.15
Gamma:   0.02  0.04  0.06      0.04  0.02   ← peaks ATM
Theta:  -0.02 -0.05 -0.08     -0.05 -0.02   ← peaks ATM
Vega:    0.05  0.12  0.18      0.12  0.05   ← peaks ATM (long-dated)
```

ATM options have the highest gamma, theta, and vega in absolute terms.

## In finterm

Surface Analytics renders the full Greek surface. Derivatives screen displays per-strike Greeks for a chosen expiration. Always check portfolio Greeks before adding any options position.
