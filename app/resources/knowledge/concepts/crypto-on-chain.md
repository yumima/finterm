# Crypto On-Chain Fundamentals

Unlike equities or bonds, cryptocurrencies have no earnings, no cash flows, and no management guidance. But they do have something else: a **public ledger** — the blockchain — that records every transaction, wallet balance, and network activity in real time. On-chain analysis is the discipline of reading that ledger to assess value, momentum, and risk.

This article focuses on Bitcoin, where on-chain data is most mature and most studied, with notes on where Ethereum differs.

## Why on-chain data matters

Equity analysts model future earnings. Macro analysts read GDP and CPI. On-chain analysts read the blockchain itself — who is holding what, at what cost, for how long, and with what level of network activity. These metrics give you a window into:

- **Valuation**: is the current price above or below a reasonable estimate of fair value?
- **Holder behavior**: are long-term holders distributing or accumulating?
- **Network health**: is adoption growing or contracting?
- **Miner economics**: is mining profitable or stressed?

## MVRV: Market Value to Realized Value

**Realized Value** (or Realized Cap) is a Bitcoin-specific concept: instead of valuing all bitcoins at the current market price, each coin is valued at the price it last moved on-chain (the last time it was transacted). This gives an estimate of the aggregate cost basis of all coins.

```
Realized Cap = Sum of (each coin × price when it last moved)
Market Cap   = Current price × circulating supply
MVRV         = Market Cap / Realized Cap
```

**Interpretation**:
- **MVRV > 3.5–4**: Market is substantially above aggregate cost basis; most holders are sitting on large unrealized gains; historically a high-risk zone and often near cycle tops
- **MVRV near 1**: Market price is near the aggregate cost basis; most coins are at breakeven; historically a value zone
- **MVRV < 1**: Market is below aggregate cost basis; average holder is in loss; historically near cycle bottoms

The 2021 Bitcoin cycle peaked with MVRV ~3.9. The 2022 lows corresponded to MVRV dropping below 1, consistent with prior bear market bottoms. MVRV is one of the most respected on-chain valuation metrics.

## NVT: Network Value to Transactions

NVT (proposed by Willy Woo) is the crypto analog to a P/E ratio:

```
NVT = Network Value (Market Cap) / Daily Transaction Volume (USD)
```

A high NVT means the network is valued highly relative to its actual transaction utility. A low NVT means the network processes a large economic value relative to its market cap.

**NVT Signal** (NVT-S) uses a 90-day moving average of transaction volume to reduce noise. A rising NVT-S while price is stable or falling often precedes corrections. Falling NVT-S while price consolidates is a bullish signal.

Limitation: NVT is more relevant for Bitcoin-as-payment-network; for Bitcoin-as-store-of-value, transaction volume is less tied to fundamental utility.

## Hash rate and miner security

**Hash rate** is the total computational power being devoted to mining Bitcoin — the sum of all mining machines globally, measured in exahashes per second (EH/s).

Hash rate is a measure of **network security** and miner confidence:

- Rising hash rate: more miners are adding equipment; they expect future profitability; network security increasing
- Falling hash rate: miners are turning off machines; either energy prices rose, bitcoin price fell, or next-generation equipment displaced old hardware
- Hash rate **never lies** (unlike price, which can be manipulated); miners vote with real capital and electricity

**Mining difficulty** adjusts every ~2 weeks (every 2016 blocks) to keep block times near 10 minutes. If hash rate rises, difficulty goes up. If hash rate falls, difficulty drops. This automatic adjustment is what makes Bitcoin resilient to large swings in mining participation.

**Miner revenue** (block reward + fees) relative to their cost of production is a proxy for network sustainability. When Bitcoin price falls near or below **cost of production** for marginal miners (typically $25,000–$45,000 depending on energy costs and equipment generation), miner selling pressure intensifies as they liquidate to cover costs.

## Active addresses

**Active addresses** counts the number of unique Bitcoin addresses involved in transactions each day. It is the closest proxy to **user adoption and actual economic use**:

- Rising active addresses with rising price = legitimate demand growth; healthier signal
- Rising price but flat or falling active addresses = price driven by speculation without underlying adoption growth
- Falling active addresses in bear markets suggest reduced interest; recovery requires a re-acceleration

For comparison across cycles, analysts use a 30- or 90-day moving average to smooth daily volatility.

## Long-term holder (LTH) vs. short-term holder (STH) behavior

On-chain data allows segmenting coin supply by holding duration. Coins that haven't moved in more than 155 days are classified as **Long-Term Holder (LTH) supply**; shorter-held coins are **Short-Term Holder (STH) supply**.

Key patterns:

- **LTH supply rising in bear markets**: long-term holders are accumulating; historically a sign of a maturing bottom
- **LTH supply falling in bull markets**: long-term holders are distributing to new buyers; typically necessary for a bull market top
- **STH supply rising sharply**: new market entrants buying; demand is broadening

**SOPR** (Spent Output Profit Ratio) measures whether coins being moved are being sold at a profit or loss. SOPR > 1 means coins are being moved at a profit on average; SOPR < 1 means loss-taking. Bear markets are characterized by sustained SOPR below 1; recovery is often confirmed when SOPR crosses back above 1 and holds.

## Exchange flow

Tracking Bitcoin moving **to and from exchanges** is a real-time indicator of selling intent:

- **High exchange inflows**: coins moving to exchanges are typically being prepared for sale; often bearish short-term
- **High exchange outflows** (to cold wallets): coins being withdrawn from exchanges signal long-term holding intent; bullish

Exchange reserves — the total BTC held on exchange wallets — declining over time is a structural bullish signal: supply available for immediate sale is shrinking.

## Ethereum-specific on-chain metrics

For Ethereum, additional metrics matter:

- **Staked ETH**: post-Merge (September 2022), ETH is staked to secure the network. Rising stake ratio reduces circulating supply and signals long-term conviction
- **Gas fees**: ETH spent on transaction fees (burned post-EIP-1559) signals network demand; high fees = congestion = active use
- **Net issuance**: EIP-1559 burns a portion of fees; when burns exceed new issuance, ETH supply is deflationary — this is described as ETH becoming "ultrasound money"
- **DeFi and stablecoin TVL**: Total Value Locked in DeFi protocols on Ethereum is a measure of ecosystem financial activity

## Limitations of on-chain analysis

On-chain data is transparent but can be **misread**:

- **Exchange wallets**: a single exchange controls many addresses; internal transfers look like on-chain volume
- **UTXO accounting**: the "last moved" price in Realized Cap ignores wallets that hold multiple entry tranches
- **Privacy coins and L2s**: activity moving off-chain (Lightning Network, Arbitrum) reduces on-chain signal quality
- **Manipulation**: wash trading in some smaller coins inflates volume metrics

On-chain analysis is one input, not a complete trading system. Macro conditions, regulatory risk, and Bitcoin cycle position all matter alongside the on-chain picture.

## In finterm

The Crypto screen (where available) shows current price alongside key on-chain metrics including MVRV, active addresses, and exchange flows. Compare current readings to historical cycle context — the absolute level matters less than the percentile within prior cycles.
