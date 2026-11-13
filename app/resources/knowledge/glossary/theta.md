# Theta (Θ)

The Greek that captures **time decay** — the dollar amount an option loses per day from the passage of time alone, holding everything else equal.

```
Theta = ∂(option price) / ∂(time)
```

By convention, theta is reported as a **negative number for long options** (you lose) and positive for short options (you collect).

## Worked example

```
AAPL @ $175
Long 30-day $180 call:
  Premium: $2.50
  Theta:   −$0.05/day

Hold one day, all else equal:
  New premium ≈ $2.45  (lost $0.05 to decay)

Hold over weekend (Fri close → Mon open):
  Decay = 3 days × $0.05 = $0.15
```

Even if the stock doesn't move, **time alone bleeds the option holder**.

## Theta profile

| Moneyness × DTE | Theta behavior |
|---|---|
| ATM, 30 DTE | Moderate; ~−$0.05 to −$0.10/day on a $5 option |
| ATM, 7 DTE | High; theta accelerates into expiration |
| ATM, 0 DTE | Maximum; option decays almost entirely in hours |
| Deep OTM | Low theta but high % loss per day |
| Deep ITM | Low theta; mostly intrinsic value |

**Theta accelerates in the last 30 days** — the famous "theta decay curve."

## The theta-vs-gamma tradeoff

**Long options**: pay theta daily, gain on movement (gamma) and IV rises (vega).
**Short options**: collect theta daily, lose on big moves and IV spikes.

Successful premium-sellers manage this by **sizing for gamma risk** — never collect more theta than the worst-case gamma move can offset.

## Decision rules

- **Long options held > 7 days near expiration** → theta dominates; close or roll
- **Selling weekly premium** → most efficient theta capture, but most gamma exposure
- **Calendar spreads** = short front-month theta, long back-month vega; theta-positive structure
- **Covered calls** → use theta to enhance income on long stock positions

## Pitfalls

- "Just one more day" → theta accelerates, not decelerates
- **Friday afternoon close → Monday morning open**: weekend theta hits hard
- Selling theta blindly without gamma protection (the GME / Volmageddon-style blowups)
- Theta on short options is **delta-and-gamma-conditional** — it's not free money

## In finterm

The Derivatives screen shows theta per strike. For premium-selling strategies, total portfolio theta (sum across positions) helps gauge daily decay income.
