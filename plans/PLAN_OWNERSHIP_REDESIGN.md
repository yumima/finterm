# Ownership — Redesign

**Status:** All three phases implemented — phases 1 and 2 on 2026-09-06, phase 3 on 2026-09-07 (watchlist ownership columns, insider-buy alerts on holdings and watchlists, the Ownership Calendar dashboard widget, and the twelve-month forward-return check — see `plans/research/form4_forward_returns_2026-09.md`). LARGEST FUNDS tab added 2026-10-02 at the user's request — see the revision note in §4.3
**Replaces:** `src/screens/ownership/*` (7,000 lines), the OWNERSHIP screen's BY FIRM / INSIDERS tabs, and the ER › Ownership tab's tile grid
**Screen IDs:** `ownership` (market-wide), `equity_research` › Ownership (per-stock)
**Date:** 2026-09-05

---

## The verdict in five lines

1. **Keep the feature, narrow the claim.** Every terminal and every retail product ships a Holders tab. None of them ships an ownership-based return forecast, and the evidence says they are right not to.
2. **"Who owns what" is context, not an indicator.** Institutional ownership level, aggregate 13F buying, "smart-money consensus" — all measured at zero. Copycat funds trail the index by 0.8 pp/yr; four of five 13F-clone ETFs are dead.
3. **"Who is trading what" is an indicator for exactly three actors:** insiders buying on the open market (esp. clusters, small caps, decays within a month), the short side (days-to-cover and short-interest ÷ institutional shares, a borrow-fee proxy: 1.5 %/mo four-factor alpha over 33 years), and holder-count change (breadth). Everything else on the current screen is noise dressed as signal.
4. **The split is right: market-wide scan on OWNERSHIP, per-stock register in ER.** What is wrong is that the market-wide screen lands on a firm browser nobody arrives with a question for, and then squeezes a duplicate of the ER tab into its right half.
5. **Delete the 13F analytics** (demand scatter, buying curve, flow scale, conviction bars, read-through chip wall, firm ranking as landing page). Build three things instead: a FINRA short-interest history, a daily Form 4 ingest, and holder classification. Both screens get simpler and every number gets a date.

---

## 1. Does the goal make sense? What is the clear goal?

The stated goal — *find who owns what and who is trading what, to have an indicator* — bundles two things that the evidence separates:

| Question | What the data can honestly answer | Role |
|---|---|---|
| Who owns it | Institutions / insiders / index complexes / retail-residual, as % of shares out **and** % of float; top-10 concentration; holder count | **Context & risk.** Concentration predicts volatility and a conditional drawdown (−9 % of a σ in the worst 5 % of quarters, ≈0 otherwise). Level predicts nothing. |
| Who is trading it — institutions (13F) | Buyers/sellers count, new/closed, breadth change — 45–135 days stale, long-only, index-dominated | **Context**, one weak indicator (breadth change, 0.67 %/mo, half of it in the short leg) |
| Who is trading it — insiders (Form 4) | Open-market buys, clusters, 10b5-1 flag, 10 % owners, 2-day lag | **Indicator.** The one narrow alpha. Buys only; sells are zero or inverted. |
| Who is trading it — shorts (FINRA) | SI % float, days-to-cover, SI ÷ 13F shares, bi-monthly + daily short volume | **Indicator.** DTC 1.19 %/mo (t = 6.7); SIRIO 1.51 %/mo (t = 9.0). Small-cap effect; squeeze cost eats ⅔ of it at ≥90 % utilisation. |

**The clear goal, restated:**

> For the stock in front of me: who holds it and how tightly (so I know how it will trade), and are the *informed* parties acting — insiders buying, shorts constrained, holders arriving or leaving — with every number carrying the date it was true.
>
> Market-wide: where are those three things happening right now, so I leave with a ticker.

The structural finding from the literature, confirmed six independent ways: **classifying the actor carries the signal; the holdings arithmetic does not.** The same breadth metric is −23 %/yr when retail drives it and +8 %/yr when institutions do. Routine insiders are −0.2 %/mo (n.s.); opportunistic ones +0.8 %/mo. Fundamental hedge funds' best ideas earn 32 bp/mo; everyone else's earn nothing. The current code already knows this (`is_broad_book`, `Pattern::Opportunistic`) — the redesign builds the UI around it instead of around the scatter plot.

