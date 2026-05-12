# Value Factor (HML, B/M, EBIT/EV)

Buy cheap. The oldest documented anomaly in equities, the centerpiece of [[factor-investing|factor investing]], and — depending on what window you look at — either the greatest empirical regularity in finance or a strategy that just spent 13 years getting destroyed.

This entry covers the **original Fama-French construction, the alternative ratios (EBIT/EV, FCF/EV), the 2007-2020 drawdown, and the AQR case for why value isn't dead**.

## The original Fama-French construction

Fama & French (1992), "The Cross-Section of Expected Stock Returns" (*JF* 47), documented that book-to-market (B/M) ratios predict the cross-section of returns better than CAPM beta does. Fama & French (1993), "Common Risk Factors in the Returns on Stocks and Bonds" (*JFE* 33), formalized this into the HML factor:

```
HML = (1/2)·(Small-Value + Big-Value) − (1/2)·(Small-Growth + Big-Growth)
```

Construction recipe (the version that runs at every quant shop):

1. **Sort universe by size** (market cap) at end of June, into Small (bottom 50%) and Big (top 50%).
2. **Sort independently by B/M** using book value from previous fiscal year-end and market value from December: bottom 30% = Growth, middle 40% = Neutral, top 30% = Value.
3. **Form 6 portfolios** (2×3 intersection), value-weight within each.
4. **HML** = average of the two value portfolios minus average of the two growth portfolios.
5. **Rebalance annually** at end of June.

