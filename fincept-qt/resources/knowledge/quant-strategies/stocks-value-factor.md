# Value Factor (Book-to-Price)

The **value premium** — cheap stocks (high book-to-price ratio) outperform expensive stocks (low book-to-price ratio) on a risk-adjusted basis over multi-year horizons — is the second most-studied equity anomaly (after size), the cornerstone of the **Fama-French three-factor model**, and the strategy that defined a generation of "value investing" funds.

It is also the **factor that almost died** between 2007 and 2020 — a 13-year drawdown that prompted serious academic debate about whether the anomaly had been arbitraged away. Then 2021–2022 happened.

Kakushadze & Serur (2018) §3.3 cover the construction tersely. This entry expands the definition variants, the post-2007 controversy, and the practitioner adjustments that kept value alive when the classical version stopped working.

## The signal — Book-to-Price (B/P)

```
B/P_i  =  Book value of equity_i / Market capitalisation_i
```

Or equivalently:
```
B/M_i  =  Book value per share / Price per share
```

A high B/P means the company is trading **near or below its accounting book value** — usually because the market doesn't expect much growth or sees risk. A low B/P means it's trading at a large premium to book — typically growth, brand value, or intangibles not on the balance sheet.

The strategy (§3.3):

```
1. Each year (or quarter), rank stocks by B/P.
2. Long the top decile (cheapest = highest B/P).
3. Short the bottom decile (most expensive = lowest B/P).
4. Hold for 1–12 months. Rebalance.
```

## Why does this work?

The classical case is **Fama & French (1992, 1993)**. Three economic stories:

### 1. Risk-based — value stocks are riskier

Cheap stocks are often **distressed** or in declining industries (telecom in 2003, banks in 2009, energy in 2016, energy/financials in 2020). Investors demand a higher expected return to hold them. The "value premium" is compensation for this distress risk.

Fama-French argued this was the right way to interpret value: it's not anomalous, it's risk.

### 2. Behavioural — investors over-extrapolate growth

**Lakonishok, Shleifer, Vishny (1994) "Contrarian Investment, Extrapolation, and Risk"**: investors project recent earnings growth too far into the future. Growth stocks get overpriced; value stocks get underpriced. When reality reverts to mean (most growth eventually slows), the cheap stocks recover and the expensive ones disappoint.

This is the **"glamour vs. value"** framing — and is empirically supported by the fact that value stocks' subsequent earnings are typically better than priced in, and growth stocks' are worse.

### 3. Limits to arbitrage

Value stocks are often illiquid, in declining industries, and require holding through 1–3 years of underperformance before paying off. Many institutional investors face career and benchmark risk that makes long-holding-period contrarian bets impossible to maintain.

## Definition variants — and they matter

The "B/P" of the textbook is one possibility. Practitioners use many:

| Definition | Source | Comment |
|---|---|---|
| **Book-to-Price (B/P)** | Fama-French | Original; book value can be stale and mismeasured |
| **Earnings-to-Price (E/P)** | Basu 1977 | Trailing-12-month earnings; simpler but earnings-volatile |
| **Cash-Flow-to-Price (CF/P)** | Lakonishok et al. 1994 | More stable than earnings |
| **Sales-to-Price (S/P)** | Levine 1989 | Useful for companies with negative earnings |
| **Dividend Yield** | Long history | Old-school, limited to dividend-payers |
| **EV/EBITDA** | Industry practice | Capital-structure neutral |
| **Composite score** | Asness et al. 2013 | Average of standardised B/P, E/P, CF/P, S/P |

Asness, Moskowitz, Pedersen (2013) found that the **composite value score** is more stable than any single ratio and explains essentially all the value premium documented across asset classes.

## The 2007–2020 value drawdown

From mid-2007 to late 2020, the long-value / short-growth portfolio in US equities lost ~30%, with growth (tech, especially FANG/MAANG) crushing value (energy, financials, materials).

Three explanations:

### 1. Intangibles broke the book value measure

Modern companies have huge intangible assets — software, brand, IP, customer base — that **don't appear on the balance sheet**. A company like Microsoft or Google might have $1B of book value but $1T of market cap. By the textbook B/P metric, they are "growth"; by the economic-reality metric, they might just be reasonably priced.

**Arnott, Harvey, Kalesnik, Linnainmaa (2021) "Reports of Value's Death May Be Greatly Exaggerated"** quantified this: adjusting book value to include intangibles partially restores the value premium.

### 2. Macro regime shifts

Persistent zero interest rates and quantitative easing benefited long-duration assets (growth stocks have cash flows far in the future) and hurt short-duration assets (value stocks have near-term cash flows). The 2007–2021 ZIRP regime was structurally adverse for the value trade.

### 3. Sector concentration

A pure B/P sort circa 2018 put essentially all of large tech in the *short* leg and all of energy + banks in the *long* leg. When tech rallied and energy stagnated, the trade lost — but this was as much a sector bet as a value bet.

