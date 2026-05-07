# POWER TRADER Tab — Congressional & Political Insider Trading

**Status:** Planning
**Priority:** 2 (after KNOWLEDGE, before PRE-IPO)
**Estimated effort:** 5–7 weeks
**Screen ID:** `power_trader`

---

## Background & Market Context

Under the **STOCK Act (Stop Trading on Congressional Knowledge Act, 2012)**, all members of Congress — 535 senators and representatives — are legally required to disclose stock trades within **45 days** of execution. These disclosures are public record.

Academic research and platforms like **Quiver Quantitative**, **Capitol Trades**, **UnusualWhales**, **Senate Stock Watcher**, and **House Stock Watcher** have documented statistically significant alpha in congressional portfolios — particularly from members on relevant oversight committees. A 2004 study found US senators outperformed the market by ~12% annually. The data is:

- **Legally mandated** to be public
- **Freely downloadable** from official government websites
- **Deeply underutilized** by mainstream platforms
- **Inherently viral** — high public interest, political salience

**Key signal logic:** A senator on the Armed Services Committee buying Raytheon 10 days before a defense budget vote is not random. The POWER TRADER tab makes these correlations visible.

---

## Goals

1. Display all congressional trade disclosures in a clean, searchable, filterable table
2. Reconstruct estimated portfolios for each member and track P&L vs. benchmark
3. Surface "alpha signals" — trades with statistically suspicious timing relative to legislation, committee work, or earnings
4. Cross-reference trades with committee assignments, campaign donors, and sector exposure
5. Build a "mirror trade" portfolio simulator — what would happen if you followed a given member's trades?

---

## Legal & Ethical Framework

- All data sourced from **official government disclosures** (Senate eFTS, House disclosures portal)
- All analysis is **purely observational** — no accusations, just pattern surfacing
- Disclosures are **already public** — this is aggregation, not leaking
- UI language will be factual: "filed disclosure" not "caught trading"
- Precedent: this exact functionality is live on Quiver Quant, Capitol Trades, UnusualWhales — well-established legal ground

---

## Data Sources

### Primary: Official Government Sources (Free)

| Source | Data | URL / Access |
|---|---|---|
| **Senate eFTS (Electronic Filing System)** | Machine-readable XML of all Senate financial disclosures | `https://efts.senate.gov/` |
| **Senate PTR (Periodic Transaction Reports)** | Trade-by-trade XML feed — the main data source | `https://efts.senate.gov/LATEST/search-index?q=ptr` |
| **House Financial Disclosures Portal** | House member PDFs + newer structured data | `https://disclosures.house.gov/` |
| **House PTR Data** | Newer filings available as XML; older as PDF requiring OCR | House portal |
| **ProPublica Congress API** | Member names, party, state, district, committee assignments, bio | `https://projects.propublica.org/api-docs/congress-api/` |
| **FEC (Federal Election Commission)** | Campaign finance — donor lists, PAC contributions | `https://api.open.fec.gov/` |
| **OpenSecrets API** | Industry contributions, lobbying, revolving door | `https://www.opensecrets.org/api` |
| **GovInfo API** | Legislation text, bill status, congressional record | `https://api.govinfo.gov/` |
| **Congress.gov API** | Bill cosponsors, committee hearings, vote records | `https://api.congress.gov/` |

### Secondary: Aggregated / Enriched Sources (Paid)

| Source | Data | Cost |
|---|---|---|
| **Quiver Quantitative API** | Cleaned, normalized congressional trades + backtested returns | ~$50/mo |
| **Capitol Trades API** | Parsed trades with company metadata and signal scores | Free tier + paid |
| **UnusualWhales** | Community-verified trade data + narrative summaries | Subscription |

### Existing Scripts (Already in Repo)

```
fincept-qt/scripts/congress_gov_data.py     — Congress.gov API integration (already exists)
fincept-qt/scripts/open_secrets_data.py     — OpenSecrets API integration (already exists)
fincept-qt/scripts/govinfo_data.py          — GovInfo federal data (already exists)
fincept-qt/scripts/sec_data.py              — SEC filings (for any 13F cross-reference)
```