---

## 2. What external products do

Studied: Bloomberg HDS/OWN/FLNG/PHDC, FactSet Ownership, LSEG Workspace OWNS, S&P Capital IQ, WhaleWisdom, Fintel, Dataroma, GuruFocus, HedgeFollow, 13f.info, Stockcircle, Insider Monkey, OpenInsider, SecForm4, InsiderScreener, Quiver, Unusual Whales, Nasdaq, Yahoo, Stockanalysis, Koyfin, Simply Wall St, Finviz, Morningstar, TIKR, TradingView. Full notes with verification marks in `plans/research/ownership_research_2026-09.md`.

### What every product converges on

1. **Two units of analysis, never one.** By security (HDS, FactSet *Company*, LSEG *Firm Ownership*, WhaleWisdom `/stock/`) and by holder (FLNG, FactSet *Holder*, LSEG *Fund Ownership*, `/filer/`). Products that pick one (Dataroma filer-first, Fintel security-first) do so as positioning.
2. **Level and change in adjacent columns; level leads.** `Position | Pos Change` (FactSet), `Shares Held | Change | Change %` (Nasdaq), `Amount Held | Latest Chg` (Bloomberg). Nobody makes you toggle.
3. **Buyers/sellers *count* is a headline number**, separate from share sums. Bloomberg OWN: `# of Buyers/Sellers 1,017 / 958`. Nasdaq: Increased / Decreased / Held / New / Sold-out × (Holders, Shares). FactSet: Ownership Activity panel.
4. **Two denominators, always both:** % of shares outstanding and % of float, side by side (`66.3% / 68.8%`).
5. **Conviction ≠ size; the good products let you switch.** WhaleWisdom's *BY % PORTFOLIO / BY LARGEST $ CHANGE*. FactSet's `% Port` beside `% O/S`.
6. **Concentration is a named panel.** LSEG: Top 10 / 20 / 50 / 100. FactSet: `Top 10 Inst. Holders (%)` in the statistics block.
7. **Institutional products classify holders; retail products don't.** FactSet by style (Growth/Value/GARP/Yield/Index/Hedge Fund), LSEG by style *and* type, CapIQ by organisational type (bank, pension sponsor, SWF, family office, hedge fund). This is the biggest gap between tiers — and the part that carries signal.
8. **Freshness is disclosed structurally or not at all.** Best: 13f.info's `DATE FILED` beside `QUARTER`; Dataroma's `Reported Price*` with the caveat inside the column name. Worst: Nasdaq silently mixing 12/31/2025 and 6/30/2026 rows in one size-sorted table.
9. **Retail is a residual**, never measured (Simply Wall St "General Public", CapIQ "Public and Other").
10. **Forward returns on insider trades are the specialist mark.** OpenInsider's `1d / 1w / 1m / 6m`; InsiderScreener's per-insider success rate over a fixed 120-day window. No retail Holders tab attempts it.

### What they deliberately don't do

- No ownership-based **return forecast**. Bloomberg's only ownership score predicts activist-campaign likelihood; LSEG StarMine Smart Holdings predicts future *ownership*, not returns; CapIQ has none.
- Nobody scores **insider sells**.
- Nobody pretends the 13F register is the **beneficial owner** (Bloomberg partnered with CMi2i rather than claim it).
- TradingView omits ownership **entirely** — a legitimate answer.

### Design ideas worth stealing outright

- GuruFocus *Real-Time Picks*: use the faster regulatory channel where one exists (13D/G amendments report in days, not 45).
- OpenInsider's cluster definition is a **group-by with a tunable minimum insider count**, not a constant.
- InsiderScreener: "this insider has been right X % of the time over 120 days" — computable from Form 4 + prices.
- Koyfin: ownership fields as **watchlist / screener columns**, not only a tab.
- FactSet: activism as an **inline badge on the holder row** (SharkWatch), not a separate table.

---

## 3. Evidence — what carries signal

