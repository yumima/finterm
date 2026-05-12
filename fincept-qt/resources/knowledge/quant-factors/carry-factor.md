# Carry Factor (FX, Rates, Commodities, Equity Dividends)

The yield you earn if nothing moves. Borrow in a low-rate currency, invest in a high-rate currency. Buy a high-dividend stock. Hold a backwardated commodity future. In every asset class, **going long the higher-carry instrument and short the lower-carry one has historically generated positive return** — and the same risk premium appears to underlie all of them.

This entry covers **FX carry (Lustig-Verdelhan, Brunnermeier-Nagel-Pedersen), fixed-income roll-down, commodity roll yield, equity dividend yield as carry, and the unified Koijen-Moskowitz-Pedersen-Vrugt (2018) framework**.

## Carry, defined

The general form Koijen-Moskowitz-Pedersen-Vrugt (2018) propose:

```
Carry_t = (Expected_t[r_{t+1}] | nothing changes) / Price_t
```

The return you earn if every price stays constant and only the contractual payouts accrue. This isolates the structural yield component from the price-change component.

Examples:

- **FX**. Carry = interest-rate differential (`r^foreign − r^domestic`). Borrow JPY at 0.1%, lend AUD at 4.5%, pocket ~4.4% if exchange rate is unchanged.
- **Rates / fixed income**. Carry ≈ yield to maturity + roll-down. As a bond ages, if the yield curve stays put, it "rolls down" to the lower yield of a shorter maturity, generating capital gains.
- **Commodities**. Carry ≈ −(roll yield) of the futures curve. Backwardated curve (front > back) means rolling the long generates positive return as you sell the higher near contract and buy the cheaper next one.
- **Equities (index / cross-section)**. Carry ≈ dividend yield − financing cost. Approximately the dividend yield on a long-only basis.
- **Credit**. Carry ≈ spread over Treasuries (compensation for default risk that has not yet been realized).

## FX carry — the original

The empirical anchor and historical workhorse:

```
FX carry portfolio:
  rank G10 currencies by short rate
  long top 3, short bottom 3
  hold 1 month, rebalance
```

Lustig & Verdelhan (2007, *AER* 97), "The Cross-Section of Foreign Currency Risk Premia and Consumption Growth Risk," documented the cross-section: high-interest-rate currencies have higher excess returns in US dollar terms — uncovered interest parity fails systematically. Average G10 carry portfolio Sharpe: ~0.5-0.8 depending on window.

The trade is **negatively skewed**. Carry currencies (AUD, NZD, BRL, TRY historically) crash when global risk appetite collapses; funding currencies (JPY, CHF) rally. Brunnermeier, Nagel, Pedersen (2008), "Carry Trades and Currency Crashes" (*NBER Macro Annual*), framed this as a "funding liquidity" risk premium — you earn carry as compensation for tail losses during global stress.

Classic episodes: October 2008 (yen +25% in a week as carry positions unwound), August 2007 (initial subprime stress), January 2015 (Swiss franc depegging).

## Fixed-income carry / roll-down

Two components:

