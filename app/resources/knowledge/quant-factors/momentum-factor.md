# Momentum Factor (Cross-Sectional and Time-Series)

Recent winners keep winning. The single most replicated cross-sectional anomaly outside of value, present in nearly every asset class anyone has bothered to test. Embarrassing for efficient-markets theorists, profitable for those who size it right, and prone to spectacular crashes when it fails.

This entry covers **Jegadeesh-Titman's original cross-section, the Carhart UMD factor, the formation/skip/holding mechanics, Moskowitz-Ooi-Pedersen's time-series momentum (TSMOM), and the "momentum crash" risk that makes sizing the hard part**.

## Jegadeesh & Titman (1993) — the original

Jegadeesh & Titman (1993), "Returns to Buying Winners and Selling Losers" (*JF* 48), constructed monthly long-short portfolios using simple past-return sorts:

1. At month-end, rank all stocks by their cumulative return over the past J months (formation period: J ∈ {3, 6, 9, 12}).
2. Long the top decile, short the bottom decile.
3. Hold for K months (K ∈ {3, 6, 9, 12}), equal-weight, then rebalance.

All 16 (J, K) combinations produced positive long-short returns over 1965-1989. The (12, 3) variant — formation over 12 months, hold for 3 — gave the strongest result. The pattern survived size and beta adjustments, persisted out-of-sample, and was robust to subsample splits.

The factor was not absorbed by the Fama-French 3-factor model — momentum has a **negative loading on HML** (winners are growthy) and roughly zero on SMB, so neither size nor value explains it.

## Carhart UMD — the 4-factor standard

Carhart (1997), "On Persistence in Mutual Fund Performance" (*JF* 52), built the momentum factor that became the practitioner standard:

```
UMD = (1/2)·(Small-High + Big-High) − (1/2)·(Small-Low + Big-Low)
```

Construction parallels HML but the second sort is on **prior 12-month return, skipping the most recent month** (the formation period is months t−12 to t−2). Sorts into Low (bottom 30%), Medium, High (top 30%). Rebalanced monthly.

The Carhart 4-factor model — Mkt-Rf + SMB + HML + UMD — became the workhorse for performance attribution. Hedge fund managers can't claim alpha if it's just exposure to UMD.

## The formation/skip/holding mechanics

Three windows define every momentum strategy:

- **Formation period (lookback).** Standard: 12 months. Shorter (1-3 months) shows **reversal**, not momentum (De Bondt-Thaler 1985, *JF* 40). Longer (3-5 years) also shows reversal.
- **Skip period.** The most recent 1 month is excluded. Why: short-horizon reversal from bid-ask bounce and liquidity provision dominates at the 1-month horizon. Without the skip, your "momentum" trade includes a contrarian leg that wipes out most of the alpha.
- **Holding period.** Standard: 1 month with monthly rebalance, or 3-6 months with overlapping portfolios. Longer holding = lower turnover = lower transaction costs, with some signal decay.

The (12-2, 1) variant — past 12 months excluding most recent, hold 1 month — is the default Fama-French / AQR specification.

Empirical anchors:

- **US UMD, 1927-2020** (Ken French data) — average ~7% per year, Sharpe ~0.5. Much higher than HML's Sharpe.
- **Crashes** — UMD lost 80% from March-May 2009 in the GFC rebound. April 2009 alone: −47%.

## Time-Series Momentum (TSMOM)

Moskowitz, Ooi, Pedersen (2012), "Time Series Momentum" (*JFE* 104), separated momentum into its **time-series** (own-asset trend) and **cross-sectional** (relative rank) components:

```
Signal_{i,t} = sign( cum_return_{i, past 12m} )
Position_{i,t} = (vol_target / σ_{i,t}) · Signal_{i,t}
TSMOM_t = (1/N) · Σ_i Position_{i,t} · r_{i,t+1}
```

Long if own past 12-month return is positive, short if negative. Size each position to constant volatility (~40% annualized). The strategy worked in **all 58 instruments** they tested (equity indices, currencies, bonds, commodities) over 1985-2009 with high statistical significance.

TSMOM is the academic name for what CTAs and managed-futures funds have been doing since the 1970s — **trend following**. AQR's "A Century of Evidence on Trend-Following Investing" (Hurst-Ooi-Pedersen 2017) extended back to 1880, confirming persistence across 137 years and four major asset classes.

