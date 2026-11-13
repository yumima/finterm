---
name: morning-note
description: Produce a daily market-open brief tying overnight moves, scheduled catalysts, and the user's watchlist into an actionable pre-market pack.
---

# Morning Note

You are writing today's pre-market note. Audience is the user
looking at their own positions and watchlist before the open.

## Workflow

1. **Index check.** Pull SPX / NDX / RUT futures + last close.
   Note overnight move with magnitude (`+0.4%`, not "slightly up").
2. **Macro overnight.** Anything that moved index futures: Asian
   close, European session, overnight Fed speak, scheduled data
   for today. Two bullets max.
3. **Watchlist scan.** For each ticker in
   `finterm://watchlist/all`, fetch overnight news + scheduled
   events today. Surface only items with a price-impact angle.
4. **Earnings on deck.** Tickers reporting this morning or after
   close — for each, last quarter's KPI walk + sell-side bar.
5. **Sector skew.** Which sectors are bid / offered pre-market and
   why (one-line cause for each).
6. **Open questions.** Three things to watch in the first hour.

## Data-source priority

- `int__fd_*` — financial-datasets REST for fundamentals, news,
  filings, earnings calendar.
- `int__get_news*` — finterm's news aggregator (resource
  `finterm://news/digest` is a curated read).
- `int__get_quote*` — pre-market / overnight quotes.
- `int__get_holdings`, `int__get_portfolios`,
  `int__get_watchlist_*` — what the user actually owns / watches.
- External MCPs (yfinance, brave-search) — fall-through only when
  the internal surface returns nothing.

Avoid speculation. If a claim isn't backed by a tool call result,
say so explicitly or omit it.

## Output shape

Plain markdown, ~400 words, sections in the order above. No
preamble, no sign-off — the user reads this with coffee.
