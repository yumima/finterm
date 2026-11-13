# Market Microstructure

The plumbing of markets — **how orders, exchanges, market makers, and dark pools actually interact** to produce the prices you see. Most retail traders trade for years without understanding this; it costs them money.

## The order lifecycle (US equity, simplified)

```
1. You hit "buy" in your broker app
2. Broker decides where to route:
   - National exchange (NYSE, Nasdaq, ARCA, BATS, IEX, etc.)
   - Wholesaler (Citadel, Virtu, etc. — payment for order flow)
   - Dark pool (broker-internal or external)
3. Order matches against:
   - A resting order in the chosen venue's order book
   - A market maker's inventory (with possible price improvement)
4. Trade executes; reported to consolidated tape
5. Settlement: T+1 (next business day) — your account updates
```

## Who's on the other side of your trade

- **Other retail traders** — actually a minority of volume
- **Market makers / wholesalers** — provide liquidity, profit on spread + flow
- **HFT firms** — provide intraday liquidity, prey on slow flow
- **Index funds** — rebalancing flows, big size at known times
- **Active funds** — fundamental buyers/sellers
- **Hedge funds** — variety of strategies
- **Corporates** — buybacks (huge volumes)
- **Central banks** — Treasury operations

## Order types in the wild

Beyond [[market-order]] and [[limit-order]]:

| Order type | What it does |
|---|---|
| **IOC** (Immediate or Cancel) | Fill what's available now, cancel rest |
| **FOK** (Fill or Kill) | All-or-nothing immediately |
| **Iceberg** | Show only a fraction of total size |
| **Hidden** | Don't display in order book at all |
| **Pegged** | Anchored to NBBO |
| **MOO / MOC** | Market on open / close — auction-only |
| **VWAP / TWAP algos** | Spread execution over time |
| **Implementation shortfall** | Algo balancing impact + delay risk |

## Exchange types

- **Lit exchanges** (NYSE, Nasdaq) — public order books, full transparency
- **Dark pools** (Liquidnet, ITG POSIT, broker-internal) — opaque, ~40% of US equity volume
- **ATSes** — Alternative Trading Systems, typically dark
- **Inverted venues** (BATS Inverted, etc.) — pay-for-take fee model

Dark pools serve institutional traders who want to move size without telegraphing intent.

## Payment for order flow (PFOF)

Retail brokers (Robinhood, etc.) sell your order flow to wholesalers. Wholesalers:
- Get to internalize the trade (often at slightly better price than displayed)
- Pay the broker per share
- Make money on the bid-ask spread + speed advantage

**Outcome for you**: usually small price improvement vs displayed quote. But in stress (Mar 2020, GME Jan 2021), the model can break — fills get worse, brokers experience operational issues.

## Tick size and market structure

- US stocks tick in $0.01 increments above $1 (sub-penny for very low priced)
- US options usually tick in $0.01 (some $0.05)
- Futures tick sizes vary by contract
- Sub-penny pricing is common in dark pools

The tick size affects spread math: a 1-cent tick on a $5 stock is 20 bps; on a $500 stock is 0.02 bps.

## Auctions: the open and close

- **Opening auction** (9:30 AM ET on NYSE/Nasdaq) — single price determined by aggregating MOO orders
- **Closing auction** (4:00 PM ET) — same; sets the official close
- Closing auctions handle 5–10% of daily volume in some names; index rebalances pour through here

Don't trade at open or close with market orders unless you know what you're doing.

## ETF arbitrage and the AP system

ETFs trade like stocks but are **created/redeemed in kind** by Authorized Participants (APs). If ETF price > NAV: AP creates new units (buying basket, selling ETF). If ETF price < NAV: AP redeems (selling basket, buying back ETF). This keeps ETF price near NAV in normal conditions.

In stress, the link can break briefly (Mar 2020 bond ETFs traded 5%+ discount to NAV).

## Where retail loses to microstructure

- **Wide spreads** in small-cap, options, crypto
- **Slippage** on stop-loss orders triggered in fast moves
- **After-hours trading** — thin liquidity, terrible execution
- **Fast-moving news**: by the time you click "buy," the market has moved
- **Market-on-open orders**: opening auction prints can be 1–3% from prior close

## Decision rules

- **Default to limit orders** for everything but the most liquid mega-caps
- **Avoid trading at open and close** unless using algos
- **Check ADV and spread before sizing**
- **Understand whether your broker uses PFOF** and how it affects you
- **For large orders, use TWAP/VWAP** to reduce market impact

## In finterm

Markets and Equity Trading screens display the inside quote, depth (where supported), and recent trades. For execution-quality analysis, look at fills vs reference prices over time.
