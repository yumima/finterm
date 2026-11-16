# Company report — a quarterly investor report on any stock

Open **RESEARCH**, load a symbol, and click the **Report** tab. The report
builds itself from everything the terminal holds on the company; nothing has
to be configured for the figures and charts.

## What is in it

1. **At a glance** — the summary, what the numbers show for and against the
   company, the year against the S&P 500.
2. **The business** — what it sells and to whom, revenue by segment, product
   and geography, the customers its annual report singles out, competition.
3. **What is new** — this quarter's developments, new products, new business,
   headlines about the company.
4. **Financial performance** — revenue, profit and margins by year and quarter.
5. **Cash and where it goes** — cash generated; R&D, capex, acquisitions,
   buybacks and dividends; cash against debt.
6. **Earnings against expectations** — reported against consensus, how the
   stock moved, where estimates are heading, the coming report.
7. **Valuation and peers** — multiples, what a multiple would put on next
   year's earnings, the company against its peers.
8. **Analysts and ownership** — ratings, price targets, rating changes,
   institutions, insiders, short interest.
9. **Advantages and risks** — as the annual report describes them, and how
   the shares have actually traded (volatility, drawdown, beta).
10. **Outlook** — what analysts expect, the case for and against, what to
    watch, and a direction for the next three months.

The last table, **Sources**, lists every input, when it was read, and what
could not be loaded and why — with links to the filings on sec.gov.

## Where the content comes from

- **Figures** are the vendor's (Yahoo Finance, SEC EDGAR) or arithmetic on
  them. "What the numbers show" is read off the data by fixed rules; each
  line states its figure and its threshold.
- **Segment, product and geography revenue** and **named customers** are the
  company's own audited tags in its annual report. A breakdown is shown only
  when it adds up to the year's revenue.
- **Prose** is written by the language model you configured (Settings › AI
  Config), from the company's 10-K / 10-Q (20-F for foreign companies) and
  the figures on the page. Any number the model writes that is not in that
  material is removed, and the report says how many were. Without a model the
  report still stands on its charts and rules.
- **The direction** is a word — up, flat or down — never a target or a
  probability. It is recorded with the other AI calls (FinGPT tab) and graded
  against the real close when its three months are up.
- **Peers** are a hand-kept group where one exists; otherwise the companies
  nearest in size in the same Yahoo industry, and the report says which.

## Practical notes

- **SEC filings need a contact email** (Settings › Credentials › Contact
  email). SEC refuses requests without one; the report says so if it is
  missing and builds the rest.
- **A local model takes a few minutes** to write the two sections. They are
  kept for the day; **REWRITE NARRATIVE** asks again.
- **Returning to the tab retries** any source that failed.
- **EXPORT PDF** writes an A4 report on white; **EXPORT HTML** writes one
  self-contained page with the charts inside it.
- A fund or an index has no statements or filings of its own; the report
  says so rather than showing empty charts as if they meant something.
