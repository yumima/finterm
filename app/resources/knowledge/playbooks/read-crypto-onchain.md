# How to Read Crypto On-Chain Data

On-chain data is the blockchain's publicly available transaction record — permanently logged, auditable, and surprisingly revealing about market conditions. Unlike traditional markets where positioning data lags by weeks, on-chain metrics are real-time. This playbook covers the most actionable on-chain signals for Bitcoin and Ethereum.

## When to Use This

Use this when assessing whether the market is in an early-stage accumulation zone, at a structural top, in a distribution phase, or in capitulation. On-chain data is most useful as a medium-term framework (weeks to months), not a short-term timing tool.

---

## Step-by-Step Process

### Step 1: MVRV Ratio — Are You Buying at a Profit or Loss to Others?

**MVRV = Market Value / Realized Value**

- **Market Value**: current price × total supply = standard market cap
- **Realized Value**: the value of all coins at the price they last moved on-chain (each coin is valued at its last transaction price, not current price)

**Interpretation:**
- MVRV > 3.5: historically signals overheating; prior cycle tops occurred in this zone (2017: MVRV ~4.7; 2021: ~3.8)
- MVRV 1.0–2.0: fair value zone; moderate accumulation
- MVRV < 1.0: market trading below its aggregate cost basis — historically the deepest capitulation zones and best long-term entry points

**Why it works**: When MVRV is high, the aggregate holder base is sitting on large unrealized gains — and the incentive to sell is highest. When MVRV is below 1, the average coin holder is underwater, and many weak hands have already sold.

### Step 2: NVT Ratio — Network Value to Transactions

**NVT = Network Value (Market Cap) / Daily On-Chain Transaction Volume**

This is roughly the crypto equivalent of a P/E ratio: how much are you paying per unit of economic activity on the network?

- High NVT (>150 for Bitcoin): network is valued highly relative to actual usage; may indicate speculative premium
- Low NVT (<50): network usage is high relative to price; historically preceded bull runs
- NVT Signal (90-day smoothed NVT): better for trend analysis than raw daily NVT

**Limitation**: NVT is a blunt instrument because "transaction volume" includes internal transfers and exchange movements. It works better as a long-term trend indicator than a tactical signal.

### Step 3: Exchange Flows — Are Coins Moving to Sell or to Hold?

Coins sitting in a personal wallet are illiquid — they signal long-term holding intent. Coins deposited to exchanges are liquid — they can be sold immediately.

**Watch two flows:**
- **Exchange inflows**: coins moving *to* exchanges. Sustained large inflows are historically precede selling pressure (people deposit to sell)
- **Exchange outflows**: coins moving *off* exchanges to cold storage. Sustained outflows indicate accumulation and reduced sell-side supply

**Derivative exchange flows** (BTC/ETH moving to Bybit, Binance futures, etc.) can signal leveraged long or short buildup.

**Stablecoin exchange balances**: rising stablecoin balances on exchanges indicate dry powder — potential buying power sitting ready to deploy.

### Step 4: Long-Term vs. Short-Term Holder Supply

Blockchain analysis tools (Glassnode, CryptoQuant) separate the supply held by:

- **Long-term holders (LTH)**: coins that haven't moved in 155+ days — statistically likely to be held by conviction investors
- **Short-term holders (STH)**: recently moved coins — more likely to represent recent buyers

**Key signals:**
- When LTH supply is rising and STH supply is falling: accumulation phase, LTH absorbing distribution
- When LTH begins distributing into rising prices: late-cycle signal; these are your most experienced sellers

### Step 5: Hash Rate (Bitcoin-specific)

Hash rate measures the total computational power securing the Bitcoin network.

- **Rising hash rate**: miners are investing in new equipment; economically rational when they expect sustained or rising prices. Represents miner conviction.
- **Hash rate crash (miner capitulation)**: when mining is unprofitable (e.g., during bear markets), miners shut down, hash rate drops. Historically correlated with price bottoms — the weakest miners sell their coins and leave, removing persistent sell pressure.
- **Difficulty adjustment**: Bitcoin automatically adjusts mining difficulty every ~2 weeks. Post-capitulation difficulty drops can signal that the bottom is near.

### Step 6: Funding Rates (Derivatives)

Funding rates on perpetual futures tell you the cost of the leveraged long position:

- **Positive funding (>0.01% per 8 hours)**: longs are paying shorts; market is leveraged long; complacent
- **Negative funding**: shorts are paying longs; market is leveraged short or bearish
- **Extremely positive funding** (>0.05% per 8 hours): often precedes sharp corrections as over-leveraged longs get liquidated

---

## Common Mistakes

- **Using on-chain data for short-term timing.** MVRV and NVT operate on weeks-to-months timescales. A low MVRV doesn't mean the bottom is in today.
- **Ignoring the difference between Bitcoin and Ethereum on-chain metrics.** Ethereum's tokenomics (staking, EIP-1559 burning) mean the same metrics have different interpretations.
- **Treating a single signal as definitive.** Use on-chain data as one input alongside macro context, sentiment, and technical levels — not as a standalone system.
- **Confusing exchange inflows for all-time-high selling.** Large exchange inflows can also represent institutional custody (e.g., ETF creation/redemption). Context matters.

---

## What Success Looks Like

You can look at the current MVRV ratio, exchange flow trend, LTH supply change, and funding rate — and synthesize them into a coherent position on whether the market is in accumulation, distribution, or a neutral zone. You use this to inform sizing decisions, not as a binary buy/sell trigger.
