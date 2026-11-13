# Quality Factor (QMJ, Profitability, Earnings Stability)

Buy companies that earn high stable returns on capital. The third pillar of modern factor investing after value and momentum — and arguably the easiest one to defend on first principles, since "good businesses outperform bad businesses" is closer to common sense than "cheap beats expensive."

This entry covers **Asness-Frazzini-Pedersen's QMJ definition, Novy-Marx's gross profitability premium, the profitability/safety/growth decomposition, and why quality is often called the factor that "works for free."**

## Novy-Marx — gross profitability

Novy-Marx (2013), "The Other Side of Value: The Gross Profitability Premium" (*JFE* 108), kicked the modern quality literature into gear with a single simple measure:

```
GP/A = (Revenue − Cost of Goods Sold) / Total Assets
```

The claim was sharp: **gross profitability has roughly the same power to predict the cross-section of stock returns as book-to-market**, but in the *opposite* direction relative to other measures of "good" companies. High-GP firms are typically growth firms that have high B/M (i.e., are *expensive*), so the long-short premium had been hiding inside the value sort.

Why gross profits rather than net? Gross profits are the cleanest measure of a firm's underlying productivity — they're not yet contaminated by SG&A and R&D spending decisions, which firms make strategically (high-quality firms invest more in R&D, which depresses net earnings but is not a sign of weak business economics).

Result: a long-short portfolio sorted on GP/A earned ~0.3% per month, with Sharpe comparable to HML, and a **negative correlation** with HML. That negative correlation is the key — quality and value combined give Sharpe far above either alone.

## Quality Minus Junk — Asness-Frazzini-Pedersen

Asness, Frazzini, Pedersen (AQR, working paper 2013; published 2019 *RAF* 24), "Quality Minus Junk" (QMJ), built the practitioner-standard quality factor by defining quality as a **composite of profitability + safety + growth + payout**:

- **Profitability** — gross profits / assets, ROE, ROA, accruals, cash flow / assets (negative weight on accruals).
- **Safety** — low beta, low idiosyncratic volatility, low leverage, low bankruptcy risk (Ohlson O-score, Altman Z-score), low ROE volatility.
- **Growth** — five-year growth in profitability measures.
- **Payout** — net stock issuance (negative weight: firms issuing lots of stock are lower-quality), net debt issuance (negative).

Each component is z-scored within country and industry; the quality score is the average of the component z-scores. QMJ is then the long-short portfolio sorted on this composite.

Results from the QMJ paper (24 countries, 1986-2012):

- **QMJ Sharpe ~0.8** in the US, 0.6-1.0 in other markets — among the highest of any documented factor.
- Risk-adjusted return after controlling for market, size, value, momentum: **alpha of ~4-6% per year**.
- **Negative correlation with HML** (~−0.5). High-quality firms tend to be expensive, so value-sorted long-shorts are mechanically short quality. Combining QMJ with HML is the canonical diversifying pair.

## "Quality works for free"

Asness-Frazzini-Pedersen's framing: under any reasonable Gordon growth model, **high-quality firms should trade at higher multiples than low-quality firms**. If profitability, growth, and safety are all higher, the fair P/B for the firm is higher. So in equilibrium you'd expect no excess return from being long high-quality and short low-quality — the prices should already reflect it.

The empirical fact: prices reflect quality **less than they should**. The "explained-by-fundamentals" multiple gap between high- and low-quality firms is wider than the actual multiple gap. So buying quality gets you the better business at less than the warranted premium — you get the quality "for free."

This is the cleanest behavioral story in factor investing. Investors systematically pay too much for low-quality lottery-like stocks (chasing growth, gambling) and not enough for boring high-quality compounders — the same mechanism behind the [[low-vol-factor|low-volatility anomaly]].

## The Buffett decomposition

Frazzini, Kabiller, Pedersen (2013), "Buffett's Alpha," decomposed Berkshire Hathaway's record (1976-2011) into systematic factor exposures:

- Positive loading on **market**, **value** (HML), **quality** (QMJ), **low beta** (BAB).
- Average leverage ~1.6× (cheap insurance float as financing).
- After accounting for these factors, **alpha statistically indistinguishable from zero**.

The interpretation: Buffett's edge wasn't unobservable genius — it was an early, leveraged, disciplined application of **value × quality × low-beta**. The same combination available to any systematic investor today.

## Implementation choices

- **Profitability metric.** Gross profits / assets (Novy-Marx), operating profits / book equity (Fama-French 5-factor), or ROE/ROIC. All work; gross profits / assets is most robust in the smallest-cap segment where accruals manipulation is worst.
- **Industry / sector neutralization.** Critical. Without it, the quality portfolio loads heavily on staples, healthcare, software and shorts financials/energy. Neutralize within GICS sector or industry.
- **Composite vs single metric.** Composites are noisier per metric but more stable in aggregate. QMJ uses ~10 underlying variables; simpler 3-metric composites (profitability + safety + growth) capture most of the alpha.
- **Combine with value.** The standard institutional implementation is a value × quality double-sort or a composite z-score that adds value and quality with equal weight. Drastically reduces the 2007-2020 value drawdown.

## Where quality doesn't work

- **Junk rallies.** In risk-on rebounds from severe sell-offs (April 2009, April 2020), low-quality stocks beat high-quality dramatically. QMJ had its worst months in these episodes.
- **Highly leveraged carry / yield environments.** Cheap debt benefits low-quality more than high-quality firms.
- **Late-cycle growth euphorias.** 1998-2000, 2019-2021. Money flows to highest-growth, lowest-profit names; QMJ underperforms.

These are the same regimes where value also tends to underperform — which is why combining value with quality doesn't fully diversify against macro regime risk, only against single-factor crashes.

## Common pitfalls

- **Accounting accruals.** Naive earnings-based quality metrics get gamed. Earnings minus operating cash flow (accruals) should be on the *safety* side of the score, not the profitability side.
- **Survivorship bias is worse than for value.** Bankrupt low-quality firms drop out of the universe; the surviving low-quality firms look less bad than they really were. Use point-in-time universe.
- **Static thresholds.** Quality is industry-specific (banks have different ROA distributions than software firms). Always rank within sector.
- **Conflating quality with growth.** Quality firms have stable, high returns on capital. Growth firms have rapid sales/earnings expansion. They overlap but aren't the same; growth as a standalone factor has historically had a *negative* premium.

## See also

- [[factor-investing|Factor investing]] — the umbrella
- [[value-factor|Value factor]] — value × quality is the high-conviction combination
- [[low-vol-factor|Low-volatility factor]] — closely related (low-beta firms tend to be high-quality)
- [[regression-basics|Regression basics]] — Fama-French 5-factor estimation

## External references

- Asness, Frazzini, Pedersen (2019). "Quality Minus Junk." *Review of Accounting Studies* 24(1).
- Novy-Marx (2013). "The Other Side of Value: The Gross Profitability Premium." *JFE* 108(1).
- Fama & French (2015). "A Five-Factor Asset Pricing Model." *JFE* 116(1).
- Frazzini, Kabiller, Pedersen (2018). "Buffett's Alpha." *Financial Analysts Journal* 74(4).
- Sloan (1996). "Do Stock Prices Fully Reflect Information in Accruals and Cash Flows About Future Earnings?" *Accounting Review* 71(3).