### New Scripts to Create

```
fincept-qt/scripts/senate_disclosures_data.py   — Senate eFTS XML parser + PTR downloader
fincept-qt/scripts/house_disclosures_data.py    — House disclosure portal parser
fincept-qt/scripts/fec_data.py                  — FEC campaign finance API
fincept-qt/scripts/congress_members_data.py     — Member roster, committees, bio data
fincept-qt/scripts/quiver_congress_data.py      — Quiver Quant API (if subscribed)
fincept-qt/scripts/power_trader_signals.py      — Signal scoring: timing vs. legislation/earnings
```

---

## Data Models

### `PowerTraderTypes.h`

```cpp
enum class TradeDirection { Buy, Sell, Exchange };
enum class AssetType { Stock, Bond, Option, ETF, MutualFund, Crypto, RealEstate, Other };
enum class MemberChamber { Senate, House };

struct CongressMember {
    QString id;                      // slug, e.g., "nancy-pelosi"
    QString full_name;
    QString party;                   // "D", "R", "I"
    MemberChamber chamber;
    QString state;
    QString district;                // House only
    QString photo_url;
    QStringList committees;          // e.g., ["Armed Services", "Intelligence"]
    QStringList subcommittees;
    QDate term_start;
    QDate term_end;
    double estimated_net_worth;      // from disclosure annual reports, millions
    int trade_count_ytd;
    double portfolio_return_ytd;     // estimated from PTRs
    double spy_return_ytd;           // for benchmark comparison
    double alpha_ytd;                // portfolio_return - spy_return
    QString opensecrets_id;          // for campaign finance cross-reference
    bool watched;
};

struct PoliticalTrade {
    QString id;                      // unique disclosure ID
    QString member_id;
    QString member_name;
    QString party;
    MemberChamber chamber;
    QDate transaction_date;
    QDate disclosure_date;
    int disclosure_lag_days;         // transaction_date → disclosure_date
    QString ticker;                  // if equity; blank for private
    QString asset_name;              // full name, e.g., "Apple Inc. Common Stock"
    AssetType asset_type;
    TradeDirection direction;
    double amount_low;               // filing gives ranges, e.g., $15,001
    double amount_high;              // e.g., $50,000
    QString amount_range_label;      // "$15,001 – $50,000"
    QString committee_relevance;     // derived: which committee overlaps with this sector
    double price_at_trade;           // fetched from market history
    double price_at_disclosure;      // fetched from market history
    double return_at_disclosure_pct; // (price_at_disclosure - price_at_trade) / price_at_trade
    QString legislation_context;     // if a bill vote occurred within 60 days
    double signal_score;             // 0–100: timing anomaly score
    QString notes;
    QString source_url;
};

struct CampaignContribution {
    QString member_id;
    QString contributor_name;
    QString contributor_type;        // "individual", "PAC", "corporate"
    QString industry;
    double amount;
    QDate date;
    QString cycle;                   // "2024", "2022"
};

struct LegislationEvent {
    QString bill_id;
    QString title;
    QDate vote_date;
    QStringList related_sectors;     // ["defense", "tech", "pharma"]
    QStringList related_tickers;     // derived from sector mapping
    QString congress_member_id;      // sponsoring or committee member
    QString outcome;                 // "passed", "failed", "committee"
};

struct PowerTraderSignal {
    QString member_id;
    QString member_name;
    QString ticker;
    double signal_score;             // 0–100
    QString signal_type;             // "committee_overlap", "pre_vote", "cluster", "disclosure_lag"
    QString description;             // human-readable explanation
    QDate generated_at;
    QVector<QString> trade_ids;      // supporting trades
    QVector<QString> legislation_ids;
};
```

---

## Screen Layout & UX Design

