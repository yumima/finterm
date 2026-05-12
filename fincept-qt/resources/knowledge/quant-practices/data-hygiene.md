# Data Hygiene

Every quant strategy is downstream of its data. If the data has lookahead, survivorship, restatement, or corporate-action errors, the backtest is fiction — independently of how rigorous the methodology above it is. Most "alpha discoveries" that fail to replicate in production traced back to a data hygiene mistake, not to a flaw in the strategy logic.

This entry is the pre-flight checklist that should precede every backtest. Brown, Goetzmann & Ross's "Survival" (1995, *Journal of Finance*) is the foundational paper on survivorship-induced inflation; the modern reference work is López de Prado's *Advances in Financial Machine Learning* (2018), Chapters 1–4.

## Point-in-time databases — the non-negotiable

A **point-in-time (PIT)** database stores not just "what is the value today," but "what was the value as it was known on date T." For fundamentals, this is what the *original 10-K* reported on its filing date, before subsequent restatements. For prices, it's what the closing print was at 16:00 on date T, before later corrections.

Without PIT, a backtest can:

- Use a 2014 restated earnings number to make a 2013 trade decision. The market traded on the original 2013 number, not the corrected 2014 one — your backtest sees the wrong reality.
- Use a 2019-revised CPI print to time a 2018 trade. BLS routinely revises CPI for up to 5 years; the original print is what moved bond futures on the release day.
- Use a survivor-only constituent list, missing every company that was in the index on the trade date but has since been removed.

PIT vendors include S&P Compustat Snapshot, Wharton WRDS-PIT, Refinitiv-Worldscope timestamped, FactSet Fundamentals "as-reported." For prices, CRSP is the gold standard for US equities; Bloomberg PR_BNI tracks corporate actions cleanly. Free data sources (Yahoo Finance, Stooq, most CSV dumps) are almost never PIT for fundamentals.

## Corporate actions — the silent backtest killer

A trade in a name that splits 2:1 the next day must adjust both the shares and the price. The standard handling is the **adjustment factor** approach:

```
adjusted_price_t = raw_price_t · cumulative_adjustment_factor_t
adjusted_volume_t = raw_volume_t / cumulative_adjustment_factor_t
adjusted_position_t = raw_position_t · cumulative_adjustment_factor_t
```

The corporate actions to handle, in approximate order of frequency:

- **Cash dividends.** Adjust by the ex-dividend amount on the ex-date. For total-return strategies, *do not* adjust price but treat the dividend as cash inflow.
- **Stock splits / reverse splits.** Multiply prices and divide shares by the split ratio on the effective date.
- **Stock dividends.** Equivalent to a split for price-adjustment purposes.
- **Spin-offs.** Cost-basis allocation between parent and spin-co. CRSP delivers this; ad-hoc data sources usually mangle it.
- **Mergers.** Cash mergers settle at deal price; stock-for-stock mergers convert one share into another. Tracking the surviving entity correctly is essential for backtests that span 5+ years.
- **Delistings.** The biggest source of survivorship bias if mis-handled. See below.
- **Rights issues.** Adjust by the theoretical ex-rights price.
- **Ticker changes.** Tesla's ticker history: TSLA throughout, but Facebook changed from FB to META in 2022 and any naive join on ticker will lose history.

CRSP and Bloomberg apply adjustment factors automatically when you query for adjusted close. Always store *both* raw and adjusted prices — adjusted for return calculation, raw for dollar P&L sanity checks.

## Survivorship bias

The classic mistake: take today's S&P 500 constituents, pull their 20-year history, run a backtest. The result is inflated by the fact that every company on the list is alive today — Bear Stearns, Lehman, WorldCom, Enron, Pacific Gas & Electric (pre-bankruptcy), Sears, and the rest are silently excluded.

Brown, Goetzmann & Ross (1995) demonstrated that survivorship inflates measured alpha by ~150 bps annually in mutual-fund samples; for stat-arb backtests on equity universes, the effect can be 200–500 bps. The fix:

- **Use a live constituent list.** For each date T, the universe is exactly the names that traded on date T — including ones that subsequently went to zero.
- **Include delisting returns.** When a name is delisted, the final return should reflect what holders actually got: bankruptcy → likely −100% in equity; M&A buyout → deal price; voluntary delisting → typically 0 to −80%. Shumway (1997) documented that CRSP previously misreported delisting returns; modern CRSP includes a corrected delisting code.
- **Index reconstitutions.** Historical S&P 500, Russell 1000/2000, MSCI memberships are time-stamped. Use a reconstitution-aware index history; don't slice today's membership backward.

## Look-ahead from accounting restatements