Cross-sectional vs time-series momentum are related but distinct:

- **Cross-sectional** (Jegadeesh-Titman / Carhart) — long winners, short losers, market-neutral on average.
- **Time-series** (TSMOM) — long if up, short if down, directional exposure on average.
- The two correlate ~0.5 in equities but TSMOM has additional directional/macro exposure.

## The momentum crash

Daniel & Moskowitz (2016), "Momentum Crashes" (*JFE* 122): the worst feature of momentum is that **after large negative market returns followed by sharp rebounds, momentum has its worst months**. Mechanism:

1. Big bear market → past losers become high-beta, high-vol distressed stocks.
2. Momentum strategy is short these high-beta names.
3. Market rebounds sharply.
4. Short leg explodes upward, long leg lags.

The 1932 (post-Great Depression) and 2009 (post-GFC) crashes are the canonical episodes — UMD lost ~75% and ~85% respectively in the rebound months.

The sizing fix:

- **Vol-scaled momentum** (Barroso & Santa-Clara 2015, *JFE* 116) — scale UMD exposure inversely to realized momentum volatility. Cuts the crash, roughly doubles the Sharpe.
- **Dynamic momentum** (Daniel-Moskowitz 2016) — condition on bear-market indicator and momentum's own realized vol.

Both are now standard in practitioner momentum books.

## Why does momentum work?

The mechanism debate is unresolved:

- **Underreaction** to news (Barberis-Shleifer-Vishny 1998, *JFE* 49; Hong-Stein 1999, *JF* 54). Investors process information slowly; price drift after news creates the autocorrelation.
- **Overreaction / herding.** Momentum buyers chase performance, pushing prices past fundamentals. Reverses at the 3-5 year horizon (long-term reversal).
- **Risk premium.** Whatever risk it represents is asymmetric and concentrated in rebound episodes.

For practitioners the mechanism matters less than the persistence; 130 years of out-of-sample data is the relevant fact.

## Practitioner-grade implementation

- **(12-2, 1)** formation/skip/holding for cross-sectional equity momentum.
- **Vol-targeted** position sizing (Barroso-Santa-Clara). Not optional in real money.
- **Combine with value** — value/momentum is the textbook diversifying pair (negative ~−0.3 correlation). Asness-Moskowitz-Pedersen (2013) showed the 50/50 mix has Sharpe far above either alone.
- **Skip rebalancing into illiquid losers.** UMD's worst implementation drag comes from shorting hard-to-borrow names. See [[transaction-costs|transaction costs]].
- **Multi-frequency momentum** — combining 1-month, 3-month, 12-month signals diversifies horizon risk.

## Common pitfalls

- **No skip period.** Bid-ask bounce eats the (1,1) variant.
- **Equal-weight without vol scaling.** Concentrates in high-vol names, magnifies crashes.
- **Backtest survivorship.** Past winners are biased toward survivors; need a point-in-time universe.
- **Microcap concentration.** Most "momentum alpha" in academic studies lives in stocks below the bottom 10% NYSE-cap cutoff. Real-world capacity is much smaller than the paper alpha suggests.

## See also

- [[factor-investing|Factor investing]] — the umbrella
- [[value-factor|Value factor]] — negative correlation, natural diversifier
- [[time-series-basics|Time-series basics]] — autocorrelation is the empirical foundation
- [[trend-following|Trend following]] — TSMOM in the CTA / managed-futures setting

## External references

- Jegadeesh & Titman (1993). "Returns to Buying Winners and Selling Losers." *JF* 48(1).
- Carhart (1997). "On Persistence in Mutual Fund Performance." *JF* 52(1).
- Moskowitz, Ooi, Pedersen (2012). "Time Series Momentum." *JFE* 104(2).
- Asness, Moskowitz, Pedersen (2013). "Value and Momentum Everywhere." *JF* 68(3).
- Daniel & Moskowitz (2016). "Momentum Crashes." *JFE* 122(2).
- Barroso & Santa-Clara (2015). "Momentum Has Its Moments." *JFE* 116(1).
- Hurst, Ooi, Pedersen (2017). "A Century of Evidence on Trend-Following Investing." AQR.
