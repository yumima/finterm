# Peter Lynch Value screen (2026-11-26)

Growth at a reasonable price, as rules. Code: `lynch_facts`, `lynch_tests`, `lynch_ratio`, `LYNCH` in
`app/scripts/screener_data.py`. The live screen and the backtest import the same functions.

## What "Peter Lynch Value" is

Lynch ran Fidelity Magellan from 1977 to 1990. He published no screen, but *One Up on Wall Street* (1989), chapter 13
"Some Famous Numbers", gives the numbers he checked:

- "The p/e ratio of any company that's fairly priced will equal its growth rate", and "a p/e ratio that's half the
  growth rate is very positive, and one that's twice the growth rate is very negative". This is the PEG ratio (P/E ÷
  growth). GuruFocus's "Peter Lynch Fair Value" is the same idea run backwards: growth × earnings, the price at which
  PEG = 1 (GuruFocus now uses 5-year EBITDA growth clamped to 10–20%; its older version and the book use earnings).
- With the dividend: "add the dividend yield … and divide by the p/e ratio … Less than a 1 is poor, and 1.5 is okay,
  but what you're really looking for is a 2 or better". The book's example: (12 + 3) / 10 = 1.5.
- "A normal corporate balance sheet has 75 percent equity and 25 percent debt" (*Beating the Street* asks for at least
  twice as much equity as debt, a weaker test).
- Also in the book, and not in this feed: inventories growing faster than sales are a red flag; few institutions
  holding the stock and insiders buying are good signs. His six categories (slow growers, stalwarts, fast growers,
  cyclicals, turnarounds, asset plays) come first; the ratio misleads on cyclicals, whose P/E is lowest at the peak.

AAII's published Lynch screen (2019) adds a cap of 50% a year on 5-year earnings growth and excludes financials.

TradingView has no built-in Lynch field. Its "Peter Lynch Value" is community indicators: the most used (Uptrader,
"Peter Lynch Value (Dynamic Growth)", 2025) draws fair price = EPS × growth rate with the multiple capped at 25 (15
when growth is unknown); an older one (AntoineLeo, 2020) draws the earnings line of Lynch's charts, EPS × 15. Both are
columns here: **Lynch val.** (P/E = growth, at most 25) and **15× earn.** Neither changes the list; the ratio does.

Checked against TradingView's own fields on the 21 names listed or setting up on 2026-11-25: our 5-year growth equals
its `net_income_cagr_5y` on every one, and our P/E (market cap / net income) is within a few percent of
`price_earnings_ttm` (per diluted share). Its `price_earnings_growth_ttm` (PEG) does not match and is not meant to: it
divides by ONE year's EPS growth (USLM 10.5, NXT 5.4 against 0.6 and 0.5 here); Lynch's is the long-term rate.

## Rules and where each comes from

