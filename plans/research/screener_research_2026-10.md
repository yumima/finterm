# Screener strategies — what the record says, and what we measured (2026-10-19)

Companion to the SCREENER tab. Scripts and raw output in
`plans/research/screener_backtest/` (`collect.py` → prices, `collect_raw.py` →
split-only closes, `fund_collect.py` → SEC XBRL frames; then `backtest.py`,
`fund_backtest.py`, and `export_evidence.py`, which writes the tab's evidence
card to `fincept-qt/scripts/screener_evidence.json`; output in
`results_2026-10-19.txt` and `fund_results_2026-10-19.txt`).

## Bottom line

**No strategy we could test beats an equal-weighted S&P 500 with statistical
significance over 2010-01 → 2026-08.** The best — RS composite (+2.0%/yr,
t 0.8), Trend Template top 25 by RS (+1.7%, t 0.5), Momentum 12-1 (+1.5%,
t 0.6), Quality+Momentum (+1.0%, t 0.5) — are inside noise, and every momentum-family result earned its excess in
2023-26 alone (the AI-led rally) after losing in each earlier period. This
agrees with the repo's own earlier harness (`scripts/factor_rating/__init__.py`:
12-1 momentum IC +0.012, t 0.6, on 178 large caps and again on 408 small/mid caps).

So the tab ships the strategies as **screens with their measured record shown
next to them**, not as "winning" systems. A screen is a fast, rule-exact way
to find the names a method would look at; it is not evidence the method pays.

## Where the strategies come from

Competition winners (US Investing Championship — brokerage-statement verified,
one year, self-selected entrants, so the winners are the lucky tail of a
large field):

| Trader | Result | Method | Independent test of the rules |
|---|---|---|---|
| Mark Minervini | +334.8% (2021, $1M+ div.), ~155% (1997) | SEPA / Trend Template | none rigorous |
| Oliver Kell | +941.1% (2020) | cycle of price action (Minervini-family) | none |
| David Ryan | 161% / 160% / ~100% (1985-87) | CAN SLIM | AAII 23.8%/yr 1998-2007, hypothetical, pre-cost |
| Leif Soreide | +60.9% (2019) | trend-template growth | none |
| Kristjan Kullamägi | self-reported | breakouts / episodic pivots | none |