- **Yield carry**. Just the coupon income over the holding period.
- **Roll-down**. The price gain from a bond moving down a steep yield curve. If the 10-year yields 4% and the 9-year yields 3.8%, holding a 10-year for 1 year (assuming the curve doesn't move) gives you a ~1.6% price gain plus the 4% coupon.

Cross-sectional fixed-income carry strategies (Koijen-Moskowitz-Pedersen-Vrugt 2018) rank sovereign bonds by their carry (yield + roll-down) and go long highest, short lowest. Persistent positive returns across G10.

The path-risk story is the same as FX: carry-rich bonds tend to be from countries with steeper curves and more inflation/credit risk; they crash together in global flight-to-quality.

## Commodity carry — roll yield and convenience yield

Commodity futures roll over time. A long position in a 1-month future, held for one month, must be rolled into the 2-month future to maintain exposure.

- **Backwardation** (front > back) — rolling generates positive return. Indicates physical scarcity / high convenience yield.
- **Contango** (front < back) — rolling loses money. Indicates oversupply / high storage costs.

The "carry" of a commodity is essentially the **slope of the futures curve**. Long-short carry strategies in commodities (Gorton, Hayashi, Rouwenhorst 2013, *Review of Finance* 17) generate ~9% per year Sharpe ~0.6 — buying the most-backwardated, shorting the most-contangoed commodities.

The link to fundamentals: convenience yield (Working 1949) reflects the value of having physical inventory available. High convenience yield = scarcity = bullish for spot price relative to deferred. Roll yield is the futures-market reflection of this.

## Equity dividend yield as carry

The dividend yield (or earnings yield) is the per-period payout you receive for holding the equity. Cross-sectional equity carry strategies (long high-DY indices or stocks, short low-DY) have positive but modest returns. The closer link is to the [[value-factor|value factor]] — high-DY stocks tend to be value stocks.

For single-stock equity strategies, "carry" overlaps heavily with the value sort and provides limited additional information once value is included. For *index-level* equity strategies (long high-DY country indices, short low-DY country indices), carry is a distinct and tradable signal.

## The unified Koijen-Moskowitz-Pedersen-Vrugt framework

Koijen, Moskowitz, Pedersen, Vrugt (2018), "Carry" (*JFE* 127), constructed carry portfolios for **9 asset classes** (G10 FX, US Treasuries, 10 sovereign bond markets, S&P 500 futures, 13 commodity futures, 24 country equity indices, equity options, US credit, US equity individual stocks). Findings:

- **Carry predicts returns in every class** — Sharpe ratios of 0.3–0.9.
- **A global carry factor** that averages across all classes has Sharpe ~1.1, far above any single-class carry. Diversification across asset classes is large.
- **Negative skew is the common feature** — carry has its worst months in global liquidity stress.
- The global-carry factor has **time-varying expected return** — high when carry spreads are wide, low when compressed. Tradable in real time.

This was the paper that unified what had been four separate literatures (FX carry, bond roll-down, commodity roll yield, equity dividend) into a single phenomenon. It is now the standard reference for "carry as a cross-asset factor."

## Why does carry pay?

Two competing stories, both with empirical support:

- **Risk premium**. Carry-rich assets crash together in global tail events. Investors require compensation for bearing this skew/funding-liquidity risk. Brunnermeier-Nagel-Pedersen (2008) and Lustig-Roussanov-Verdelhan (2011, *JFE* 100) "Common Risk Factors in Currency Markets" formalize this.
- **Segmented markets / supply effects**. Carry currencies/bonds attract local supply (corporates borrowing in high-rate currencies, etc.) creating persistent imbalance that produces the premium.

For practitioners the dominant question is **what hedges the tail**. Combining carry with [[momentum-factor|trend-following / TSMOM]] dampens the worst carry drawdowns — when carry begins to crash, trend signals flip negative and reduce the exposure. AQR-style "carry + trend" portfolios are a standard diversifier in CTA and global-macro books.

## Implementation choices

- **Funding cost realism.** Carry returns in academic papers often ignore the bid-ask spread on rolling FX forwards and the financing costs of leverage. In G10 the gap is small (~10-20bp per year); in EM FX it can wipe out half the academic premium.
- **Cross-asset, not single-asset.** Single-asset-class carry is exposed to its own tail; cross-asset diversifies.
- **Carry × momentum overlay.** Long the high-carry assets that also have positive momentum. Stops you from buying carry into a regime change.
- **Vol scaling.** Same logic as [[momentum-factor|momentum]] — scale position size to a target vol, dampening drawdowns.

## Common pitfalls

- **Carry = "free money."** No. The Sharpe is real but the skew is severe. Sizing a carry book like a Sharpe-1 strategy at face value will produce a 6-sigma drawdown roughly once per decade.
- **Confusing carry with yield.** Carry is yield *net of expected price decay* (or plus expected price gain). A bond with high yield in a bear-flattening curve has negative carry.
- **Ignoring funding currency dynamics.** JPY/CHF as funding currencies are themselves tail-hedging — their rallies in crashes are part of why carry crashes are so violent.
- **Crowding.** When global "carry trade" is in vogue, the unwinds are sharper. Position size accordingly.

## See also

- [[factor-investing|Factor investing]] — the umbrella
- [[momentum-factor|Momentum factor]] — the natural diversifier for carry's skew
- [[value-factor|Value factor]] — equity dividend yield is essentially a value variant
- [[time-series-basics|Time-series basics]] — autocorrelation in carry vs trend signals

## External references

- Koijen, Moskowitz, Pedersen, Vrugt (2018). "Carry." *JFE* 127(2).
- Lustig & Verdelhan (2007). "The Cross-Section of Foreign Currency Risk Premia and Consumption Growth Risk." *AER* 97(1).
- Brunnermeier, Nagel, Pedersen (2008). "Carry Trades and Currency Crashes." *NBER Macroeconomics Annual* 23.
- Lustig, Roussanov, Verdelhan (2011). "Common Risk Factors in Currency Markets." *JFE* 100(3).
- Gorton, Hayashi, Rouwenhorst (2013). "The Fundamentals of Commodity Futures Returns." *Review of Finance* 17(1).
- Asness, Moskowitz, Pedersen (2013). "Value and Momentum Everywhere." *JF* 68(3).
