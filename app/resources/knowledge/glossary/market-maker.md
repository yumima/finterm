A **market maker** is a firm or individual that continuously quotes both a bid price (to buy) and an ask price (to sell) for a financial instrument, providing liquidity to the market and earning the spread between the two prices.

Market makers are the infrastructure of financial markets — without them, buyers and sellers would have to wait for each other to arrive simultaneously, making markets illiquid and erratic.

## How market makers profit

```
Market maker quotes AAPL:
  Bid: $185.00  (will buy from you at this price)
  Ask: $185.02  (will sell to you at this price)
  Spread: $0.02

If 1,000 transactions (buys + sells) occur, market maker captures:
  Revenue ≈ 1,000 × 100 shares × $0.01 (half spread) = $1,000
  Less: inventory risk (if AAPL moves against the book)
```

Market makers' primary risk is **inventory risk** — if they buy stock from sellers and the price falls before finding buyers, they lose. They hedge this continuously.

## Types of market makers

| Type | Examples | Venue |
|---|---|---|
| Exchange designated MM | Cboe, NYSE specialists | Listed exchanges |
| Electronic market makers | Citadel Securities, Virtu, Jane Street | All venues |
| Options market makers | SIG, IMC, Susquehanna | Options exchanges |
| Crypto market makers | GSR, Wintermute | Crypto exchanges |

## Market making strategies

**Passive market making**: Post quotes and earn spread when orders arrive. Profitable in stable markets; loss-prone during trends.

**Statistical arbitrage**: Hedge positions across correlated instruments while providing liquidity — earn spread while maintaining near-zero net exposure.

**Options market making**: Delta-hedge each options position to maintain delta-neutral book; earn implied volatility premium vs. realized volatility.

## Obligations and privileges

In exchange-designated market maker programs, designated market makers (DMMs) receive:
- Order flow preferences.
- Reduced exchange fees.

In return they must:
- Quote continuously within defined spread maximums.
- Provide liquidity during market opens, closes, and volatility.
- Fulfill minimum size requirements.

## Market makers vs. brokers

| Feature | Market Maker | Broker |
|---|---|---|
| Trades as principal | Yes | No (agency model) |
| Owns inventory | Yes | No |
| Earns spread | Yes | Earns commission |
| Fiduciary to client | No | Yes (in some contexts) |

## Pitfalls

- Market makers are not always obligated to quote in all market conditions — during extreme volatility or a flash crash, many withdraw quotes, making the market one-sided.
- In payment for order flow, retail brokers route to market makers who provide price improvement. Critics argue this creates conflicts of interest — market makers profit from information in the order flow.
- Market maker profits come from liquidity takers — understanding that your trade provides data to market makers is important for sophisticated traders.

See also: [[nbbo|NBBO]], [[bid-ask-spread|Bid-Ask Spread]], [[payment-for-order-flow|Payment for Order Flow]], [[dark-pool|Dark Pool]], [[liquidity|Liquidity]].
