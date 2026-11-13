**Momentum** is the tendency of assets that have performed well recently to continue outperforming (and poorly performing assets to continue underperforming) over the near to medium term — one of the most robust and widely documented anomalies in financial markets.

Momentum is both a trading strategy and a risk factor. It has been observed across equities, bonds, commodities, currencies, and virtually every liquid asset class studied.

## Time horizons

| Horizon | Phenomenon | Note |
|---|---|---|
| 1–5 days | **Short-term reversal** | Microstructure noise; contrarian |
| 1–12 months | **Price momentum** | The classic momentum effect |
| 3–5 years | **Long-term reversal** | Mean reversion to fundamental value |

The one-to-twelve month momentum (the "Jegadeesh-Titman momentum") is the most studied and tradeable.

## Measuring momentum

**Simple price momentum:**
```
12-1 month momentum = (Price now / Price 12 months ago) − 1
(skipping the most recent month to avoid short-term reversal)
```

**Momentum factor (WML — Winners Minus Losers):**
```
Go long top 30% performers (winners) + short bottom 30% (losers)
Rebalance monthly
Annual return premium historically: ~8–12% before transaction costs
```

## Worked example

```
Universe: S&P 500 stocks, ranked by 12-1 month return
Portfolio (monthly rebalance, May 2026):

Top decile (long):  NVDA (+180%), VST (+120%), etc.
Bottom decile (short): INTC (−35%), WBA (−50%), etc.

Long minus short spread over rolling 12 months: historically ~10% annualized
```

## Why momentum persists

Competing explanations:

1. **Behavioral — underreaction**: Investors update views slowly; news is gradually priced in over months.
2. **Behavioral — herding**: Investors chase performance, creating trend-following cascades.
3. **Institutional constraints**: Fund managers cannot immediately implement large positions; rebalancing creates persistent flows.
4. **Risk-based**: Momentum stocks have higher crash risk (momentum crashes) which compensates for the premium.

## Momentum crashes

Momentum is subject to catastrophic reversals ("momentum crashes") in periods of sharp market rebound after distress:

```
March 2009 market recovery:
  Momentum portfolio lost ~40% in one month as shorts (beaten-down stocks) rallied violently
```

Classic momentum crashes occur when:
- Market has just had a severe drawdown.
- High-IV environment (cheap puts on losers are very expensive).
- Rapid broad market recovery.

## Cross-asset and cross-sectional

- **Cross-sectional momentum**: buy relative winners, short relative losers within an asset class (most studied).
- **Time-series (trend following)**: buy an asset when its trend is positive, sell/short when negative — across many asset classes.

AQR, Two Sigma, and many CTAs exploit momentum systematically.

## Pitfalls

- Momentum strategies have high turnover and can be tax-inefficient in taxable accounts.
- Transaction costs matter greatly — the momentum premium is partly competed away by trading costs in less liquid markets.
- Momentum crashes happen without warning and can eliminate years of gains in weeks.
- Past momentum rankings don't guarantee future outperformance — each rebalance is a fresh bet.

See also: [[mean-reversion|Mean Reversion]], [[beta|Beta]], [[alpha|Alpha]], [[volatility|Volatility]], [[sector-rotation|Sector Rotation]].