Academic (out-of-sample, post-publication decay ~58% on average per McLean &
Pontiff 2016): 12-1 momentum (Jegadeesh-Titman), 52-week-high nearness (George
& Hwang), gross profitability (Novy-Marx), Piotroski F-score, Magic Formula
(Greenblatt), Trending Value (O'Shaughnessy), low volatility (Blitz & van Vliet).

What the contest winners trade is not what is testable: discretionary entries
on a base, 7-8% stops, position sizing and concentration. The screens below
code only the *selection* rules. The measured numbers say the selection rules
alone carry no edge in large caps; they cannot say anything about the
discretionary layer.

## Method

- **Universe:** point-in-time S&P 500 membership (fja05680/sp500, 2,720
  membership dates). Every month-end 2010-01 → 2026-08 (200 rebalances), the
  members on that date with ≥260 Yahoo bars.
- **Survivorship, measured:** Yahoo has 655 of the 900 tickers ever in the index
  since 2008 — coverage of members is 65.5% in 2010 rising to 98.3% in 2026.
  The missing third of early members are mostly delisted (bankrupt, acquired),
  so absolute returns are biased up; *excess vs the equal-weighted eligible
  universe* compares names against the same biased pool and is far less
  affected.
- **Holding:** equal weight, one month; 10 bp per unit of one-way turnover.
- **Fundamentals point-in-time:** SEC XBRL frames (450 requests, one per
  concept × year). An annual record is visible from its period end + 120 days.
  Market cap = 10-K cover-page **public float** (dollars, stamped at the
  company's fiscal Q2 end, visible +270 days) rolled forward by the stock's own
  price — because shares × split-adjusted price is wrong by every later split,
  and frames carry split-restated share counts. Float excludes insider shares,
  so founder-held names (META) read cheaper than they are.
  Median 260 of 409 eligible names have a usable record (financials mostly
  lack gross profit / current assets).
- **Ties:** stable sorts over a sorted universe. A first run varied between
  processes — names AT their 52-week high tie at 1.0 and set order is
  hash-randomised — now deterministic (two runs byte-identical).

## Results (excess per year vs equal-weighted universe; t on monthly excess)

| Strategy | CAGR | maxDD | Excess/yr | t | 2010-14 | 2015-19 | 2020-22 | 2023-26 |
|---|---|---|---|---|---|---|---|---|
| *EW universe* | 13.6% | −26.9% | — | — | | | | |
| *SPY* | 14.5% | −23.9% | +0.6% | 0.49 | −3.0% | +0.7% | −2.7% | +8.0% |
| Momentum 12-1, top decile | 14.8% | −23.5% | +1.5% | 0.57 | −1.6% | −2.2% | −1.5% | +13.1% |
| 52-week-high nearness, top decile | 9.1% | −20.5% | **−4.6%** | **−2.02** | −2.8% | −5.1% | −5.4% | −5.8% |
| Minervini Trend Template, all passers | 12.9% | −21.1% | −0.9% | −0.42 | −1.9% | −1.8% | −3.3% | +3.6% |
| Trend Template, top 25 by RS | 15.1% | −22.6% | +1.7% | 0.51 | +1.4% | −4.1% | −0.0% | +11.6% |
| RS composite, top decile | 15.5% | −19.6% | +2.0% | 0.77 | +0.3% | −2.0% | +1.4% | +10.1% |
| Leaders top 2% by 1/3/6m | 12.6% | −35.4% | +0.3% | 0.06 | −2.0% | −3.2% | +4.3% | +5.6% |
| Low volatility, lowest decile | 10.9% | −19.6% | −3.0% | −1.08 | −2.9% | +1.4% | −6.5% | −6.2% |
| 1-month reversal | 10.4% | −39.9% | −1.5% | −0.58 | | | | |
| Gross profitability GP/A, top decile | 12.4% | −32.6% | −0.8% | -0.44 | +4.3% | −2.3% | +1.7% | −7.8% |
| Piotroski F ≥ 8 | 14.2% | −29.2% | +0.6% | 0.46 | −0.1% | +0.2% | +1.5% | +1.5% |
| Piotroski F ≥ 8 in cheapest 30% B/M | 4.9% | −62.2% | −5.2% | −0.99 | (median 3 names) | | | |
| Magic Formula, top 30 | 13.7% | −24.6% | +0.2% | 0.14 | +2.5% | −0.2% | +1.6% | −3.4% |
| Value composite, top decile | 13.1% | −48.1% | +0.8% | 0.32 | −3.2% | −0.7% | +6.8% | +3.3% |
| Trending Value | 13.3% | −44.2% | +0.7% | 0.29 | −0.7% | −2.7% | +3.7% | +4.8% |
| Quality+Momentum (GP/A top 30% → 25 by 12-1) | 14.6% | −25.2% | +1.0% | 0.47 | +0.4% | −2.1% | +0.1% | +6.8% |

**Corrections made in review (2026-10-19), all now in the table:** the first
Magic Formula run ranked ascending and negated, selecting the 30 WORST names
(it printed +1.9%, t 1.0 — the inverted rule looked better than the real one,
which is itself a lesson about noise); balance sheets from the Dec-31 frame
were visible before they were filed for non-December fiscal years (a record
is now dated by its LATEST period end); the float roll-forward used total-return
prices (now split-only closes, `collect_raw.py`); an unknown shareholder yield
scored 0; one-bar price holes blanked a stock's 52-week signals for a year;
and (third review) the Magic Formula / GP/A / Quality+Momentum backtests did
not exclude Finance and Utilities as the shipped screens do — now excluded by
SEC SIC code (6000-6799, 4900-4999; `collect_sic.py`), which took the Magic
Formula from +0.9% to +0.2%/yr (t 0.1).
`fund_backtest.py` now asserts every ranked screen's picks beat the universe
median on their own metric, and the card's numbers are exported
(`export_evidence.py` → `scripts/screener_evidence.json`), never hand-copied.

Market-timing filter (CAN SLIM "M": cash when SPY < 200-day average) made
every strategy worse — including the plain index (−4.7%/yr, t −1.8): the
2010-26 sell-offs recovered faster than a month-end 200-day rule re-enters.

Monthly IC (Spearman vs next-month return), none significant: 12-1 +0.007,
52w-high −0.001, RS +0.004, −vol −0.010, reversal +0.008, Trend Template
−0.004, GP/A +0.011 (t 1.25), Piotroski +0.006, Magic +0.006, value +0.005.

## What the tab does with this

- Ships **ten screens**, rule-exact, each with its source, its rules, and the
  row above as its evidence card. None is labelled "winning".
- **Minervini / Trend Template is a filter**, not a ranking (its edge, if any,
  is criteria 7-8: nearness to the high and RS — the same family as momentum).
- **52-week-high nearness is not shipped as a standalone ranking**: it is the one
  result that is significantly *negative* here.
- **CAN SLIM and the breakout screens are not backtested** (quarterly EPS
  history and base/consolidation detection need data or judgement the test does
  not have) — the card says so.
- Fundamentals come live from the TradingView scanner (one request for the
  whole US market, incl. a computed Piotroski score); prices from Yahoo daily
  bars, computed with the same definitions as `backtest.py`.
