**Leverage** is the use of borrowed capital or financial instruments to amplify potential returns from an investment — at the cost of proportionally amplified downside risk.

Leverage is fundamental to corporate finance, investing, and trading. A small amount of equity controls a much larger asset, meaning gains and losses are magnified relative to the capital actually risked.

## Financial leverage formula

```
Leverage Ratio = Total Assets / Equity
             = 1 + Debt/Equity

Leverage Effect = Leverage Ratio × Asset Return = Equity Return
```

## Worked example — corporate leverage

```
Company A (No leverage):
  Assets: $1,000  Debt: $0  Equity: $1,000
  Asset return 10%: Equity return = 10%
  Asset return −10%: Equity return = −10%

Company B (2× leverage):
  Assets: $2,000  Debt: $1,000  Equity: $1,000
  Asset return 10%:  Profit = $200, Interest = $50
                     Equity return = ($200−$50)/$1,000 = 15%
  Asset return −10%: Loss = $200, Interest = $50
                     Equity return = (−$200−$50)/$1,000 = −25%
```

Leverage turns 10% gains into 15% but turns 10% losses into 25%.

## Types of leverage

| Type | Context | Example |
|---|---|---|
| **Financial leverage** | Corporate balance sheet | D/E ratio, debt financing |
| **Operating leverage** | Cost structure (high fixed costs) | Airlines, streaming |
| **Trading leverage** | Margin, futures, options | 10× margin, futures notional |
| **Embedded leverage** | Options, structured products | Calls: 10–50× leveraged exposure |

## Operating leverage

Operating leverage is distinct from financial leverage — it refers to the proportion of fixed vs. variable costs:

```
High operating leverage: fixed costs dominate
  Revenue +10% → Operating income +30% (leverage: 3×)
  Revenue −10% → Operating income −30% (downside symmetrical)

Software company (high fixed R&D, low marginal cost):
  Revenue +10% → most incremental revenue falls to operating income
```

## Leverage in trading

Futures and options use leverage through notional exposure:

```
E-mini S&P 500 futures (ES):
  Notional: $5,000 × S&P 500 level = ~$26M
  Initial margin: ~$15,000
  Effective leverage: ~1,730× vs. margin (extremely high)
  More practically: 26× if account holds $1M

CFD example:
  10× leverage: $10,000 controls $100,000
  1% adverse move = 10% equity loss
```

## Leverage and ruin

The risk of ruin increases non-linearly with leverage:

```
Kelly Criterion insight:
  Optimal leverage = (Expected Return − Risk-free) / Variance
  
  2× leverage on S&P 500:
  If S&P 500 has 70-year CAGR of 10%, variance ~0.04:
  Optimal Kelly leverage ≈ 2.7× (for long-term terminal wealth)
  
  Above Kelly leverage: not just risky — terminal wealth approaches zero over long periods
```

## Leverage in macro context

Country-level leverage (debt/GDP) matters for fiscal sustainability and sovereign credit risk. High private-sector leverage amplifies recessions (Minsky moment).

## Pitfalls

- Leverage math is asymmetric: a 50% loss requires a 100% gain to recover; leverage accelerates this problem.
- Volatility targeting (e.g., leveraged ETFs) rebalances daily, causing systematic "volatility drag" — a 2× ETF does NOT deliver 2× the long-term return.
- Margin calls force delevering at the worst possible time (market bottoms).

See also: [[margin|Margin]], [[debt-to-equity|Debt-to-Equity]], [[lbo|LBO]], [[margin-call|Margin Call]], [[wacc|WACC]].
