# Duration

The **price sensitivity of a bond** to a 1 percentage point change in interest rates — measured in years. The single most important risk metric for fixed income.

```
Approx %ΔP ≈ −Duration × Δy
```

A bond with **8-year duration** falls ~8% if yields rise 1%. Rises ~8% if yields drop 1%.

## Worked example — SVB-style risk

```
Bank holds $20B portfolio of 10-year Treasuries:
  Avg coupon: 1.5% (bought 2020–2021)
  Duration: ~9 years

Fed hikes rates from ~0% to 5% (2022–23):
  Approx loss = 9 × −5% = −45%
  Mark-to-market loss ≈ $9B

If forced to sell (deposit run): realize the loss → insolvency
```

This is exactly what destroyed Silicon Valley Bank in March 2023.

## Two common duration measures

- **Macaulay duration** — weighted average time to cash flows; in years
- **Modified duration** — Macaulay / (1 + YTM/n); the practical sensitivity number

For most purposes, "duration" means modified duration.

## Reference durations

| Instrument | Approx duration |
|---|---|
| 3-month T-bill | 0.25 |
| 1-year T-bill | 1.0 |
| 5-year T-note | 4.5 |
| 10-year T-note | 8.5 |
| 30-year T-bond | 20+ |
| 30-year zero-coupon | 30 (max possible) |
| Investment-grade corporate (avg) | 7 |
| High-yield corporate (avg) | 4 (shorter; more equity-like) |
| Bond mutual fund (intermediate) | 4–6 |
| Bond mutual fund (long-term) | 10–20 |

## Duration on bond funds

A bond fund's duration is the weighted average of its holdings'. Watch fact sheets — funds shift duration over time and may not match your assumptions. **A "long bond fund" can have 15+ year duration**, meaning it's extremely rate-sensitive.

## Tactics

| Outlook | Duration choice |
|---|---|
| Rates falling (cuts ahead) | Long duration (lock in higher yields) |
| Rates rising (hikes ahead) | Short duration (limit price losses) |
| Uncertain | Match duration to your liability horizon |

## Pitfalls

- **Duration is linear approximation**; for big rate moves, also use convexity (second-order)
- **Negative convexity** in callable bonds and mortgages — losses bigger than duration suggests
- **Floating-rate notes** have ~0 duration; very different from fixed
- Bond fund duration **changes with manager decisions** — re-check periodically
- Pension/insurance use **duration matching** to immunize against rate moves; mismatch = solvency risk

## Decision rules

- **Hold-to-maturity individual bond** → duration matters less (you'll get face)
- **Bond fund / mark-to-market** → duration is your daily risk
- **For 30-year horizon retirement** → long-duration bonds offset long-duration liabilities
- **For 1–3 year cash needs** → short-duration only; never long bonds

## In finterm

Portfolio risk panel shows weighted-avg portfolio duration. Gov Data shows Treasury durations across the curve. Always pair duration with [[convexity]] when sizing rate risk.
