# LME Nickel Short Squeeze — March 2022

On March 8, 2022, nickel prices on the London Metal Exchange (LME) rose **250% in two days** — from ~$29,000/ton on March 4 to a peak of **$101,365/ton** in early Asian trading on March 8. The LME suspended nickel trading for the first time in 145 years and controversially **cancelled billions of dollars in trades** that had already executed. The episode exposed fundamental flaws in commodity market infrastructure and the fragility of concentrated short positions.

## Background

### Xiang Guangda and the Short

**Xiang Guangda** — known as "Big Shot" in Chinese commodity circles — is the founder and chairman of Tsingshan Holding Group, the world's largest nickel and stainless steel producer. He had built his empire by pioneering a low-cost process to produce nickel pig iron (NPI) from low-grade laterite ore, disrupting the traditional high-purity nickel market.

In mid-2021, Xiang built a massive short position in LME nickel futures — at its peak estimated at **150,000–200,000 metric tons** of notional exposure, roughly equivalent to several weeks of global nickel production. His stated rationale: he had access to physical nickel inventory, and prices looked elevated. A producer shorting their own product to hedge is legitimate; the position size was not.

### The Macro Context

Russia invaded Ukraine on February 24, 2022. Russia is the world's third-largest nickel producer, accounting for ~17% of global output of high-purity (class 1) nickel. Markets immediately priced in potential supply disruption. LME nickel rose from ~$25,000 to ~$29,000 in the week following the invasion.

This modest initial move masked the coming explosion. Tsingshan's short position required margin as prices rose. By early March, the position had generated enormous paper losses, and margin calls were mounting.

## The Squeeze

On March 7 (Monday), a coordinated buying effort — attributed to traders aware of Tsingshan's vulnerability — began driving the price aggressively higher. By end of day, nickel had risen 66% to $48,000/ton.

Overnight into March 8, trading on the LME's after-hours electronic platform continued. Between approximately 3 a.m. and 6 a.m. London time, nickel traded in a frenzy. With Tsingshan needing to buy to cover its short and margin calls cascading, the price exploded:

- March 7 close: $48,078/ton
- March 8 intraday peak: **$101,365/ton** — reached in pre-open electronic trading

At $101,365/ton, nickel was worth more than silver on a per-ounce basis. The move was entirely divorced from physical supply and demand.

## LME's Response — The Cancellation

The LME suspended nickel trading at approximately 8:15 a.m. on March 8 and announced it would **cancel all nickel trades executed after midnight London time** on that date — erasing approximately **$3.9 billion in executed trades**.

The justification was "disorderly market conditions." The practical reality: several banks had written put options or sold contracts to Tsingshan and its brokers (notably ICBC Standard Bank and BNP Paribas); cancellation prevented their losses from becoming settlement failures.

Critics — including multiple hedge funds — argued the LME had intervened selectively to protect systemically important counterparties at the expense of traders who had legitimately profited from the squeeze. **Elliott Management** and **Jane Street** both filed lawsuits against the LME (ultimately unsuccessful, with UK courts ruling for the LME in 2024).

Trading resumed on March 16 with daily price-move limits imposed. Nickel spent the following months slowly declining back below $30,000/ton.

## Resolution

Tsingshan negotiated a "standstill agreement" with its prime brokers (March 15, 2022): banks agreed not to force immediate liquidation of the short in exchange for Tsingshan gradually reducing the position over time. Xiang reportedly pledged personal assets. Tsingshan's total estimated loss on the position was in the billions, though the precise figure was never publicly disclosed.

The Chinese government-backed CITIC Group and other state-linked entities reportedly provided backstop financing to prevent Tsingshan from defaulting on its margin obligations — which would have triggered a cascade of failures across its prime broker counterparties.

## The Physical vs. Financial Disconnect

One important nuance: Tsingshan produces **nickel pig iron**, not the high-purity class 1 nickel that LME contracts specify. It could not deliver its physical inventory to settle LME shorts. This is why Xiang was caught in a purely financial position despite being the world's largest nickel producer — a critical detail that illustrates the basis risk between financial contracts and physical reality.

## Lessons Learned

1. **The largest physical producer can still be caught in a short squeeze.** Having the physical commodity is not the same as being able to deliver it against a specific contract specification. Basis risk — the gap between what you produce and what the contract requires — is a real and separate risk.

2. **Position size relative to market liquidity is the critical constraint.** Tsingshan's short was multiple weeks of global nickel production. A position that size cannot be covered without moving the market massively against itself.

3. **Exchange intervention creates moral hazard.** When an exchange cancels trades to protect counterparties, it socializes losses (paid by those who profited) and privatizes gains. The precedent undermines market confidence. The LME saw record volume departures to CME Group following the episode.

4. **Concentration in commodity markets is systemically dangerous.** A single entity controlling a significant fraction of the global short in a major commodity is an inherently fragile structure. Prime brokers enabled it by not aggregating cross-broker exposure.

5. **State-backed counterparties don't fail cleanly.** Tsingshan's ability to negotiate its way out of the corner (rather than being forced into liquidation) was attributable to its political and financial connections. A smaller, non-state-linked entity would have been forced to buy regardless of price.

## Related Concepts

- [[futures-mechanics|Futures Mechanics]] — how LME contracts work
- [[margin-call|Margin Call]] — the cascade mechanism
- [[liquidity|Liquidity]] — how a thin market moves on concentrated buying
- [[short-selling|Short Selling]] — the concentrated short dynamic
- [[basis-risk|Basis Risk]] — physical vs. financial contract mismatch
