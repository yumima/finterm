---
name: earnings-analysis
description: Analyse a printed quarter — KPI walk vs consensus, guidance change, call reveal, and the bull/bear set-up into the next print.
---

# Earnings Analysis

You are reviewing a printed quarter. Audience is an analyst who
already knows the company and wants a tight, specific read.

## Workflow

1. **Confirm the print.** Period, reported date, key headline
   numbers (revenue, EPS, FCF). Don't restate the press release;
   reference it.
2. **Identify the 3-5 KPIs that move the stock.** Different for
   every business — for SaaS it's NRR + new ARR + magic number;
   for retail it's comp + traffic + ticket; for banks it's NIM +
   credit + capital return. Walk each vs consensus and prior
   quarter.
3. **Guide change.** What did management raise / cut / hold?
   What's the implied second-half walk vs the new full-year? If
   the cut was buried in a slide, surface it.
4. **Call reveal.** The single biggest new thing from the call —
   the change of tone, the new disclosure, the dropped line. One
   sentence.
5. **Bull / bear set-up.** What does the bull need to see at the
   next print to stay bull. What does the bear need to see to
   roll the thesis.
6. **Model deltas.** What needs to change in a 3-statement model
   today based on this print, line by line. Skip if the user
   didn't supply a model.

## Data-source priority

- `int__fd_*` — financial-datasets REST: segmented financials,
  operational KPIs, earnings data.
- `int__get_filing*` — 10-Q / 8-K sections for the just-printed
  quarter.
- `int__get_news*` — sell-side reaction, peer reads.
- External MCPs (yfinance, brave-search) — pricing context + post-
  print drift only.

Cite the line item or filing reference for every number. If
consensus isn't available, say "consensus N/A" — don't fabricate.

## Output shape

Plain markdown, sections in the order above. Be specific with
magnitudes (`+220 bps`, not "expanded materially"). No hedging
language unless the data genuinely disagrees with itself.
