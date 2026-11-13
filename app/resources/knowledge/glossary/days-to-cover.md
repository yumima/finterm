**Days to cover** (also called short ratio) is the number of days it would take all short sellers in a stock to buy back (cover) their positions at the stock's average daily trading volume.

It measures the potential squeeze pressure — the more days required to cover, the more a short squeeze can be self-reinforcing, because covering demand outpaces supply.

## Formula

```
Days to Cover = Short Interest (shares) / Average Daily Volume (shares)
```

## Worked example

```
Stock: AMC Entertainment
Short interest:  60,000,000 shares
Avg daily volume: 30,000,000 shares

Days to cover = 60M / 30M = 2.0 days

Stock: Overstock.com (hypothetical)
Short interest:  25,000,000 shares
Avg daily volume: 2,000,000 shares

Days to cover = 25M / 2M = 12.5 days (high squeeze risk)
```

## Interpreting DTC

| DTC | Interpretation |
|---|---|
| < 1 day | Low; shorts could cover quickly with minimal impact |
| 1–3 days | Moderate; manageable short-covering demand |
| 3–5 days | Elevated; significant covering demand relative to supply |
| 5–10 days | High; notable short squeeze potential |
| > 10 days | Very high; classic short squeeze setup if catalyst emerges |

## Why DTC matters for short squeezes

A short squeeze occurs when shorts are forced to buy back shares (due to margin calls, stop losses, or fundamental thesis breaking), creating buying pressure that pushes the price higher — which forces more covering — a feedback loop.

```
Short catalyst (positive news) → Price rises
→ Shorts face losses → Margin calls / stop-losses trigger
→ Forced buying → Price rises more
→ High DTC means each buy creates more impact (less volume available)
→ Self-reinforcing squeeze
```

Low DTC means shorts can exit quickly; high DTC means covering creates sustained price pressure.

## DTC as a component of short squeeze scoring

Popular "short squeeze scoring" systems (Finviz, Ortex, S3 Partners) combine:
1. Short % of float (how many shares are short)
2. Days to cover (how hard it is to exit)
3. Borrow rate / cost to borrow (how expensive it is to maintain the short)
4. Catalyst proximity (earnings, contract announcements)

## Limitations

- DTC uses **average** daily volume, which may not reflect actual liquidity during a squeeze (volume can spike 10–50× during a short squeeze).
- DTC assumes **all** short sellers try to cover — in practice, some have conviction and add to positions on the way up.
- Volume-based DTC ignores that institutional trading on exchanges represents only part of total liquidity.

## Borrow rate complement

High DTC often coincides with high **borrow cost** (short sellers pay a fee to borrow shares). If borrow rates exceed 50–100% annualized, the cost of maintaining the short position makes the thesis much harder to hold.

## Pitfalls

- High DTC alone is not a buy signal — it is only one factor. A fundamentally weak company with high DTC may stay weak for a long time before squeezing.
- DTC is calculated on reported short interest which is bi-weekly delayed — actual current short interest can be meaningfully different.
- "Most shorted" lists are widely followed; any squeeze thesis is already well-known, limiting surprise catalyst impact.

See also: [[short-interest|Short Interest]], [[short-selling|Short Selling]], [[float|Float]], [[margin-call|Margin Call]], [[liquidity|Liquidity]].
