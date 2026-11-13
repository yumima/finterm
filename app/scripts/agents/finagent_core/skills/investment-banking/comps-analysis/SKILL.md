---
name: comps-analysis
description: Build a defensible comparable-company table — peer set selection, multiple choice, normalisation, and the implied valuation range for the target.
---

# Comparable Company Analysis

You are building a comps table for the user's target ticker.
Audience is someone who will use this in a memo or pitch — every
choice needs to be defensible.

## Workflow

1. **Target snapshot.** Sector, business model, growth profile,
   margin profile, capital intensity, geographic mix.
2. **Peer set selection.** Pick 5-8 comparables on three axes:
   business model, size, and growth/margin profile. List your
   inclusion criterion *and* what you rejected and why. A mis-
   anchored peer set is the #1 way comps lie.
3. **Multiple choice.** For each peer:
   - EV / NTM revenue
   - EV / NTM EBITDA (skip when EBITDA-irrelevant; SaaS, banks)
   - P / NTM EPS
   - PEG (only when growth profiles are comparable)
   Pick the multiple that's load-bearing for this sector and lead
   with it.
4. **Normalise.** Strip one-time items, stock-based comp where
   industry convention demands, FX effects, accounting-method
   gaps. Show the as-reported and the adjusted side by side.
5. **Implied range.** Median + 25-75 percentile from the peer
   set, applied to the target's NTM metric → implied price range.
   Walk the assumption from each multiple to a number.
6. **Where this lies vs comps.** Target's current multiple vs
   peer median. Justify any premium or discount with one specific
   business-quality difference.

## Data-source priority

- `int__fd_*` — financial-datasets REST: segmented financials,
  comparable-company peers, consensus estimates.
- `int__get_filing*` — most recent 10-K / 10-Q for normalisation
  adjustments (one-time items, SBC schedules).
- `int__get_quote*` — current price and shares-out for EV calc.
- External MCPs (yfinance, brave-search) — backup pricing /
  shares-out only.

Cite the source for every number. Don't fabricate peer multiples
when data is missing — drop the row and note the gap.

## Output shape

Plain markdown with a comps table (peer / metric / value /
adjustment) plus the workflow steps above. End with the implied
price range and a one-sentence answer to "is this cheap or rich
relative to peers, and why."
