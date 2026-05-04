# Liquidity

How easily an asset can be **bought or sold without significantly moving the price**. Liquidity is the most under-appreciated risk in retail finance.

## Three components

1. **Tightness** — how narrow is the bid-ask spread?
2. **Depth** — how much volume can trade at the inside price?
3. **Resilience** — how quickly does liquidity return after a large trade?

A stock with tight spreads but thin depth (only 100 shares at the inside) is **less liquid** than one with slightly wider spreads and 10,000 shares of depth.

## Worked example — depth matters

```
Stock A:                     Stock B:
  Bid: $99.99 × 100 shares     Bid: $99.95 × 5,000
  Ask: $100.01 × 100           Ask: $100.05 × 5,000
  Spread: 2 bps                Spread: 10 bps

Sell 1,000 shares (market):
  Stock A:                     Stock B:
    100 @ $99.99                 1,000 @ $99.95
    300 @ $99.50                 (full fill at inside bid)
    400 @ $99.00                 Effective price: $99.95
    200 @ $98.50
    Avg: $99.18  →  ~0.8% slippage    Effective slippage: 5 bps
```

Stock B looks "wider" but is actually far more liquid for sizes above 100 shares.

## Liquidity buckets

| Category | ADV (avg daily $ volume) | Notes |
|---|---|---|
| **Mega-liquid** | >$1B | SPY, QQQ, AAPL, MSFT — institutional-friendly |
| **Liquid** | $100M–$1B | Most large-caps |
| **Moderate** | $10M–$100M | Mid-caps |
| **Tradable** | $1M–$10M | Small-cap; size your position |
| **Illiquid** | $100k–$1M | Micro-cap; danger zone |
| **Untradable** | <$100k | OTC, nano-cap; expect 5%+ slippage |

For institutional traders: rule of thumb that you can trade ~10% of ADV per day without significant impact. Retail rarely hits this limit.

## Liquidity in different asset classes

| Asset | Typical liquidity profile |
|---|---|
| US Treasury bills | Best in the world |
| US large-cap equity | Excellent |
| US Treasury bonds (off-the-run) | Good |
| Investment-grade corporate bond | Moderate; can stale-price |
| Single-stock options | Very variable; check OI |
| ETF (popular) | Excellent (with creation/redemption arbitrage) |
| ETF (niche) | Can be wider than underlying basket |
| HY corporate bond | Moderate-poor; basket pricing in stress |
| Private equity / private credit | None until exit |
| Real estate | Days/months/years |
| Crypto major (BTC/ETH) | Excellent on top exchanges |
| Crypto small-cap | Very poor; rug-pull risk |

## Liquidity disappears in stress

Liquidity is **conditional**. The same stock that trades $100M/day in normal conditions can:
- Trade $1B/day in panic (high vol, lots of activity, but spreads explode)
- OR trade $1M/day in seizure (Mar 2020 Treasury market briefly broke)
- OR halt entirely (single-stock circuit breakers)

Famous liquidity disappearances:
- **Aug 2007 quant quake** — quant strategies all crowded into same trades, all unwound at once
- **May 2010 Flash Crash** — order book emptied for 20 minutes
- **Mar 2020 Treasury seizure** — hedge fund deleveraging; Fed intervened
- **FTX collapse Nov 2022** — single counterparty failure → market-wide crypto liquidity vacuum

## Pitfalls

- **Treating ETF liquidity as equal to underlying** — they can decouple in stress
- **Mutual fund daily liquidity** is a promise that depends on conditions
- **Crypto exchange liquidity** is exchange-specific; Binance vs Bybit can show very different depth
- **Underestimating liquidity in your "safe" position** — government bonds have failed too
- **Single-name concentration risk** — large position in mid-cap name = de facto illiquid

## Decision rules

- **Position size < 5% of ADV** for safe one-day exit
- **Position size < 1% of ADV** for one-day exit in stress
- **Match liquidity to time horizon** — illiquid asset = long horizon required
- **Always know your exit cost** before entry — model slippage in worst-case
- **In a crisis, sell what you can, not what you should** — liquidity defines what's possible

## In finterm

Markets shows ADV / volume per name. The Watchlist liquidity column flags positions that may be size-constrained. For bonds, check turnover stats; for crypto, check exchange depth.
