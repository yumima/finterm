# Dispersion Trading

**Dispersion trading** is one of the most distinctive options strategies in existence: take **long volatility positions on each of the index constituents** (single-stock straddles) and an **offsetting short position on the index** (index straddle). The trade isolates **correlation** — it pays when realised correlation among constituents is lower than implied correlation, and loses when realised correlation exceeds implied.

It is the canonical **correlation trade**, run by volatility arbitrage desks at investment banks and specialised hedge funds. Kakushadze & Serur (2018) §6.3 give the construction in one page with a particularly elegant statistical-risk-model refinement.

## The math — why long-stock-vol vs. short-index-vol = short correlation

The theoretical index volatility is determined by single-stock vols and pairwise correlations:

```
σ²_I  =  Σ_{i,j} w_i · w_j · σ_i · σ_j · ρ_{ij}          (Eq. 6.3)
```

Where:
- `w_i` = weight of stock `i` in the index.
- `σ_i` = implied volatility of stock `i` from single-stock options.
- `ρ_{ij}` = pairwise correlation matrix.

Now consider the **implied** index volatility (from index options): call it `σ̃_I`.

If `σ̃_I > σ_I` (index options imply higher vol than the theoretical relationship would predict), then either:
- The market expects higher single-stock vols than currently priced (unlikely to be wrong systematically), or
- The market expects higher correlations than priced into the average `ρ_{ij}` (much more common).

**Empirically, implied index vol is usually higher than the theoretical value** — meaning the average implied correlation is higher than realised correlation tends to be. This is the **correlation risk premium**.

The strategy harvests this premium:

```
Long N single-stock straddles (one per constituent).
Short M index straddles.

Sized so the dollar-gamma exposure on the single-stock side equals
that on the index side.
```

When realised correlation comes in *lower* than the implied (the usual case), single stocks move idiosyncratically — single-stock straddles realise their value while the index straddle decays slowly because the moves cancel. The trade profits.

When correlations *spike* (crisis episodes, where everything moves together), the index straddle realises huge value while individual single-stock straddles also rise but **less than the index** because individual moves are dwarfed by the systematic move. The trade loses.

## The sizing — how many single-stock options per index option

The book's recipe (§6.3, Eq. 6.4):

```
n_i  =  (S_i · P_I) / Σ_j (S_j · P_j)
```

Where `S_i` = shares outstanding for stock `i` and `P_I` = index level. With this weighting, `P_I = Σ_i n_i · P_i`, so the **index option straddle payoff matches the weighted sum of single-stock straddle payoffs as closely as possible**.

All options have approximately 1 month to expiration. Hold until expiry.

## Why does the correlation risk premium exist?

The economic explanation is **demand-based option pricing** (Gârleanu, Pedersen, Poteshman 2009):

- Many institutional investors (mutual funds, pension funds) buy index puts as portfolio insurance.
- This buying pressure pushes up index option implied vol.
- The same investors don't equally buy single-stock options.
- The result: index IV is bid up relative to weighted single-stock IV.

Dispersion traders sell into the bid (short index straddles) and buy single-stock options (which are not bid up). They earn the gap.

## What about the singular correlation matrix?

The naive correlation matrix `ρ_{ij}` is **singular** for a typical lookback period (1 year of daily data, ~252 observations) when N is large (500 for S&P 500). The book's refinement (§6.3):

Replace `ρ_{ij}` with a **statistical risk model** that is non-singular and stable. The construction (Kakushadze & Yu 2017):

```
Let V^(A)_i be the principal components of ρ_{ij} with eigenvalues λ^(A)
in decreasing order.

Statistical risk model:
ψ_{ij}  =  ξ²_i · δ_{ij}  +  Σ_{A=1}^{K} λ^(A) · V^(A)_i · V^(A)_j     (Eq. 6.5)

Specific risk:
ξ²_i  =  1  −  Σ_A λ^(A) · [V^(A)_i]²                                  (Eq. 6.6)

Then use ψ_{ij} (not ρ_{ij}) in Eq. 6.3.
```

The result decomposes index vol into:
- **Systematic component**: from the top K principal components.
- **Specific component**: from individual stock idiosyncratic risk.

For S&P 500, typical choice: K = effective rank (eRank, Roy-Vetterli 2007), often around 50–100 PCs needed to capture meaningful structure.

The **long portfolio only includes the lowest specific-risk stocks** (lowest `w²_i · σ²_i · ξ²_i`) — for S&P 500, often N* = 100 stocks instead of 500. This dramatically reduces transaction costs while capturing most of the dispersion premium.

## Empirical record

