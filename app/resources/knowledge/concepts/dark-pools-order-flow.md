# Dark Pools and Order Flow

About 40% of US equity volume never touches a public exchange. It executes in a parallel world of dark pools, wholesaler internalization, and alternative trading systems — invisible to the consolidated tape until after the fact. Understanding this infrastructure explains why your market order sometimes fills at surprising prices, and why professional traders spend significant effort on execution quality.

## The visible vs. the invisible market

When you look at the bid-ask spread on a stock, you're seeing the **National Best Bid and Offer (NBBO)** — the best visible quotes across all lit exchanges. What you're not seeing:

- Hidden and iceberg orders on lit exchanges
- Orders queued in broker dark pools
- Flow pre-committed to wholesaler internalization
- Orders being worked by institutional desks in negotiated block trades

The NBBO is a floor, not a ceiling, for execution quality. And it's often far from where large orders actually trade.

## Payment for order flow (PFOF)

Most US retail brokers — especially zero-commission brokers — generate revenue by selling their customers' order flow to **wholesalers** (market makers like Citadel Securities, Virtu Financial, Jane Street). The wholesaler:

1. Gets first look at your retail order before it reaches an exchange
2. Chooses to internalize the trade at the NBBO or slightly better
3. Pays the broker a per-share rebate (PFOF)
4. Profits on the bid-ask spread plus any informational value in the flow

**The stated benefit for you**: you often get "price improvement" — a fill slightly better than the displayed quote. On a $50 stock with a $0.01 spread, you might save $0.002/share.

**The hidden cost**: your flow is being used. Wholesalers make money on your orders, which means they extract something from you on net — it's just in a form you can't easily measure. The larger the order and the faster your intention is revealed, the more you give up.

**Regulatory status**: PFOF is legal in the US but banned in the UK, Canada, and most of Europe, where markets generally have tighter spreads and less internalization.

## What dark pools actually are

Dark pools are **private trading venues** that allow participants to execute large orders without displaying their intentions to the market. They come in several flavors:

| Type | Examples | Typical users |
|---|---|---|
| **Broker-operated** | Goldman Sigma X, Morgan Stanley MSPooling | Buy-side institutions using the bank for execution |
| **Independent / agency** | Liquidnet, BIDS Trading | Large block trades between institutions |
| **Exchange-operated** | NYSE Regulation, Cboe BIDS | Overflow from lit exchanges |
| **HFT / electronic** | Various ATSes | Algorithmic flow |

The core value proposition: **price discovery happens elsewhere (on lit exchanges), but your large order can execute at those prices without moving them**. If a fund wants to buy 2 million shares of a stock, posting that intention on a lit exchange would move the price against them before they finished — dark pools prevent this.

## The institutional perspective

For a large asset manager, the execution challenge is real. Moving even 0.5% of a stock's average daily volume can cause 30–50 basis points of market impact. The difference between good and bad execution on a $500 million position is millions of dollars per year.

Institutional execution desks use:

- **VWAP/TWAP algorithms**: spread the order over time to minimize impact
- **Dark pool routing**: send non-displayed orders to find contra interest
- **Block facilitation**: call a dealer desk to find the other side
- **Dark aggregators**: algorithmic routers that scan multiple dark venues simultaneously

The "implementation shortfall" — the gap between the decision price when you decided to trade and the actual execution price — is the standard measure of institutional execution quality.

## What this means for retail traders

Retail flow is generally **uninformed** in the eyes of wholesalers — retail traders don't have the informational edge that predatory HFT strategies exploit. This is actually why wholesalers are willing to internalize retail flow at tight spreads: they're not being front-run by it.

But several situations push retail execution quality down:

- **Fast-moving news**: in the seconds after a major announcement, spreads widen and wholesalers reduce risk exposure; fills worsen
- **Thin names**: low-ADV stocks have wide spreads in dark venues too; internalization doesn't help much
- **Market orders in illiquid options**: options markets are shallower; PFOF is more extractive
- **Pre-market and post-market**: no consolidated market; fills are from individual venues with worse quotes
- **Meme stocks in crisis** (Jan 2021 GME): extremely unusual order flow overwhelmed internalization models

## Order routing transparency

Brokers are required to publish **Rule 606 reports** quarterly, disclosing where they route orders and how much PFOF they receive. Most retail traders never read these. Key things to look for:

- What percentage of orders go to a single wholesaler?
- Is the broker capturing PFOF on options? (Often higher than stocks)
- Is there payment for directed flow on limit orders?

Smart-order routing (SOR) on institutional-grade brokers scans multiple venues for best execution. Retail brokers often route by contract, not order-by-order.

## The informational content of order flow

In aggregate, **order flow carries information** — the direction and size of executed orders predicts short-term price movements. This is why:

- HFT firms pay large amounts for co-location and data feeds
- Some hedge funds analyze "alternative data" including broker-level flow summaries
- Consolidation of PFOF into a few wholesalers gives those firms extraordinary market intelligence

The market microstructure concept of **order flow toxicity** measures how much adverse selection a market maker faces — how often the order they just took the other side of turns out to be "informed" and moves against them. See [[market-microstructure]] for more on this framework.

## Internalization and the controversy

Critics argue PFOF and internalization:
- Fragments liquidity, making the lit order book less deep
- Creates a conflict of interest between brokers and clients
- Concentrates market-making in a few hands, creating systemic fragility

Defenders argue:
- Retail investors demonstrably get price improvement vs. exchange NBBO
- Competition among wholesalers is fierce; margins are thin
- Without zero-commission trading, retail investors would pay much more

The SEC proposed significant reform (Rule 605 overhaul, best execution rules) in 2022–2023. Full abolition of PFOF for US equities has been proposed but not enacted.

## Decision rules

- **Use limit orders** for anything that isn't a mega-cap liquid stock; don't give a wholesaler an easy market order to clip
- **Don't trade in the first and last minutes** of regular session in names where you don't have to
- **Check your fills** against the NBBO — most brokers report the reference price at the time of execution
- **For larger positions**, consider a broker with direct market access (DMA) rather than PFOF routing

## In finterm

The Market Microstructure view shows real-time depth and recent prints. Cross-reference print prices against the displayed NBBO to observe price improvement — or lack of it — in different market conditions.
