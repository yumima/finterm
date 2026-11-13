# Expected Value (EV)

The **probability-weighted average outcome** of a decision — the right way to compare bets, especially across different odds and payoffs.

```
EV = Σ ( probability(outcome_i) × value(outcome_i) )
```

## Worked example — coin-flip vs trade

```
Coin flip:  +$10 if heads, -$10 if tails
  EV = 0.5 × $10 + 0.5 × (-$10) = $0  (fair bet)

Trade A:  60% chance of +$200, 40% chance of -$100
  EV = 0.6 × $200 + 0.4 × (-$100) = $120 + (-$40) = +$80  (positive EV)

Trade B:  10% chance of +$1,000, 90% chance of -$50
  EV = 0.1 × $1,000 + 0.9 × (-$50) = $100 + (-$45) = +$55  (positive EV)

Both positive EV — both worth taking. But A is steadier; B is lottery-like.
```

## Where EV reasoning helps

- **Trading**: comparing strategies with different win rates and payoff ratios
- **Options**: pricing whether implied vol is fair vs realized expectations
- **Insurance**: are you paying more in premium than expected payouts?
- **Lotteries**: almost all have negative EV (state takes 30–50% off the top)
- **Career decisions**: probability × payoff of different paths

## EV vs most-likely outcome

```
Outcome A: 80% chance of +$50  →  $40 EV contribution
Outcome B: 20% chance of -$300 →  -$60 EV contribution
Total EV: -$20  (negative — don't take this bet)

But the "most likely" outcome (A) is positive.
Always think probability-weighted, not modal.
```

A "usually wins, occasionally catastrophic" trade is exactly how option-sellers blow up.

## Why EV alone isn't enough

- **Variance matters**: high-variance strategies can ruin you before EV plays out
- **Path dependence**: if you go bankrupt en route, the long-run EV is irrelevant (you're out)
- **Sample size**: realizing EV requires enough trials. Small samples are dominated by noise.
- **Stationarity**: if probabilities change, your EV calc is stale

This is why we also use **Kelly sizing** — to cap variance and avoid ruin while pursuing EV.

## Pitfalls

- **Ignoring tail outcomes**: many EV calculations assume thin tails; reality has fat tails
- **Wrong probability estimates**: garbage in → garbage EV
- **Survivorship bias** in calibration data (e.g., "80% of breakouts succeed" — only counted ones that survived)
- **Trading positive-EV strategies with insufficient capital** — get wiped out before edge plays out
- **Assuming independence** when trades are correlated (same factor exposure)

## Decision rules

- **Take positive-EV bets** at sustainable size (not catastrophe-risk size)
- **Avoid negative-EV bets** even when "fun" (lottery, casino, crypto pumping)
- **Insurance is positive-EV** for the insurer (negative for you), but justified by risk transfer
- **EV requires enough trials** — single-decision EV is incomplete; think portfolio-level
- **Pair EV with variance** — high-EV/high-vol may not survive its own drawdowns

## Common applications

- **Options pricing**: market price vs your model's EV → opportunity if mispriced
- **Sports betting**: only positive-EV bets after juice = +EV in expectation
- **Trading strategy filtering**: cut anything with negative expected value after costs
- **Decision under uncertainty**: when uncertain, compute EV before deciding

## In finterm

Backtesting computes per-trade EV (in dollars and R-multiples). Strategy evaluation should always include EV alongside Sharpe ratio and max drawdown.

## Related

- [[r-multiple]] — EV expressed in units of risk
- [[expectancy]] — same concept, different name (more common in trading)
- [[kelly-criterion]] — sizing for EV-positive strategies given variance constraints