Historical performance (Ken French's data library, US, 1927-2020): HML averaged ~3.5% per year with Sharpe ~0.3-0.4. Not huge — but stacked with SMB and the market factor, it explained 90%+ of the cross-sectional variation in diversified portfolio returns. The Fama-French 3-factor model (Mkt-Rf, SMB, HML) dethroned single-factor CAPM in the 1990s; the 5-factor extension (Fama & French 2015) added profitability and investment factors.

## Why book-to-market

B/M isn't magic — it's a noisy proxy for "expected return relative to fundamentals." High B/M firms are typically those the market has priced for distress: low growth, financial stress, or out-of-favor industries. The factor return is compensation for bearing this risk (Fama-French's interpretation) **or** correction of behavioral mispricing (Lakonishok-Shleifer-Vishny 1994, *JF* 49). Both stories survive in the literature; for a practitioner the mechanism matters less than the persistence.

Empirical points worth knowing:

- **Value works internationally.** Fama & French (1998), "Value vs Growth: International Evidence" (*JF* 53): documented in 12 of 13 country markets they studied. AQR's "Value and Momentum Everywhere" (Asness-Moskowitz-Pedersen 2013, *JF* 68) extended to 8 asset classes.
- **Small-cap value is the strongest sub-portfolio.** Big-Growth has been the weakest.
- **Concentration in financials, energy, utilities.** Sector-neutralizing changes the picture substantially.

## Beyond B/M — EBIT/EV and friends

B/M has problems. Book value is a stale accounting number, increasingly distorted by buybacks (which depress book equity), intangibles (R&D and brand value mostly expensed, not capitalized), and goodwill from M&A. By 2015 the median S&P 500 firm had book value that arguably bore little economic relationship to value-of-the-firm.

Practitioner alternatives:

- **EBIT / Enterprise Value.** Earnings yield on the whole capital structure. Less distorted by capital structure choices than P/E. The Acquirer's Multiple (Carlisle 2014); also the core ratio in Greenblatt's "magic formula."
- **Free Cash Flow / EV.** Captures cash generation; immune to accruals manipulation.
- **Sales / EV** or **EBITDA / EV.** Useful in capital-intensive industries.
- **Composite value scores.** AQR, Research Affiliates, and others build value as an equal-weighted z-score of 4-6 ratios. Reduces single-ratio noise.

Asness, Frazzini, Israel, Moskowitz (2015), "Fact, Fiction, and Value Investing" (*JPM* 42): all the major value metrics work; B/M is not uniquely good; intangibles-adjusted book ratios improve B/M.

## The 2007-2020 drawdown

From mid-2007 to late-2020, long-short HML lost ~55% peak-to-trough on the standard Fama-French definition. The longest underwater period in the factor's recorded history. The narrative:

- **GFC (2007-09)** — financials and cyclicals (value-heavy) fell harder than tech/staples.
- **2010-2017 grind** — slow QE-fueled outperformance of growth/tech (FAANG).
- **2018-2020 crash** — value collapse accelerated as growth-tech multiples expanded. By the August 2020 trough, HML's z-score on cumulative drawdown was at a ~3-sigma low.

The explanations split:

1. **Value is dead** — intangibles dominate the modern economy; book-based value is obsolete; the premium has been arbitraged away.
2. **Spreads got extreme, mean reversion was just delayed.** Value-growth spreads (cheap-stock B/M ÷ expensive-stock B/M) hit 1999-dot-com extremes by mid-2020.

## "Is Systematic Value Investing Dead?" — AQR's rebuttal

Israel, Laipply, Liew, Pedersen (AQR, 2020), "Is (Systematic) Value Investing Dead?", argued for option 2:

- Value's underperformance was almost entirely driven by **multiple expansion of growth stocks**, not by fundamental decay of value firms. Earnings growth of value vs growth was similar to historical norms.
- **Spreads were at all-time wides** — value cheaper relative to growth than at any point since 1999.
- **Intangibles-adjusted value** had performed better than book-based value but still drew down — the problem was not the ratio.
- Implication: the **expected forward return on value was higher than it had been in 20 years**.

The rebound (November 2020 vaccine news → mid-2022) vindicated this view: HML returned ~30% over those 18 months, the strongest stretch since 2000-2002. The 2022-2023 tech-led rally then partially reversed it. The factor remains live but the volatility regime is different from the 1980s-90s.

## Practitioner-grade implementation

- **Multi-ratio composite** (B/M + EBIT/EV + FCF/EV + sales/EV), z-scored within sector.
- **Sector-neutral construction** to avoid mechanical financial/energy tilts.
- **Quality overlay.** AQR's QMJ research (see [[quality-factor|quality factor]]) shows value works dramatically better when intersected with quality — buying cheap junk has lower expected return than buying cheap-and-good. The "Buffett alpha" decomposition (Frazzini-Kabiller-Pedersen 2013) is essentially value × quality × leverage.
- **Slow rebalance.** Value signals decay slowly; quarterly or semi-annual rebalance gets most of the alpha with a fraction of the transaction costs — see [[transaction-costs|transaction costs]].

## Common pitfalls

- **Using P/E with negative earnings.** Throws out distressed-but-recoverable firms. Use earnings yield (E/P) and let it go negative.
- **Look-ahead in book value.** Fama-French use a 6-month gap (December book, July rebalance) precisely to avoid using data that wasn't yet filed.
- **Survivorship bias.** Including only "current" firms ignores the value firms that went bankrupt — biases the premium upward.
- **Single-metric value.** Single ratios are noisy; composites are more stable.

## See also

- [[factor-investing|Factor investing]] — the umbrella framework
- [[quality-factor|Quality factor]] — value × quality is the high-conviction trade
- [[momentum-factor|Momentum factor]] — value/momentum negative correlation is the basis for combining them
- [[regression-basics|Regression basics]] — Fama-MacBeth procedure used in the cross-section

## External references

- Fama & French (1992). "The Cross-Section of Expected Stock Returns." *JF* 47(2).
- Fama & French (1993). "Common Risk Factors in the Returns on Stocks and Bonds." *JFE* 33(1).
- Fama & French (1998). "Value vs Growth: International Evidence." *JF* 53(6).
- Lakonishok, Shleifer, Vishny (1994). "Contrarian Investment, Extrapolation, and Risk." *JF* 49(5).
- Asness, Moskowitz, Pedersen (2013). "Value and Momentum Everywhere." *JF* 68(3).
- Israel, Laipply, Liew, Pedersen (AQR, 2020). "Is (Systematic) Value Investing Dead?"
- Asness, Frazzini, Israel, Moskowitz (2015). "Fact, Fiction, and Value Investing." *JPM* 42(1).
