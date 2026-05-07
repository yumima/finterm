# Flash Crash — May 6, 2010

At 2:32 p.m. ET on May 6, 2010, the Dow Jones Industrial Average began an extraordinary decline. Within 36 minutes, it had fallen **998.5 points** — approximately 9% — the largest intraday point decline in Dow history to that date. Then, in roughly 20 minutes, it recovered nearly all the losses. Roughly **$1 trillion** in market value evaporated temporarily.

Individual stocks traded at absurd prices: **Accenture went from $40 to $0.01**. Apple traded briefly above $100,000. Sotheby's fell from $34 to $0.01 and back to $34 — all within seconds. These were not errors. They were real trades.

## Background: The Market Structure in 2010

To understand the Flash Crash, you need to understand how equities traded in 2010. Since the early 2000s, U.S. equity markets had fragmented across more than 50 trading venues — NYSE, Nasdaq, BATS, ARCA, dark pools, and others — connected by the National Market System (NMS) rules. At the center of this system were **High-Frequency Traders (HFTs)**: firms using co-located servers, ultra-low-latency connections, and algorithmic strategies to make markets and provide liquidity.

HFTs earned money primarily through two mechanisms:
1. **Market-making**: posting limit orders on both sides of the book, earning the spread
2. **Latency arbitrage**: trading at faster speeds than competitors across venues

The crucial point: HFT market-making was *profitable only in stable conditions*. When volatility spiked, HFTs had a simple choice — withdraw from the market. Unlike traditional market makers (NYSE specialists) who had affirmative obligations to provide two-sided quotes, HFTs had no such obligation. They could, and did, simply disconnect.

## What Happened

**Background conditions on May 6**: European sovereign debt concerns were escalating (Greek parliament was debating austerity measures; riots in Athens). U.S. markets were already under pressure, down ~3% on the day before the crash began. Liquidity was thinner than normal.

**2:32 p.m.**: A mutual fund complex (**Waddell & Reed**, later identified by the Joint CFTC-SEC report) executes a large algorithmic sell order: **75,000 E-mini S&P 500 futures contracts** (~$4.1 billion notional) with an instruction to execute based on volume, with no price limits.

The algo was designed to sell 9% of the prior minute's volume — intended to distribute the order gradually. But as the market fell and volume spiked, the algorithm accelerated selling. It was self-reinforcing: as it sold, it increased the volume it was tracking, which increased its own sell rate.

**The hot potato effect**: HFT firms began buying the E-mini contracts as they fell, but quickly resold them to each other — passing the position around rather than absorbing it. The same contracts changed hands 27,000 times in 14 seconds, according to the CFTC. No fundamental buyer was absorbing supply.

**2:45 p.m.**: CME Group activates its "Stop Logic Functionality" — a brief 5-second pause in E-mini trading. This breaks the feedback loop. Prices begin recovering immediately.

The rebound in E-mini futures should have pulled equity prices back through arbitrage. But the damage in individual stocks took longer to unwind because:
- Many HFTs had **paused or restricted** their market-making as volatility spiked
- **Stub quotes** — placeholder orders at extreme prices ($0.01, $999,999) that market makers post when they're effectively withdrawing — became the best available bids/asks
- As markets fell and crossed through these stub quotes, legitimate market orders executed at $0.01 for stocks worth $40

## The $0.01 Trades

Exchanges had "clearly erroneous trade" rules, and about 20,500 trades — representing around $1 billion in notional — were busted (cancelled) after the fact. This included the Accenture $0.01 trades.

This created its own controversy: market participants who had bought "cheap" on the way down and sold on the way back up saw their profits erased by the cancellations. The rules for what constituted a "clearly erroneous" trade were defined but applied inconsistently.

## Regulatory Response

The Joint CFTC-SEC report (October 2010) identified Waddell & Reed's order but stopped short of calling it the cause; the order "lit the fuse" in an already fragile environment.

- **Circuit breakers** for individual stocks were introduced (halt if ±10% in 5 minutes)
- **Market-wide circuit breakers** were updated
- **Stub quote rules** were tightened — market makers must post quotes within specified ranges
- The Consolidated Audit Trail (CAT) was proposed to create a unified audit trail across all venues

A decade later, in 2015, Navinder Singh Sarao — a British HFT trader working from his parents' house in Hounslow, England — was arrested and pleaded guilty to spoofing E-mini futures. Whether his activity *caused* the Flash Crash or merely amplified it remains disputed; his orders totaled ~$200M, small relative to the $4.1B Waddell & Reed order.

## Lessons Learned

1. **HFT liquidity is conditional, not structural.** In calm markets, HFTs provide cheap, abundant liquidity. In stress, they withdraw simultaneously — creating a "liquidity air pocket" precisely when it's most needed. This is the central market-structure criticism of HFT-dominated markets.

2. **Algorithmic orders without price limits are dangerous at scale.** A $4B sell order with no price constraint in a fragile market is a wrecking ball. Most institutional algorithms now have price limits specifically because of May 6.

3. **Market fragmentation creates synchronization failures.** With 50+ trading venues, price discovery can fragment. An E-mini futures drop was supposed to be arbitraged away in milliseconds; instead, the arbitrage mechanism itself was overwhelmed.

4. **Circuit breakers work but must be coordinated.** CME's 5-second pause stopped the spiral. Individual stock circuit breakers prevent the $0.01 trades. These are now permanent features of market infrastructure.

5. **In a crash, market orders become limit orders at any price.** If you place a market order when a stock is temporarily trading at $0.01, you get filled at $0.01. Limit orders are not optional during volatility.

## Related Concepts

- [[liquidity|Liquidity]] — the HFT withdrawal dynamic
- [[market-order|Market Order]] — the danger of market orders in thin markets
- [[volatility|Volatility]] — how VIX spiked and retracted
- [[futures-mechanics|Futures Mechanics]] — how E-mini drives equity markets
- [[case-volmageddon-2018|Volmageddon 2018]] — a later case of ETF/ETP feedback loops
