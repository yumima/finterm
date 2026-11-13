# Market-Making and Alpha Combos (HFT)

**Market-making** is the strategy of **earning the bid-ask spread by quoting on both sides** of an order book. It is the foundational HFT strategy: in a market where retail order flow is mostly "dumb" (uninformed), quoting bid and ask continuously yields a steady cents-per-share profit. In a market where order flow is mostly "smart" (informed), the same naïve quoting strategy is a way to lose money fast — the **adverse selection** problem.

Kakushadze & Serur (2018) §3.19–§3.20 cover market-making and the related "alpha combo" methodology. This entry merges them because in modern HFT shops they're often the same desk: market-make on hundreds of names simultaneously, modulated by short-horizon predictive signals (alphas) for direction.

## The naïve market-making rule

```
At any time:
  Place a limit order to BUY at the current bid.
  Place a limit order to SELL at the current ask.       (Eq. 3.94)

When the bid order fills: you bought a share at the bid.
When the ask order fills: you sold a share at the ask.
Net profit per round-trip: (ask − bid) = the spread.
```

In a stock with $0.01 bid-ask spread and 10 million shares of daily volume, capturing a meaningful fraction of that spread is a multi-million-dollar business. Multiply across hundreds of stocks and you have a strategy.

## Adverse selection — the central problem

The naïve rule fails because **informed traders disproportionately hit your quote when you're wrong**.

Imagine the stock's "true value" is $50.00 and you're quoting $49.99 bid / $50.01 ask. Then:

- **Uninformed traders** (retail, mechanical algos) cross your spread at random. Your bid fills sometimes, your ask fills sometimes. On average, you earn the spread on these.
- **Informed traders** — those with private information that the true value is now $50.10 — hit your ask aggressively at $50.01 and don't hit your bid. You sell at $50.01, the price moves to $50.10, you're down $0.09 on each share you sold.

**Adverse selection** is the asymmetry: your quotes get filled disproportionately by counterparties who know you're wrong. The book (§3.19) puts it succinctly: "where most order flow is smart (informed), this strategy as stated would lose money."

## How market-makers actually survive

Three mechanisms:

### 1. Stay on the "right side" via short-horizon signals