Companies routinely restate prior-period financials after the fact. If your backtest sees the restated number on the date of the original release, it has effectively seen the future. The corrupted-result risk is largest for:

- **Value strategies.** Built on book values, earnings, and similar accounting numbers — all heavily restated.
- **Quality strategies.** ROE, accruals, asset turnover — restatements move these substantially.
- **Earnings-surprise / PEAD strategies.** A restated EPS that "missed" originally but "beat" after restatement flips the entire signal.

The fix: only use the **as-reported, as-of-filing-date** value. Compustat Snapshot, WRDS-PIT and FactSet's "as-reported" feeds maintain this. If you only have a "current" view, lag fundamentals by at least 90 days from the fiscal period end as a crude proxy — the typical 10-K filing window. This still misses material 1–5-year restatements but blocks the worst lookahead.

## Time-zone and timestamp hygiene

A surprising number of backtests fail in production because of timestamp misalignment:

- **US equity close is 16:00 ET; FX quotes are continuous.** Daily-close EUR/USD aligned to NY close differs from London close by 5 hours.
- **Asia open data on a US-trading model.** Be careful which day's close you use; "yesterday's Nikkei close" can mean Friday night in NY or Saturday morning in Tokyo.
- **Daylight saving transitions.** US and Europe switch on different weekends; some emerging markets don't switch at all.
- **Holiday alignment.** Cross-market joins must drop non-trading days correctly; a US-Japan strategy must handle Japanese Golden Week and US Thanksgiving asymmetrically.

Best practice: store all timestamps in UTC, convert to local market time only at the boundary of strategy logic, document the convention in every dataset header.

## Pre-flight checklist before any backtest

Before clicking "run," verify each item:

- **Universe.** Is it a live constituent list? Or today's snapshot projected backward?
- **Prices.** Adjusted for splits and dividends? Are total-return and price-return separated?
- **Fundamentals.** As-reported, as-of-filing? Or current-restated?
- **Index membership.** Reconstitution-aware?
- **Delistings.** Included? With realistic delisting returns?
- **Lag policy.** What is the delay from data observation to trading? (Typically 1 bar for prices, ~3 months for fundamentals.)
- **Timezone.** All data on one consistent clock?
- **Cost model.** Does the backtest charge realistic spread, commission, impact? See [[transaction-costs|transaction costs]].

A strategy that fails this checklist is not "promising" — it is unmeasured. See [[backtesting-discipline|backtest hygiene]] for the full set of methodological sins built on top of data hygiene.

## Common mistakes

- **Joining on current ticker.** META vs FB join fails silently; merge on permanent identifier (CRSP permno, Compustat gvkey, FactSet entity ID) instead.
- **Forward-fill of fundamentals.** Carrying last-known earnings forward across quarters is fine; carrying *into the future* past the next earnings release is lookahead.
- **Free-data shortcuts.** Yahoo Finance has adjusted close that retroactively re-applies *all* dividends — including 2024 dividends applied to 2020 prices. Acceptable for charting, fatal for backtests.
- **OHLC bar mis-handling.** The open of bar t is *not* a tradeable price for a signal computed at the close of bar t-1; there's a meaningful gap. Use VWAP-of-bar-t for realistic fills.
- **Survivorship in custom universes.** "Tech stocks that existed in 2010" sourced from Wikipedia today is survivor-biased even for a tech-only strategy.

## Where to do this in the terminal

The **Data Audit** panel shows PIT status, survivorship handling, and delisting coverage for every dataset loaded into a backtest. The **AI Quant Lab** refuses to publish a strategy without an explicit data-hygiene checklist sign-off. The **Backtesting** screen warns when a fundamentals join may have lookahead based on the as-of vs filed dates.

## See also

- [[backtesting-discipline|Backtest hygiene]] — the methodological sins downstream of data sins
- [[walk-forward-validation|Walk-forward validation]] — only honest with honest data
- [[quant-research-workflow|Quant research workflow]] — where data hygiene fits in the pipeline
- [[hypothesis-first|Hypothesis-first research]] — pre-registering protects against post-hoc data fixes
- [[log-keeping|Log-keeping]] — record every data-cleaning decision; future-you will need to reproduce them

## External references

- Brown, Goetzmann & Ross (1995). "Survival." *Journal of Finance* 50(3).
- Shumway (1997). "The Delisting Bias in CRSP Data." *Journal of Finance* 52(1).
- López de Prado (2018). *Advances in Financial Machine Learning*. Wiley. Chapters 1–4 on data structures and labelling.
- Harvey & Liu (2014). "Backtesting." *Journal of Portfolio Management* — includes data-quality checklists.
- Cochrane (2011). "Discount Rates." *Journal of Finance* — discussion of how data revisions corrupt asset-pricing tests.