## 2021–2023 — value revived

When inflation returned in 2021, the macro regime flipped. Long-duration assets sold off (Nasdaq −33% in 2022), short-duration assets held up (energy was the best-performing sector in 2022 by a wide margin). Long-value / short-growth delivered roughly 30%+ in 2022 alone — its best year in two decades.

This revival did not bring value back to its pre-2007 levels. But it reminded everyone that **value's drawdowns are macro-regime-driven, not death of the factor**.

## Variants — combined and improved

### Composite value (Asness et al. 2013)

```
v_i  =  rank(B/P_i) + rank(E/P_i) + rank(CF/P_i) + rank(S/P_i)
```

Average rank across multiple valuation metrics. More stable than any single ratio.

### Industry-neutral value

Within each industry, rank by value. Combine across industries. Avoids the "long energy, short tech" sector concentration.

### Quality-adjusted value (QMJ + Value, Asness et al. 2019)

Combine value (cheap) with quality (high profitability, low leverage). A stock that is both cheap AND high-quality is a much stronger long than a stock that is just cheap (which might be a value trap).

### Profitability-augmented (Novy-Marx 2013)

Long high-B/P + high-profitability stocks. Short low-B/P + low-profitability. **Novy-Marx (2013) "The Other Side of Value: The Gross Profitability Premium"** documented that profitability is a separate factor that diversifies value beautifully.

## Common mistakes

- **Using stale book values.** Book value is updated quarterly. Don't use a year-old book against today's price.
- **Ignoring negative book equity.** Some distressed companies have negative book. B/P is undefined. Exclude them or use alternative ratios.
- **Not adjusting for intangibles.** Especially post-2010, this is a real issue. Tech companies look artificially "expensive" by B/P because their intangibles aren't on the balance sheet.
- **Holding too long.** Academic studies use 1–5 year holds. Live operators use 1–12 months. Beyond that, the alpha decays and turnover savings disappear.
- **Treating all "cheap" stocks the same.** A cheap small-cap bank in distress is different from a cheap mega-cap utility. Industry and quality overlays matter.

## Risk management essentials

- **Cap sector exposure.** Without sector neutralisation, value portfolios get concentrated in a few sectors (typically financials, energy, materials).
- **Filter for distress.** Stocks with very high B/P often have legitimate fundamental problems. Combine with credit-spread filter (avoid stocks where bond spreads > 500bps over Treasuries unless you specifically want distressed exposure).
- **Pair with quality.** Cheap + high-quality is the durable trade. Cheap + low-quality is the value trap.
- **Monitor regime indicators.** Value performance is correlated with the term spread, real rates, and inflation expectations. When the regime is value-adverse (falling rates, low inflation, growth dominance), reduce position size.

## Where to do this in the terminal

- **AI Quant Lab** — value factor template with multiple definition choices and combination weights.
- **Equity Research** — multi-valuation-ratio screener.
- **Backtesting** — value strategy P&L decomposed by sector and regime.

## See also

- [[stocks-multifactor|Multifactor Portfolio]] — value combined with momentum, low-vol, quality
- [[stocks-price-momentum|Price Momentum]] — the natural diversifier (often negatively correlated with value)
- [[stocks-residual-momentum|Residual Momentum]] — momentum after stripping FF factors
- [[strategy-patterns|Strategy Patterns]] — value as one of the archetypal premia

## External references

- Basu, S. (1977). "Investment Performance of Common Stocks in Relation to Their Price-Earnings Ratios." *Journal of Finance* 32(3), 663–682.
- Fama, E., French, K. (1992). "The Cross-Section of Expected Stock Returns." *Journal of Finance* 47(2), 427–465.
- Fama, E., French (1993). "Common Risk Factors in the Returns on Stocks and Bonds." *Journal of Financial Economics* 33(1), 3–56.
- Lakonishok, J., Shleifer, A., Vishny, R. (1994). "Contrarian Investment, Extrapolation, and Risk." *Journal of Finance* 49(5), 1541–1578.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Novy-Marx, R. (2013). "The Other Side of Value: The Gross Profitability Premium." *Journal of Financial Economics* 108(1), 1–28.
- Asness, C., Frazzini, A., Pedersen, L. (2019). "Quality Minus Junk." *Review of Accounting Studies* 24(1), 34–112.
- Arnott, R., Harvey, C., Kalesnik, V., Linnainmaa, J. (2021). "Reports of Value's Death May Be Greatly Exaggerated." *Financial Analysts Journal* 77(1), 44–67.
- Rosenberg, B., Reid, K., Lanstein, R. (1985). "Persuasive Evidence of Market Inefficiency." *Journal of Portfolio Management* 11(3), 9–16.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.3. https://doi.org/10.1007/978-3-030-02792-6
