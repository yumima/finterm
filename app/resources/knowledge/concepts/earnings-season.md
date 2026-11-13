# Earnings Season

Four times a year, US public companies file their quarterly financial results. The concentrated release of these reports — roughly 2–3 weeks after each calendar quarter ends — is called **earnings season**. It's the single most predictable source of large individual stock moves on a given calendar.

## The calendar

| Quarter | Fiscal period ends | Earnings season peak | Key reporters |
|---|---|---|---|
| Q1 | March 31 | Mid-to-late April | Banks first (mid-April), then tech (late April) |
| Q2 | June 30 | Mid-to-late July | Same sequence |
| Q3 | September 30 | Mid-to-late October | Same sequence |
| Q4 | December 31 | Mid-to-late January + February | Extended: fiscal year wrap-ups |

Banks and financial companies typically report first, tech and consumer companies in the second week, and the long tail of smaller-caps through the following weeks. A handful of major companies — MSFT, AAPL, AMZN, GOOG, META — have outsized influence on index moves during their reporting windows.

## The anatomy of an earnings report

A quarterly report has three main outputs:

**1. Earnings Per Share (EPS)**

```
EPS = (Net Income − Preferred Dividends) / Diluted Share Count
```

The "adjusted" or "non-GAAP" EPS that companies report strips out one-time charges, stock compensation, and restructuring costs. Analysts build models to these non-GAAP numbers. The **beat/miss/in-line** classification is relative to the consensus analyst estimate.

**2. Revenue**

Top-line revenue (sometimes called net sales). More important for high-growth companies where EPS is small or negative. Revenue is harder to manipulate than earnings.

**3. Guidance**

Management's forward outlook for the next quarter and/or full year. Often **more important than the reported numbers**. A company that beat Q1 but guided Q2 below consensus will typically sell off despite the beat.

## How analyst estimates work

Sell-side analysts at investment banks build financial models for covered companies. Each analyst publishes an EPS estimate. Data providers (Bloomberg, FactSet, Visible Alpha) aggregate these into a **consensus estimate**.

The key level is not the actual earnings — it's the **gap between actual and consensus**:

```
Surprise % = (Actual EPS − Consensus EPS) / |Consensus EPS| × 100
```

A 5% EPS beat against consensus is a meaningful positive surprise. A 2% beat against consensus when guidance is cut below consensus is a net negative event.

## The whisper number

Beyond the published consensus sits the **whisper number** — an informal estimate reflecting what sophisticated traders and buyside analysts actually expect. In bull markets, whisper numbers trend higher than consensus, so even a consensus "beat" can disappoint. The setup matters: buying into a name that "needs" a big beat to hold its valuation is dangerous.

## Revenue and segment breakdown

For large, diversified companies, the total EPS and revenue numbers are less informative than the **segment breakdown**:

- **Segment revenue and margin**: which businesses are growing/shrinking?
- **Geographic mix**: are China revenues accelerating or decelerating?
- **Bookings / backlog / RPO**: forward revenue indicators
- **Gross margin**: is pricing power holding?
- **Operating margin**: are costs under control?

For banks, the relevant metrics are net interest margin (NIM), loan growth, and provision for credit losses. For cloud/SaaS, watch revenue growth rate, ARR, NRR (net revenue retention), and free cash flow margin.

## Stock reaction patterns

The average large-cap stock moves 4–5% on earnings day. For smaller, more speculative names, 10–20% moves are common. The move direction is not always obvious:

| Scenario | Typical reaction |
|---|---|
| Beat + raised guidance | Strong rally, often 5–10%+ |
| Beat + unchanged guidance | Modest rally or flat |
| Beat + lowered guidance | Sell-off despite beat ("sell the news") |
| In-line + neutral guidance | Minimal move |
| Miss + maintained guidance | Moderate sell-off |
| Miss + cut guidance | Sharp sell-off |
| Miss + cut guidance in weak macro | Severe sell-off |

The "sell the news" phenomenon is common when a name has already rallied into earnings and expectations are elevated. Implied volatility (via options) collapses after the print regardless of direction — this is the "IV crush."

## IV crush and options during earnings

In the week before earnings, options are expensive because implied volatility rises to reflect the uncertainty of the event. Immediately after the print, IV collapses. This means:

- Buying options just before earnings usually loses money unless the move significantly exceeds the priced-in move
- **Expected move** = roughly 1× the at-the-money straddle price
- **Straddle sellers** profit if the actual move is smaller than expected
- Selling premium into earnings is a definable edge — but catastrophically wrong when the stock gaps 20%+

See [[options-basics]] and [[greeks-deep-dive]] for context.

## Revisions matter as much as the beat

The earnings reaction isn't a single day. Over the following weeks, analysts revise their models. A company that beat and raised will see estimate revisions higher — which is the engine of continued stock performance. A name with multiple negative revisions after a weak print tends to have prolonged underperformance (known as the "negative revision cycle").

Watching the **EPS revision trend** over 3–6 months is often more predictive than the single-print beat/miss.

## What to do during earnings season

For individual stock positions:

- Know the expected move (ATM straddle price) before the print
- Decide whether to hold through or reduce before the release
- Read the press release and transcript, not just the headline beat/miss
- Focus on guidance and forward indicators over reported numbers
- Check gross margin trends — they lead earnings quality

For market-level positioning:

- Aggregate beat rates (the percentage of S&P 500 companies beating consensus) signal whether expectations were set appropriately
- Revenue beat rates are harder to game than EPS, so more informative
- Forward earnings estimate revisions for the index indicate where full-year estimates are heading

## In finterm

The Equity Research screen shows historical EPS actuals, estimates, and surprise percentages by quarter. The News screen aggregates earnings releases in real time. During peak earnings season, watch multiple prints in a single day for sector-wide read-throughs (e.g., one large bank's NIM guidance affects all banks).