### Top-level layout: 3-column + signal strip

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  POWER TRADER          [Search member or ticker...]   [Filters ▼]  [★ Only] │
├────────────────┬────────────────────────────────────┬────────────────────────┤
│  LEADERBOARD   │  TRADES FEED                       │  MEMBER DETAIL         │
│                │                                    │                        │
│  See below ▸   │  See below ▸                       │  See below ▸           │
│                │                                    │                        │
├────────────────┴────────────────────────────────────┴────────────────────────┤
│  ALPHA SIGNALS  ──────────────────────────────────────────────────────────── │
│  [Signal strip — see below]                                                  │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Leaderboard Panel (left column)

Ranked table of members with summary stats:

```
┌────────────────────────────────────────────────────┐
│  LEADERBOARD         [Sort: Alpha ▼]  [All ▼ Party]│
├──┬──────────────────┬──┬────────┬───────┬──────────┤
│# │ Name             │P │ Return │ Alpha │ Trades   │
├──┼──────────────────┼──┼────────┼───────┼──────────┤
│1 │ Pelosi, N.       │D │ +34%   │ +18%  │ 14 YTD   │
│2 │ Tuberville, T.   │R │ +28%   │ +12%  │ 8 YTD    │
│3 │ Collins, S.      │R │ +22%   │ +6%   │ 22 YTD   │
│4 │ Ossoff, J.       │D │ +19%   │ +3%   │ 5 YTD    │
│5 │ Johnson, R.      │R │ +18%   │ +2%   │ 11 YTD   │
│  │ ...              │  │        │       │          │
├──┴──────────────────┴──┴────────┴───────┴──────────┤
│  [Search member]   [Filter: Senate | House | All]  │
└────────────────────────────────────────────────────┘
```

Click any member → loads Member Detail panel.

**Sort options:** Alpha YTD, Total Return, Trade Count, Portfolio Size, Disclosure Lag

### Trades Feed Panel (center column)

Chronological feed of all recent trade disclosures:

```
┌───────────────────────────────────────────────────────────────────┐
│  TRADES FEED                [Date ▼] [All Tickers] [All Members]  │
├──────────┬─────────────────┬───────┬───────┬───────┬─────────────┤
│ Disclosed│ Member          │ Ticker│ B/S   │Amount │ Lag (days)  │
├──────────┼─────────────────┼───────┼───────┼───────┼─────────────┤
│ 05/05    │ Tuberville (R)  │ NVDA  │ BUY   │$50K–  │ 12d lag     │
│ 05/04    │ Pelosi (D)      │ MSFT  │ SELL  │$100K+ │ 44d lag ⚠  │
│05/03     │ Collins (R)     │ AAPL  │ BUY   │$15K+  │ 7d lag      │
│ 05/02    │ Ossoff (D)      │ AMZN  │ BUY   │$50K+  │ 31d lag     │
│ 04/30    │ Johnson (R)     │ RTX   │ BUY   │$15K+  │ 15d lag 🔔  │
│          │                 │       │       │(RTX=  │ Armed Svc   │
│          │                 │       │       │Defense│ Committee)  │
├──────────┴─────────────────┴───────┴───────┴───────┴─────────────┤
│  Filters: Party  Chamber  Committee  Ticker  Sector  Date range   │
│           Min amount  Signal score >__  Disclosure lag >__ days   │
└───────────────────────────────────────────────────────────────────┘
```

**Inline signals:** 
- ⚠ = disclosure filed at 44/45 days (maximum lag, possible intentional)
- 🔔 = committee relevance detected (member's committee overlaps with stock's sector)
- ★ = unusually large trade size for this member

Click any row → expands to show: legislation context, price chart with trade date marked, committee overlap explanation, campaign donor cross-reference.

### Member Detail Panel (right column)

