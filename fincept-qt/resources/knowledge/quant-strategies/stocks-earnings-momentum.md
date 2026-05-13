# Earnings Momentum (PEAD / SUE)

**Companies that surprise positively on earnings continue to outperform for weeks to months afterwards.** This is the **post-earnings-announcement drift (PEAD)** — one of the oldest, most-replicated, and most-stubborn anomalies in equities, first documented by **Ball & Brown (1968)** and refined by **Bernard & Thomas (1989)**. Kakushadze & Serur (2018) §3.2 give the standardised-unexpected-earnings (SUE) construction in a single paragraph.

The strategy buys positive-surprise stocks and shorts negative-surprise stocks, holding for several months. The signal earns a premium that has been documented in **every developed market and every decade since the 1960s** — and has only modestly decayed despite being known for 50+ years.

## The signal — Standardised Unexpected Earnings

For each stock `i`:

```
SUE_i  =  (E_i − E'_i) / σ_i                            (Eq. 3.9)
```

Where:
- `E_i` = most recently announced quarterly earnings per share (EPS).
- `E'_i` = expected EPS — most commonly defined as the EPS announced 4 quarters ago (the "seasonal random walk" forecast).
- `σ_i` = standard deviation of `(E_i − E'_i)` over the past 8 quarters.

In words: **how many standard deviations did this quarter's earnings come in above/below the year-ago quarter?**

Stocks with SUE in the top decile are "positive surprises"; bottom decile are "negative surprises."

## The strategy

```
1. Each quarter (after the earnings release), rank stocks by SUE.
2. Long the top decile, short the bottom decile.
3. Equal-weight or vol-weight each leg.
4. Hold for ~6 months (academic standard; live operators use 1–3 months).
5. Rebalance as new earnings come out.
```

A dollar-neutral, equal-weighted SUE portfolio has historically earned roughly **8–12% annualised excess return** over 1965–2015 samples, with Sharpe of 0.6–1.0 depending on holding period.

## Why does PEAD exist?

The textbook efficient-market response: it shouldn't. Earnings are public information. By the time you see SUE, every investor sees the same number. Markets should price the news instantly and drift to zero.

Empirically, prices drift in the direction of the surprise for **6–12 months**. Three competing explanations:

### 1. Slow processing of fundamental information

Most investors are **not** systematically tracking unexpected earnings across 3000+ stocks. Analysts revise estimates gradually after surprises (**Bernard & Thomas 1989**, **Bartov, Radhakrishnan, Krinsky 2000**). Retail investors react even more slowly.

The price absorbs the news in stages: the day of the announcement, the days after, then over weeks as analysts upgrade and institutional investors rebalance. The drift is the *predictable* portion of that staged absorption.

### 2. Underreaction to the implications

**Hirshleifer, Lim, Teoh (2009) "Driven to Distraction"** showed PEAD is stronger when an earnings announcement coincides with many other announcements that day — investors literally don't have attention bandwidth.

**Daniel, Hirshleifer, Subrahmanyam (1998)** model: investors are overconfident in their priors, so they discount new contradictory information. A positive earnings surprise that contradicts a bearish view gets discounted; only later, as confirming evidence accumulates, does it get fully priced.

### 3. Limits to arbitrage

Trading PEAD requires:
- High-quality fundamental data (clean earnings, dividend-adjusted prices).
- Cross-sectional rebalancing infrastructure.
- Patience to hold for 1–6 months through interim volatility.

These costs prevent the trade from being arbitraged to zero. The premium is what's left after frictions.

## PEAD in numbers — what to expect

In the academic samples:

- **Top minus bottom decile**: ~10% annualised excess return.
- **Sharpe**: 0.7–1.0 (varies by sample window and holding period).
- **Drift horizon**: peaks 60–90 days after the announcement, mostly faded by 180 days.
- **Cross-section size**: works on the full US large/mid-cap universe; harder to implement in small caps due to liquidity.

Post-decimalisation (2001+) and post-Regulation FD (2000), PEAD has **decayed but not vanished**. Currently in the 3–6% annualised range net of trading costs.

## Variants — Standardised Unexpected Revenues, EPS revisions