Primary-source numbers only (✅ read from the paper's own tables). Full table with ⚠️/❌ items in the research file.

| Signal | Effect | t | Sample | Verdict |
|---|---|---|---|---|
| Insider open-market buys, VW, 4-factor | 0.50 %/mo | ≈3.8 | 1975–96 (Jeng/Metrick/Zeckhauser) | alpha, front-loaded: days 0–5 carry 2.5 %/mo, days 21+ n.s. |
| Same, modern US 2003–21, **value-weighted** | **0.14 %/mo** | **0.68 n.s.** | Heckmann et al. | small-cap only |
| **Cluster buy** (≥3 insiders) | +4.8 %/yr all; **+7.3 %/yr small caps** | 2.6 / 3.1 | Lakonishok & Lee | best-supported enhancement |
| Cluster **sell** | +2.8 %/yr (wrong sign) | 1.4 n.s. | L&L | never score sells |
| 10 % beneficial owners | decile ranking **inverts** | n.s. | L&L | exclude |
| Opportunistic − routine insiders | 0.62 %/mo Carhart | 1.71 | Cohen/Malloy/Pomorski | marginal VW; needs 3 yrs history, drops ⅔ of trades |
| Insider **rank** (CEO vs director) | 0.65 / 0.43 / 0.51 %/mo | — | JMZ | **no hierarchy** — don't weight by title |
| 13F "Best Ideas" (top-1 conviction) | 26–39 bp/mo | 2.9–3.3 | 1983–2018 | only for concentrated fundamental managers; **giant funds negative** |
| 13F **all holdings** ("All Ideas") | 1–6 bp/mo | n.s. | Antón/Cohen/Polk | **zero** — a follow-the-money list is built on this |
| Copycat funds vs index | **−0.83 pp/yr** net | — | 1985–2008, Verbeek & Wang | fresher holdings worth 0.01 pp/yr (t = 0.13) |
| 13F clone ETFs (GURU, ALFA, IBLN, GVIP) | −3 to −4 pp/yr; GVIP +0.7 with β 1.11 | — | 2012–26, price data | four of five dead or losing |
| **Breadth change** (Δ holder count) | 0.67 %/mo | 3.96 | Chen/Hong/Stein; OSAP grade 1_clear | half in the short leg; sign flips by holder type |
| Institutional ownership **level** | ΔIO n.s. (t = 1.79); all in high-inflow quarters | — | Gompers & Metrick | demand shock, not information |
| Plain short interest | 0.35 %/mo | — | Dechow; OSAP grade **2_fair** | weakest tier |
| **Days to cover** | **1.19 %/mo, Sharpe 1.33** (EW) | **6.67** | 1988–2012, Hong et al. | beats SI ratio on every cut; small-cap |
| **SIRIO** = SI ÷ 13F shares | **1.51 %/mo FF4 α** | **8.97** | 1980–2013, Drechsler & Drechsler | borrow-fee proxy; both inputs free |
| SI 99th pct × low IO | −2.15 %/mo EW | −4.17 | Asquith/Pathak/Ritter | ≈21 names/month; VW n.s. |
| Squeeze cost at ≥90 % utilisation | 56–73 bp/mo | — | Schultz 2024 | eats > ⅔ of gross short alpha |
| **Top-10 holder concentration** | +3.3 % of σ volatility; **−9.2 % of σ returns in worst 5 % of quarters, ≈0 otherwise** | — | Ben-David et al. | risk read, conditional |
| Owner **count** | 100→300 owners **reduces** vol 0.1 %/day | — | Greenwood & Thesmar | "high inst. ownership = risky" has the sign backwards |
| Hedge-fund crowding (days-ADV) | crowded quintile earns **more** (11.3 vs 8.5 %/yr), crisis tail reverses | — | Brown/Howard/Lundblad | contested (Sias et al. find nothing); label as such |
| S&P 500 addition, 2010s | +0.8 % (n.s.); **−3 bp ex-Tesla** | 1.33 | Greenwood & Sammon | dead; don't build |

**Do not build:** a 13F consensus / "smart money" buy list; an index-addition trade; a bearish insider-sell score; a CEO-weighted insider score; a raw-short-interest alpha claim; any "high institutional ownership = risky" flag.

---

## 4. The redesign

### 4.1 Rules that apply everywhere

1. **Every number carries the date it was true**, in the cell or the column header — quarter end *and* filed date for 13F, settlement date for SI, trade date *and* filed date for Form 4. Never sort a table that mixes as-of dates without showing them.
2. **Both denominators.** Institutional % is shown against shares out and against float. Computed from the index (a floor, N filers) with the vendor figure beside it.
3. **Level and change are adjacent columns.** No toggles between them.
4. **Three flags, evidence-tagged, and nothing else is interpreted.** Insider cluster buy · Short-constrained · Concentrated register. Each names its rule, its number, its source, and its date. No return forecast anywhere; forward returns are shown *after the fact* on insider trades.
5. **Buys are scored; sells are shown.** Grants, exercises and withholding are hidden by default.
6. **The vocabulary is the industry's.** Holders, Insiders, Short Interest, Filer, Book, Days to cover. Not "register", "read-through", "smart money", "demand", "conviction tilt".

### 4.2 Per-stock — Equity Research › Ownership

One header, three panels, no tiles-within-tiles, no chip wall.

```
AAPL — Apple Inc.                                                    Ownership
┌─────────────────────────────────────────────────────────────────────────────┐
│ INSTITUTIONS  62% of shares out · 64% of float   6,004 filers · Q1 2026     │
│               (vendor 66.4%)                      filed by 15 May            │
│ INSIDERS      1.7%                                Form 4 · 6M: 0 buys 4 sells│
│ TOP 10        39% of institutional value          index books 21% of value   │
│ Q1 vs Q4      1,810 added · 2,059 reduced · 190 new · 107 closed            │
│ SHORT         0.8% of float · 1.2 days to cover · SI/13F 0.013   15 Aug 2026 │
│ 13D           none on file                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ ▌ Concentrated register   top-10 = 39% · volatility read, not a forecast    │
│   (no insider cluster · not short-constrained)                              │
└─────────────────────────────────────────────────────────────────────────────┘

HOLDERS   [All] [New] [Added] [Reduced] [Closed]   Sort: [by size ▾ | by % of their book]
Holder                     Type          Shares    % out   Δ shares    Δ%    % of book  Book      As of
BlackRock, Inc.            Broad (49,751) 1,08B    7.4%   +12.1M     +1.1%   3.9%      $7.3T   Q1 26 · 15 May
Vanguard Capital Mgmt      Broad (…)      0.90B    6.2%   −3.4M      −0.4%   4.1%      …
Berkshire Hathaway  [13G]  Focused (29)   300M     2.1%   held       0.0%    22.0%     $263B   Q1 26 · 14 May
…
6,004 filers · 264 hold options only (not counted) · shown: 100 · positions up to 135 days old

INSIDERS — FORM 4   [Open-market only ✓]  [show grants/exercises]
 ▁▁▁▁▂▁▁▁▁▁▁▁▁▁▃▁▁▁▁▁▂▁▁▁ (timeline, buys up / sells down, cluster shaded)
Traded      Filed       Insider             Role       Buy/Sell  Shares   Price     Value    Δ own   10b5-1  Since trade
2026-09-01  2026-09-03  Newstead, Jennifer  GC         Sell      1.4K     $325.86   $456K    −2%     yes     −1.9%
2026-06-16  2026-06-18  Borders, Ben        Dir        Sell      116      $295.00   $34K     −1%     —       +8.5%
…
24 filings in 24 months · 4 open-market · no cluster · 10% owners excluded from scoring

SHORT INTEREST                                  FINRA · settlement 15 Aug 2026 · published 27 Aug
 ▂▂▃▃▂▂▂▁▁▂▂▃  SI % float, 12 months (bi-monthly)         Shares short 117M (prior 148M, −21%)
 Days to cover 1.2  ·  SI ÷ 13F shares 0.013 (universe pctl 18)   Daily short volume 41% (20d avg 44%)
```

**HOLDERS.** One table, the industry's columns. Default sort by shares held (Bloomberg/Nasdaq); second sort "by % of their book" **with a book-size floor ($1 B) and a breadth floor**, so the list is Berkshire-at-22 %, not a two-man RIA at 67 %. Filter chips as WhaleWisdom. `Type` is honest: *Broad (n names)* for books ≥ 1,000 names, *Focused (n)* otherwise, plus a small name list for the passive complexes — 13F cannot tell a hedge fund from a pension, so we do not claim to. A 13D/13G badge sits on the holder's row (FactSet pattern); the separate 5 % Stakes tile is gone. Click a holder → filer drill (§4.4).

**INSIDERS.** Open-market rows by default; grants/exercises/withholding behind a toggle. Adds the two fields the evidence asks for, both already in the Form 4 XML: `<aff10b5One>` (pre-planned trade — an exact marker, replaces the calendar heuristic for post-2022 filings) and `<isTenPercentOwner>` (excluded from scoring). `Since trade` is the OpenInsider forward return, computed from the price history the app already has. A cluster (≥ 2 distinct insiders, code P, within 30 days — tunable) is a shaded band on the timeline and a header line, never a score. The timeline widget is kept.

**SHORT INTEREST.** New data: FINRA consolidated short interest history (free, unauthenticated, bi-monthly, all symbols in one file per settlement date). Sparkline of SI % float over 12 months; shares short with prior; days to cover; **SIRIO** = SI ÷ 13F institutional shares from the index, with its percentile across the indexed universe; the daily short-volume ratio kept as one line with its 20-day average. Every figure dated; the publication lag (settlement + ~10 business days) stated once.

**Removed:** READ-THROUGH chip strip (9 rules → 3 flags in the header), DemandQuadrant, the "share of movers buying" curve, the DISTRIBUTING…ACCUMULATING scale, the conviction bar list, SmartMoneyPanel, the 5 % Stakes table, the portfolio dropdown, the in-tab index controls.

### 4.3 Market-wide — OWNERSHIP screen

Three scans and a search box. Every row is a **stock**; every row opens ER › Ownership. The firm browser is no longer the landing page.

```
OWNERSHIP    Where the informed parties are acting.            13F index: Q1 2026 · 10,647 filers   [Update]
[ INSIDER BUYS ]  [ SHORT-CONSTRAINED ]  [ 13F MOVERS ]                          Filer: [ search 13F filers… ]

INSIDER BUYS · open-market purchases, last 30 days · daily from EDGAR (last ingest 05 Sep 06:10)
Filed       Traded      Ticker  Company            Insiders  Roles        Value    Δ own   10b5-1  1w     1m    Mkt cap
2026-09-04  2026-09-02  XYZ     …                  3 ▲       CEO, CFO, D  $1.2M    +4%     no      +2.1%  —     $840M
…
Group by: [issuer ▾]   Min insiders: [2]   Min value: [$25K]   Exclude 10% owners ✓   Exclude 10b5-1 ✓

SHORT-CONSTRAINED · FINRA settlement 15 Aug 2026 · ranked by SI ÷ 13F shares
Ticker  SI % float  Days to cover  SI/13F   ΔSI vs prior  Inst % out  Mkt cap   Squeeze note
…

13F MOVERS · Q1 2026 vs Q4 2025 · filed by 15 May · positions up to 135 days old
Ticker  Holders  Δ holders  New  Closed  Net shares (focused books)  Top-10 %  Inst % out
…                                                                               Sort: Δ holders ▾ | breadth ↑ | breadth ↓
```

**INSIDER BUYS** replaces the SCAN EDGAR button with a **daily background ingest** of EDGAR's daily index (Form 4 only) into `form4.sqlite`, so the tab is full on first open and stays current. Columns are OpenInsider's; the cluster is a group-by with a minimum insider count, not a constant. Forward returns fill in as prices arrive.

**SHORT-CONSTRAINED** is the one screen that needs the new FINRA file joined to the 13F index. Universe = indexed tickers with a CUSIP map. Sorted by SIRIO; DTC and Δ SI beside it. A squeeze note when SI % float and DTC are both in the top decile — a risk statement, not a trade.

**13F MOVERS** is what the demand scatter was trying to say, as a table: holder count and its change (breadth), new/closed, and net shares among *focused* books (the only ones expressing a view). Explicit quarter and lag in the section header.

**Filer search** (top right) is the only route to a firm's book. It opens the filer drill in place. The ranked "Largest books / Concentrated" list is deleted — nobody arrives with "show me Capital World Investors" as the question.

> **Revised 2026-10-02:** the user asked for exactly that question — "the top 50 largest funds in the world, their portfolio, movement". A **LARGEST FUNDS** tab is back, but not as the deleted signal-dressed ranking: it is a register (top 50 by US-listed 13F book, each filer's own two newest filings compared — new / added / trimmed / exited, book change, estimated net flow, biggest buy and sell), labelled as context, with no score. A row opens the filer drill at the quarter the row describes; PULL NEWEST FROM EDGAR runs the existing `ingest_current` for the top 100 so the ranking is not a quarter behind. Stock clicks on this screen now open Equity Research *alongside* (`split_alongside`), as Portfolio does. Script action: `top_firms` in `sec_13f_bulk.py`.

### 4.4 Filer drill (reached from a holder row or the search box)

Keep `FirmDetailPanel`'s table — it is the one part of the current screen that is already the industry shape (`Issuer | Ticker | % of book | Shares | Value | Move | since Q-end | 3M | 6M`). Add: the quarter and filed date in the title, the book-size and breadth figures, and a **"Also holds"** sort so the peer-holdings question (FactSet Peer Analysis, CapIQ Crossholdings) has an answer: from AAPL → BlackRock's book → sorted by weight, the reader sees what else the largest holders of AAPL own.

### 4.5 Where else ownership shows up (phase 3)

- **Watchlist / screener columns** (Koyfin pattern): Inst % out, Short % float, Days to cover, SIRIO pctl, Insider net 6M, Last cluster buy.
- **Alerts**: Form 4 open-market buy on a portfolio holding; SI publication for a holding crossing the top decile.
- **Calendar**: 13F filing deadline (45 days), SI settlement/publication dates, lock-up expiry for recent IPOs (IPO Watch already knows the dates).

---

## 5. Is the OWNERSHIP-tab / ER-tab split right?

Yes — every product surveyed has both units of analysis, and per-security belongs inside the workflow where a security is already on screen (ER), while the market-wide view has to hand you a ticker you were not thinking of. Two corrections:

1. The market-wide screen must be **keyed on the three indicators** (insider buys, short-constrained, 13F movers), not on firms. A firm is a drill, not a home.
2. The ER tab and the OWNERSHIP right pane must **not be the same widget**. The right pane goes away; a stock row on OWNERSHIP opens ER. One per-stock surface, full width, no "Back to Capital World Investors".

---

## 6. Questions that were not asked

1. **What is the date on this number?** The single biggest UX gap. Three sources, three clocks (13F quarter + filed date; SI settlement + publication; Form 4 trade + filed). Today the index quarter and the vendor quarter differ and the screen mentions it in a tooltip.
2. **Which denominator?** Yahoo's `heldPercentInstitutions` is institutions ÷ float in practice, and summing it with insiders ÷ shares-out produced "118 %" in an earlier build. Show both, label both.
3. **Who counts as a holder?** 37 filers hold 56.7 % of all 13F value; BlackRock alone files 49,751 positions. Any count across "all filers" is a count of index complexes. The classification tier (broad / focused / named passive) has to be a first-class column, and every aggregate has to say which tier it was computed over.
4. **What is the user going to *do* with it?** Sizing (float, concentration, DTC → how violently it trades), a catalyst calendar (filing dates, lock-ups), and idea generation (insider cluster scan). Not "buy what Berkshire bought". The screens are organised around those three uses.
5. **Where does the scope end?** 13F is US-listed long equity only; Form 4 is US issuers; FINRA is US consolidated tape. Non-US symbols in ER should get the yfinance holder table and a one-line "no filing-level data outside the US" rather than empty panels. (`akshare_stocks_holders.py` exists for China A-shares and is unused — leave it for a later scope decision.)
6. **What about retail?** A residual. Show "other / retail ≈ 100 − institutions − insiders" as a plug, labelled, or not at all.
7. **Does the insider signal survive our own data?** `form4.sqlite` plus the price history is enough to compute the OpenInsider forward-return columns for every filing we ingest. Before "cluster buy" is promoted from a flag to an alert, run that table for 12 months and look — the literature says the VW alpha is zero and the decay is < 1 month; the app should know whether that holds on its universe. (Same discipline as the earnings-move backtest.)
8. **What does it cost to keep fresh?** 13F bulk: quarterly, 100 MB. FINRA SI: bi-monthly, one file. Form 4 daily index: ~1 request/day plus one per filing. Short volume: one file/day. All free, all cacheable; none should be behind a button the user has to remember.
9. **Options in 13F.** A put is bearish. Already excluded from weights — keep the "264 hold options only" note but as a footnote, not a headline.
10. **Liability.** No retail product scores, plausibly because a score implies a recommendation. The three flags are risk/context statements with their rule attached; there is no buy/sell verdict anywhere.

---

## 7. Defects found in the current build (live, AAPL, 2026-09-05)

1. **"Concentrated register 93 %"** is computed over the 80-row, weight-sorted slice the holders query returns, while the basis text says "measured across every 13F filer". The true top-5 share across all 6,004 filers is **39.1 %** (26.4 % excluding index books). A wrong number, ranked Elevated, on both screens.
2. **"Highest conviction"** list = JOSH ARNOLD INVESTMENT CONSULTANT 67.5 %, 9823 Capital 58.6 %, Prospect Hill 57.2 % — micro-RIAs with Apple as most of a tiny book. Weight-sort without a size floor.
3. **Demand scatter** — 5,255 points, ten overlapping 8-px callouts, four counters, a curve and a five-zone scale in ~300 px. The only readable line is the text one above it.
4. **Insider table on the OWNERSHIP right pane** is ~240 px wide: only Date and Insider are visible; Action, Shares, Value, Pattern are clipped — the columns the code comments say were the point.
5. **"No Form 4 filings in the window."** rendered as a fact while the fetch was still loading.
6. **Half the OWNERSHIP screen is dead** once a stock is open: the left INSIDERS tab says "Nothing scanned yet" and stays there.
7. Grants, exercises and withholding interleaved with the four real sales; "routine" on sales and "—" on everything else.
8. Six coined terms on one tab (register, read-through, conviction, discretionary, smart money, demand).

Items 1, 2, 5 must not survive into phase 1 under any layout.

---

## 8. What is kept, deleted, built

**Kept (with edits):** `form13f.sqlite` + `sec_13f_bulk.py` (the real asset — complete universe, local, fast; `holders`, `book`, `top_firms` stay; `demand` is reduced to counts), `sec_ownership_data.py` (Form 4 + 13D/G parser; add `aff10b5One`, `isTenPercentOwner`, role flags), `finra_short_volume.py`, `EventTimeline` (insider timeline), `FirmDetailPanel` table, the `OwnershipTypes.h` optional-field discipline, the `test_ownership_signals` idea (rewritten for the three flags).

**Deleted:** `DemandQuadrant`, `SmartMoneyPanel`, `ReadThroughStrip`, `HoldersChart::RankedBarChart` (bars for three percentages), `FirmBookPanel` (ranked list), `InsiderLeadersPanel` (replaced by the ingest-backed scan), `OwnershipSignals.h` (9 rules → 3), the `Manager`/`DemandPoint`/`InstitutionalDemand`/`ShortVolume` percentile machinery, the OWNERSHIP screen's detail stack and back button. Net: roughly −4,500 lines of UI.

**Built:**
1. `finra_short_interest.py` — consolidated SI per settlement date, all symbols, into `short_interest.sqlite`; `history {symbol}` and `rank {quarter}` actions.
2. Form 4 daily ingest — EDGAR daily index → `form4.sqlite`, scheduled with the app's existing background jobs; `recent {days, min_insiders, min_value}` action.
3. Holder classification — `books.stock_count` tier + a 10-name passive list; a `type` column on every holders row.
4. Forward returns on Form 4 rows — from the price cache; 1w / 1m / 3m.
5. Three widgets: `OwnershipHeader`, `HoldersTable`, `ShortInterestPanel`; `InsidersPanel` is the current timeline + a rewritten table.
6. OWNERSHIP screen: three scan tables + filer search + filer drill.

---

## 9. Order of work

| Phase | Scope | Result |
|---|---|---|
| **1 — per-stock** | ER › Ownership: header + HOLDERS + INSIDERS + SHORT INTEREST; FINRA SI history; Form 4 flags; deletions | Every defect in §7 gone; one per-stock surface; −4,500 LOC |
| **2 — market-wide** | Daily Form 4 ingest; INSIDER BUYS / SHORT-CONSTRAINED / 13F MOVERS scans; filer search + drill; delete the old landing tabs | OWNERSHIP screen answers "where is it happening" without a button press |
| **3 — integration** | Watchlist/screener columns; portfolio alerts on Form 4 buys; calendar entries; 12-month forward-return check on our own Form 4 data before any flag becomes an alert | Ownership reaches the reader where they already are |

### Phase 3 notes (2026-09-07)

- **Watchlist columns** (`WatchlistScreen`): 13F HOLDERS, Δ HOLDERS, SI ÷ 13F, DAYS TO COVER, INSIDER BUYS 30D, from `ownership_watch.py rows` — the three local stores joined per symbol (the 13F figure resolves the same single CUSIP the stock tab uses, so the two cannot disagree). Milliseconds; FINRA's index of settlement dates is asked at most once a day. Header tooltips carry the three dates. Holder and short figures are dashes where the stores have never seen the symbol; the insider count appears (possibly 0) only where the Form 4 store has read a day inside the window.
- **Alerts**: when a Form 4 scan cycle ends with new filings read, `OwnershipService::check_holdings_alerts` asks the store for scorable buys on portfolio + watchlist symbols in the last 7 days, stamps the newest filing date and trade count per issuer in `data/ownership_alerts.json`, and sends one `NotifTrigger::OwnershipAlert` notification per new filing (the notification service owns the in-app toast and the user's switches). Settings › Notifications › "Insider Buys on Holdings", default on — a never-saved setting is the default.
- **Calendar**: `OwnershipCalendarWidget` (dashboard, Research) — 13F due dates (45 days after quarter end, moved off weekends), FINRA publication dates (10 business days after each settlement, including a past settlement whose publication is still ahead), IPO lock-up expiries (180 days after pricing from Nasdaq's calendar, months cached — a closed month never changes; SPACs skipped because their lock-up runs to the business combination). Lock-up rows open the stock in ER. Left for later: `PreIpoService` carries Finnhub's per-deal lock-up dates behind a key; the 180-day convention should defer to those when present.
- **The check**: `sec_form4_bulk.py` ingests SEC's quarterly Insider Transactions Data Sets (five quarters, originals only, 18 s) and evaluates forward returns over SPY from the *filing* date, with clusters marked point in time. Result: cluster buys +1.8 % median excess at one month, 59 % hit rate; single buys +1.2 % / 55 %; 10 %-owner and plan buys negative on the median — the alert is justified and both exclusions are right. The first draft of the note overstated this by about half (trade-date anchor, hindsight clusters, amendments admitted, a mislabelled tercile); review caught it and the numbers were regenerated. Details and caveats in the research note.

### Implementation notes (2026-09-06)

Found while building, all fixed in the same change:

- The daemon socket was one fixed path for every instance; a second instance (another profile, a duplicate) deleted the first one's socket on start and on exit. Now `runtime/yfinance-<pid>.sock`, with stale files swept only when their owner pid is gone.
- `batch_closes` for a single symbol returned nothing: current yfinance returns `(Ticker, Price)` columns even for one ticker, so `data["Close"]` raised. The forward-return columns depend on it.
- A Qt stylesheet `QTableWidget::item { color: … }` overrides every per-cell foreground; the green/red/amber cells silently rendered white.
- The Form 4 scan held one write transaction per day (minutes); anything else touching the store timed out with "database is locked". It now commits every 25 filings, reports a lock instead of raising, stops at a day boundary if its parent process is gone, and the service retries in 15 minutes rather than 6 hours.
- Leveraged ETFs top any SI ÷ 13F ranking by construction; funds are excluded by issuer-name keywords (stated in the footer) and the ranking has a 50-holder floor.
- Aggregates over "every filer" must include blank-CIK filers (they hold real shares); only the cross-quarter comparison needs the CIK.

Each phase: build → `/review` → fix → ctest (`test_ownership_signals` rewritten in phase 1) → commit.
