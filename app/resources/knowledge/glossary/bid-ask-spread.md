# Bid-Ask Spread

The difference between the **highest price a buyer is currently willing to pay (bid)** and the **lowest price a seller is currently willing to accept (ask)**. Effectively the round-trip cost of a trade.

```
Bid: $99.95   ← what buyers will pay
Ask: $100.05  ← what sellers want
Spread: $0.10  (10 bps on a $100 stock)
Mid:  $100.00
```

The market maker pockets the spread on each round trip; you pay it.

## Worked example — round-trip cost

```
You buy 1,000 shares of AAPL at the ask:    $175.05
You sell same 1,000 shares at the bid:      $174.99
Round-trip slippage:  $0.06/share × 1,000 = $60

If you do this 100 times in a year:  $6,000 in pure spread cost
On a $175,000 portfolio:             3.4% annual drag
```

For active traders, spread is one of the largest hidden costs.

## Spreads by asset class (typical)

| Asset | Spread (% of mid) |
|---|---|
| SPY, QQQ, top ETFs | <0.01% |
| Mega-cap stocks (AAPL, MSFT) | 0.01–0.05% |
| Large-cap stocks | 0.05–0.15% |
| Mid-cap stocks | 0.10–0.30% |
| Small-cap stocks | 0.20–1% |
| Micro-cap / OTC | 1–10% |
| Liquid options (ATM, near expiry) | 1–3% of premium |
| Illiquid options | 10–25% of premium |
| Major crypto (BTC) | 0.05–0.20% |
| Mid-cap crypto | 0.5–2% |
| Small-cap crypto | 2–10%+ |
| US Treasury (active) | <0.001% |
| HY corporate bond | 0.5–2% |
| EM sovereign bond | 1–3% |

## What drives spread

- **Volume** — more trades → tighter spreads
- **Market maker competition** — more participants → tighter spreads
- **Time of day** — wider at open, close, news; tightest mid-session
- **Volatility** — higher vol → wider spreads (compensates MM for risk)
- **Asset complexity** — options spreads wider than stocks; structured wider than vanilla

## Effective vs displayed spread

The **displayed spread** is the inside (NBBO). The **effective spread** is what you actually pay after midpoint comparison:

```
Effective spread = 2 × |trade price − midpoint|
```

Often less than displayed because market makers improve prices for retail flow (PFOF).

## Pitfalls

- **Treating displayed spread as fixed cost** — for large orders you'll sweep through multiple price levels (slippage)
- **Assuming retail flow gets midpoint** — sometimes yes, sometimes no
- **Ignoring spread on round-trip backtests** — costs compound
- **Crypto exchanges with tiny "displayed" spread but huge depth issues** — basket gets sliced

## Decision rules

- **Active trader** → favor highest-liquidity instruments (top ETFs, mega caps)
- **Holder** → spread doesn't matter much for one-time trades
- **Large order in mid-cap** → break into smaller pieces (TWAP/VWAP)
- **Options with > 5% spread** → almost always avoid; risk-reward is destroyed by execution
- **Pre-trade check** — always look at spread + ADV before entering an order

## In finterm

Markets and Equity Trading screens display bid/ask in real time. The spread column on the watchlist is one of the most under-appreciated columns for execution-quality decisions.