The classic SUE uses the seasonal random walk (last year's same quarter) as the expected. Variants:

- **Analyst-consensus SUE**: `E'_i` = mean analyst forecast. More widely used in practice. Has the advantage of incorporating analyst information; has the disadvantage of being noisier when analyst coverage is thin.
- **Standardised Unexpected Revenue (SUR)**: same construction using revenue rather than earnings. Revenue is harder to manipulate via accounting; SUR is often more persistent.
- **Earnings estimate revisions**: track the *change* in analyst consensus over the past N weeks. Stocks with rising consensus → long. **Stickel (1991)** and modern factor providers (MSCI Barra, Northfield) use this.
- **Whisper number deviations**: pre-2000 alternative metric using "whisper numbers" — informal pre-announcement expectations. Mostly defunct after Reg FD.

## Where PEAD has failed

- **Extreme growth bubbles.** During 1998–2000 and 2020–2021, low-quality stocks rallied without earnings support; positive-SUE stocks earned the premium but high-quality short-leg names also rallied, eating the spread.
- **Beat-the-number era (mid-2000s).** Companies systematically guided analysts down to ensure beats. SUE became less informative when ~75% of S&P 500 companies "beat" every quarter.
- **2008 and 2020 crises.** PEAD assumes the market processes news efficiently *eventually*. In crisis periods, fundamental signals are dominated by liquidity and macro flow.

## How PEAD relates to price momentum

The two are **correlated but not identical**:

- A positive earnings surprise often triggers price momentum (the stock rallies on the announcement and continues).
- But PEAD captures the **fundamental-driven** portion, while [[stocks-price-momentum|price momentum]] is signal-agnostic.

Empirically, combining the two signals improves Sharpe by 30–50% over either alone. **Chordia & Shivakumar (2006)** and **Asness, Frazzini, Pedersen (2014)** document the diversification benefit.

## Practical implementation

- **Data: needed for SUE** — quarterly EPS time series, ideally from Compustat or directly from filings.
- **Announcement date is the rebalance trigger**, not month-end. Stocks announce on different days; trades happen as announcements come out (or in weekly/monthly rebalance batches).
- **Hold for 1–3 months** if you have transaction costs > 5 bps; hold longer in liquid universes.
- **Cap single-stock weight** at 2–5% to avoid concentration.
- **Filter out micro-caps and recent IPOs**: liquidity and pricing reliability issues.

## Common mistakes

- **Using stale earnings.** A "recent" earnings report from 6 months ago is no longer informative. PEAD is fresh-news.
- **Ignoring guidance/announcement quality.** A "beat" with declining guidance is different from a "beat" with raised guidance. The pure-SUE signal misses this. Modern implementations adjust SUE for guidance.
- **Not handling pre-announcements.** Companies sometimes pre-announce; the "official" announcement date is no longer the news date. Use the *first* date the market learned the result.
- **Cross-correlation across earnings dates.** Tech earnings cluster in last week of January, April, July, October. Treating these as independent observations overstates the Sharpe.

## Risk management essentials

- **Earnings season concentration risk.** Most of the long-short P&L generates in 2-week windows post-major-earnings-weeks. Position-size accordingly.
- **Filter for accounting irregularities.** Negative SUE driven by an accounting writedown is different from one driven by operational weakness. Manual filter or rule-based (large negative SUE within first 4 quarters after restatement → exclude).
- **Diversify across announcement dates.** Don't load all positions on companies announcing the same day.
- **Cap industry exposure.** Same-industry stocks beat/miss together (sector-driven cycles). Maintain industry diversification.

## Where to do this in the terminal

- **AI Quant Lab** — PEAD strategy template with SUE construction, analyst-consensus variant toggle.
- **Equity Research** — earnings calendar with SUE column for upcoming reporters.
- **Backtesting** — historical PEAD performance with earnings-calendar replay.

## See also

- [[stocks-price-momentum|Price Momentum]] — the natural diversifier
- [[stocks-multifactor|Multifactor Portfolio]] — PEAD combined with value, momentum, quality
- [[stocks-residual-momentum|Residual Momentum]] — momentum after stripping out fundamentals
- [[stocks-value-factor|Value Factor]] — the diversifying complement

## External references

- Ball, R., Brown, P. (1968). "An Empirical Evaluation of Accounting Income Numbers." *Journal of Accounting Research* 6(2), 159–178.
- Bernard, V., Thomas, J. (1989). "Post-Earnings-Announcement Drift: Delayed Price Response or Risk Premium?" *Journal of Accounting Research* 27, 1–36.
- Bernard, V., Thomas, J. (1990). "Evidence That Stock Prices Do Not Fully Reflect the Implications of Current Earnings for Future Earnings." *Journal of Accounting and Economics* 13(4), 305–340.
- Chan, L., Jegadeesh, N., Lakonishok, J. (1996). "Momentum Strategies." *Journal of Finance* 51(5), 1681–1713.
- Hirshleifer, D., Lim, S., Teoh, S. (2009). "Driven to Distraction: Extraneous Events and Underreaction to Earnings News." *Journal of Finance* 64(5), 2289–2325.
- Bartov, E., Radhakrishnan, S., Krinsky, I. (2000). "Investor Sophistication and Patterns in Stock Returns after Earnings Announcements." *Accounting Review* 75(1), 43–63.
- Stickel, S. (1991). "Common Stock Returns Surrounding Earnings Forecast Revisions." *The Accounting Review* 66(2), 402–416.
- Livnat, J., Mendenhall, R. (2006). "Comparing the Post-Earnings Announcement Drift for Surprises Calculated from Analyst and Time Series Forecasts." *Journal of Accounting Research* 44(1), 177–205.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.2. https://doi.org/10.1007/978-3-030-02792-6