```
┌─────────────────────────────────────────────────────────────────┐
│  Nancy Pelosi (D)  · House · California 11th                    │
│  [Photo]  Net Worth: ~$120M–$170M est.  ★ Watching              │
├─────────────────────────────────────────────────────────────────┤
│  PERFORMANCE                                                      │
│  YTD Return: +34%   SPY: +16%   Alpha: +18%                     │
│  All-time (since STOCK Act): +320%   SPY same period: +210%     │
│  [Mini equity curve chart — portfolio vs SPY]                    │
├─────────────────────────────────────────────────────────────────┤
│  COMMITTEES                                                       │
│  House Minority Leader (no committee assignment in this role)    │
│  Former: Appropriations, Intelligence, Rules                     │
├─────────────────────────────────────────────────────────────────┤
│  DISCLOSED HOLDINGS                                               │
│  MSFT     $1M–$5M    (options)                                   │
│  NVDA     $500K–$1M  (stock)                                     │
│  AAPL     $250K–$500K                                            │
│  SPY ETF  $1M–$5M                                                │
│  TSLA     $100K–$250K                                            │
│  ...      (reconstructed from PTR filings)                       │
├─────────────────────────────────────────────────────────────────┤
│  RECENT TRADES                                                    │
│  05/01  MSFT SELL $100K–$250K  (44d lag ⚠)                     │
│  03/15  NVDA BUY  $100K–$250K                                    │
│  02/10  AAPL BUY  $50K–$100K                                     │
├─────────────────────────────────────────────────────────────────┤
│  TRADE ACTIVITY CHART                                            │
│  [Bar chart: buys vs sells per month, 24 months]                 │
├─────────────────────────────────────────────────────────────────┤
│  TOP SECTORS TRADED                                               │
│  Tech 68%  · Healthcare 15%  · Financials 10%  · Energy 7%     │
├─────────────────────────────────────────────────────────────────┤
│  CAMPAIGN FINANCE (OpenSecrets)                                  │
│  Top industry donors: Tech ($2.1M) · Finance ($1.8M)            │
│  Notable PACs: EMILY's List, DSCC                                │
├─────────────────────────────────────────────────────────────────┤
│  MIRROR PORTFOLIO [Simulate →]                                   │
│  "If you mirrored Pelosi's trades with 45d lag, your return     │
│   since 2020 would be: +127% vs SPY +89%"                       │
└─────────────────────────────────────────────────────────────────┘
```

### Alpha Signals Strip (bottom)

A horizontal scrolling signal ticker:

```
🔔 CLUSTER: 4 senators from Intelligence Committee bought PLTR in last 10 days
⚠  TIMING: Johnson bought RTX 8 days before Armed Services vote on NDAA ($500B)
📊 TREND: NVDA most bought by Congress this month (7 members, $2.1M total)
🕐 LAG: Pelosi filed 44 days late on $250K+ MSFT sell — maximum allowed lag
🔁 MIRROR: Following Tuberville YTD: +28% vs SPY +16%
```

---

## C++ Implementation Plan

### New files to create

```
fincept-qt/src/screens/power_trader/
├── PowerTraderScreen.h/.cpp         — Main screen, layout manager
├── PowerTraderTypes.h               — All data structs (see above)
├── LeaderboardPanel.h/.cpp          — Member ranking table
├── TradesFeedPanel.h/.cpp           — Chronological trade disclosure feed
├── MemberDetailPanel.h/.cpp         — Per-member portfolio/performance view
├── SignalStrip.h/.cpp               — Bottom alpha signals horizontal scroller
├── MirrorPortfolioPanel.h/.cpp      — "Follow this member" backtest simulator
├── LegislationCorrelator.h/.cpp     — Correlates trade dates with bill votes/hearings
├── CommitteeOverlapEngine.h/.cpp    — Tags trades with committee relevance
└── SignalScorer.h/.cpp              — Computes signal_score for each trade
```

New service:
```
fincept-qt/src/services/power_trader/
├── PowerTraderService.h/.cpp        — Data orchestrator, cache manager
├── DisclosureParser.h/.cpp          — Parses Senate XML / House HTML disclosures
└── PowerTraderDataHub.h/.cpp        — DataHub producer for power_trader: topics
```

---

## Signal Scoring Logic

Each `PoliticalTrade` receives a `signal_score` (0–100) computed by `SignalScorer`:

### Scoring factors

