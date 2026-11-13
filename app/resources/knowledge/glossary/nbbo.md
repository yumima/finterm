The **NBBO (National Best Bid and Offer)** is the best (highest) bid price and the best (lowest) ask price across all registered U.S. stock exchanges and electronic trading venues at any given moment.

Regulation NMS (National Market System) requires brokers to execute customer orders at the NBBO or better — this is the "order protection rule" that prevents customers from receiving inferior prices when better prices exist elsewhere.

## Components

```
NBBO = Best Bid (NBO) across all exchanges
     + Best Offer (NBO) across all exchanges

NBBO spread = Best Ask − Best Bid
```

## Worked example

```
AAPL order book across venues (simplified):
Exchange A bid: $184.95, ask: $185.02
Exchange B bid: $185.00, ask: $185.03
Exchange C bid: $184.98, ask: $185.01

NBBO:
  Best Bid:  $185.00 (Exchange B — highest bid)
  Best Ask:  $185.01 (Exchange C — lowest ask)
  NBBO Spread: $0.01
```

Your broker must fill your market buy order at $185.01 (or better), not $185.03.

## Why NBBO matters

Before Regulation NMS (2007), fragmented markets meant traders on different exchanges could execute at very different prices for the same stock. NBBO provides price transparency and best-execution guarantees.

**For market orders:** You receive the NBBO price (or better).
**For limit orders:** An order at the NBBO contributes to setting it.

## NBBO vs. displayed quotes

The NBBO reflects **displayed** (visible) liquidity. A significant amount of trading volume occurs:
- In **dark pools** — off-exchange venues that match orders away from public quotes.
- Via **payment for order flow (PFOF)** — where brokers route orders to wholesalers who improve on the NBBO by a fraction of a cent ("price improvement").

## Locked and crossed markets

- **Locked market**: NBBO bid = NBBO ask (spread = $0). Unusual; often a temporary routing artifact.
- **Crossed market**: NBBO bid > NBBO ask. Briefly occurs during fast markets; an arbitrage opportunity that disappears instantly.

## Inside spread vs. depth

The NBBO reflects only the best price — not how much size is available at that price. A stock can have a tight 1-cent NBBO but only 100 shares at the best bid. Institutional traders monitor the full order book depth, not just the NBBO.

## Pitfalls

- NBBO is a snapshot; in high-frequency trading, quotes change in microseconds. A displayed NBBO may not exist by the time your order arrives.
- For thinly traded stocks, the NBBO spread can be very wide (5–10%+ for nano-cap stocks).
- Price improvement via PFOF is real but small — typically $0.001–$0.005 per share. For small orders it's meaningful; for large institutional orders, the cost of PFOF routing (information leakage) may exceed the benefit.

See also: [[market-maker|Market Maker]], [[payment-for-order-flow|Payment for Order Flow]], [[dark-pool|Dark Pool]], [[bid-ask-spread|Bid-Ask Spread]], [[order-book|Order Book]].
