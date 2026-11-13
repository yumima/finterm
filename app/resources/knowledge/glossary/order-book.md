An **order book** is the real-time, electronic ledger of all outstanding buy (bid) and sell (ask) limit orders for a financial instrument at a given exchange or venue — the fundamental mechanism through which price discovery occurs in continuous auction markets.

Every price at which someone wants to buy or sell is visible in the order book. Trades occur when a buyer and seller agree on a price, which happens automatically when a new order matches an existing one.

## Structure

```
ASK (SELL) side (offers):
  Price    Size
  $185.05  300 shares
  $185.04  800 shares
  $185.03  1,200 shares
  $185.02  500 shares     ← Best ask (NBBO ask)

BID (BUY) side:
  $185.01  700 shares     ← Best bid (NBBO bid)
  $185.00  2,500 shares
  $184.99  1,000 shares
  $184.98  400 shares
```

Bid-ask spread here: $185.02 − $185.01 = $0.01

## How orders interact with the order book

**Limit order**: Rests in the book at a specified price. Adds liquidity.
```
Place limit buy at $185.00 → Joins 2,500 shares at the bid
```

**Market order**: Executes immediately against the best available price. Takes liquidity.
```
Market buy for 1,000 shares:
  Fills 500 at $185.02 (exhausts best ask)
  Fills 500 at $185.03 (moves up to next level)
  New best ask: $185.03 (price impact = $0.01)
```

## Market depth and price impact

Market depth measures how much size exists at each price level. A shallow book means large orders cause significant price movement (slippage):

```
Deep book at $185.02: 50,000 shares → a 1,000-share order causes minimal impact
Thin book at $185.02: 200 shares → a 1,000-share order runs through multiple levels
```

## Order book imbalance

The ratio of bid size to ask size provides a real-time momentum signal:

```
Bid size (top 5 levels): 8,000 shares
Ask size (top 5 levels): 2,000 shares
Order book imbalance: 8,000 / (8,000 + 2,000) = 80% on bid side
→ Buying pressure; likely upward price movement short-term
```

High-frequency traders and quantitative strategies use order book imbalance as a primary short-term price predictor.

## Level 1 vs Level 2 vs Level 3

| Data Level | Contains | Available to |
|---|---|---|
| Level 1 | Best bid, best ask, last trade | All retail investors |
| Level 2 | Full order book (all prices and sizes) | All with Level 2 subscription |
| Level 3 | Private: order entry, modification | Exchange members only |

## Hidden orders and iceberg orders

**Iceberg orders** display only a fraction of their total size (the "visible tip") while reserving a large hidden quantity that refreshes when the displayed portion is filled.

```
Iceberg sell: 100,000 total, 1,000 displayed
→ Book shows 1,000 at $185.02
→ Each time 1,000 fills, another 1,000 appears
→ Disguises institutional selling pressure
```

## Pitfalls

- The order book reflects displayed orders only — dark pools, algorithmic order routing, and iceberg orders mean the "true" supply and demand is larger than visible.
- Orders can be canceled in microseconds; "quote stuffing" (placing and canceling thousands of orders) can create the illusion of liquidity.
- For illiquid securities, large bid-ask spreads and thin books mean transaction costs are high regardless of commission levels.

See also: [[bid-ask-spread|Bid-Ask Spread]], [[nbbo|NBBO]], [[market-maker|Market Maker]], [[liquidity|Liquidity]], [[slippage|Slippage]].