| Factor | Max points | Logic |
|---|---|---|
| **Committee overlap** | +30 | Member's committee has oversight of the stock's sector |
| **Pre-vote timing** | +25 | Trade occurred within 30 days before a related bill vote |
| **Disclosure lag** | +15 | Lag > 30 days gets +5; > 40 days gets +15 |
| **Trade size** | +10 | >$250K gets +5; >$1M gets +10 (relative to member's typical) |
| **Cluster signal** | +10 | 3+ members from same committee buy same sector in 14-day window |
| **Profitable** | +10 | Trade was profitable at disclosure date (price moved >5% in their favor) |

### Committee → Sector mappings

```
Armed Services, Veterans Affairs → Defense (LMT, RTX, NOC, BA, GD)
Finance, Banking → Financials (JPM, BAC, GS, MS, BRK)
Intelligence (SSCI, HPSCI) → Cybersecurity, Defense (PLTR, CRWD, PANW)
Commerce, Science → Tech, Telecom (AAPL, MSFT, AMZN, GOOG, VZ)
Energy, Natural Resources → Energy, Mining (XOM, CVX, COP, FCX)
Health, Education, Labor → Healthcare (JNJ, UNH, PFE, MRNA)
Agriculture → Ag commodities, Food (ADM, BG, MOS)
Foreign Relations → International stocks, defense
Judiciary → Legal/regulatory-sensitive sectors
```

---

## Python Scripts

### `senate_disclosures_data.py`

```python
# Senate eFTS API — machine-readable XML
# https://efts.senate.gov/LATEST/search-index?q=ptr&dateRange=custom
# Parses Periodic Transaction Reports (PTRs) for all senators
# Data: senator name, transaction date, disclosure date, ticker, direction, amount range
# Update cadence: daily

import requests, xml.etree.ElementTree as ET
from datetime import date, timedelta

EFTS_BASE = "https://efts.senate.gov/LATEST/search-index"

def fetch_senate_ptrs(days_back=7):
    params = {
        "q": "ptr",
        "dateRange": "custom",
        "startdt": (date.today() - timedelta(days=days_back)).isoformat(),
        "enddt": date.today().isoformat(),
    }
    r = requests.get(EFTS_BASE, params=params, timeout=15)
    return parse_ptr_results(r.json())
```

### `house_disclosures_data.py`

```python
# House disclosures portal: https://disclosures.house.gov/
# Newer filings available as structured data; older require PDF parsing
# Annual data files available as ZIP downloads at:
# https://disclosures.house.gov/public_disc/financial-pdfs/[YEAR]FD.zip
```

### `fec_data.py`

```python
# FEC API: https://api.open.fec.gov/v1/
# Fetches: candidate financials, committee totals, industry contributions
# Cross-reference: for each member, what industries fund their campaigns?
```

### `power_trader_signals.py`

```python
# Signal scoring engine
# For each trade:
# 1. Fetch member's committee assignments (ProPublica)
# 2. Map stock ticker to sector (yfinance sector field)
# 3. Check committee-sector overlap
# 4. Fetch legislation events from congress_gov_data.py within ±60 days of trade
# 5. Compute cluster: query trades DB for same sector by same-committee members
# 6. Compute disclosure lag
# 7. Fetch price at trade date and at disclosure date (yfinance history)
# 8. Return signal_score
```

---

## Phase Plan

### Phase 1 — MVP Data Layer (Weeks 1–2)

- [ ] Define `PowerTraderTypes.h` complete struct set
- [ ] Create `senate_disclosures_data.py` — poll Senate eFTS XML
- [ ] Create `congress_members_data.py` — member roster via ProPublica
- [ ] Create `PowerTraderService` skeleton with caching
- [ ] Parse and store ~2 years of Senate PTR history as local cache
- [ ] Create `TradesFeedPanel` — sortable/filterable trades table
- [ ] Create `LeaderboardPanel` — basic member ranking (trade count, estimated size)
- [ ] Register screen as `power_trader`, add to FKeyBar

**Deliverable:** Working screen with real Senate trade data, filterable feed, basic leaderboard.

### Phase 2 — Member Detail + House Data (Weeks 3–4)

- [ ] Create `house_disclosures_data.py` — House disclosure parser
- [ ] Combine Senate + House data into unified feed
- [ ] Build `MemberDetailPanel` — portfolio reconstruction from PTR history
- [ ] Build performance tracking: reconstruct portfolio P&L vs SPY from trade history
- [ ] Integrate OpenSecrets campaign finance data
- [ ] Build committee assignment tagging for each trade
- [ ] Add `CommitteeOverlapEngine` — flag committee-sector overlaps in feed
- [ ] Add disclosure lag warnings (⚠ indicator for >30/44 day lags)

**Deliverable:** Full Senate + House data, member portfolios reconstructed, committee overlap flagging.

### Phase 3 — Signals & Intelligence (Weeks 5–6)

- [ ] Build `SignalScorer` — compute 0–100 score per trade
- [ ] Build `LegislationCorrelator` — cross-reference with Congress.gov bill votes
- [ ] Build `SignalStrip` — bottom horizontal alpha signal scroller
- [ ] Build cluster detection — alert when 3+ committee peers buy same sector
- [ ] Build `MirrorPortfolioPanel` — backtest simulator for following a member
- [ ] Create `fec_data.py` — campaign finance cross-reference
- [ ] Add campaign donor overlap: trades in industries that fund their campaigns
- [ ] AI narrative generator: per-member summary of trading patterns

### Phase 4 — Polish & Advanced Features (Week 7)

- [ ] Ticker watchlist integration — alert when any member trades your watchlist stocks
- [ ] Member watch alerts — push notification when a watched member files a new disclosure
- [ ] Export: download trade history as CSV
- [ ] "Most traded" weekly digest widget for DASHBOARD
- [ ] Link to PRE-IPO tab: members who hold private equity positions
- [ ] Search: find all members who have ever traded a given ticker

---

## Navigation & Integration

**Screen registration:**
```cpp
dock_router_->register_factory("power_trader", [this]() {
    auto* screen = new power_trader::PowerTraderScreen;
    connect(screen, &power_trader::PowerTraderScreen::navigate_to_screen, this,
            [this](const QString& id, const QString& ticker) {
                if (!ticker.isEmpty())
                    SymbolContext::instance().set_group_symbol(
                        SymbolGroup::A, SymbolRef::equity(ticker), nullptr);
                dock_router_->navigate(id);
            });
    return screen;
});
```

**FKeyBar position:** After PORTFOLIO, before NEWS. This gives it prominence as a research/intelligence tool.

**Cross-screen integrations:**
- From MARKETS stock detail: "Congress trades in [TICKER] →" tooltip/button linking to filtered POWER TRADER view for that ticker
- From PORTFOLIO: "Congress members who own what you own" badge
- From NEWS: When news article is about a senator, link to their POWER TRADER profile
- From PRE-IPO: "Congress members with PE exposure" cross-reference
- From KNOWLEDGE: New playbook `Understanding Congressional Disclosures` links to this screen

---

## Data Freshness Strategy

| Data type | Update cadence | Method |
|---|---|---|
| Senate PTR filings | Every 6 hours | Poll Senate eFTS API |
| House disclosures | Every 6 hours | Poll House portal |
| Member roster | Weekly | ProPublica Congress API |
| Committee assignments | Weekly | ProPublica Congress API |
| Campaign finance | Monthly | OpenSecrets API |
| Legislation events | Daily | Congress.gov API |
| Price at trade date | On demand (cached) | yfinance historical |
| Signal scores | Recomputed on new trade ingestion | `power_trader_signals.py` |

---

## Success Metrics

- 535 members tracked (full House + Senate)
- 2+ years of trade history loaded at launch
- Trade disclosure latency <6 hours from Senate eFTS publication
- Signal scores computed for 100% of trades
- Committee overlap detection for all major sectors
- Mirror portfolio backtest available for any member with >10 trades
- Ticker cross-reference: searchable by any ticker symbol
- Campaign finance cross-reference for 100% of House + Senate members
