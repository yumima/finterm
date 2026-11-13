# Slippage

The difference between the **expected execution price** of a trade and the **actual fill price**. Slippage is what happens when the market moves between the time you decide and the time you fill.

```
Slippage = |Expected price − Actual fill price|
```

## Sources of slippage

1. **Crossing the spread** — paying the ask when you wanted the mid
2. **Sweeping the order book** — large orders consume multiple price levels
3. **Fast-market price drift** — price moves before your order fills
4. **News-event gaps** — price reopens far from prior level
5. **Halt → reopen** — price reopens at a discovered level, possibly far from halt price

## Worked example

```
Strategy backtests at $100.00 entry / $105.00 exit (5% gross gain)

In live trading:
  Entry slippage:   pay ask ($100.05) + spread cross
  Exit slippage:    sell at bid ($104.95)
  Round-trip cost:  $0.10 + commissions

Realized:
  Entry $100.05, exit $104.95 → 4.9% gross
  After commissions ($0.05 round-trip): 4.85%
  
Backtest said 5%, live delivers 4.85% — small difference, but on 100 trades:
  Backtest: $5,000 expected
  Live:     $4,850 actual
  Difference: $150 of slippage drag

If strategy edge was only 0.5%, the slippage may eat all of it.
```

## Slippage ranges

| Trade type | Typical slippage |
|---|---|
| Mega-cap, normal hours, small order | <0.05% |
| Mid-cap, normal hours, modest order | 0.1–0.3% |
| Small-cap, normal hours | 0.3–1% |
| Stop-loss in fast move | 0.5–5% |
| News-event order | 0.5–10% |
| Halt-reopen | 5–30% |
| Crypto, large order | 0.5–3% |
| Penny stock or thin OTC | 5%+ |

## Why backtests under-model slippage

Most backtests assume mid-price fills with no impact. Reality:
- You don't always get mid (especially on retail platforms)
- Large orders move the market ("market impact")
- Stops trigger at the worst time (vol spikes)
- Backtests rarely include realistic intraday spread widening
- Live data is messier than the cleaned historical OHLC backtests use

A common rule: assume real slippage ≈ 1.5–2× modeled in backtest, then reconsider whether the strategy still works.

## Order types and slippage

| Order type | Slippage profile |
|---|---|
| Market | Variable; can be very high in stress |
| Limit | Zero slippage if filled; risk is no fill |
| Stop-loss | Same as market on trigger |
| Stop-limit | Possibly zero or no fill |
| TWAP/VWAP | Spread over time → reduces market impact |
| Iceberg | Hides size → reduces signaling cost |

## Pitfalls

- **Strategy that's profitable at $10k but unprofitable at $1M** — slippage scaling killed the edge
- **Backtest "buys at close" assumption** — close is the official last price, but at-close orders often fill at MOC auction price (different)
- **Stop-loss in low-volume names** — discovered slippage can dominate the trade
- **Crypto on illiquid exchanges** — quoted spread looks fine, but actual depth is thin

## Decision rules

- **Always assume worse-than-quoted spread** for order sizing
- **Use limits** wherever possible to control execution
- **Break large orders** into pieces (TWAP) for liquid names; use VWAP algo for institutional
- **Avoid trading** at open, close, around news, and in low-liquidity hours
- **Track your own slippage** in a journal; refine based on realized cost

## In finterm

Equity Trading screens record fill price vs reference price (when configured). For strategy development in Backtesting, conservative slippage assumptions (5–20 bps for liquid, more for illiquid) are recommended over zero-cost assumptions.
