# Archegos Capital — March 2021

In the last week of March 2021, several of Wall Street's largest prime brokerage desks executed massive block trades in names like ViacomCBS, Discovery, Baidu, and GSX Techedu. No public announcement. No obvious news. The trades were forced liquidations from a single family office that most people had never heard of: **Archegos Capital Management**.

By the time the dust settled, Archegos's collapse wiped out over **$100 billion in market capitalization** from related stocks, cost banks **$10 billion in losses**, and led to criminal charges against its founder Bill Hwang.

## Background

Bill Hwang ran a hedge fund called Tiger Asia from 2001 to 2012, generating strong returns as a "Tiger cub" — one of many managers who trained under Julian Robertson at Tiger Management. In 2012, he pleaded guilty to insider trading and paid a $44 million settlement with the SEC. Several banks cut ties with him.

By 2013, Hwang had reconstituted as a family office (managing only his own money), which placed him outside most hedge fund reporting requirements. Over the following years he rebuilt a spectacular track record — reportedly turning ~$200 million into **$35 billion** by 2021. But the method was dangerous.

## The Structure — Total Return Swaps

Instead of buying stocks directly, Hwang used **total return swaps (TRS)** through multiple prime brokers. In a TRS, the bank buys the stock and takes legal title; Hwang receives the equity return (including dividends) and pays a financing fee. The crucial consequence: **Hwang's positions didn't appear in 13F filings or on Bloomberg's ownership screens**, because the bank was the nominal owner.

He held overlapping TRS positions with at least **six major prime brokers simultaneously**: Goldman Sachs, Morgan Stanley, Credit Suisse, Nomura, Deutsche Bank, and UBS. Each broker could see only its own book — not the aggregate exposure. Total leverage was estimated at **5:1 to 8:1** — meaning a $50 billion gross position funded with perhaps $10 billion of equity.

His largest positions were concentrated in a handful of names:
- ViacomCBS (now Paramount): ~$20B notional
- Discovery Communications: ~$10B notional
- Various Chinese ADRs: Baidu, GSX Techedu, Vipshop

## What Happened

The trigger was a **secondary offering by ViacomCBS on March 22, 2021**. ViacomCBS management sold $3 billion in new shares at $85. The stock was already trading near all-time highs — which had been driven partly by Archegos's own massive hidden buying. When the offering dropped the price (as secondaries do), momentum reversed.

- March 22: ViacomCBS announces secondary at $85 — stock falls to $75 the next day
- March 24–25: More selling pressure; Archegos's mark-to-market equity erodes as positions fall
- March 25 (evening): Archegos **fails to meet margin calls** from prime brokers
- March 25–26: A frantic call among prime brokers to coordinate an orderly unwind fails. **Goldman and Morgan Stanley break ranks** and begin selling immediately. Credit Suisse and Nomura wait.
- March 26: Goldman, Morgan Stanley execute multi-billion-dollar block sales: $22B of stock moves in a single morning. ViacomCBS falls 27% in one day. Discovery falls 27%.
- Credit Suisse and Nomura, which waited, end up holding positions that continue declining through the weekend. Their total losses: Credit Suisse ~$5.5 billion, Nomura ~$2.9 billion.

## The Losses

| Bank | Loss |
|---|---|
| Credit Suisse | ~$5.5B |
| Nomura | ~$2.9B |
| Morgan Stanley | ~$1B |
| UBS | ~$800M |
| Goldman Sachs | Minimal (sold first) |
| Deutsche Bank | Minimal (sold first) |

Goldman's early exit was decisive: they sold $6.6 billion in blocks before market open on March 26, at prices 10–15% higher than where the stock ended the day. Being first to sell in a forced-liquidation situation is worth billions.

Bill Hwang was arrested in April 2022 and charged with racketeering, fraud, and market manipulation. He was convicted in July 2024. CFO Patrick Halligan was also convicted.

## Why It Happened

The failure had three layers:

**Concentration.** Hwang owned a structurally significant percentage of the float of several companies through swaps. When he became a forced seller, there was no natural buyer at anything close to prevailing prices.

**Opacity.** Each prime broker extended credit as if Archegos had no other positions. Total notional exceeded $100 billion with $10–15 billion of equity — a leverage ratio no single bank would have permitted had they seen the aggregate book.

**Coordination failure.** Once margin was breached, the orderly unwind required trust between prime brokers. Goldman and Morgan Stanley rationally defected: being first to sell meant better prices and lower losses. This is a classic prisoner's dilemma.

## Lessons Learned

1. **Swap structures can hide position concentration.** Total return swaps don't appear in public filings. During the Archegos blowup, ViacomCBS had no idea it was a position — the company discovered it through price action.

2. **Prime brokers have a coordination problem.** Each broker optimizes for itself. In a forced unwind, the first to sell wins. This creates a race to liquidate that amplifies losses for everyone who waits.

3. **Family offices operate outside normal reporting.** Funds above $100M must file 13Fs quarterly; family offices often don't. Archegos's opacity was structural, not accidental.

4. **Concentrated leveraged positions have non-linear risk.** A 10% price drop on a 7:1 leveraged position wipes 70% of equity. There's almost no time to respond.

5. **The banks also lost.** Credit Suisse's $5.5B loss accelerated its eventual collapse (it failed in 2023). Concentration risk in counterparties is real.

## Related Concepts

- [[margin-call|Margin Call]] — the cascade trigger
- [[liquidity|Liquidity]] — how large positions become illiquid
- [[float|Float]] — how Archegos owned more than was visible
- [[leverage|Leverage]] — the amplification mechanism
- [[counterparty-risk|Counterparty Risk]] — prime broker exposure