Have a price-prediction signal active in real time. If the signal says "price will rise":
- **Keep the bid up** (you want to buy here).
- **Pull the ask** (or move it higher — you don't want to sell here).

If the signal says "price will fall":
- **Pull the bid**.
- **Keep the ask down**.

This is **directional market-making**: you're not symmetrically quoting; you're modulating quotes based on signal. The book (§3.19) describes this as having a "short-horizon signal indicating the direction of the market."

### 2. Modulate with longer-horizon signals

Even when the short-term signal says "price will fall in the next minute," a *longer-horizon* signal might say "price will rise over the next hour." If the longer signal is strong, you might be willing to lose a few cents on adverse selection to establish a position at a price you'll exit profitably later.

The book makes this exact point: "Such aggressive order flow is not dumb but smart, as it is based on nontrivial short- and long-horizon signals with a positive expected return."

The marriage of market-making with directional signals is the **distinguishing feature of modern HFT firms** (Virtu, Citadel Securities, Jane Street, IMC, Tower Research) over the simpler market-makers of the 1990s.

### 3. Be #1 in queue — speed matters

When you and ten other market-makers post at the same bid, only the first one in queue gets filled when an aggressor sells. The book (§3.19) notes: "the trader would have to be #1 in the queue among many other market participants placing limit orders at the same price point. This is where high frequency trading comes in — it is essentially all about speed."

Modern HFT firms invest hundreds of millions in:
- **Co-location** at exchanges.
- **Microwave/laser links** between Chicago and New York (faster than fibre).
- **FPGA-based execution** (sub-microsecond decision-to-order).

The latency arms race exists because **being half a microsecond faster than the next firm captures the lion's share of profitable trades**.

## Alpha combos — combining hundreds of signals

The book's §3.20 covers the methodology for combining hundreds-to-thousands of individual short-horizon "alphas" (predictive signals).

The challenge: individual HFT alphas are extremely faint (predictive power on the order of 0.1% per period) but **many of them are roughly uncorrelated**. Combining N alphas multiplies the signal magnitude by ~√N if their correlations are low.

The book's recipe (§3.20):

```
1. Time series of M+1 realised alpha returns R_is for each alpha i.
2. Serially demean: X_is = R_is − mean.
3. Variance-normalise: Y_is = X_is / σ_i.
4. Cross-sectionally demean: Λ_is = Y_is − cross-section mean.
5. Compute expected returns E_i (e.g., trailing d-day average).
6. Normalise: Ẽ_i = E_i / σ_i.
7. Regress Ẽ_i on Λ_is to get residuals ε̃_i.
8. Set weights w_i = η · ε̃_i / σ_i.
9. Normalise so Σ|w_i| = 1.                              (Eq. 3.95 and surrounding)
```

The endpoint is a **mega-alpha** — a linear combination of hundreds of underlying signals, weighted by their expected returns adjusted for cross-correlation and volatility.

## How HFT firms actually generate signals

Real HFT signals come from:

- **Order book imbalances** (more buy orders than sell orders).
- **Time-and-sales analysis** (recent print direction and size).
- **Correlated-asset moves** (S&P futures move → individual stocks move with a lag).
- **News and event detection** (Reuters/Bloomberg keyword triggers).
- **Cross-venue price latency** (a quote on NYSE may lag the same quote on BATS by microseconds).
- **Quote sniping** (detecting when a stale quote is about to be updated).

Each signal has weak individual predictive power. Combined, with the alpha-combo machinery, they generate a profitable trading book.

## Capacity and economics

Market-making and HFT have **finite capacity**. The strategy works because:

- Most volume is uninformed (retail, passive flow).
- A small fraction of HFT firms can extract spreads from this flow.

If too many HFT firms compete for the same uninformed flow, **spreads compress** and per-trade profits shrink. This has empirically happened over 2010–2020: industry-wide HFT revenue declined despite higher volumes, as competition intensified.

Total US equity HFT revenue:
- 2010: ~$7B/year.
- 2018: ~$1.5B/year.

The business has consolidated to a few large players (Citadel Securities, Virtu, Jane Street) operating at scale.

## When market-making strategies fail

- **High-volatility regimes**: spreads widen, but so does adverse selection. May 6, 2010 Flash Crash, March 2020 — many market-makers pulled quotes entirely.
- **News events**: pre-positioned market-makers get run over by big directional moves.
- **Information asymmetries**: trading against a single counterparty with better information (a corporate insider, a hedge fund with research).
- **Regulatory shocks**: new tick sizes, new auction rules, new latency requirements can favour different infrastructure.

## What this means for non-HFT readers

Most readers of this catalogue will never run a true market-making strategy — the infrastructure cost is prohibitive ($10M+ to start). But understanding market-making matters because:

1. **You are the uninformed counterparty.** Your retail or institutional orders cross the spread. Market-makers profit from your trades.
2. **Bid-ask spread is a real cost.** Most retail traders underestimate it. A 1bp spread on a $100K position is $10 per trade.
3. **Execution strategies that minimise spread crossing matter**: VWAP, TWAP, limit orders, dark pools. These are *anti*-market-making techniques.
4. **HFT improves your execution.** Tighter spreads = lower costs for liquidity takers. The 2010s saw US large-cap spreads compress from ~$0.03 to ~$0.01 partly due to HFT competition.

## Common mistakes

- **Thinking you can market-make at retail latency.** You can't. Retail brokers route your "limit-at-bid" orders to internalisers who fill them against their own inventory — the broker captures the spread, not you.
- **Confusing market-making with momentum.** Market-making is symmetric quote provision; momentum is directional. They have opposite alpha sources.
- **Underestimating adverse selection.** Naïve market-making models often assume order flow is random. It isn't.
- **Treating spreads as risk-free.** They're not. You earn spreads on average but lose to informed flow on individual trades.

## Risk management essentials

- **Inventory limits**: cap aggregate position size per stock.
- **Adverse-selection kill switches**: if recent fills suggest informed flow, pull quotes for a cooling-off period.
- **News-event filters**: pull quotes when news triggers fire.
- **Position-aging**: don't hold market-making inventory overnight unless that's the explicit strategy.

## Where to do this in the terminal

- **Algo Trading** — execution algorithms that interact with market-making (smart-order routing, dark-pool access).
- **Backtesting** — limit-order-book simulation for studying market-making strategies (limited; full HFT backtesting requires tick-level data and microsecond timestamps).
- **AI Quant Lab** — alpha-combo construction module for combining multiple short-horizon signals.

## See also

- [[stat-arb|Statistical Arbitrage]] — close cousin (also high-frequency, cross-sectional)
- [[stocks-multifactor|Multifactor Portfolio]] — alpha combos in slower-horizon settings
- [[fx-triangular-arbitrage|FX Triangular Arbitrage]] — the FX-market analogue of HFT
- [[strategy-patterns|Strategy Patterns]] — market-making as a strategy archetype

## External references

- Glosten, L., Milgrom, P. (1985). "Bid, Ask and Transaction Prices in a Specialist Market with Heterogeneously Informed Traders." *Journal of Financial Economics* 14(1), 71–100.
- Kyle, A. (1985). "Continuous Auctions and Insider Trading." *Econometrica* 53(6), 1315–1336.
- Avellaneda, M., Stoikov, S. (2008). "High-frequency Trading in a Limit Order Book." *Quantitative Finance* 8(3), 217–224.
- Aldridge, I. (2013). *High-Frequency Trading: A Practical Guide to Algorithmic Strategies and Trading Systems* (2nd ed.). Wiley.
- Hasbrouck, J., Saar, G. (2013). "Low-Latency Trading." *Journal of Financial Markets* 16(4), 646–679.
- Brogaard, J., Hendershott, T., Riordan, R. (2014). "High-Frequency Trading and Price Discovery." *Review of Financial Studies* 27(8), 2267–2306.
- Menkveld, A. (2013). "High Frequency Trading and the New Market Makers." *Journal of Financial Markets* 16(4), 712–740.
- O'Hara, M. (2015). "High Frequency Market Microstructure." *Journal of Financial Economics* 116(2), 257–270.
- Kirilenko, A., Kyle, A., Samadi, M., Tuzun, T. (2017). "The Flash Crash: High-Frequency Trading in an Electronic Market." *Journal of Finance* 72(3), 967–998.
- Kakushadze, Z., Yu, W. (2017). "How to Combine a Billion Alphas." *Journal of Asset Management* 18(1), 64–80.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.19–§3.20. https://doi.org/10.1007/978-3-030-02792-6