| Rule | Source |
|---|---|
| Six consecutive fiscal years reported, a profit in every one | needed for a 5-year rate; never across a loss (AAII's screen implies it) |
| Growth = net income, last fiscal year vs five years before, a year; above 0%, at most 50% | 5 years: AAII and GuruFocus; 50%: AAII. Net income, not EPS: the SEC history's share counts are not split-safe, and the live screen must be the tested one |
| Last twelve months' earnings positive; P/E = market cap / those earnings | a P/E must exist; whole-company numbers on both sides of the ratio |
| Total debt ≤ 25% of debt + equity, equity positive | "75 percent equity and 25 percent debt": no weaker than his normal |
| (growth % + dividend yield %) / P/E ≥ 2, ranked, at most 30 | "a 2 or better" |
| Setting up: every other test passed, ratio 1.5 to 2 (or ≥ 2 outside the 30) | "1.5 is okay" |
| Lynch value = the price at which P/E = growth, the P/E at most 25 | "fairly priced will equal its growth rate"; the cap is TradingView's indicator's |
| Earnings line = 15 × earnings a share | the line his charts draw the price against |
| Excludes Finance and Utilities | as every fundamental screen here |

The thresholds are the book's and AAII's, written once and not tuned. The inventory test is left out because the
scanner carries no inventory history (only a turnover figure for the current year).

## Measured

Point-in-time S&P 500, SEC annual filings visible 120 days after period end, monthly, 10 bp a side, months that
held names (`fund_backtest.py`, `setups_all_backtest.py`, `export_evidence.py`; checks in `lynch_robust.py`).

| | excess a year vs equal weight | t | months held | median names |
|---|---:|---:|---:|---:|
| Lynch Value (ratio ≥ 2, the shipped list) | +25.3% | 3.02 | 108 of 200 (2014-04 to 2026-06) | 1 |
| the same, holding the index when the list is empty | +13.7% | 2.97 | 200 | — |
| ratio ≥ 1.5 (research, not shipped) | +5.3% | 0.76 | 146 | 2 |
| every test before the price, any ratio | +0.9% | 0.47 | 150 | 24 |
| setting-up list (1.5 to 2) | −7.9% | −0.79 | 108 | 1 |

It is the only screen in this tab with t above 2. How far to trust it:

- **Thin.** Twenty-two companies in twelve years, usually one at a time. PulteGroup was on the list in 42 of the 108
  months, Apple in 23, Robert Half and Regeneron in 13, Lennar in 12, Meta in 11.
- **Not one name.** Without PulteGroup: +21.1%, t 2.10. Without all homebuilders: +25.8%, t 2.56. Without PulteGroup,
  Apple and Meta together: +19.5%, t 1.80.
- **Not the passers.** The same number of names drawn at random from the same month's passers of every test before
  the price, 100 draws: median +0.0%, 95th percentile +9.7%; none reached the shipped list's +25.3%.
- **Steady, recently not.** Positive in 11 of 13 calendar years; negative in 2025 (−4.6%) and so far in 2026 (−29.2%,
  5 months).
- **Not monotone.** Names between 1.5 and 2 did worse than the index, so the cut at 2 separates them sharply. A real
  effect of price for growth would rather rise smoothly with the ratio; this pattern is what a few lucky names also
  produce.
- **Many tests.** The evidence file holds 27 backtests on this data (12 shipped screens and their variants); one
  t of 3 among them is less surprising than one alone.
- Shared with every screen here: SEC frames carry each period's latest-filed value (a restatement can leak back);
  delisted members are missing in the early years; market cap is the filed public float rolled forward.

Read it as: in large caps the book's cut is rarely met, and when it was, those few stocks did well — a record
worth watching, not a proven edge. The card says so in words; its numbers come from the export.

## Live, 2026-11-25

12 names of 1,686 above $2B: DAC, STRL, MLI, DLO, ZM, CTVA, EXEL, BSM, GRBK, GMED, NXT, DECK. The first miss, URBN,
grew earnings 227% a year off its 2021 base and fails the 50% cap, which is what the cap is for.

CTVA looked like a data fault (an $8 price beside a large company's balance sheet) and is not one: SEC's filings
(10-Q filed 2026-11-12, after a notice of late filing) give diluted EPS of $1.96 for nine months and a $0.18
quarterly dividend, the same as TradingView, and Yahoo's prices agree with TradingView's 52-week range ($7.82–14.62).
What the screen cannot see is the news: a third-quarter loss and a late filing. The ratio rests on fiscal-year
history; read the latest 10-Q before trusting it.

## Effect on the other records

None: their rules and code are unchanged, and every existing column of `fund_results.pkl`,
`setups_all_results.pkl` and the evidence file came out byte-identical. `buffett_facts` now counts its years with
the shared `_fiscal_run`; `fund_backtest.buffett_at`'s value lookup became the module's `tagged()`. The direction
guard skips months with an empty list (an empty list has no direction to get wrong).
