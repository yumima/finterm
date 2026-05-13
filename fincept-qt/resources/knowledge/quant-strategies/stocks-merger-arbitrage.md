# Merger (Risk) Arbitrage

**Merger arbitrage** — also called "risk arbitrage" — captures the **spread between a publicly-announced M&A offer price and the target company's current trading price**. After an acquisition is announced, the target trades below the offer price (because the deal might fall through). The arbitrageur buys the target, hedges if necessary, and collects the spread when the deal closes.

This is one of the oldest systematic strategies on Wall Street, run by **Goldman Sachs's arbitrage desk** since the 1960s and now by dedicated hedge funds (Pentwater, Sachem Head, P. Schoenfeld) and event-driven funds globally. Kakushadze & Serur (2018) §3.16 give the construction tersely.

The strategy has produced steady, bond-like returns for decades, with the occasional brutal drawdown when deals collapse en masse — most famously in 2008.

## The two deal types

### Cash deals

Acquirer offers $X cash per target share. Target trades below $X — say at $X − $1. The arbitrage:

```
1. Buy 1 share of target at $X − $1.
2. Wait for deal to close (typically 3–6 months).
3. Receive $X cash at close.
4. Profit: $1 per share, minus financing cost.
```

Annualised return depends on time-to-close. A $1 spread on a $50 stock over 6 months = 4% gain = ~8% annualised.

### Stock deals

Acquirer offers `r` shares of acquirer stock per target share. The book's example (§3.16): A offers $67, B trades at $35, deal terms 2 shares of B per 1 share of A.

```
1. Buy 1 share of A at $67.
2. Short 2 shares of B at $35 each (proceeds: $70).
3. Net cash credit: $70 − $67 = $3 per share of A bought.
4. At deal close: 1 share A → receive 2 shares B, used to cover the short.
5. Profit: $3 per share of A.
```

The short hedges the stock-deal risk. If A appreciates (or B depreciates) on factors unrelated to the deal, the trade is unhurt because the position is constructed as a self-hedging spread.

## The probabilistic structure

The target trades below the offer price because there's **deal break risk** — the deal might not close. Mathematically:

```
Target price  =  Offer × P(close)  +  Break price × P(no close)
```

If `P(close) = 95%`, `Offer = $50`, `Break price = $30`, then:
```
Target  =  50 × 0.95 + 30 × 0.05  =  47.5 + 1.5  =  $49
```

So a target trading at $49 vs. a $50 offer reflects a 5% probability that the deal breaks and the stock falls to $30 — a $20 downside.

**The arbitrageur is selling deal-break insurance**.

## Why does this work?

Three economic explanations:

### 1. Compensation for deal-break tail risk

Deal breaks happen due to:
- **Regulatory rejection** (antitrust — DOJ, FTC, EU Commission).
- **Shareholder rejection** of the offer.
- **Material adverse change** (MAC) clauses — pandemic, war, earnings collapse.
- **Hostile competing bids** (occasionally good for the arb if the new bid is higher).
- **Financing failures** (rare in modern era; more common pre-2008).

When a deal breaks, the target stock typically falls 15–30% in a day. The 4–8% annualised spread is the premium for bearing this binary risk.

### 2. Patient capital arbitrage

Many shareholders **want out immediately** once a deal is announced — they got the merger premium, they don't want to bear 6 months of regulatory uncertainty. They sell at the discount. The arbitrageur is the patient buyer.

The premium is compensation for the patience and the regulatory-process-tracking work.

### 3. Specialised skill barriers

Successful merger arb requires:
- Deep understanding of antitrust law.
- Ability to estimate political/regulatory risk.
- Patience and balance sheet stability.

These are non-trivial barriers that keep the trade from being arbitraged to zero.

## Empirical record

**Mitchell & Pulvino (2001) "Characteristics of Risk and Return in Risk Arbitrage"** — the canonical study. Their findings:

- Annualised return: 6–10% net of costs over 1963–1998.
- Sharpe: ~0.8 in normal periods.
- **Beta to market**: roughly 0 in normal periods, but ~0.5 in crisis periods. The return profile is **nonlinear** — like a short put on the market.
- **Maximum drawdown**: 15–25%, typically clustered in periods of broad deal breaks (1990 recession, 2001 dot-com bust, 2008 GFC).

The "short-put-on-the-market" characterisation is important: merger arb earns positively when markets are calm and breaks down when markets crash because deal-break risk rises with macro stress.

## The 2008 episode

In Q3–Q4 2008:
- Multiple deals fell through (Lehman-Korea Development Bank, Bear Stearns negotiations, several middle-market deals).
- Spreads widened dramatically across all live deals.
- Merger arb funds had drawdowns of 15–30%.

Then in 2009 spreads compressed and the trade made back the losses within 9 months. The lesson: the trade is real, the premium is real, but you need to survive the bad years to collect.

## Practical execution

### Universe

- US large/mid-cap announced M&A deals.
- Typical universe size: 20–60 live deals at any time.
- Deal size threshold: avoid deals < $500M (illiquid target stocks).

### Position sizing

- Cap each deal to ~2–5% of book.
- Vol-scale based on estimated deal volatility (regulatory complexity, size, sector).

### Diversification

