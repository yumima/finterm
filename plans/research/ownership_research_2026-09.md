# Ownership research — products and evidence (2026-09-05)

Companion to `plans/PLAN_OWNERSHIP_REDESIGN.md`. Compiled by a research pass over 25+ products and ~60 papers; the verification key below is the researcher's own; anything it marks as unverified must not be cited from this file.

---

# Equity Ownership: How Terminals Present It, and What the Evidence Says

Research report for fincept-qt. Consolidated from seven parallel research streams plus direct
primary-source verification. Corrections from both addenda are folded in inline — where an earlier
claim was wrong, the retraction is stated at the point the claim appears.

**Method.** ~200 web searches across seven streams; primary-source PDFs extracted locally with
`pdftotext` rather than trusted via summaries; live product APIs hit directly with curl (Nasdaq,
FINRA, SEC EDGAR, OpenInsider, 13f.info) to read schemas firsthand.

**Verification key used throughout:**

| Mark | Meaning |
|---|---|
| OK | Read from the primary source's own tables / DOM / API this session |
| ABS | Abstract or publisher page only |
| SEC | Credible secondary source (another paper's lit review, vendor whitepaper) |
| UNVERIFIED | Could not verify — **do not cite** |

---

# PART (a) — Per-product design descriptions

## 1. Bloomberg Terminal

### HDS `<GO>` — Holdings Display

`TICKER <EQUITY> HDS <GO>`. **Unit of analysis: one row = one holder** (institution or insider),
for a fixed security.

**Tabs** (OK — OCR'd 2012 NYU Stern screenshot of INTC): **Current · Historical · Matrix ·
Ownership · Transactions · Options**. SEC: a 2025 Babson guide instead names tabs "Ownership
Summary" and "Insider Transactions" — I could not reconcile whether these are renames or a
different screen. My clearest column-level evidence is a decade old; treat current-day labels as
unconfirmed.

**Columns**, reconstructed from a verbatim OCR'd row (OK):

```
2. STATE STREET CORP | STATE STREET CORPORA | 13F | 207,444,400 | 4.17 | 9,862,787 | 09/30/12
```

→ rank · **Holder Name** · **Portfolio Name** (often duplicates holder) · **Source** (`13F`,
`Form 4`, `n/a`) · a flag column (a "y" appears intermittently; meaning unconfirmed) ·
**Amount Held** · **% Out** · **Latest Chg** (share delta since last filing) · **File/Report Date**.
A `%Out` total (69.83%) sits at the bottom.

**Default sort: by position size, largest first** (OK). `HDC` is the alphabetical counterpart.
`HDSM` is the filterable variant (SEC — sources disagree on what the "M" denotes). HBS Baker
Library confirms a **"Sort By"** dropdown plus filter dropdowns by holder **Types** and
**Countries** (OK) — but no source gave Bloomberg's actual type enum, so the Investment-Advisor /
Hedge-Fund / SWF / Pension taxonomy in the brief is **unconfirmed**.

### OWN `<GO>` — Ownership Summary

**Unit of analysis: the security**; holders aggregated into category totals.

Four quadrants (OK — Cranfield LibGuide): institutional holder statistics, insider transactions,
geographic ownership, ownership-type breakdown. Clicking any heading expands a percentage
breakdown. An orange **"Compare Current Stats Against"** date box does two-date comparison. The
Current tab expands an institution into its constituent fund families. The Insider Transactions tab
plots transactions on a chart with a "Table" toggle — **red = sell**, and clicking a point reveals
price, share count and date (OK).

**Top-of-page summary fields** (OK — from a DES sub-panel pulling OWN data):

| Field | Example |
|---|---|
| # of Inst. Owners | 2,650 |
| Shares Owned | 3.3B |
| **Shares Out/Float** | **66.3% / 68.8%** |
| **# of Buyers/Sellers** | **1,017 / 958** |
| Shares Bought | 4.61M |
| % Held by Insiders | 3.55% |
| Net change last 6M | −0.03% |

Two design decisions matter. Institutional ownership is reported against **both denominators side
by side** (% of shares outstanding *and* % of float), never collapsed. And the **buyers-vs-sellers
count is a first-class headline statistic**, not a derived extra.

### PHDC `<GO>`

OK — confirmed via a U. Delaware reference sheet: *"Customizable equity search on holdings,"*
sitting in the Portfolio Manager function family (alongside PRTU/PORT/PREP). No column list found
in any source.

### Functions in the brief I could NOT confirm

- **MHD** — searches drown in a ticker collision (BlackRock MuniHoldings). One source lists it as a
  "public portfolio search, multiple-security function" (SEC), consistent with a holders×securities
  matrix, but **no primary source documents it as "Matrix Holdings Display."** Note HDS already has
  a **Matrix** tab, which may be what was meant.
- **13F `<GO>`** — UNVERIFIED; no evidence this mnemonic exists. The real reverse-direction function
  is **FLNG** (OK) — "search for 13F filings": given a manager, list its securities.
- **HOLD `<GO>`** — UNVERIFIED; no evidence across ~10 targeted searches. Likely not a real mnemonic.

### Change vs level

**Level leads.** Level = Amount Held / % Out / % of Float. Change = **absolute share delta since
last filing** (quarterly for 13F filers), shown as a raw count, not a percentage. Insider change is
separately a **rolling 6-month percentage**. No user-selectable change-period toggle was found.

### What a user does with it

- **IR investor targeting** — the `IR <GO>` dashboard (OK) carries a "Peer Barometer" and "Company
  Barometer" plus *shareholder analytics showing which institutions are buyers or sellers in a
  company's own stock and its peer stocks*, explicitly built as targeting tools.
- **Sell-side sales list building** — HDSM described as valuable "especially in sales."
- **Activist-target screening** — Bloomberg's own **Activism Screening Model** (OK) runs in
  `EQS <GO>` and uses 3 ownership factors out of 17: *"High institutional ownership"* (rationale:
  *"makes it easier for an activist to win support in a proxy fight"*), *"High 30-day volume as % of
  outstanding,"* and *"Float higher than 70%"* (rationale: *"Non-float shares almost always side
  with management"*). Stated hit rate: **20% of the top percentile face an activist campaign within
  12 months vs a 4.5% Russell 3000 base rate.**
- **Concentration/liquidity risk** — Babson frames HDS as answering *"concentration risk in a small
  group of shareholders and whether management has invested in the business."*

### What Bloomberg admits it can't do

In 2017 Bloomberg partnered with **CMi2i** rather than building in-house, specifically because 13F
and custodian disclosures show the **nominee, not the beneficial owner** (OK). That is a vendor
admission that the raw register does not answer "who actually owns this."

---

## 2. FactSet Ownership (formerly LionShares)

**Unit of analysis: both, explicitly.** Top-level tabs (OK): **Company | Holder | Screening |
Profile Builder | Cross-Watchlist | UK Index | Watchlists | Settings**. The *Company* tab is
holders-of-a-security; the *Holder* tab is securities-of-a-holder.

**Company sub-tabs** (OK): **Search | Summary/Detail | Comprehensive Detail | Historical Analysis |
Insider Transactions | ADR/ORD | Peer Analysis | Projected Ownership | Graphical Ownership |
Inactive Company**. Note there is **no top-level "Buyers & Sellers" tab** — that lives inside
Summary/Detail as the Ownership Activity panel.

**Holder grid columns** (OK, verbatim):

```
N | Holder Name | Activism | Details | Position | Pos Change | Mkt Val | % Port | % O/S | Rpt Date | Source
```

The **Activism column is a per-holder inline badge** (OK) sourced from FactSet SharkWatch, with
tooltip text like *"Activism Threat Level: Medium. Norges Bank Investment Management has been
involved in 4 activist campaigns against 4 different companies."* **No other vendor puts activism
inline in the register.**

**Top-of-page summary — Ownership Statistics panel** (OK): Shares o/s · Short Interest (%) ·
Float (%) · Insider/Stake Ownership (%) · Custodial Ownership (%) · Institutional Ownership (%)
split into *North American* / *Non-North American* · Institutional Ownership as % of Float ·
Top 10 Inst. Holders (%).

**Ownership Activity panel** (OK — the Buyers & Sellers view): Total Positions · New Positions ·
Increased Positions · Decreased Positions · Sold-out Positions · Net Position Change — each with
**Holders and Shares** sub-columns.

**Investor Analysis panel** (OK): Metro Region Summary · Country Summary · **Holdings Style
Summary** · **Manager Style Summary** · **Cap Group Style Summary** · **Turnover Summary** ·
**Institution Type Summary**.

**Peer Analysis** (OK) — the classic IR targeting screen. A header table:

```
Company Name | Mkt Val | Price | Float (%) | Inst Ownership (%) | Top 10 Inst Holders (%) | Holders
```

above a **holder × security cross-tab**:

```
Holder Name | [Peer 1] Pos | [Peer 2] Pos | Mkt Val Of Peer | Rpt Date | Equity Assets | City | Metro Region | State | Region | Country
```

Literally "who owns Home Depot AND Lowe's, at what size in each" — the underweight/targeting
workflow.

**Taxonomy** (OK — FactSet's own OpenAPI SDK): `HolderType` = `F` Institutions · `M` Mutual Funds ·
`S` Insiders/Stakeholders · `FS` Institutions/Insiders · `B` Beneficial Owners. Style tags: Growth,
Value, GARP, Yield, Index, Hedge Fund. A per-holder **Turnover** column shows values like `LOW`
(OK), but the full four-tier enum is unconfirmed.

**API fields worth stealing** (OK): `SecurityHolders` carries `investor_type, holder_type,
adj_holding, adj_market_value, weight_close, percent_outstanding, source`.
`InstitutionalTransactions` has `pct_os, pct_change, pos_change, market_val, period_of_measure,
report_date, as_of_date` — note **`report_date` and `as_of_date` are separate fields**.
`InsiderTransactions` has 20 fields including `transaction_code`, `is_derivative`, `is_direct`, and
**`sec_rule10b51`** (the 10b5-1 plan flag). `PeriodOfMeasure` enum = `1M, 3M, 6M, 12M`.
`AssetType` = `ALL, EQ, FI`. A separate **Report Builder API** adds `/holders` and
`/ultimate-parent` (groups 13F filers by ultimate parent, e.g. sub-advisors rolled into the parent).

**Coverage**: ~50,000 institutions, ~60,000 mutual fund portfolios, ~400,000 insiders across ~110
countries. US institutional from 13F; mutual funds from N-CSR/N-Q; insiders from Forms 3/4/5 and
13D/G; non-US via a per-country "Stakes" collection.

**Conviction / fund score:** SEC — **ambiguous and I could not settle it.** The "Fund Sentiment
Score" description my research surfaced traces to Fintel's site, not FactSet's own documentation.
FactSet's confirmed scoring layer is **SharkWatch Activism Threat Level** (event risk, not alpha).
**Do not assume FactSet ships a conviction score.**

**Change vs level:** parallel columns — `Position` and `Pos Change` adjacent; level leads.

---

## 3. LSEG Workspace / Refinitiv Eikon — app code `OWNS`

**Unit of analysis: both, as sibling sub-tabs** — **Firm Ownership Summary** (who owns this stock)
vs **Fund Ownership Summary** (what does this fund own) (OK).

**Ownership menu** (OK, verbatim, 2025): **Ownership Summary | Shareholders Report | Fund Ownership
Summary | Shareholders Peers Analysis | Insider Report | Smart Holdings Model | Equity Holdings › |
Private Equity Holdings ›**

**Top-of-page header** (OK): `Primary Exchange | Free Float | Free Float % | Shares Outstanding |
Strategic Entities Ownership % | Market Capitalization (M)`

**Top Investors columns** (OK):

```
Investor Rank | Investor Name | % O/S | Position (M) | Position Change (M) | Value ($,M) | Value Change ($,M) | Latest Filing Date
```

Default sort is by rank (position size). Older Eikon UI also showed a per-row **Turnover** tag
(e.g. `LOW`).

**Recent Activity panel** (OK) — splits **BUYS / SELLS**, each `Investor Name | Activity Details |
Value ⚬ Shares` (radio toggle), documented as *"derived from share changes using filing dates over
the past 6 months."*

**Breakdown panel** (OK) — tabs **Type | Style | Location | Rotation | Turnover**, switchable
list/pie. Table: `[category] | Investors | % O/S | Position (M Shares) | Value (M USD)`. Live
**Style** values (OK): `VALUE, GARP, GROWTH, OTHER, INDEX, INCOME, HEDGE FUND, QUANTITATIVE,
MOMENTUM` — richer than FactSet's, adding Quantitative and Momentum.

**Holdings Concentration panel** (OK) — explicit tiers **Top 10 | Top 20 | Top 50 | Top 100** as %
of shares outstanding, with a drill-down.

**StarMine Smart Holdings** (OK) — the one genuinely *predictive* ownership model among the four
institutional vendors. It reverse-engineers each fund's purchasing profile (P/E, D/E, price
momentum) from its known 13F/Lipper holdings, then scores every global stock by aggregate "appeal,"
ranking by **predicted future increase or decrease in institutional ownership**. 45,000+ companies,
point-in-time from 2000, daily EOD. *Smart Holdings Plus* adds 13 ESG-selected quant factors from
870+ candidates, marketed as *"Predict Smart Money's Next Move."* **Note what it predicts: future
ownership flow, not future returns.**

**Formula fields** (OK): `TR.InvestorFullName, TR.InvestorType, TR.InvInvmtOrientation,
TR.PctOfSharesOutHeld, TR.OwnTrnverRating, TR.InvestorTotalAssets, TR.InvParentType,
TR.SharesHeld`. The complete `TR.InvestorType` enum could not be retrieved (UNVERIFIED).

**Coverage**: equity holdings worth $50tn across 70 countries; history from 1997, with **US 13F back
to 1978 and US insider back to 1986**. Local-market specialty content (Japanese Detailed
Shareholdings, UK Share Registers) is called out as a differentiator.

---

## 4. S&P Capital IQ / CapIQ Pro

**Unit of analysis: the security.** SEC — the fund→security direction appears thinner than
FactSet/Refinitiv; no screenshot evidence of a symmetric "fund's holdings" screen was found.

Left-nav (OK): **Investors > Private Ownership | Public Ownership**. Public Ownership tabs (OK):
**Summary | Detailed | History | Crossholdings | Insider Trading | Charts | Holders Analysis**.

**Ownership Summary table** (OK — transcribed from S&P's own training-deck screenshot of MSFT):

```
Type | Common Stock Equivalent Held | % of Total Shares Outstanding | Market Value (USD in mm)
```

Rows: **Institutions · Corporations (Private) · Individuals/Insiders · Public and Other · Total**,
with a donut of the three top-level buckets.

**Institutional Ownership Details By: [Owner Type ▾]** (OK):

```
Type | Common Stock Equivalent Held | % of Inst. Ownership | % of Total Shares Outstanding | Market Value (USD in mm) | Number of Holders
```

Owner-type rows — CapIQ's distinctive contribution, because its primary lens is **legal /
organizational type, not investment style**: *Traditional Investment Managers · Banks/Investment
Banks · Government Pension Sponsors · Hedge Fund Managers (<5% stake) · Family Offices/Trusts ·
Charitable Foundations · Corporate Pension Sponsors · Insurance Companies · VC/PE Firms (<5%) ·
Educational/Cultural Endowments · Sovereign Wealth Funds (<5%) · REITs · Unclassified*.

**This is where the "government / sovereign" ownership category in the brief actually gets
first-class treatment.** No other product surveyed breaks out sovereign wealth funds and government
pension sponsors as distinct rows.

**Top Holders** (OK): `Holder | Common Stock Equivalent Held | % of Total Shares Outstanding |
Market Value (USD in mm) | Position Date`.

**Other tabs** (SEC, via university LibGuides, not screenshot-verified): *Detailed* (All Owners /
Corporations / Individuals-Insiders), *History* (same categories over time), *Crossholdings*
("holders/holdings in common across comparable companies" — the peer cross-tab), *Insider Trading*
(all history of individual trades), *Charts / Holders Analysis* (graphing trading activity or
composition over time).

CapIQ tracks **~12,000+ activism campaigns**, 13D-triggered, with companies removed 24 months after
a campaign launches — but SEC: no evidence it's surfaced inline per-holder the way FactSet's is.

**Scoring:** a separate **"CIQ Alpha Models"** section exists (US Value/Growth/Quality/Momentum) but
it is a general factor product, **not ownership-derived**. **CapIQ is the most purely descriptive of
the four.** `IQ_`-prefixed field names: UNVERIFIED.

---

## 5. WhaleWisdom

**Unit of analysis: fully dual** — `/filer/` and `/stock/` are architecturally symmetric (OK).

**Filer page tabs** (OK): `SUMMARY | HOLDINGS | IND. MANAGERS | GLOBAL HOLDINGS | 13D/G |
INSIDER (FORM 4) | COMMENTS AND UPDATES`

**Top of the filer page** (OK) — three gauge widgets side by side: *Equal-WT WhaleScore 2.0*,
*S&P 500 WhaleScore* (e.g. 86/100), *Equal-WT WhaleScore 1.0*. Below them, **Top Buys (13F)** and
**Top Sells (13F)** panels (columns: Name, % Change) carrying a sub-toggle:

> **"BY % PORTFOLIO / BY LARGEST $ CHANGE"**

That toggle is the single most-imitated idea in this space: *the same trade ranks differently by
conviction than by dollar size, so let the user pick the denominator.*

An auto-generated narrative line reads e.g. *"Berkshire Hathaway… has at least 189 13F filings, 46
13D filings, 262 13G filings… top 10 holdings concentration of 88.47%."*

**Stock page tabs** (OK): `SHAREHOLDERS (13F, 13D/G) | CURRENT MUTUAL FUND/ETF HOLDERS | ACTIVE
13D/G FILINGS | SEC FILINGS | INSIDERS | SHORT POSITIONS | CONGRESS | PROPOSED SALES | COMMENTS AND
UPDATES`

Stock pages also carry **Weekly Scores, 0–100 percentile ranks** (OK): an **"Overcrowding Risk"**
score (e.g. 49, rank 2725 of 5573) and a **"Momentum"** score (69.99, rank 674 of 5573) —
independent of WhaleScore.

**Holdings table columns** (SEC — canvas-rendered; consistent across multiple sources but not read
off the live DOM): Symbol/Issuer Name · Sector · Shares Held · Market Value · % of Portfolio ·
**Previous % of Portfolio** · Rank · Change in Shares · % Change · % Ownership · **Qtr 1st Owned** ·
**Est Avg Price Paid** · Source. Filter chips: `Any | New | Closed Out | Added To | Reduced |
No Change`, plus a "hedge funds only" toggle.

**Whale Score** (SEC, per WhaleWisdom's own whitepaper): *"uses a mixture of risk-return measures
that predict future Alpha,"* ranks managers quarterly on ~3 years of data, scale 1–100 vs the
S&P 500. Methodology beyond that is proprietary.

**13F Heatmap** (OK) — lists "the 100 hottest stocks based on the latest 13F filing data." A
separate **Activist Heat Map** restricts to funds classified as activists.

**Backtester** (OK) — the only user-runnable one in the space. Inputs: fund(s) or custom group, 1–50
holdings, equal-weight vs actual-allocation, market-cap/sector/price filters, rebalance frequency
(**default 46 days post quarter-end**), optional S&P-short hedge overlay. Output: performance chart
vs S&P 500 plus downloadable Excel of performance stats and actual rebalancing transactions.
Published claims (SEC — self-reported, unaudited): Appaloosa top-10 clone *"1,241% total return"*
since 2001 vs *"152%"* for the S&P; an "Elite Funds" group *"4,829%."*

**Staleness handling** (OK) — acknowledged, but buried in a whitepaper rather than shown on the
page: *"The 45 day delay in reporting after a quarter's end can misrepresent the current holdings of
a fund"* — up to **135 days stale**.

**Other confirmed features:** Combined Holdings, Consensus Holdings, 13F Fund Performance Evaluator,
13F Stock Screener, Latest 13F Filings feed, 13F Trend Charts, Schedule 13D/G Search, Email Alerts,
Excel Add-in, Developer API (CSV/JSON).

---

## 6. Fintel.io

**SEC / UNVERIFIED — everything here is unverified.** Cloudflare/reCAPTCHA blocked both WebFetch and
browser access on `/so/us/aapl`, `/institutional-ownership-data` and `/ownershipExplorer`; the
research agent correctly declined to bypass it.

From secondary sources: the flagship is the **Fund Sentiment Score** (formerly **"Ownership
Accumulation Score"**) — a 0–100 percentile ("50 = average institutional accumulation level"),
described as a *"multi-factor quantitative model"* combining **the total increase in disclosed
owners** and **changes in portfolio allocations across disclosed owners**, sourced from **13F *and*
N-PORT** (broader than pure 13F). A **Fund Sentiment Leaderboard** ranks securities by it.

Architecturally Fintel is **the most security-primary product in the space**: one per-ticker
dashboard fusing ownership + sentiment + short interest (incl. borrow fee rate and a "short squeeze
score") + insider Form 4, with no real filer-centric equivalent. Exact column headers and the full
formula: **not confirmed**.

---

## 7. Dataroma

**Unit of analysis: strictly filer-first.** 83 tracked "Superinvestors"; security pages exist only
as a thin cross-reference.

Nav (OK): `Home | Commentaries/Articles | Superinvestors | Activity | S&P500 Grid | Grand Portfolio |
RealTime | Insider`

Homepage shows "Superinvestor Portfolio Updates" (latest filing feed), "Latest significant insider
buys of Superinvestor holdings" (`Date Filed | Stock | Total Value $ | Price $`), and stats blocks
(top-10 most-owned, top-10 by allocation %, "big bets," near-52-week-lows).

**Activity Summary grid** (OK): `Portfolio Manager - Firm | Period | Top 10 Buys/Sells` — one row
per manager, e.g. *"Warren Buffett - Berkshire Hathaway | Q2 2026 | GOOGL GOOG BAC DAL COF KR NUE
LEN DVA M."*

**Manager holdings page** (OK; tabs `Holdings | Activity | Buys | Sells | History`):

```
Stock | % of Portfolio | Recent Activity (Add/Reduce/Buy + %) | Shares | Reported Price* | Value | Current Price | +/- Reported Price | 52 Week Low | 52 Week High
```

Footnoted: *"\*Reported Price is the price… as of the portfolio date… the last known price at which
the security was still held."* **Best-in-class staleness honesty — the disclaimer is inside the
column name.**

**Security page** (OK): `Portfolio Manager | % of portfolio | Recent activity | Shares | Value`,
with header stats *"Ownership count: 31," "Ownership rank: 5th,"* aggregate "% representation across
all portfolios," and a "Hold price reference." Sub-tabs: Ownership, Activity, Buys, Sells, Insider
Transactions.

**No score at all**, by policy. Help notes warn explicitly that percentages *"only apply to the
stock portions of portfolios — cash and bond holdings are not included,"* that a purchased security
*"may have had a significant run-up in price since the time of the transaction and no longer
represents good value,"* that sales are often forced redemptions, and that users should
*"independently evaluate a security and not blindly follow."* It retroactively adjusts historical
share counts for splits.

---

## 8. GuruFocus

**Unit of analysis: filer-first**, heavily tab-differentiated by data recency.

Tabs (OK): `Latest Picks | Real Time Picks | Top 10 Holdings | Sector Picks | International Picks |
Group Picks | Aggregated Portfolios | Consensus Picks | Guru Bargains | Hot Picks | Trends |
Options Holdings | European Shorting`

**Real-Time Picks** (OK) — the standout design idea. Trades by holders above the **5% Schedule
13D/G threshold**, which report in days rather than 45:

```
Picked by | Ticker | Company | Trade Date | Buy/Sell | Impact % | Price | Current Price | Change % | Comment | Current Shares
```

**This is the cleanest structural answer to 13F staleness anyone has: use the faster regulatory
channel where one exists.**

**Guru portfolio page** (OK) — header shows *"Last update 2026-08-15, 29 Stocks (1 new), Value
$299.25 Bil, Turnover 6%."* Sub-tabs `Summary | Stock Picks | Current Portfolio | News | Related
Guru Trades` with secondary filters `Portfolio | Top Holdings | Valuations | Undervalued | Low PE |
Top Yield | Top Growth | High Quality | High Conviction | 52w Lows | Insider Buys | ETF | Preferred |
International`. Holdings table:

```
Ticker | Company | Shares | Value ($1000) | Shares Change % | Weighting % | Trade Impact % | % of Shares outstanding | 3M Change % | YTD Change % | Market Cap $Mil | Industry
```

**Score Board** (OK) — unusually transparent: a filer-level ranking on **raw multi-horizon returns**
rather than a composite:

```
Guru Name | Type | Since latest 13F-filing | Since 6m ago | Since 12m ago | 1y | 5y | 10y | Since Inception | Period Tracked | Fund Tracked | Value ($Mil) | # stocks
```

The **"GF Score"** (0–100, five pillars: financial strength, profitability, growth, GF Value,
momentum) is a *company-quality* score, **not** a guru score; weighting is proprietary. GuruFocus
tracks 144 Gurus at Premium, 18,000+ portfolios at Premium Plus, and repeats prominently: *"Guru
trades should be used as an idea-generation and research tool, not as automatic buy or sell
signals."*

---

## 9. HedgeFollow

**Unit of analysis: dual and symmetric** (`/funds/` and `/stocks/`).

**Fund table header** (OK): `Hedge Fund | Portfolio Manager | Performance | 26Q2 AUM (13F) |
# of Holdings | Performance Rank | Allocation` — HedgeFollow computes its own **Performance Rank**
per fund.

Filer tabs (OK): `Top Holdings | Largest Trades | AI Insights (beta) | Portfolio Structure |
Performance History | AUM Profile` plus a holdings treemap.

**Holdings columns** (OK):

```
Stock | Company Name | % of Portfolio | Shares Owned | Value Owned | Latest Activity | Ownership Hist | Average Buy Price | Price History | Date
```

"Latest Activity" fuses percentage and share delta in one cell (`-17.62% (-827k)`); "Average Buy
Price" folds in appreciation-since-purchase (`$141.71 (+62.6%)`).

**Stock page** (OK) includes sections "Shareholders of NOW," "Largest Shareholders," "Largest
Buys/Sells since Q2 2026," and **"Option Holders of NOW"** — 13F option exposure, which most peers
discard. Columns across its tables add `Calls/Puts`, `Net Ownership`, and a **Fund Rating**. Extra
charts: "Buys vs. Sells," "Ratings of Buyers vs. Sellers," money-flow, "frequently co-purchased
stocks."

**Consensus page** (OK): `Stock | Company | Total # of Holders | # of Medium Stakes | # of Large
Stakes | Value Owned | Sector | Price-to-52W-Range`, with filters `All Hedge Funds | Outperforming
Funds (3+ Stars) | Strongly Outperforming Funds (4+ Stars) | All Institutional Investors` — a
quality overlay applied to the consensus.

---

## 10. 13f.info — the deliberate minimum

**Unit of analysis: filer → filing → line item.**

**Manager page** (OK): `QUARTER | HOLDINGS | VALUE ($000) | TOP HOLDINGS | FORM TYPE | DATE FILED |
FILING ID`, explicitly separating `13F-HR` from `NEW HOLDINGS` and `RESTATEMENT`.

**Filing page** (OK — I fetched this myself): `SYM | ISSUER NAME | CL | CUSIP | VALUE ($000) | % |
SHARES | PRINCIPAL | OPTION TYPE` — the unmodified SEC Form 13F information-table schema, plus a
quarter-comparison dropdown and CSV export.

No market price join, no chart, no score, no sector tag, no delta highlighting. **It has the most
explicit filing-date-vs-quarter-end presentation of any product surveyed.**

**Stockcircle** (OK, adjacent baseline): merges 13F and Form 4 into one chronological feed —
`Stock | Investor | Guru Price | Current Price (+%) | Date | Action details | Holdings info`. No
scoring; quarter-navigable.

**Insider Monkey** (OK): article-centric rather than a live table; ranks purely by **count of hedge
fund holders** from a database of 912 funds. Its fund-return leaderboard (`Rank | Hedge Fund |
# of Stocks | Total Value | Quarterly Return | Return (1-Year)`) explicitly assumes *"these funds
haven't made any changes to their positions during the quarter"* and drops derivatives, cash, bonds,
private positions and sub-$1bn names; funds with <5 qualifying positions are dropped entirely.

---

## 11. OpenInsider — verified firsthand by me via curl

The research stream couldn't reach the site; I pulled it directly.

**Exact `<th>` sequence on `/latest-cluster-buys`** (OK):

```
X | Filing Date | Trade Date | Ticker | Company Name | Industry | Ins | Trade Type | Price | Qty | Owned | ΔOwn | Value | 1d | 1w | 1m | 6m
```

Note the cluster view **swaps the person column for `Ins`, a count of insiders** — it aggregates
from the person to the company/event level.

**Screener form fields** (OK) — this answers the cluster-definition question directly:

| Group | Params |
|---|---|
| Identity | `s` (ticker), `o` (owner) |
| Dates | `fd`/`fdr`, `td`/`tdr`, `daysago`, `fdlyl`/`fdlyh` |
| Size | `pl`/`ph` (price), `vl`/`vh` (value), `ocl`/`och` (ΔOwn) |
| Industry | `sic1`,`sic2`,`sic3`, `sicl`/`sich` |
| **Role** | `isofficer, iscob, isceo, ispres, iscoo, iscfo, isgc, isvp, isdirector, istenpercent, isother` |
| **Clustering** | **`grp`** (group-by) + **`nil`/`nih`** (# insiders) + `nfl`/`nfh` (# filings) + `nol`/`noh` (# officers) |
| Group aggregates | `v2l`/`v2h`, `oc2l`/`oc2h` |
| Other | `excludeDerivRelated`, `tmult`, `sortcol`, `cnt`, `page` |

**So a "cluster buy" on OpenInsider is not a fixed constant** — it's a group-by with a tunable
minimum-insider-count plus group-level value/ΔOwn thresholds. The canned page just picks defaults.

**Canned screens** (OK): Latest Cluster Buys · Latest Penny Stock Buys · Latest Insider Trading (all
filings) · Latest Insider Purchases · …$25k+ · Latest Officer Purchases $25k+ · **Latest CEO/CFO
Purchases $25k+** · Latest Insider Sales · …$100k+ · Latest Officer Sales $100k+ · Latest CEO/CFO
Sales $100k+ · Top Officer Purchases Today/Week/Month · Top Insider Purchases Today/Week/Month ·
Top Insider Sales Today/Week/Month · Charts.

**What the user does:** the `1d/1w/1m/6m` forward-return columns are the point. They convert a
static disclosure into a testable one — *was this insider right?*

---

## 12. Other insider / congress trackers

**SecForm4** (OK): `Transaction Date | Reported DateTime | Company | Symbol | Insider / Relationship |
Shares Traded | Average Price | Total Amount | Shares Owned | Filing`, with `(Indirect/Direct)`
ownership tags and `(A)` amendment flags. **Free tier is delayed 6 months** ("This module requires
Insider Pro or above. Data are delayed by 6 months"); real-time is paywalled. Also runs a "Top 10
Searched" leaderboard, an "Insiders Buy/Sell Ratio" sentiment tool, and separate real-time 13D/13G
tracking.

**InsiderScreener** (OK) — **the most transparent scored product found anywhere.** Feed columns:
`Transaction date | Notification date | Transaction type | Insider name + title | Price | Quantity |
Value`. Screens: *Largest buys, Largest sells, Top insider buys, Top insider sells, Top cluster buys
("Companies with the most insiders buying"), Top cluster sells, Buying the dip ("Insiders buying
after 5%+ price drops"), 52-week high/low + Insider activity, Top executive buys/sells, Top insider
buys from high-rated insiders, Recent trades from top insiders, Insider activity signals ("Unusual
activity detected via statistical analysis against historical baselines")*.

Its **0–5 insider rating** is disclosed and reproducible:
- *Holding-period performance*: fixed **120-day** window, with per-insider **Success Rate, Average
  Return, Transaction count** (e.g. "100% success, 53% avg return, 16 trades → rating 4.4").
- *Realised performance*: actual return from closed positions, with **Average return, Average
  holding period, Success Rate, Transactions**.

**"This insider has personally been right X% of the time" is a design idea nobody else exposes.**

**Quiver Quantitative** (OK): Congress page `Stock | Transaction | Politician | Filed | Traded` plus
a Description field showing **estimated excess return of the stock since the transaction**.
Leaderboards: *Most Active Congressional Traders, Highest Net Worth Congressmembers, Politician
Stock Portfolio Leaderboard*. Ships a Congress Long-Short backtest with CAGR/return/drawdown/Sharpe.
Also carries Institutional Holdings (13F), Whale Moves (13D), Insider Trading, Gov Contracts,
Lobbying, Executive Comp, Patents, App Store Ratings, Google Trends. Its **"Smart Score"/Bull-Bear
methodology is undisclosed**.

**Unusual Whales** (OK): politics rows show `Reporter (name, chamber, party, district) | Symbol |
Trade Date | Transaction type + amount BAND | Filing Date`. Amounts are **bands**
(`$1,001–$15,000`) because the STOCK Act mandates ranges, not exact values. Live example: trade
**2026-08-12, filed 2026-09-02** — a 21-day lag vs Form 4's 2 business days. Ships two real ETFs
(**NANC**, **KRUZ**). Its 2026 Congressional Trading Report: *"only 32.2% of Congress beat SPY."*
The `/insiders` page: `Ticker | Last | Buy Amount | Buy % | Avg Buy Price | Sell Amount | Sell % |
Avg Sell Price | Sector | Marketcap`, with filters for S&P-500-only, market-cap tier, 1–12 month
lookback, and sector.

**SEC EDGAR itself** (OK): `Form type ("4") | Description | Filing Date | Documents link`. No ticker,
insider, value or title columns — you must open each XML. This is precisely the gap every product
above fills.

**Washington Service / 2iQ**: professional vendors, gated; not independently verified. 2iQ's own
cluster-buying methodology post confirms the same open-market / P-code / multi-insider definition
used elsewhere.

---

## 13. Retail baseline

### Nasdaq — verified firsthand by me via `api.nasdaq.com` (OK)

**Ownership Summary** (top of page):

| Field | Value |
|---|---|
| Institutional Ownership | 76.58% |
| Total Shares Outstanding (millions) | 14,594 |
| Total Value of Holdings (millions) | $3,575,860 |

**Active Positions** — tabs `Total / New / Increased / Decreased / Activity / Sold Out`:

| Active Positions | Holders | Shares |
|---|---|---|
| Increased Positions | 2,868 | 333,674,739 |
| Decreased Positions | 3,204 | 240,416,646 |
| Held Positions | 409 | 10,601,518,691 |
| **Total Institutional Shares** | **6,481** | **11,175,610,076** |
| New Positions | 190 | 43,735,321 |
| Sold Out Positions | 104 | 18,657,913 |

**Holder table**: `Owner Name | Date | Shares Held | Change (Shares) | Change (%) |
Value (In 1,000s)`. **Default sort: shares held, descending.**

**A real defect visible in the live data** (OK): Vanguard's row is dated `12/31/2025` while
BlackRock's is `6/30/2026`. The table **mixes as-of dates across holders and still sorts by size**,
so a two-quarter-stale filer outranks a current one with no visual distinction. Do not copy this.

**Nasdaq insider** (OK): summary tables `Number of Insider Trades` and `Number of Insider Shares
Traded` (Open Market Buys / Sells / Total / Net, **3-month vs 12-month**), then `Insider | Relation |
Last Date | Transaction | Owner Type | Shares Traded | Price | Shares Held`, with All Trades / Buys /
Sells tabs.

### Yahoo Finance (OK)

Current sub-tabs: **Major Holders · Insider Roster · Insider Transactions · Insider Sentiment**. The
separate *Top Institutional Holders* / *Top Mutual Fund Holders* tabs appear to have been
**retired**.

Major Holders summary block (verbatim): `% of Shares Held by All Insider | % of Shares Held by
Institutions | % of Float Held by Institutions | Number of Institutions Holding Shares`.

Two derived summary tables: **"Insider Purchases Last 6 Months"** and **"Net Institutional Purchases
Prior Quarter to Latest Quarter"**, rows `Shares / Transactions / Net Shares Purchased (Sold)`.

### Stockanalysis.com (OK)

**No dedicated holders page on the free tier** (nav is Overview/Financials/Forecast/Statistics/
Metrics/Dividends/History/Profile/Chart). Ownership lives inside *Statistics*: `Owned by Insiders
(%) | Owned by Institutions (%) | Float | Shares Outstanding | Shares Change YoY/QoQ`. No
transaction-level insider table.

### Koyfin (OK) — yes, it exists

**"Ownership Snapshot"** (Snapshots → **OWN**), three tabs:

- *Insider Ownership* — top 20: `Insider Name & Title | Market Value | % of Market Cap | Shares Held |
  Position Date` (sourced from S&P Capital IQ, global coverage)
- *Insider Transactions* — 2-year history: `Insider Name & Title | Transaction Type (8 buckets) |
  Category (Acquisition/Disposal) | Transaction Date | Shares Transacted | % of Shares Transacted |
  Transaction Value | Shares Owned | Form Type`
- *Institutional Ownership* — exists; columns undocumented (UNVERIFIED)

Crucially, **Koyfin exposes ownership as screener and watchlist columns and as a graphable metric**,
not just a static tab. That's institutional-style treatment at a retail price point.

### Others

- **Simply Wall St** (OK) — richest free ownership page. **Ownership Breakdown** by *Private
  Companies / Individual Insiders / **State or Government** / **General Public** / Institutions*,
  each with Shares + Ownership %. Plus Recent Insider Transactions (`Date | Buy/Sell | Value | Name |
  Entity type | Shares | Max Price`) with a narrative line, and Top Shareholders (`Ownership % |
  Name | Shares | Current Value | Change % | Portfolio %`). Sources disclosed as SEC Form 4 / 13D.
  The only mainstream retail product giving **government and retail their own first-class slices**.
- **Finviz** (OK) — `Ticker | Owner | Relationship | Date | Transaction | Cost | #Shares | Value ($) |
  #Shares Total | SEC Form 4`. Canned tabs: Latest Insider Trading, Top Insider Trading Recent Week,
  Top 10% Owner Trading Recent Week. URL filters: `or=10` (10% owner), `tv=` (min value), `tc=`
  (transaction code).
- **Morningstar** (SEC) — `Shares Held | % Total Shares Held | Share Change | % Change from Prior
  Portfolio | % Total Assets`, split Major / Concentrated (>1% of fund AUM) / Mutual-Fund tabs.
- **TIKR** (SEC) — Shareholders tab (investor name, % change in shares held, % shares outstanding
  held) + Insider Transactions tab (role, buy/sell, size-sortable).
- **TradingView** (OK) — **no ownership tab at all.** A deliberate scope decision.

---

# PART (b) — Common design patterns

## What every product converges on

1. **Two units of analysis, never one.** Bloomberg splits across functions (HDS vs FLNG); FactSet
   across tabs (Company vs Holder); Refinitiv across siblings (Firm vs Fund Ownership Summary);
   WhaleWisdom and HedgeFollow across symmetric URL trees. Picking one (Dataroma filer-first, Fintel
   security-first) is a positioning choice, not a shortcut.

2. **Level and change sit in adjacent columns; level leads.** `Position | Pos Change` (FactSet) ·
   `Position (M) | Position Change (M)` (Refinitiv) · `Shares Held | Change (Shares) | Change (%)`
   (Nasdaq) · `Amount Held | Latest Chg` (Bloomberg). **No product makes you toggle between them.**

3. **The buyers/sellers *count* is a headline statistic, separate from share sums.** Bloomberg
   `# of Buyers/Sellers 1,017/958` · Nasdaq's Increased/Decreased/Held/New/Sold-Out × (Holders,
   Shares) · FactSet's Ownership Activity panel · Refinitiv's Recent Activity BUYS/SELLS. **Counting
   holders is treated as different information from summing shares — and the evidence in Part (c)
   says that instinct is right.**

4. **Two denominators, always both.** % of shares outstanding *and* % of float. Bloomberg shows
   `66.3%/68.8%` side by side; its activism model uses float>70% as a distinct governance factor;
   FactSet carries "Institutional Ownership as % of Float" as its own field. Conflating them is the
   most common retail error.

5. **Conviction ≠ size, and good products let you switch.** WhaleWisdom's *BY % PORTFOLIO / BY
   LARGEST $ CHANGE*; FactSet's `% Port` beside `% O/S`. "BlackRock owns 8% of AAPL" and "AAPL is
   22% of Berkshire's book" are different claims requiring different denominators.

6. **Concentration is a named panel, not a computed afterthought.** Refinitiv ships Top 10/20/50/100;
   FactSet has `Top 10 Inst. Holders (%)` in its statistics block; WhaleWisdom auto-narrates *"top 10
   holdings concentration of 88.47%."*

7. **Institutional products classify holders; retail products don't.** FactSet by investment style,
   Refinitiv by style *and* type, CapIQ by organizational type (bank/pension/SWF/family office).
   Retail shows a flat name list. **This is the largest capability gap between tiers — and Part (c)
   says it is also the part that carries the signal.**

8. **Freshness is disclosed structurally, or not at all.** Best: 13f.info's `DATE FILED` beside
   `QUARTER` with form-type distinction; Dataroma's `Reported Price*` footnote inside the column
   name; congress trackers' mandatory `Traded | Filed` pairing. Worst: Nasdaq silently mixing as-of
   dates in a size-sorted table.

9. **Retail is estimated as a residual, never measured.** Simply Wall St's "General Public" and
   CapIQ's "Public and Other" are both *shares outstanding − institutions − insiders*. Present it as
   a plug or not at all.

10. **Forward-return columns are the specialist/retail dividing line.** OpenInsider's `1d/1w/1m/6m`,
    Quiver's excess-return-since-transaction, InsiderScreener's per-insider success rate. Raw
    filings answer *who traded*; forward returns answer *was the trader informed*. No retail Holders
    tab attempts it.

11. **Scoring transparency is inversely correlated with prominence.** Fully disclosed:
    InsiderScreener (120-day window, per insider). Partially: Fintel, WhaleWisdom. Undisclosed black
    boxes: Quiver's Smart Score, HedgeFollow's Fund Rating, WhaleWisdom's Overcrowding Risk. Zero
    score by policy: Dataroma, 13f.info, and every retail Holders tab.

## What they deliberately DON'T do

- **No terminal ships an ownership-based return forecast.** Bloomberg's only ownership score predicts
  *activist-campaign likelihood* (4.5%→20%). Refinitiv's StarMine Smart Holdings predicts *future
  institutional ownership*. CapIQ has none. Fintel's Fund Sentiment describes *recent accumulation* —
  descriptive momentum, not a forecast.
- **They don't collapse the two denominators.** Outstanding and float answer different questions
  (liquidity vs governance).
- **They don't claim the register is the beneficial owner.** Bloomberg partnered with CMi2i rather
  than pretend otherwise.
- **Nobody scores insider sells.** No product ships a bearish insider indicator — which, per Part
  (c), is exactly right.
- **They don't surface what 13F structurally omits** — shorts, most derivatives, non-US listings,
  debt, sub-threshold positions, confidential holdings, restatements. HedgeFollow's option-holder
  table is the rare partial exception.
- **Retail tabs don't score at all.** Plausibly liability avoidance: a score implies a
  recommendation.
- **TradingView omits ownership entirely.** A legitimate answer.
- **Dataroma and 13f.info refuse to score on principle**, and Dataroma actively warns users off naive
  copying.
- **BI ACT excludes** environmental/social campaigns by small shareholders from its activism dataset
  — deliberately narrowed to "economic activists."
- **Bloomberg's IR dashboard excludes** event management, meeting scheduling and information
  distribution — scoped to intelligence, leaving logistics to other vendors.

---

# PART (c) — Evidence table

## C1. Insider trading (Form 4)

| Signal | Source | Sample | Effect | t | Horizon | Verified |
|---|---|---|---|---|---|---|
| Any buy vs sell, size+B/M adj | Lakonishok & Lee 2001 *RFS* | 1975–95 | **4.8%/yr** spread (raw 7.7%) | — | 12m | OK |
| Same, Fama-MacBeth w/ controls | L&L Table 8 | " | all **1.52%/yr**; small 3.28%; **large −0.60%** | 1.45 **ns** / 3.32 / −0.65 | 12m | OK |
| **Cluster buy** (≥3 insiders, NPR≥0.95, top-quartile $) | L&L `DPL` dummy | " | **+4.82%/yr** all; **+7.27%/yr small**; large +1.32% | 2.60 / **3.10** / 0.56 | 12m | OK |
| Cluster **sell** | L&L `DSL` | " | **+2.81%/yr — wrong sign** | 1.40 ns | 12m | OK |
| **10% beneficial owners** | L&L Table 6 | " | Top NPR decile 13.5% vs bottom **19.7% — inverted** | ns | 12m | OK |
| All purchases, value-wtd | Jeng/Metrick/Zeckhauser 2003 *REStat* | 1975–96 | CAPM 0.67%/mo; **4-factor 0.50%/mo ≈ 6%/yr**; DGTW 0.53% | ≈3.8 | 6m | OK |
| All sales, VW | JMZ | " | 4F **−0.06%/mo** | −0.6 **ns** | 6m | OK |
| **Timing decomposition** | JMZ Tables 4–5 | " | 4F α: days 0–5 **2.52%/mo**; 5–21 **1.14%/mo**; **21–6mo 0.29% ns** | 7.9 / 4.4 / **ns** | — | OK |
| **Insider rank** | JMZ Table 9 | " | top exec 0.65%/mo · officer 0.43% · **director 0.51%** | ≈2.0 / 2.2 / 3.4 | 6m | OK |
| Large insider **sales** | JMZ Table 7 | " | **+1.44%/mo — wrong sign** | sig | 0–5d | OK |
| Firm size split | JMZ Table 8 | " | small 0.37%/mo · med 0.43% · large 0.12%; **difference not significant** | 2.85/1.54/0.57 | 6m | OK |
| Opportunistic − routine, VW | Cohen/Malloy/Pomorski 2012 *JF* | 1989–2007 | **5-factor 0.82%/mo**; **Carhart 0.62%/mo** | 2.15 / **1.71** | 1m | OK |
| Same, equal-weighted | CMP | " | 5F **1.80%/mo**; Carhart 1.64% | **6.07** | 1m | OK |
| Opportunistic **buys alone**, VW | CMP | " | Carhart **0.52%/mo** | **1.73** | 1m | OK |
| Opportunistic **sells alone**, VW | CMP | " | Carhart **−0.09%/mo** | −0.50 **ns** | 1m | OK |
| **Routine trades**, VW | CMP | " | 5F **−0.20%/mo** | −0.57 **ns** | 1m | OK |
| CMP panel regression | CMP Table V | " | opp buys **+90bp** vs all trades; routine buys +14bp; opp buy−sell **158bp/mo** | 4.64 | 1m | OK |
| Opportunism (pre-earnings P&L) Q5 L/S | Ali & Hirshleifer 2017 *JFE* | 1989–2014 | EW 4F **1.59%/mo**; **VW 1.12%/mo** | 6.88 / **3.38** | 1m | OK |
| Generic Form 4 baseline | Ali & Hirshleifer Table 2 | " | EW 0.88%/mo; **VW 0.50%/mo** | 9.69 / 4.20 | 1m | OK |
| Generic sells, short leg | Ali & Hirshleifer | " | EW **−0.03%/mo** | −0.64 **ns** | 1m | OK |
| Opportunistic sells (Q5) | Ali & Hirshleifer | " | EW **−0.34%/mo** | −2.62 | 1m | OK |
| **Composite, modern US** | Heckmann/Jacobs/Schwarz 2023 | **2003–2021** | **EW 1.159%/mo · VW 0.142%/mo**; short leg EW 0.053% | **6.37** / **0.68 ns** / 0.95 ns | 1m | OK |
| Same, global | Heckmann et al. | 34 countries, 3.7M txns | **24/34 countries sig EW; only 8/34 VW**; 6-mo holding halves EW alpha | — | 1m→6m | OK |
| Unconditional baseline | Heckmann et al. | " | EW 0.895%/mo; **VW 0.021% ns** — composite adds only ~26bp EW | — | 1m | OK |
| Post-SOX filing reaction | Brochet 2010 *TAR* | pre/post Aug 2002 | purchase CAR **0.59% → 1.89%**; volume +1.03% → **+12.03%** | "all significant" | 3d | SEC |
| Cluster vs solitary | Alldredge & Blank 2019 *JFR* | 1986–2014 | **2.1%/mo vs 1.2%/mo** (+0.9%) | — | 1m | SEC |
| Cluster vs non-cluster | Kang/Kim/Wang 2018 | 1986–2016 | **3.8% vs 2.0%** over 21d; 2.5% gap at 90d | — | 21d/90d | SEC |
| CFO vs CEO purchases | Wang/Shin/Francis 2012 *JFQA* | 1992–2002 | CFO beats CEO by **~5%** | "significant" | 12m | ABS |
| Independent directors | Ravina & Sapienza 2010 *RFS* | — | positive; gap vs officers *"relatively small at most horizons"* | — | — | ABS |
| Position-size capacity | Oenschläger & Möllenhoff 2025 *FRL* | post-SOX | returns *"vanish and even become negative"* when $/signal is capped | — | short | ABS |
| Large-cap only (FTSE-350) | — | 2005–15 | t0→t+20 **−0.074% ns**; roundtrip cost **2.9%** → *"not exploitable"* | −4.85 | 20d | ABS |
| ~30% of insiders informed | Blonien/Crane/Crotty 2023 | US universe | **10% of purchases, 4% of sales** informed | — | — | SEC |
| Norway (lax enforcement) | Eckbo & Smith 1998 *JF* | 1985–92, 18k trades | *"zero or negative abnormal performance by insiders"* | — | — | ABS |
| Cost to counterparties | JMZ | 1975–96 | 0.21 bp over 6 months = **21 cents per $10,000** | — | 6m | OK |

### Three corrections to common belief

1. **There is no insider-rank hierarchy.** JMZ (OK), CMP (OK) and Ravina-Sapienza (ABS) all find
   none; the lone dissent favours **CFOs over CEOs**, not "CEO is best." A "CEO buy = strongest"
   badge invents a distinction the data doesn't support.
2. **CMP's "82bp" is the 5-factor number.** On standard Carhart the same VW long-short is **62bp at
   t=1.71** — marginal. Its classification also requires a trade in *each* of the three preceding
   years, dropping the sample to **~⅓ of all Form 4 transactions**; of what remains ~45% is
   opportunistic. And Ali & Hirshleifer (OK) report that with both measures included, CMP's
   nonroutineness index gives **+3bp, insignificant**, while theirs gives +51bp.
3. **McLean & Pontiff contains no insider predictor.** I grepped their 97-predictor appendix (OK).
   You cannot cite it for insider decay. What *does* apply cuts against: their conditional finding is
   that decay is **greater** for predictors concentrated in low-liquidity, high-idiosyncratic-risk
   stocks — this signal's exact profile. (Headline: **26% lower OOS, 58% lower post-publication.**)

**UNVERIFIED — do not use:** the widely-repeated "2008–2024 replication shows CMP alpha falling from
1.2–1.6%/mo to 0.3–0.4%/mo." Vendor blogs, no traceable citation.

**Also note (SEC):** CMP did *not* test earnings-announcement prediction — footnote 18 excludes
pre-scheduled events. Their information set is headline news, analyst revisions, management
forecasts, SEOs and M&A.

## C2. 13F signals

| Signal | Source | Sample | Effect | t | Verified |
|---|---|---|---|---|---|
| **Best Ideas** (top-1 conviction), MFs | Cohen/Polk/Silli 2008 | 1991–2005 | **39–127 bp/mo**; primary spec **6F 39bp**, 4F 29bp; best measure 115bp | 3.08 / 2.24 / **5.31** | OK |
| Best Ideas **by liquidity** | Same, Table 9 | " | **less-liquid +41bp · more-liquid −18bp** | 2.64 | OK |
| Best Ideas, extended | Antón/Cohen/Polk 2021 | **1983–2018** | **2.8–4.5%/yr**; 6F 37/35bp; **DGTW 6F 26 & 29bp** | 3.34/3.29; 2.91/3.29 | OK |
| Best Ideas, **hedge funds** | ACP 2021 Table V | 1,662 pure-play HFs | 6F **28–32bp/mo**; DGTW **20–25bp** | 2.78–3.17 / 2.46–2.92 | OK |
| **"All Ideas" control** | ACP 2021 | " | **HFs 1 bp/mo · MFs 6 bp/mo — insignificant** | ns | OK |
| Best Ideas by manager size | ACP 2021 | " | small HFs (≤$100m) **58bp/mo**; **giant HFs negative**; spread **>1.26%/mo** | 2.40 / 2.58 | OK |
| 13F best-ideas w/ realistic lag | Farouk & Jivraj | Q1 2004–Q2 2019 | **47-day lag**, Sharpe 0.69, **+3.80%/yr**; top-50 conviction α **0.32%/mo**, Sharpe 0.75 | — | OK |
| Same, **wrong manager set** | Farouk & Jivraj | " | **α −0.003%, insignificant** for non-fundamental-equity managers | ns | OK |
| **Copycat funds vs targets** | Verbeek & Wang 2013 *JBF* | 1985–2008, 60-day lag | net-of-everything **+2.0 bp/mo** | 1.67 (10% only) | OK |
| **Copycat funds vs the index** | Same, Table 2.2 | " | copycat net **10.52%/yr** vs CRSP VW **11.35%/yr** → **−0.83 pp/yr** | — | OK |
| **Value of fresher holdings** | Same, Table 2.4 | " | quarterly vs semi-annual: +0.11pp gross, **net 0.01 pp/yr** | **0.13 ns** | OK |
| Earlier copycat study | Frank/Poterba/Shackelford/Shoven 2004 | 1992–99 | 6-mo diffs ≤42bp, mixed sign; monthly never >20bp | mixed | OK |
| **Aggregate HF 13F holdings** | Griffin & Xu 2009 *RFS* | — | *"not beneficial in predicting the cross-section"*; 1.32%/yr VW is 1999–2000 tech, **insignificant EW** | ns EW | OK |
| **Confidential 13F holdings** | Agarwal/Jiang/Tang/Yang 2013 *JF* | 1999–2007 | Carhart-4 diff **6.48%/yr @2m**, 5.17% @12m; DGTW 5.26–7.51% | 3.02 / 3.11 / 6.78 | OK |
| Size of what's hidden | Same | " | **27.3% mean / 13.4% median** of portfolio value; **93% arrive >45 days late** | — | OK |
| Confidential, independent | Aragon/Hertzel/Shi 2013 *JFQA* | — | positive significant abnormal returns in the confidential period | — | ABS |
| **13F restatements** | Cao/Da/Jiang/Yang, *Mgmt Sci* | 1999–2018 | **3.20% of filings**, 3× as many stocks as confidential; restated holdings **9.13%/yr**; 18.5 bp/day pre-restatement | — | OK |
| **Breadth of ownership CHANGE** | Chen/Hong/Stein 2002 *JFE* | 1979–98, mutual funds | P10−P1 raw **6.38% (4q)**; char-adj **4.95%**; 1q raw 2.02% | **4.08 / 3.93 / 3.96** | OK |
| Breadth **asymmetry** | Same | " | char-adj decile 1 **−2.62%** vs decile 10 **+2.32%** — ~half is the **short leg** | — | OK |
| Breadth **by holder type** | Choi/Jin/Yan (NBER 16591) | Shanghai, all investors | **retail breadth ↑ → −23%/yr**; **institutional breadth ↑ → +8%/yr** | — | OK |
| **Institutional ownership LEVEL** | Gompers & Metrick 2001 *QJE* | 1980–96 | IO_t **0.0116/qtr**; IO_{t−1} **0.0135** | 2.13 / 2.49 | OK |
| **Same — ΔIO** | Same, Table 5 | " | **ΔIO 0.0385 — INSIGNIFICANT**; high-inflow qtrs 0.0230 (**t=4.65**) vs low-inflow 0.0006 (**t=0.10**) | **1.79 ns** | OK |
| Institutional herding | Nofsinger & Sias 1999 *JF* | — | strong **contemporaneous** correlation; **no reversal** in the following year | — | ABS |
| Short-term institutions' Δ | Yan & Zhang 2009 *RFS* | 1980–2003 | drives the entire G&M result; long-term institutions forecast nothing | sig | ABS |
| …but only hedge funds | Caglayan/Celiker/Tepe *FAJ* 2024 | — | relation exists **only among hedge funds**, not other short-horizon institutions | — | ABS |
| Intra-quarter trades (invisible) | Puckett & Yan 2011 *JF* | proprietary tape | interim trading worth **20–26 bp/yr** after costs | sig | ABS |
| Two-week value-add | *JF* 2024 (10.1111/jofi.13331) | transaction-level | high-turnover funds add value in the **first two weeks**, **>80% on FOMC/earnings days** | — | ABS |
| **Unobserved performance** | *JF* 2024 (10.1111/jofi.13368) | HF firms | high-UP minus low-UP **6.36%/yr** risk-adjusted | sig | ABS |
| 13F consensus/crowding | arXiv 2209.08825 | 2013–2021, ~5,000 stocks | trading **with** imbalances → **negative** excess PnL; **contrarian Sharpe >1 at 21 days** | p<0.05 | OK |
| 13F replication of HF returns | GSAM | — | **94.1% correlation**, 2.4% TE after vol-scaling | — | SEC |

### 13F clone ETFs — computed directly from adjusted-close price data (OK)

| Fund | Window | Fund CAGR | SPY CAGR | Diff | Outcome |
|---|---|---|---|---|---|
| **GURU** | 2012-06 → 2026-09 (14.25y) | 12.38% | 15.35% | **−2.97 pp/yr** | Alive, $62m AUM. Underperformed in **every** sub-period; −6.2pp/yr since 2021 |
| **ALFA** | 2012-05 → 2022-09 (10.25y) | 9.32% | 13.50% | **−4.18 pp/yr** | **Liquidated 31 Aug 2022** |
| **IBLN** | 2014-08 → 2017-10 (3.25y) | 8.53% NAV | 11.56% | **−3.03 pp/yr** | **Closed Apr 2018.** Index itself returned 9.38%/yr — not tracking error |
| **GVIP** | 2016-11 → 2026-09 (9.81y) | 16.33% | 15.63% | **+0.70 pp/yr** | β=**1.11**, vol 22.0% vs 18.1%, max DD −37.1% vs −33.7% → **beta-adj α ≈ −0.43%/yr**. 2022: −31.9% vs −18.2% |

Plus GURX (2015), GURI and ACTX (2017), all liquidated. **Five products: four dead or losing
3–4pp/yr; the survivor's entire raw edge is beta.**

For calibration, the CFA Institute's independent summary (OK): since 2012 GURU −1.3%/yr and
ALFA −1.6%/yr; since 2016 GVIP +2.6%, ALFA +2.0%, GURU −0.5%. March 2020 drawdowns: S&P −19.6%,
GVIP −21.4%, ALFA −25.1%. Their verdict: *"fair weather investments that don't perform over an
entire cycle."*

### Structural limits of 13F (OK — SEC's own FAQ and Release 34-89290)

- **45-day lag**; >86% of filers use nearly the whole window → data up to **~135 days old**.
- **Long-only.** Short positions must not be reported and must not be netted against longs. Most
  derivatives, non-US listings, debt and open-end fund shares excluded.
- **De minimis**: positions <10,000 shares *and* <$200,000 may be omitted.
- **Filer proliferation**: 300 filers in 1975 → **5,089 by end-2018** (plus 1,570 13F-NT); the
  **$100m threshold is statutory from 1975 and never inflation-indexed** (the 2020 proposal to raise
  it to $3.5bn was not adopted). Coverage went from ~40% of the US equity market in 1981 to ~83%.
- **Extreme concentration**: of $25.2tn reported at 2018-12-31, **37 filers = 56.7%**; 550 filers
  (≥$3.5bn) = 90.8%; the smallest 4,539 filers = 9.2%. A "consensus across filers" count is
  dominated by index complexes by construction.
- **No fund-level attribution** — filed at management-company level, which is why every serious paper
  hand-classifies "pure-play" managers.
- **Best Ideas has never been tested at the filing date in a published table.** Both drafts assert
  it survives; the 2008 draft says "in research not shown," the 2021 draft points to a figure formed
  at the *holding* date. **This is the single largest unverified claim underpinning any 13F feature.**

## C3. Ownership-conditioned signals and short interest — where I corrected myself

**RETRACTION.** An earlier draft of this report said "short interest is the strongest
ownership-adjacent signal." **That is not supported.** Chen & Zimmermann's Open Source Asset Pricing
project reproduced 319 characteristics (98% of clear predictors replicate with |t|>1.96; slope of
reproduced on original t-stats = 0.90, R²=83%) (OK). Their per-signal grading:

| OSAP signal | Source | Sample | Original LS | t | **Repl. quality** | **Predictability** |
|---|---|---|---|---|---|---|
| `ShortInterest` | Dechow et al. 2001 | 1976–93 | 0.35%/mo | — | **2_fair** | **2_likely** |
| `IO_ShortInterest` | Asquith/Pathak/Ritter | 1980–2002 | 0.98%/mo | — | 1_good | 2_likely |
| **`DelBreadth`** | Chen/Hong/Stein | 1979–98 | 0.673%/mo | **3.96** | **1_good** | **1_clear** |
| **`RIO_MB`** | Nagel 2005 | 1980–2003 | 1.07%/mo | **4.91** | **1_good** | **1_clear** |
| **`RIO_Volatility`** | Nagel 2005 | 1980–2003 | 1.07%/mo | **4.38** | **1_good** | **1_clear** |
| **`RIO_Turnover`** | Nagel 2005 | 1980–2003 | 0.92%/mo | 2.71 | 1_good | 1_clear |
| **`RIO_Disp`** | Nagel 2005 | 1980–2003 | 0.54%/mo | 2.47 | 1_good | 1_clear |
| `Recomm_ShortInterest` | Drake/Rees/Swanson 2011 | 1994–2006 | 1.11%/mo | 4.09 | 4_lack_data | 1_clear |

**Plain short interest sits in the weakest tier that still counts. The ownership-*conditioned*
versions — residual institutional ownership interacted with an overpricing characteristic, and
breadth change — are what replicate cleanly.** The popular claim that "short interest is one of the
few anomalies that survived" is **not supported by this grading**. Institutional ownership is a
**conditioning variable**, not a standalone predictor.

| Signal | Source | Sample | Effect | t | Verified |
|---|---|---|---|---|---|
| Low SI + high turnover (long leg) | Boehmer/Huszár/Jordan 2010 *JFE* | SEC 1988–2005 | abstract only: high-SI underperformance *"transient and of debatable economic significance"*; **low-SI positive returns often larger in absolute value** | — | **UNVERIFIED — no numbers** |
| **SI 99th pct × lowest IO third** | Asquith/Pathak/Ritter 2005 *JFE* | 1988–2002 | **EW −2.15%/mo**; VW −0.39% | **−4.17** / −0.46 **ns** | OK |
| Same, monotone in IO | Same | " | 99th×lowIO −2.15 · ×midIO −0.88 · ×highIO −0.83 | −4.17/−1.87/−2.29 | OK |
| Same, longer window | Same | 1980–2002 | constrained EW **−1.31%/mo** | −3.03 | OK |
| Capacity caveat | Same, verbatim | " | *"only about 21 stocks per month… For the other 5,479 stocks, short interest ratios have only a modest ability to predict"* | — | OK |
| **Days to cover** | Hong/Li/Ni/Scheinkman/Yan NBER 21166 | 1988–2012 | **EW 1.19%/mo, Sharpe 1.33**; VW 0.67%; $1→**$30** | **6.67** / 2.24 | OK |
| Short ratio, same test | Same | " | EW **0.71%/mo, Sharpe 0.51**; VW 0.29% **ns**; $1→$6 | 2.57 | OK |
| DTC minus SR advantage | Same | " | +0.37%/mo 1988–99; +0.53%/mo 2000–12; **shrinks to 0.29% (t=1.76) excluding smallest 10%** | — | OK |
| Raw SR post-2000 | CXO replication | 2000– | **became statistically insignificant**; DTC remained robust | — | SEC |
| **Borrow fee (CME)** | Drechsler & Drechsler NBER 20282 | 2004–2013 | gross **1.31%/mo**, **net of fees 0.78%/mo (≈9.4%/yr)**, FF4 α **1.44%/mo** | 5.00 / **3.01** / 6.87 | OK |
| **`SIRIO` = SI ÷ 13F shares** | Same, long-sample proxy | **1980–2013** | **CME 1.42%/mo; FF4 α 1.51%/mo**; top half of decile 10 **1.82%/mo, α 1.92%** | **5.83 / 8.97 / 9.93** | OK |
| Fee distribution | Same | " | **80% of stocks average <30 bp/yr**; expensive decile averages $1.22bn market cap | — | OK |
| Loan-fee variance (ShortRisk) | Engelberg/Reed/Ringgenberg 2018 *JF* | 2006–2011 | LS **1.08%/mo**; FF5 α 0.80%; **0.63–1.19%/mo within each SI quintile** | 1% level | OK |
| Fee distribution | Same | " | median **11.6 bp/yr**, mean 85 bp, 99th pct **1,479 bp** | — | OK |
| …contradicted | Muravyev/Pearson/Pollet 2022 *JF* | — | borrowing-fee risk **does NOT predict returns** after adjusting for fees | — | SEC |
| Loan fee vs 102 anomalies | Engelberg et al., *Mgmt Sci* | 2006–2019 | *"larger long-short returns than… any of the 102 anomalies… highest Sharpe ratio"* | — | SEC (digits UNVERIFIED) |
| **Utilization → squeeze** | Schultz 2024 *JFQA* | 2006–2019 | **util ≥90% → all-lender squeeze every ~11 days** vs once/40yrs below 25%; fee>10%/yr → every ~25 days | — | OK |
| Squeeze cost | Same | " | util ≥90%: **56–73 bp/mo, 1.04–1.34%/qtr**; mean excess return −1.293% → **>⅔ of gross short alpha lost** | — | OK |
| Aggregate SI (market timing) | Rapach/Ringgenberg/Zhou 2016 *JFE* | — | *"arguably the strongest known predictor of aggregate stock returns"* — **R² 12.89% IS / 13.24% OOS**; **+300bp/yr** utility | — | OK (abstract) |
| Historical anchors | via Schultz lit review | various | Desai et al. SI>10% **−1.13%/mo**; Jones & Lamont 1926–33 **−1.61%/mo**; Blocher et al. fee>95th pct **−1.5%/mo**; Cohen/Diether/Malloy rising shorting demand **+4.5%/yr net** | — | SEC |

**Markit regional evidence (SEC, vendor backtest, 2007–2012):** borrow-cost measures dominate raw
short interest in every region — Implied Loan Rate EUR 1-mo **+1.53%** (69.1% hit rate) vs Plain
Short Interest EUR **−0.41% (SIGN FLIP)**. A signal that inverts across regions in a 5.7-year window
is not one you ship unhedged.

**FINRA data timing** (OK): firms file by 6pm ET **two business days after** the settlement date;
public dissemination is **~8 business days after that** (an 11-calendar-day lag) — so within a
bi-monthly cycle the oldest figure is ~4 weeks stale. SEC **Form SHO** would improve this but
compliance was extended again in Dec 2025 to a **Jan 2, 2028 compliance date, first filings
Feb 14, 2028**.

## C4. Concentration and crowding — risk, and the sign is not what you'd guess

**RETRACTION.** An earlier draft said "crowded stocks underperform ~1.5%/yr." That is MSCI's factor
construct. The academic position-level evidence says the opposite.

| Signal | Source | Sample | Effect | t | Claim | Verified |
|---|---|---|---|---|---|---|
| **HF crowdedness (days-ADV)** | Brown/Howard/Lundblad 2022 *RFS* | 2004–2016, position-level | **Q5 (most crowded) 11.3%/yr vs Q1 8.5%/yr** — crowded stocks earn **MORE** | — | RISK PREMIUM | OK |
| Same, crisis | Same | Nov 2007–Feb 2009 | crowded stocks show significantly negative 2008 CARs that **largely reverse in 2009** | 1% | — | OK (magnitudes graphical only) |
| Crowding trend | Same | 2004→2016 | HF holdings/stock $160MM→>$500MM; holders 15→28; days-ADV 18→26 | — | — | OK |
| **Stock price fragility (G)** | Greenwood & Thesmar 2011 *JFE* | 1989–2007 | p25→p75 ⇒ **+0.5% daily volatility** (~¼ of mean vol) | **15.9 FM / 8.39 NW** | RISK | OK |
| **…but owner COUNT** | Same | " | **100→300 owners REDUCES daily vol by 0.1%**, holding ownership fixed | — | — | OK |
| **Top-10 inst. ownership** | Ben-David et al. 2021 *Mgmt Sci* | 1980–2016 | 1 SD ⇒ **+3.33% of an SD** of volatility (→**16%** late-sample); middle institutions only 0.7%; **bottom institutions NEGATIVE** | — | RISK | OK |
| **…conditional tail** | Same | " | worst 5% of quarters: DGTW returns **−9.17% of an SD**; **~zero in normal times** | — | — | OK |
| Concentration of ownership | Same | Dec 2016 | largest single institution held **6.3%** of all US equity; top 10 held **26.5%** | — | — | OK |
| **Dissent** | Sias/Turtle/Zykaj 2016 *Mgmt Sci* | — | HF portfolios *"remarkably independent"*; common HF demand shocks **positively** related to subsequent returns; **no inverse relation even in extreme stress** | — | contests it | ABS |
| **Fire-sale pressure** | Coval & Stafford 2007 *JFE* | 1980–2004 | event qtr **CAAR −10.1%**; reversal **+6.15%** over 12m; tighter cut −13.6% / +17.5% | **−6.94 / 2.01** | RISK | OK |
| Placebos | Same | " | unconstrained widespread selling: drop, **no reversal**; isolated distress: only −2.87% | −4.69 | — | OK |
| Front-running | Same | " | **+1.1 to +2.2%/mo**; **controlling for momentum halves it** | 1.78–3.45 | weak ALPHA | OK |
| **Flow-induced trading (FIT)** | Lou 2012 *RFS* | 1980–2006 | ranking qtr EW **+5.19%**; **years 2–3 EW −7.20%, VW −11.04%** — full reversal by yr 3 | 7.77 / −2.70 / −2.80 | RISK | OK |
| FIT calibration | Same | " | outflows liquidate near dollar-for-dollar (0.97); inflows reinvest only **62 cents/$** | 16.82 / 15.78 | — | OK |
| FIT vs momentum | Same | " | controlling for E[FIT] cuts past-return coefficient **25–42%**; **~50% in large caps**, where momentum becomes insignificant | 2.46 | — | OK |
| **Common ownership (FCAP)** | Antón & Polk 2014 *JF* | 1980–2008, 41.4M pairs | comovement coef **0.00395**; cross-stock reversal strategy **5-factor α 76 bp/mo**, **>71% from the long leg** | **13.43 / 4.96** | BOTH | OK |
| **Quant quake detector** | Khandani & Lo 2011 *JFM* | Aug 2007 | turnover-on-factor-decile regression R² normally ~0, **>10% in early Aug 2007, >5% for the rest of the year — never crossed before July 2007** | — | RISK | OK |
| Leverage amplification | Same | " | at **8:1**, book-to-market **−24% by close of Aug 7**; momentum **−31% over Aug 8–9** | — | — | OK |
| Mutual-fund flow shock | Edmans/Goldstein/Jiang 2012 *JF* | 1980–2007 | price pressure *"significant… persisting for over a year"*; interquartile valuation fall raises takeover probability by **5.7–7.6 pp** vs a 6.2% base rate | — | RISK | OK |
| **GS Hedge Fund VIP** | GS Trend Monitor | 2001–2025 | **60% of quarters**, avg **+52 bp/qtr**; **2022 −31.85% vs S&P −18.11%**; GVIP max DD **37.09%** | — | RISK | SEC (a rival vintage says 59% / 43bp) |
| Most-shorted basket | GS `GSCBMSAL` | Jan 2021 | **+98% over 3 months**, >+40% in January alone; GME +1,260% | — | RISK | SEC |
| Currency-fund crowding | Pojarliev & Levich 2011 *FAJ* | 2005–2008 | carry crowding −7%→**+31%** then collapse; adverse outcome **~3–6 months** after the extreme | **none** — authors call it *"anecdotal"* | RISK | OK |
| MSCI HF-crowding factor | MSCI | — | cross-validated **R² gain 2.25 bp — near bottom of 14 factors**; avg \|t\| **1.35**; factor vol 1.28%/yr; MSCI: *"risk identification capability rather than return prediction"* | 1.35 | RISK | OK |
| MSCI security crowding | MSCI | since 1995 | crowded stocks **−1.5%/yr** globally; **US reversed since mid-2022** | — | RISK | OK |

**Reconciling the tension:** MSCI's factor construct says crowded stocks underperform ~1.5%/yr;
Brown/Howard/Lundblad's position-level days-ADV measure says crowded stocks earn **more** (11.3% vs
8.5%/yr) with a conditional crisis tail; Sias/Turtle/Zykaj say there's no reversal at all. These are
different constructs measuring different things — quarterly 13F demand shocks miss both the leverage
and the exit-liquidity dimensions that make crowding dangerous. **Label crowding contested, not
settled.**

**Does high institutional ownership predict volatility/drawdown?** Yes — but the working variable is
**concentration or owner size, never level**:

| Measure | Effect | Source |
|---|---|---|
| G&T fragility p25→p75 | **+0.5% daily volatility** (~¼ of mean) | Greenwood & Thesmar |
| Top-10 institutional ownership, 1 SD | **+3.33% of an SD** full-sample; **+16%** late-sample | Ben-David et al. |
| Same, worst 5% of quarters | DGTW returns **−9.17% of an SD**; ~zero normally | Ben-David et al. |
| ETF ownership, 1 SD (S&P 500) | **+16% of an SD** of daily volatility | Ben-David/Franzoni/Moussawi |
| **Number of owners, ownership fixed** | 100→300 owners **REDUCES** daily vol by 0.1% | Greenwood & Thesmar |
| **Bottom institutions matched on AUM** | coefficient is **NEGATIVE** | Ben-David et al. |

**A naive "high institutional ownership = crowded = risky" flag measures the wrong thing and gets
the sign wrong.**

## C5. Index and float effects

| Signal | Source | Sample | Effect | Verified |
|---|---|---|---|---|
| **S&P 500 addition** | Greenwood & Sammon 2025 *JF* | 1980–2020 | **+3.42% (80s) → +7.59% (90s) → +5.21% (00s) → +0.799% (2010s, t≈1.33 ns)** | OK |
| S&P 500 deletion | Same | " | −4.64% → −16.6% → −12.3% → **−0.603% ns** | OK |
| **The killer detail** | Same, verbatim | 2020 | *"Excluding Tesla, the average inclusion effect in 2020 was −3 basis points."* | OK |
| No cross-sectional link | Same | " | **no relationship between size of the mechanical buy and the return** (R²≈2% adds, 6% deletes) | OK |
| Reversion gone | Same | " | 1990s CAR peaked +7.5% at t+6; 2010s peaked **+1.1% at t+1 then flat** | OK |
| Why it died | Same | " | migrations rose from ~40% to >80% of additions, netting forced buying against selling; late-2010s direct adds **+2.2%** vs migrations **−2.3%** | OK |
| Original | Shleifer 1986 *JF* | 1976–83 | **+2.79% permanent** | SEC |
| Contradicting original | Harris & Gurel 1986 *JF* | 1978–83 | +3.13% announcement then **−2.49% over 29 days** — *"unable to reject complete reversal"* | SEC |
| Arbitrage risk modifier | Wurgler & Zhuravskaya 2002 *JB* | 1976–89 | event-day **+3.29%**, increasing in arbitrage risk; **no substitute basket hedges even ¼ of daily variance for the median stock** | OK |
| **Pure float redefinition** | Kaul/Mehrotra/Morck 2000 *JF* | TSE 1996 | float +19.3% ⇒ **+2.34% event week, p<0.01, no reversal for 15 weeks** | OK |
| Index turnover cost | Petajisto 2011 *JEF* | 1990–2005 | adds **+8.8%** S&P / **+4.7%** R2000; **hidden cost 21–28 bp/yr (S&P), 38–77 bp/yr (R2000)** | OK |
| Index rebalance drag | Sammon & Shim 2026 *JFE* | — | funds' rebalancing portfolio **−4.67%/yr**; 47–70 bp/yr index drag | OK |
| **IPO fast-track inclusion** | Sammon & Murray 2026 *RAPS* | — | Fast-Track IPOs outperform by **>5 pp**, peaking at inclusion, **reverting within 3 weeks**; $5.8bn "shadow tax" | OK |
| Who clears the market | Sammon & Shim 2026 *RFS* | — | **firms** are the primary sellers to index funds, near one-for-one — explains why the premium died | OK |
| **ETF ownership → vol** | Ben-David/Franzoni/Moussawi 2018 *JF* | 2000–2012 | 1 SD ⇒ **+16% of an SD** daily volatility (≈+20bp/day); **45% of flow impact reverts within 20 days** | OK |
| …contested ID | Coles/Heath/Ringgenberg 2022 *JFE* | — | Russell 1000/2000 RDD is fragile; BDFM themselves **prefer the more conservative panel estimate** | UNVERIFIED numbers |
| **Passive → informativeness** | Sammon 2024 *Mgmt Sci* | ~30 yrs | pre-earnings-announcement price informativeness down **~¼ of its whole-sample mean** | OK |
| **True passive share** | Chinco & Sammon 2024 *JFE* | 2021 | index funds held 16% but **true passive share is 33.5%** — holdings-based measures understate by ~2× | OK |
| IPO lockup expiry | Field & Hanka 2001 *JF* | 1,948 lockups | **−1.5% three-day**; **+40% permanent** volume increase; both larger for VC-backed | SEC |

**UNVERIFIED — do not cite:** "Patel & Welch" on the index effect (no such paper located); SEO and
buyback announcement magnitudes; Chen/Noronha/Singal; Hau/Massa/Peress; Greenwood (2005) Nikkei;
the 2004–05 S&P float-adjustment transition.

**Two units traps in this literature:** "16% increase in volatility" means **16% of a standard
deviation**, not 16% relative — routinely misquoted. And Coval-Stafford's number is **−10.1%**, not
the "7–8% price impact" that circulates.

---

# PART (d) — Verdict and build list

## Is stock-level ownership alpha or risk/context?

**It is predominantly a RISK/CONTEXT feature.** One sub-signal carries genuine alpha, and it is
narrow, fast-decaying, and small-cap-bound.

The evidence that settles it:

- **Ownership LEVEL is definitively not alpha.** Gompers & Metrick's own decomposition (OK):
  IO_{t−1} significant (t=2.49) but **ΔIO insignificant (t=1.79)**, and the entire effect lives in
  high-institutional-inflow quarters (t=4.65) versus low-inflow quarters (t=0.10). Their words:
  *"virtually all of the forecasting power occurs in quarters that have contemporaneously high
  inflows to institutions."* It's a demand shock, not information.
- **The aggregate 13F book has no predictive power.** Griffin & Xu (OK), and Antón/Cohen/Polk's own
  "All Ideas" control portfolio: **1 bp/mo for hedge funds, 6 bp/mo for mutual funds,
  insignificant** (OK). Any "follow the smart money" screen built on whole portfolios is built on a
  documented zero.
- **The implementation record is unambiguous.** Copycat funds net 10.52%/yr against an 11.35%/yr
  index over 24 years (OK). Freshness of holdings is worth **0.01 pp/yr net (t=0.13)** (OK). Five
  clone ETFs: four dead or losing 3–4pp/yr; the survivor's edge is a 1.11 beta (OK).
- **Crowding is a risk premium with a conditional tail, not a short signal.** Crowded stocks earn
  *more* on average (11.3% vs 8.5%/yr) (OK); the damage is state-dependent (−9.17% of an SD in the
  worst 5% of quarters, ~zero otherwise) (OK). MSCI's own hedge-fund-crowding factor ranks near the
  bottom of 14 on cross-validated R² and MSCI calls it *"risk identification rather than return
  prediction"* (OK).
- **The one real alpha — insider open-market buys — comes with three hard constraints.** 25% of the
  return accrues within 5 days and 50% within a month, with days 21+ statistically insignificant
  (OK); post-SOX the filing-date reaction *tripled* (SEC); in the modern US sample the
  **value-weighted alpha is 0.142%/mo, t=0.68 — zero** (OK); and it doesn't survive position sizing
  (ABS).

## The unifying structural finding

Confirmed six independent ways, and the most useful thing in this report:

> **Classifying the actor carries the signal. The holdings arithmetic does not.**

| Evidence | Same computation, different actor |
|---|---|
| Farouk & Jivraj (OK) | α **0.32%/mo** on fundamental equity hedge funds vs **−0.003% ns** on everyone else |
| Antón/Cohen/Polk (OK) | small HFs **58 bp/mo**; **giant HFs negative**; spread >1.26%/mo |
| Choi/Jin/Yan (OK) | breadth ↑ = **−23%/yr** retail-driven, **+8%/yr** institution-driven — *same metric, opposite sign* |
| Caglayan et al. (ABS) | short-horizon demand informative **only among hedge funds** |
| Cohen/Malloy/Pomorski (OK) | routine insiders **−0.20%/mo ns**; opportunistic 0.82%/mo |
| Chen & Zimmermann (OK) | plain SI `2_fair`; **IO-conditioned** variants `1_clear` |

Ownership data's value is that it tells you *who* — and the "who" is the input every replicated
signal actually uses.

## The 3–5 sub-signals worth building with free data

Given SEC 13F bulk + Form 4 + FINRA short interest/volume + yfinance:

### 1. `SIRIO` = FINRA short interest ÷ 13F institutional shares held — highest value

Drechsler & Drechsler validated it against actual Markit borrow fees: **1980–2013, 1.42%/mo,
FF4 α 1.51%/mo (t=8.97)**; top half of the extreme decile **1.92%/mo (t=9.93)** (OK). Larger than
any of the eight well-known anomalies they study. It is a *proxy for the borrow fee*, which is why
it beats raw short interest — and it's the same construct as Asquith/Pathak/Ritter's constrained
screen (**−2.15%/mo EW, t=−4.17**) (OK). Both inputs are already in reach; I verified FINRA's
`consolidatedShortInterest` API is free and unauthenticated.

**Ship it as a screen, not a portfolio** — APR's own caveat is ~21 qualifying names per month, and
it's insignificant value-weighted.

### 2. Days-to-cover, not short-interest ratio

Same inputs; FINRA returns `daysToCoverQuantity` directly. **EW 1.19%/mo (t=6.67), Sharpe 1.33** vs
SR's 0.71%/mo, Sharpe 0.51 (OK). Raw SR became insignificant post-2000; DTC didn't. Label it a
small-cap effect — the advantage shrinks to t=1.76 excluding the smallest decile.

### 3. Insider open-market buys, filtered three ways

- **Buys only.** Sells are zero or inverted, and *large* sells precede **positive** returns
  (+1.44%/mo) (OK). Display sells; never score them.
- **Exclude 10% beneficial owners.** Their decile ranking literally inverts (OK). The
  `isTenPercentOwner` flag is in the Form 4 XML.
- **Read `<aff10b5One>`.** I verified firsthand that live filings carry it, alongside
  `<isDirector>`, `<isOfficer>`, `<isTenPercentOwner>`, `<officerTitle>`. It's a mandatory post-2022
  checkbox and an *exact* marker of a pre-planned trade, where CMP's calendar heuristic is a proxy
  that leaves ⅔ of trades unclassifiable.
- **Do not weight by rank.** Three primary sources find no CEO/CFO/director hierarchy.
- **Cluster buys are the best-supported enhancement:** **+7.27%/yr in small caps, t=3.10** (OK),
  computable from Form 4 alone with no historical insider database.

### 4. Ownership concentration as a conditional risk read — never a return forecast

Top-10 holder share, **not** total institutional ownership. The sign matters: **more owners
*reduces* volatility** holding ownership fixed, and bottom institutions matched on AUM carry a
*negative* coefficient (OK). A "high institutional ownership = risky" badge would be backwards.
Render it as: 1 SD of top-10 concentration ⇒ +3.3% of an SD of volatility normally, **−9.17% of an
SD of returns in the worst 5% of quarters** (OK).

### 5. Breadth of holder-count change, labelled by holder type

`DelBreadth` is one of only two ownership signals graded `1_good`/`1_clear` by OSAP
(**0.673%/mo, t=3.96**) (OK). Two caveats to surface: ~half the spread is in the **short leg**
(decile 1 −2.62% vs decile 10 +2.32%) (OK), which a long-only reader can't act on; and the sign
flips by holder type (OK). The canonical version uses mutual-fund holdings, so **N-PORT** (free on
EDGAR; quarter-end filings public, the other two months confidential for 60 days) is the right
source; a 13F holder-count change is a usable but weaker proxy.

### Optional sixth — cheap and unusually well-targeted

The **Khandani-Lo crowding detector**: regress cross-sectional stock turnover on decile ranks of
your factor exposures. Normally ~0; it exceeded **10% in early August 2007** and stayed above 5% for
the rest of that year, a level never crossed before July 2007 (OK). **Prices and volume only,
same-day.** Every 13F-based crowding measure is ≥45 days stale by construction; this one isn't.

### What NOT to build

- A 13F consensus / "follow the smart money" buy list — documented zero, and one paper finds the
  *contrarian* side profitable at 21–42 days (OK).
- An index-addition trade — +0.8%, t≈1.33, **−3bp ex-Tesla**, no reversion, no link between flow
  size and return (OK).
- A bearish insider-sell score.
- A CEO/CFO-weighted insider score.
- Any raw-short-interest alpha claim.

### One cross-cutting UI rule

Never sort a holder table by size while mixing as-of dates. Nasdaq does exactly this in production
and a two-quarter-stale filer silently outranks a current one (OK).

---

# PART (e) — Applied to the existing `src/screens/ownership/`

The screen already exists and is unusually well-aligned with the evidence:

- `StockOwnershipPanel` tiles: **READ-THROUGH · INSIDERS — FORM 4 · INSTITUTIONAL HOLDERS ·
  FLOAT & SHORT INTEREST · 5% STAKES — 13D/13G**
- `OwnershipSignals.h` derives reads with named, arguable thresholds and refuses to emit a read
  whose inputs are missing
- CMP routine/opportunistic classification with `Pattern::Unclassified` as a first-class outcome
- Cluster detection restricted to code-P open-market buys by ≥2 distinct insiders within 30 days
- `is_broad_book` measuring discretion by **position count** (≥1000) rather than name-matching
- `DemandQuadrant` plotting conviction against direction rather than collapsing to one badge — a
  decision made because aggregate and conviction-weighted reads pointed opposite ways on AAPL, which
  is precisely the Choi/Jin/Yan finding arrived at independently

**Evidence-backed gaps, cheapest first — all four verified by me as free and available:**

1. **Read `<aff10b5One>` from Form 4.** Confirmed firsthand that a live Apple Form 4 contains
   `<aff10b5One>true</aff10b5One>`. `scripts/sec_ownership_data.py` parses `transactionCode` but not
   this flag. Highest evidence-to-effort item on the list.
2. **Exclude 10% beneficial owners** from the cluster and insider reads. Unanimous across L&L (OK)
   and JMZ (OK), who drop them from the sample entirely. The flag is already in the XML.
3. **Compute `SIRIO`** from FINRA short interest ÷ the existing 13F index — the single
   highest-alpha construct available from the data already in reach.
4. **Switch short interest to FINRA's own feed.** Verified that
   `https://api.finra.org/data/group/otcMarket/name/consolidatedShortInterest` is free and
   unauthenticated, returning `currentShortPositionQuantity, previousShortPositionQuantity,
   averageDailyVolumeQuantity, daysToCoverQuantity, changePercent, settlementDate` plus
   `stockSplitFlag` and `revisionFlag`. Given the project's `vendor_null_becomes_zero` history with
   Yahoo-derived fields, taking this from the authoritative source matters; `stockSplitFlag` also
   connects to the split-repair work.

**One finding that touches the earnings work:** Sammon (*Mgmt Sci* 2024) finds the rise in passive
ownership has cut pre-earnings-announcement price informativeness by **~¼ of its whole-sample mean**.
This does *not* contradict the prior finding that earnings moves aren't predictable — it is a
**conditioning** variable on how much of the move is already in the price before the print. High
passive ownership ⇒ less pre-priced ⇒ larger residual surprise. Note Chinco & Sammon: true passive
share is **33.5%, roughly double** what fund holdings alone imply.

---

# Standing caveats

- **Bloomberg MHD, 13F `<GO>` and HOLD `<GO>` could not be confirmed to exist as described.** HDS's
  Matrix tab and FLNG do what was attributed to them. My clearest HDS column evidence is a 2012
  screenshot.
- **Fintel is entirely unverified** (Cloudflare/CAPTCHA-gated; not bypassed).
- **Boehmer/Huszár/Jordan: not a single number verified** — every source 403'd. Only the abstract
  stands. This is the one paper whose *long*-side claim matters most to a retail product, and it
  remains open.
- **Whether FactSet ships a conviction/fund score is unresolved** — the "Fund Sentiment Score"
  description traces to Fintel, not FactSet's own docs.
- WhaleWisdom's exact holdings-grid headers are cross-source consistent but were not read off the
  live DOM.
- OSAP post-publication returns for the seven ownership signals were **not** computed (the
  `openassetpricing` package segfaulted under Python 3.14). Signal-specific decay is unverified.
- Not verified and not to be cited from this report: Muravyev et al. primary text, Nagel (2005)
  primary text, the loan-fee anomaly's specific digits, Dyakov & Verbeek on front-running fire sales.

---

# URLs

## Products

- Bloomberg Activism Model — https://assets.bbhub.io/professional/sites/10/Bloomberg-Activism-Screening-Model.pdf
- Bloomberg Security Ownership fact sheet — https://data.bloomberglp.com/professional/sites/10/Security-Ownership-fact-sheet.pdf
- NYU Stern Bloomberg guide (HDS/OWN/DES screenshots) — https://pages.stern.nyu.edu/~adamodar/pdfiles/cfovhds/guidetobloomberg.pdf
- U. Delaware Bloomberg equity sheet (PHDC) — https://my.lerner.udel.edu/wp-content/uploads/BB-Equity.pdf
- Cranfield ownership guide (OWN) — https://blogs.cranfield.ac.uk/library/researching-company-ownership/
- Scranton Bloomberg training manual — https://www.scranton.edu/academics/ksom/alperin/Bloomberg%20Training%20Manual.pdf
- WU Vienna Bloomberg ownership manual — https://library.wu.ac.at/bib/fit4research/wp-content/uploads/2024/03/Ownership_manuals_Bloomberg.pdf
- Babson equity valuation using Bloomberg — https://www.babson.edu/media/babson/assets/cutler-center/Equity-Valuation-using-Bloomberg.pdf
- HBS Baker Library stock and bond holders — https://www.library.hbs.edu/services/help-center/bloomberg-stock-and-bond-holders
- LBS ownership guide (cross-platform) — https://library.london.edu/financial_markets_data/ownership
- FactSet QuickStart manual — https://www.ulethbridge.ca/sites/default/files/FactSetQuickStart_Manual%201%20of%202.pdf
- FactSet enterprise SDK docs — https://github.com/FactSet/enterprise-sdk
- FactSet LSD_Ownership field guide — https://go.factset.com/hubfs/Website_Downloads/Statistical%20Package%20Integration/Docs%203.0/LSD_Ownership.pdf
- FactSet Ownership datafeed overview — https://insight.factset.com/resources/at-a-glance-factset-ownership-standard-datafeed
- WRDS ownership data guide — https://wrds-www.wharton.upenn.edu/documents/1414/WRDS_Ownership_Data.pdf
- Eikon Ownership Summary QRC — https://video.training.refinitiv.com/elearning_video/Documents/Eikon_QRC/Eikon%20Quick%20Reference%20Card%20-%20Ownership%20Summary.pdf
- WU Vienna LSEG Workspace ownership manual — https://library.wu.ac.at/bib/fit4research/wp-content/uploads/2025/01/Ownership_manuals_LSEG-Workspace.pdf
- LSEG Smart Holdings Plus fact sheet — https://www.lseg.com/content/dam/data-analytics/en_us/documents/fact-sheets/lseg-smart-holdings-plus-fact-sheet.pdf
- LSEG Ownership API — https://developers.lseg.com/en/api-catalog/refinitiv-data-platform/ownership-API
- S&P Capital IQ user guide (Ownership screenshots) — https://www.cbs.dk/sites/default/files/2025-11/sp_capital_iq_-_user_guide.pdf
- WhaleWisdom backtesting whitepaper — https://whalewisdom.com/whitepapers/backtesting
- WhaleWisdom methodology whitepaper — https://whalewisdom.com/whitepapers/whalewisdom
- Dataroma help notes — https://www.dataroma.com/m/inc/help_notes.php
- GuruFocus Real-Time Picks — https://www.gurufocus.com/guru/realtime-picks
- GuruFocus Score Board — https://www.gurufocus.com/guru/scoreboard
- HedgeFollow consensus picks — https://hedgefollow.com/consensus-stock-picks.php
- 13f.info — https://13f.info/
- Stockcircle transactions — https://stockcircle.com/transactions
- OpenInsider screener — http://openinsider.com/screener
- OpenInsider cluster buys — http://openinsider.com/latest-cluster-buys
- SecForm4 — https://www.secform4.com/all-buys
- InsiderScreener — https://www.insiderscreener.com/en/insiders
- Quiver congress trading — https://www.quiverquant.com/congresstrading/
- Unusual Whales politics — https://unusualwhales.com/politics
- Nasdaq institutional holdings — https://www.nasdaq.com/market-activity/stocks/aapl/institutional-holdings
- Yahoo holders — https://finance.yahoo.com/quote/AAPL/holders/
- Stockanalysis statistics — https://stockanalysis.com/stocks/aapl/statistics/
- Koyfin ownership help — https://www.koyfin.com/help/insider-ownership-transactions/
- Simply Wall St ownership — https://simplywall.st/stocks/us/software/nasdaq-msft/microsoft/ownership
- Finviz insider trading — https://finviz.com/insidertrading.ashx

## Insider literature

- Lakonishok & Lee — https://www.lsvasset.com/pdf/research-papers/Insider-Trades-Informative.pdf
- Jeng/Metrick/Zeckhauser — https://rodneywhitecenter.wharton.upenn.edu/wp-content/uploads/2014/04/9919.pdf
- Cohen/Malloy/Pomorski — https://www.nber.org/system/files/working_papers/w16454/w16454.pdf
- CMP internet appendix — https://afajof.org/wp-content/uploads/files/supplements/Decoding_Inside_Information-.pdf
- Ali & Hirshleifer — https://bpb-us-e2.wpmucdn.com/sites.uci.edu/dist/c/362/files/2020/07/Opportunism.pdf
- Heckmann/Jacobs/Schwarz — http://wp.lancs.ac.uk/fofi2024/files/2024/04/FoFI-2024-009-Jens-Heckmann.pdf
- Eckbo & Smith — http://mba.tuck.dartmouth.edu/bespeneckbo/default/AFA611-Eckbo%20web%20site/AFA611-S10C-EckboSmithJF98.pdf
- Brochet international insider sentiment — https://tuck.dartmouth.edu/uploads/content/insider_sentiment_stock_returns.pdf
- Brochet SOX (abstract) — https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1108731
- Alldredge & Blank — https://onlinelibrary.wiley.com/doi/10.1111/jfir.12172
- Wang/Shin/Francis (CFO vs CEO) — https://www.cambridge.org/core/journals/journal-of-financial-and-quantitative-analysis/article/abs/are-cfos-trades-more-informative-than-ceos-trades/B7DD71285F1326E694CC860193C87710
- Ravina & Sapienza — https://academic.oup.com/rfs/article-abstract/23/3/962/1594077
- Oenschläger & Möllenhoff — https://www.sciencedirect.com/science/article/pii/S1544612324015435
- McLean & Pontiff internet appendix — https://tevgeniou.github.io/EquityRiskFactors/bibliography/AcademicReviewFactorApp.pdf
- 2iQ academic review (secondary) — https://www.2iqresearch.com/blog/profiting-from-insider-transactions-a-review-of-the-academic-research

## 13F literature

- Cohen/Polk/Silli 2008 draft — https://conference.nber.org/confer/2008/bff08/polk.pdf
- Cohen/Polk/Silli published (LSE) — https://researchonline.lse.ac.uk/id/eprint/24471/1/Best%20ideas(published).pdf
- Antón/Cohen/Polk 2021 — https://personal.lse.ac.uk/polk/research/bestideas.pdf
- Verbeek & Wang numbers (Wang ERIM thesis) — https://repub.eur.nl/pub/26066/EPS2011242F&A9789058922854.pdf
- Frank/Poterba/Shackelford/Shoven — https://www.nber.org/system/files/working_papers/w8653/w8653.pdf
- Griffin & Xu — https://papers.ssrn.com/sol3/papers.cfm?abstract_id=924242
- Agarwal/Jiang/Tang/Yang confidential 13F — https://business.columbia.edu/sites/default/files-efs/pubfiles/6037/Hedge_fund_skill.pdf
- Aragon/Hertzel/Shi — https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1569736
- Cao/Da/Jiang/Yang restatements — https://academicweb.nd.edu/~zda/Restatement.pdf
- Chen/Hong/Stein breadth — http://www.columbia.edu/~hh2679/breadth-jfe.pdf
- Choi/Jin/Yan breadth decomposition — https://www.nber.org/system/files/working_papers/w16591/revisions/w16591.rev2.pdf
- Gompers & Metrick — https://rodneywhitecenter.wharton.upenn.edu/wp-content/uploads/2014/04/9920.pdf
- Yan & Zhang — https://academic.oup.com/rfs/article-abstract/22/2/893/1594146
- Caglayan/Celiker/Tepe (FAJ 2024) — https://www.tandfonline.com/doi/abs/10.1080/0015198X.2023.2259287
- Puckett & Yan — https://papers.ssrn.com/sol3/papers.cfm?abstract_id=1107953
- Farouk & Jivraj systematic 13F alpha — http://wp.lancs.ac.uk/fofi2020/files/2020/04/FoFI-2020-090-Farouk-Jivraj.pdf
- 13F trading imbalances — https://arxiv.org/abs/2209.08825
- CFA Institute on guru ETFs — https://rpc.cfainstitute.org/blogs/enterprising-investor/2021/does-guru-investing-work
- Direxion N-CSR (IBLN) — https://www.sec.gov/Archives/edgar/data/1424958/000110465918000153/a17-27464_1ncsr.htm
- Global X GURU fund page — https://www.globalxetfs.com/funds/guru
- Quantpedia alpha cloning — https://quantpedia.com/strategies/alpha-cloning-following-13f-fillings

## Short interest, crowding, index effects

- Chen & Zimmermann OSAP — https://www.federalreserve.gov/econres/feds/files/2021-037pap.pdf
- OSAP SignalDoc / code — https://github.com/OpenSourceAP/CrossSection
- Open Asset Pricing site — https://www.openassetpricing.com/
- Boehmer/Huszár/Jordan (abstract only) — https://econpapers.repec.org/article/eeejfinec/v_3a96_3ay_3a2010_3ai_3a1_3ap_3a80-97.htm
- Asquith/Pathak/Ritter — https://site.warrington.ufl.edu/ritter/files/2015/04/Short-interest-institutional-ownership-and-stock-returns-2005-08.pdf
- Hong/Li/Ni/Scheinkman/Yan, Days to Cover — https://www.nber.org/system/files/working_papers/w21166/w21166.pdf
- Drechsler & Drechsler, shorting premium — https://w4.stern.nyu.edu/finance/docs/pdfs/Seminars/1403f-drechsler.pdf
- Engelberg/Reed/Ringgenberg, Short Selling Risk — https://rady.ucsd.edu/faculty/directory/engelberg/pub/portfolios/SHORT_RISK.pdf
- Muravyev/Pearson/Pollet — https://onlinelibrary.wiley.com/doi/abs/10.1111/jofi.13129
- Schultz, Short Squeezes — https://www.cambridge.org/core/services/aop-cambridge-core/content/view/63F30135D28474EEFE7AC0C47967FE98/S0022109022001533a.pdf/short_squeezes_and_their_consequences.pdf
- Markit, Shining the Light on Short Interest — https://cdn.ihs.com/www/pdf/Shining_the_Light_on_Short_Interest.pdf
- Rapach/Ringgenberg/Zhou — https://ideas.repec.org/a/eee/jfinec/v121y2016i1p46-65.html
- Khandani & Lo — https://www.nber.org/system/files/working_papers/w14465/w14465.pdf
- Brown/Howard/Lundblad, Crowded Trades and Tail Risk — https://uncipc.com/wp-content/uploads/2019/02/CTTR.pdf
- Greenwood & Thesmar, Stock price fragility — https://tevgeniou.github.io/EquityRiskFactors/bibliography/Fragility.pdf
- Ben-David/Franzoni/Moussawi/Sedunov, granular investors — https://www.nber.org/system/files/working_papers/w22247/w22247.pdf
- Coval & Stafford, fire sales — https://www.newyorkfed.org/medialibrary/media/research/conference/2005/liquidity/Coval_Stafford.pdf
- Lou, flow-based return predictability — https://personal.lse.ac.uk/loud/flows.pdf
- Antón & Polk, Connected Stocks — https://personal.lse.ac.uk/polk/research/connectedstocks.pdf
- Edmans/Goldstein/Jiang — https://alexedmans.com/wp-content/uploads/2024/06/Feedback.pdf
- Pojarliev & Levich, crowded currency trades — https://www.nber.org/system/files/working_papers/w15698/w15698.pdf
- Sias/Turtle/Zykaj, Hedge Fund Crowds and Mispricing — https://pubsonline.informs.org/doi/10.1287/mnsc.2014.2131
- Greenwood & Sammon, Disappearing Index Effect — https://www.nber.org/system/files/working_papers/w30748/w30748.pdf
- Kaul/Mehrotra/Morck, TSE float redefinition — https://randallmorck.ca/wp-content/uploads/2020/02/69-demand-curves-for-stocks-do-slope-down.pdf
- Petajisto, index premium hidden cost — https://www.petajisto.net/papers/petajisto%202011%20jef%20-%20hidden%20cost%20for%20index%20funds.pdf
- Sammon & Shim, Index Rebalancing — https://www.sciencedirect.com/science/article/pii/S0304405X25002375
- Sammon & Shim, Who Clears the Market — https://academic.oup.com/rfs/advance-article/doi/10.1093/rfs/hhag032/8572664
- Ben-David/Franzoni/Moussawi, Do ETFs Increase Volatility — https://www.nber.org/system/files/working_papers/w20071/w20071.pdf
- Sammon, Passive Ownership and Price Informativeness — https://doi.org/10.1287/mnsc.2023.00836
- Chinco & Sammon, passive share is double — https://www.sciencedirect.com/science/article/pii/S0304405X24000837
- Field & Hanka, IPO lockups — https://doi.org/10.1111/0022-1082.00334
- MSCI hedge fund crowding factor — https://www.msci.com/research-and-insights/blog-post/is-there-a-hedge-fund-crowding-factor
- MSCI security crowding — https://www.msci.com/research-and-insights/blog-post/can-crowding-scores-quantify-us-stocks-fragility
- MSCI Integrated Factor Crowding Model — https://www.msci.com/research-and-insights/paper/msci-integrated-factor-crowding-model

## Regulatory and data sources

- SEC Form 13F FAQ — https://www.sec.gov/rules-regulations/staff-guidance/division-investment-management-frequently-asked-questions/frequently-asked-questions-about-form-13f
- SEC Release 34-89290 (filer counts, thresholds) — https://www.sec.gov/files/rules/proposed/2020/34-89290.pdf
- SEC Form 13F data sets — https://www.sec.gov/data-research/sec-markets-data/form-13f-data-sets
- SEC 13D/G amendments 2023 — https://www.sec.gov/newsroom/press-releases/2023-219
- FINRA equity short interest data — https://www.finra.org/finra-data/browse-catalog/equity-short-interest
- FINRA short interest reporting rules — https://www.finra.org/filing-reporting/regulatory-filing-systems/short-interest
- FINRA short interest file download API spec — https://www.finra.org/sites/default/files/Equity_Short_Interest_Data_File_Download_API.pdf
- SEC Form SHO extension to 2028 — https://www.sec.gov/files/rules/exorders/2025/34-104303.pdf
- Morgan Lewis on the Form SHO extension — https://www.morganlewis.com/pubs/2025/12/short-sale-reporting-on-form-sho-compliance-date-further-extended-to-2028

---

*No repository changes were made. This document is research output only.*
