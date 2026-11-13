**Position sizing** is the process of determining how many shares, contracts, or units of an asset to hold in a trade or investment — matching the amount of capital allocated to a position with the risk taken.

Correct position sizing is arguably more important than stock selection or entry/exit timing for long-term trading success. Oversizing consistently leads to ruin; undersizing leaves return on the table.

## Core principle

```
Position Size = Risk Capital / Risk Per Share

Risk Capital = Account × Risk % per trade
Risk Per Share = Entry Price − Stop Loss Price
```

## Worked example — fixed risk sizing

```
Account: $100,000
Risk per trade: 1% of account = $1,000
Entry: $50.00/share
Stop loss: $48.00/share
Risk per share: $50 − $48 = $2.00

Position size = $1,000 / $2.00 = 500 shares
Dollar exposure: 500 × $50 = $25,000 (25% of account)
```

This approach ensures no single trade can lose more than 1% of the account, regardless of how wide the stop is.

## Position sizing methods

| Method | Formula | Pros | Cons |
|---|---|---|---|
| Fixed % risk | Risk $ / Risk per unit | Account-size adaptive | Requires defined stops |
| Fixed % of account | Set % in each position | Simple | Ignores stop distance |
| Kelly Criterion | f = (bp − q) / b | Maximizes long-run growth | Can oversize; "half Kelly" common |
| Volatility-based (ATR) | Risk $ / ATR | Adjusts for market conditions | More complex |
| Equal weight | 1/N | Simple diversification | Ignores risk differences |

## ATR-based position sizing

Position size using [[average-true-range|Average True Range (ATR)]]:

```
ATR (14-day): $3.20
Risk per trade: $500
Position size = $500 / $3.20 = 156 shares
Stop placed at 1× ATR from entry
```

This automatically adjusts size based on current market volatility — smaller positions in volatile regimes, larger in calm periods.

## Portfolio-level position sizing

Individual trade sizing must consider portfolio concentration:

```
Account: $100,000
Max per-trade risk: 1% ($1,000)
Max per-sector concentration: 15% ($15,000)
Max correlation: no two positions >0.8 correlated

If all positions have 1% risk, maximum simultaneous positions: ~20 (4×5 diversified)
```

The [[kelly-criterion|Kelly Criterion]] applied at the portfolio level is complex; most practitioners use fractional Kelly (25–50% of Kelly) to avoid volatility and ruin risk.

## Pyramiding into winners

Some position-sizing strategies allow adding to winning positions:

```
Initial position: 300 shares at $50
Add 200 shares when price confirms at $54
Add 100 shares at $57

Average cost: (300×50 + 200×54 + 100×57) / 600 = $52.50
Risk: Stop has moved up to $52; risk = only the latest add
```

Pyramiding maintains portfolio discipline while scaling into confirmed winners.

## Pitfalls

- Oversizing is the most common cause of account blowup — one oversized wrong trade can wipe out months of gains.
- Position sizing in dollar terms without accounting for price level differences leads to inconsistent risk (100 shares of a $10 stock is very different from 100 shares of a $500 stock).
- Correlation between positions is often ignored; two "separate" positions in correlated stocks effectively double the exposure in that risk factor.
- Stop-losses in illiquid or volatile instruments may not execute at the expected price (slippage), making calculated risk estimates too optimistic.

See also: [[kelly-criterion|Kelly Criterion]], [[risk-reward-ratio|Risk/Reward Ratio]], [[average-true-range|Average True Range]], [[r-multiple|R-Multiple]], [[stop-loss-order|Stop-Loss Order]].