- Across sectors (don't load up on tech-tech deals during a tech wave).
- Across deal types (cash vs. stock).
- Across regulatory regimes (US, EU, China-affected).

### Monitoring

- Watch regulatory milestones: HSR approval (US antitrust), EU Commission filing, shareholder vote dates, expected closing dates.
- Track competing bid emergence — sometimes new bidders raise the offer (good for arb), sometimes they create complexity that delays closing (mixed).

## Sources of failure

### Antitrust blocks

Recent examples:
- **AT&T / T-Mobile (2011)**: blocked by DOJ. Target T-Mobile rallied long-term but spread blew out before the block.
- **Halliburton / Baker Hughes (2016)**: blocked by DOJ. Spread widened dramatically.
- **NVIDIA / Arm (2022)**: blocked by FTC. Spread had already moved against the trade.

### Activist interference

Sometimes shareholders or activists demand a higher price. If the bidder walks rather than raise, the deal breaks.

### Macro shocks

The 2008 episode was unusual — multiple unrelated deals broke as financial conditions tightened.

### Foreign government intervention

CFIUS (US) and similar bodies in EU/UK/China can block deals on national-security grounds. Tech and semis deals are particularly exposed.

## Variants

### Long-only deal participation

Some funds run merger arb as a **long-only** strategy on cash deals (no short leg needed since the offer is cash). Simpler operationally, similar risk profile.

### Cross-border arbitrage

Deals between companies in different jurisdictions. Adds FX risk (which can be hedged) and regulatory complexity in two countries.

### SPAC arbitrage

Special Purpose Acquisition Companies pre-merger trade close to NAV with a put-like floor. Arbitrageurs buy the SPAC, hold for the proxy vote, vote against bad mergers and redeem at par. Boomed 2020–2021, collapsed 2022.

### Going-private deals

Same logic but the target is being taken private (acquirer is a financial buyer like KKR). Often higher break risk because financing is critical. Higher historical spreads as compensation.

## Common mistakes

- **Underestimating regulatory risk.** "It's a small deal in a non-controversial sector" — until it isn't. Always run a base case + regulatory-block scenario.
- **Concentration on one bidder.** If you have 5 trades on deals being acquired by Microsoft, you have concentrated bidder risk.
- **Holding to close in shaky deals.** If the spread has widened materially, the market is telling you something. Sometimes the right move is to take the loss and exit, not double down.
- **Ignoring borrow costs on stock deals.** The short leg of a stock deal accumulates borrow cost. On hard-to-borrow targets, this can eat the spread.
- **Not modelling time-to-close uncertainty.** A 6-month base case can become 12 months if antitrust gets involved. The IRR collapses.

## Risk management essentials

- **Per-deal stop-loss**: if the spread widens by X%, reassess.
- **Aggregate stop-loss**: if total merger-arb book drawdown exceeds 5%, reduce gross exposure.
- **Concentration limits**: by deal size, sector, bidder.
- **Liquidity reserve**: maintain 20–30% of book in cash for forced unwinds.
- **Tail hedge**: some funds buy out-of-the-money index puts to hedge the systematic deal-break tail (the 2008 case).

## Where to do this in the terminal

- **Equity Research** — M&A deal screener with spread, expected close date, regulatory status.
- **AI Quant Lab** — merger-arb strategy template with regulatory-risk scoring.
- **Backtesting** — historical merger-arb P&L with 2008 stress-period flagged.

## See also

- [[pairs-trading|Pairs Trading]] — cousin event-driven strategy
- [[stocks-multifactor|Multifactor Portfolio]] — merger arb as an event-driven component
- [[volatility-quant|Volatility Trading]] — the short-put-on-the-market characterisation
- [[strategy-patterns|Strategy Patterns]] — event-driven as a strategy category

## External references

- Mitchell, M., Pulvino, T. (2001). "Characteristics of Risk and Return in Risk Arbitrage." *Journal of Finance* 56(6), 2135–2175.
- Baker, M., Savaşoglu, S. (2002). "Limited Arbitrage in Mergers and Acquisitions." *Journal of Financial Economics* 64(1), 91–115.
- Andrade, G., Mitchell, M., Stafford, E. (2001). "New Evidence and Perspectives on Mergers." *Journal of Economic Perspectives* 15(2), 103–120.
- Karolyi, G., Shannon, J. (1999). "Where's the Risk in Risk Arbitrage?" *Canadian Investment Review* 12(2), 12–18.
- Larcker, D., Lys, T. (1987). "An Empirical Analysis of the Incentives to Engage in Costly Information Acquisition." *Journal of Financial Economics* 18(1), 111–126.
- Hsieh, J., Walkling, R. (2005). "Determinants and Implications of Arbitrage Holdings in Acquisitions." *Journal of Financial Economics* 77(3), 605–648.
- Maheswaran, K., Yeoh, S. (2005). "The Profitability of Merger Arbitrage." *Australian Journal of Management* 30(2), 295–316.
- Officer, M. (2004). "Collars and Renegotiation in Mergers and Acquisitions." *Journal of Finance* 59(6), 2719–2743.
- Cornelli, F., Li, D. (2002). "Risk Arbitrage in Takeovers." *Review of Financial Studies* 15(3), 837–868.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.16. https://doi.org/10.1007/978-3-030-02792-6