Dispersion trading was one of the **most profitable options strategies** of the 2000s. Several hedge funds built their books on it (Capstone Investment Advisors, Susquehanna's options groups).

- **2003–2007**: realised correlations were low; dispersion was a steady earner.
- **2008**: correlations spiked to historical highs (everything sold off together). Dispersion traders had huge losses. Several funds blew up.
- **2009–2018**: dispersion recovered as correlations normalised post-crisis.
- **2018 vol spike (XIV liquidation)**: short-vol structures including dispersion were hit.
- **2020 March COVID**: correlations spiked; dispersion lost.
- **2020–2021 retail rally**: low correlations (meme stocks moved on idiosyncratic factors); dispersion paid.

**Average annualised premium**: 3–6% net of costs over long samples, with periodic drawdowns of 15–25%.

## The 2008 lesson

In a tail event, **correlations go to 1**. Every stock moves with the market because:
- Liquidity providers are stretched.
- Investors deleverage indiscriminately.
- Macro fear dominates fundamental information.

When ρ → 1, the index option's gamma realisation explodes while single-stock options provide diminishing offsetting gamma. The trade has a **negative skew** payoff profile, similar to short-puts on the market.

Dispersion traders survive by:
- **Sizing for the tail**, not the average.
- **Hedging with deep-OTM index puts** (a "wing hedge" that caps the catastrophic loss).
- **Stopping the trade** when implied correlation enters historically low percentiles (signal that the premium is being arbitraged or that a crisis is imminent).

## Variants

### Variance swap dispersion

Instead of straddles (which require continuous delta-hedging to maintain pure vol exposure), use **variance swaps** on the index and constituents. Variance swaps are pure-vol instruments with no Greeks to manage.

Trade construction:
```
Long N variance swaps on constituents (notional ∝ index weight).
Short M variance swap on the index.
```

This is the cleanest implementation but requires a counterparty willing to write the variance swaps — typically a bank dealer.

### Correlation swaps

A direct play on realised vs. implied correlation. Banks offer **correlation swaps** with payoff = (implied correlation − realised correlation) × notional. Cleaner than dispersion but illiquid.

### Subset dispersion (book §6.3)

Use only K largest constituents instead of all N. Reduces transaction costs and captures most of the systematic correlation.

## Practical issues

- **Single-stock option liquidity**: not all S&P 500 names have liquid options. The most-liquid ~200 names trade reasonably; the rest can have wide spreads.
- **Index option liquidity**: SPX and SPY options are extremely liquid; less so for smaller-index options (Russell, NASDAQ-100 are also liquid).
- **Margin requirements**: short index options require substantial margin. Brokers may impose haircuts.
- **Rebalance frequency**: monthly (option expiry cycle). Some implementations roll early to avoid expiry-week dynamics.

## Common mistakes

- **Equal-weighting single-stock options without index-weight adjustment**. The book is explicit about the Eq. 6.4 weighting. Without it, the dispersion isn't properly hedged.
- **Ignoring transaction costs**. Single-stock option bid-ask spreads of 5–15 bps × 500 names × monthly rebalancing = significant drag.
- **Treating the trade as pure correlation**. It's also a vol-of-vol trade and is sensitive to skew shifts. Greeks are complex.
- **Not adjusting for dividend-related option pricing**. Dividends affect single-stock options differently from index options (the index is pre-dividend; individual stocks are post-dividend).

## Risk management essentials

- **Tail hedge**: long out-of-the-money index puts to cap catastrophic loss. Cost: 50–100 bps per year.
- **Per-name position cap**: don't load up on single-name straddles in the highest-vol names.
- **Correlation regime monitoring**: when realised correlation > implied for several days, reduce position size.
- **VIX overlay**: dispersion typically underperforms when VIX > 25.

## Where to do this in the terminal

- **AI Quant Lab** — dispersion trading template with statistical-risk-model construction.
- **Derivatives screen** — implied vs. theoretical index vol comparison; correlation surface.
- **Surface Analytics** — implied correlation surface visualisation.

## See also

- [[volatility-quant|Volatility Trading and the Surface]] — the broader vol framework
- [[index-cash-and-carry|Cash-and-Carry Arbitrage]] — the cousin index arbitrage strategy
- [[index-vol-targeting|Index Volatility Targeting]] — directional vol overlay
- [[stocks-implied-volatility-signal|Implied Volatility Signal]] — single-stock vol signal
- [[strategy-patterns|Strategy Patterns]] — volatility as an archetypal premium

## External references

- Driessen, J., Maenhout, P., Vilkov, G. (2009). "The Price of Correlation Risk: Evidence from Equity Options." *Journal of Finance* 64(3), 1377–1406.
- Gârleanu, N., Pedersen, L., Poteshman, A. (2009). "Demand-Based Option Pricing." *Review of Financial Studies* 22(10), 4259–4299.
- Marshall, C. (2009). "Dispersion Trading: Empirical Evidence from U.S. Options Markets." *Global Finance Journal* 20(3), 289–301.
- Deng, Q. (2008). "Volatility Dispersion Trading." Working paper, SSRN 1156620.
- Kakushadze, Z., Yu, W. (2017). "Statistical Risk Models." *Journal of Investment Strategies* 6(2), 1–40.
- Meissner, G. (2016). "Correlation Trading Strategies: Opportunities and Limitations." *Journal of Trading* 11(4), 14–32.
- Nelken, I. (2006). "Variance Swap Volatility Dispersion." *Derivatives Use, Trading & Regulation* 11(4), 334–344.
- Bakshi, G., Kapadia, N. (2003). "Volatility Risk Premiums Embedded in Individual Equity Options." *Journal of Derivatives* 11(1), 45–54.
- Bollen, N., Whaley, R. (2004). "Does Net Buying Pressure Affect the Shape of Implied Volatility Functions?" *Journal of Finance* 59(2), 711–753.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §6.3. https://doi.org/10.1007/978-3-030-02792-6
