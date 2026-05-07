# PRE-IPO / Private Equity Tab — Implementation Plan

**Status:** Planning
**Priority:** 3 (after KNOWLEDGE and POWER TRADER)
**Estimated effort:** 6–9 weeks
**Screen ID:** `pre_ipo`

---

## Background & Market Landscape

Pre-IPO and private equity data is the last frontier of transparent market data. Platforms like **Forge Global**, **Hiive**, **Nasdaq Private Market (NPM)**, **EquityZen**, **Carta**, and **SharesPost** have proven user demand. The asymmetry of information between institutional and retail investors is widest in private markets.

Key insight: a significant amount of the foundational data is **free and public** via SEC filings. The premium value-add comes from aggregation, normalization, and secondary market price feeds (which require paid data relationships). This plan takes a phased approach — MVP on free data, premium features via partnerships or paid APIs.

---

## Goals

1. Track valuations, funding rounds, and company status for ~500 known pre-IPO / unicorn companies
2. Monitor SEC filings (Form D, S-1, F-1) for deal flow signals and IPO pipeline
3. Surface secondary market price indications where available
4. Cross-reference pre-IPO companies with investor portfolios and congressional disclosures
5. Provide comps-based valuation context when an S-1 is filed

---

## Data Architecture

### Free / Public Data Sources

| Source | Data | Script / Endpoint |
|---|---|---|
| **SEC EDGAR Form D** | All Reg D private placements — company, amount, exemption type, investors listed | `https://efts.sec.gov/LATEST/search-index?q=%22Form+D%22&dateRange=custom` |
| **SEC EDGAR S-1 / F-1 Monitor** | IPO filings — detect the moment a company files to go public | `https://efts.sec.gov/LATEST/search-index?forms=S-1,F-1` |
| **SEC EDGAR 13D/G** | Large shareholder disclosures; PE firms must disclose >5% holdings | EDGAR full-text search |
| **ProPublica Congress API** | Free; helpful for congressional holding cross-reference | `https://projects.propublica.org/api-docs/congress-api/` |
| **Wikipedia / public lists** | Unicorn companies list; CB Insights publishes this annually | Scrape + manual curation |
| **Y Combinator batch lists** | YC companies with founding dates and domains | `https://www.ycombinator.com/companies` (scrape) |
| **LinkedIn Jobs (proxy)** | Hiring velocity as a growth signal — # of job postings | LinkedIn scrape (respect ToS) |
| **CrunchBase Oddsmaker** | Limited free tier: company existence, founding date, category | Free API key |

### Paid / Partnership Data Sources (Phase 2–3)

| Source | Data | Cost tier |
|---|---|---|
| **Crunchbase Enterprise** | Full funding rounds, valuations, investor names, revenue estimates | ~$5K+/yr |
| **PitchBook** | Gold standard: valuations, cap tables, deal terms, investor returns | Enterprise, ~$20K+/yr |
| **Forge Global API** | Secondary market trade prices and bid/ask for private shares | Partnership required |
| **Hiive API / scrape** | Secondary liquidity indications, bid/ask | Partnership or scrape |
| **EquityZen** | Deals, company profiles, indicative values | Scrape or partnership |
| **CB Insights Unicorn Tracker** | Curated $1B+ company list with latest valuation | Paid subscription |
| **Dealroom** | European-focused private market data | Paid API |

### Data Script Plan

New scripts to create under `fincept-qt/scripts/`:

```
sec_form_d_data.py        — Poll EDGAR for new Form D filings; parse XML
sec_ipo_pipeline_data.py  — Poll EDGAR for S-1/F-1 filings; extract prospectus metadata
crunchbase_data.py        — Fetch company profiles, rounds, investors (free tier + paid)
forge_data.py             — Secondary market price feed (when partnership established)
hiive_data.py             — Indicative bids/asks from Hiive (scrape or API)
yc_companies_data.py      — Y Combinator batch company list scraper
unicorn_tracker_data.py   — Aggregated unicorn list from public sources
pre_ipo_comps_data.py     — Pull public comps via MarketDataService when S-1 filed
```

---

## Screen Layout & UX Design

### Top-level structure: 4 sub-panels

```
┌──────────────────────────────────────────────────────────────────────────┐
│  PRE-IPO                          [ Search companies... ]  [Filters ▼]  │
├──────────────┬────────────────────────────────┬────────────────────────-─┤
│  WATCHLIST   │  COMPANY DETAIL                │  IPO PIPELINE            │
│              │                                │                          │
│  ★ Stripe    │  ▸ See detail layout below     │  See pipeline below      │
│  ★ Klarna    │                                │                          │
│    Databricks│                                │                          │
│    OpenAI    │                                │                          │
│    SpaceX    │                                │                          │
│    Chime     │                                │                          │
│    Plaid     │                                │                          │
│    Revolut   │                                │                          │
│    Epic Games│                                │                          │
│    Discord   │                                │                          │
│    ...       │                                │                          │
├──────────────┴────────────────────────────────┴──────────────────────────┤
│  DEAL FLOW FEED                                                           │
│  New Form D filings · S-1 filings · Round close announcements            │
└──────────────────────────────────────────────────────────────────────────┘
```

### Company Detail Panel

```
┌─────────────────────────────────────────────────────────────────────┐
│  STRIPE                                      [★ Watch] [→ Markets]  │
│  Fintech · Payments Infrastructure · Founded 2010 · San Francisco   │
├─────────────────────────────────────────────────────────────────────┤
│  VALUATION                    │  SECONDARY MARKET                   │
│  Last round: $70B (Series I)  │  Forge: $28.50/share ▼ –12% (90d)  │
│  Date: Mar 2023               │  Hiive: $27.80 bid / $29.20 ask     │
│  Amount raised: $6.5B         │  Implied valuation: $63B            │
│  Lead investor: Sequoia, A16Z │  Premium to last round: –10%        │
├─────────────────────────────────────────────────────────────────────┤
│  FUNDING HISTORY                                                     │
│  [Timeline chart: Seed → A → B → ... → Series I]                   │
│  Each node: date, amount, lead investor, implied valuation           │
├─────────────────────────────────────────────────────────────────────┤
│  FINANCIALS (estimated)                                              │
│  Revenue: ~$15B (2024 est)    Revenue multiple: ~4.7x              │
│  Employees: ~8,000            Rev/employee: ~$1.9M                  │
│  Source: CBInsights / media reports                                  │
├─────────────────────────────────────────────────────────────────────┤
│  IPO STATUS                                                          │
│  S-1 Filed: Not yet           IPO Window: 2025–2026 (rumored)       │
│  Lock-up expiry: N/A          Underwriters: TBD                      │
├─────────────────────────────────────────────────────────────────────┤
│  PUBLIC COMPS (auto-generated when S-1 filed)                        │
│  Adyen: EV/Rev 12x  · Braintree (private) · Block (SQ): EV/Rev 2x  │
│  Implied range from comps: $45B – $95B                               │
├─────────────────────────────────────────────────────────────────────┤
│  KEY INVESTORS                                                        │
│  Sequoia · Andreessen Horowitz · Tiger Global · General Catalyst     │
│  Baillie Gifford · Fidelity · T. Rowe Price                          │
│  (public investors → click to see their other private holdings)      │
├─────────────────────────────────────────────────────────────────────┤
│  SECONDARY PRICE HISTORY                                             │
│  [Line chart: secondary trades over 24 months]                       │
└─────────────────────────────────────────────────────────────────────┘
```

### IPO Pipeline Panel

```
┌──────────────────────────────────────────────────────────┐
│  IPO PIPELINE                           [Filter: 2025 ▼] │
├──────────────────────────────────────────────────────────┤
│  STATUS    COMPANY       FILED       EXPECTED             │
│  ● Filed   Klarna        2025-03-12  H1 2025             │
│  ● Filed   eToro         2025-04-01  H1 2025             │
│  ◐ Rumored Databricks    —           H2 2025             │
│  ◐ Rumored OpenAI        —           2026                │
│  ○ Watch   SpaceX        —           Unknown             │
│  ○ Watch   Stripe        —           2025–2026           │
├──────────────────────────────────────────────────────────┤
│  RECENT S-1 ACTIVITY (from EDGAR)                        │
│  05/05 · Reddit $1.2B offering (follow-on)               │
│  04/28 · [NewCo] filed S-1: $500M raise                  │
│  04/15 · Klarna amended S-1: updated financials          │
└──────────────────────────────────────────────────────────┘
```

### Deal Flow Feed (bottom strip)

Live feed of new SEC Form D filings:
```
[05/05] TechCo LLC · Series B · $45M · Reg D 506(c) · Lead: Andreessen Horowitz
[05/04] HealthAI Inc · Series A · $12M · Reg D 506(b)
[05/03] QuantFund LP · Hedge Fund · $200M · Reg D 506(b)
```

---

## C++ Implementation Plan

### New files to create

```
fincept-qt/src/screens/pre_ipo/
├── PreIpoScreen.h/.cpp          — Main screen widget, layout, tab/panel management
├── PreIpoTypes.h                — PrivateCompany, FundingRound, SecondaryPrice, IpoFiling structs
├── CompanyListPanel.h/.cpp      — Left watchlist + universe browser
├── CompanyDetailPanel.h/.cpp    — Main detail view (valuation, secondary, comps)
├── IpoPipelinePanel.h/.cpp      — IPO pipeline tracker
├── DealFlowFeed.h/.cpp          — Live Form D / S-1 feed strip
├── FundingTimelineChart.h/.cpp  — Horizontal timeline chart widget
└── CompsEngine.h/.cpp           — Auto-generates public comps table when S-1 filed
```

New service:
```
fincept-qt/src/services/pre_ipo/
├── PreIpoService.h/.cpp         — Data orchestrator; calls Python scripts, caches results
└── PreIpoDataHub.h/.cpp         — DataHub producer for pre_ipo: topic family
```

### Key Data Types (`PreIpoTypes.h`)

```cpp
struct FundingRound {
    QString round_name;         // "Series A", "Series I"
    QDate date;
    double amount_usd;          // in millions
    double implied_valuation;
    QStringList lead_investors;
    QStringList all_investors;
};

struct SecondaryPrice {
    QString source;             // "forge", "hiive", "equityzen"
    double price_per_share;
    double implied_valuation;
    double premium_to_last_round_pct;
    QDateTime as_of;
    double bid;
    double ask;
};

struct PrivateCompany {
    QString id;                  // slug, e.g., "stripe"
    QString name;
    QString sector;
    QString sub_sector;
    QString hq_city;
    QString hq_country;
    QDate founded;
    QString status;              // "pre_ipo", "filed_s1", "ipo_priced", "acquired"
    double last_valuation_usd;   // billions
    QDate last_round_date;
    QString last_round_name;
    QVector<FundingRound> rounds;
    SecondaryPrice secondary;
    double revenue_est_usd;      // millions, estimated
    int employee_count;
    QStringList key_investors;
    QString ipo_status;          // "rumored", "filed", "priced", "listed"
    QDate s1_filed_date;
    QString ipo_expected_window; // "H2 2025", "2026"
    QStringList public_comps;    // ticker symbols
    QStringList tags;
    bool watched;
    QString crunchbase_url;
    QString description;
};

struct FormDFiling {
    QString company_name;
    QDate filed_date;
    double amount_raised;
    QString exemption;           // "506(b)", "506(c)"
    QString offering_type;       // "Equity", "Debt", "Pooled"
    QString state;
    QStringList related_persons;
    QString edgar_url;
};

struct S1Filing {
    QString company_name;
    QDate filed_date;
    double offering_size_usd;
    QStringList underwriters;
    QString edgar_url;
    bool is_amendment;
};
```

### Service API

```cpp
class PreIpoService : public QObject {
    // Company universe
    QVector<PrivateCompany> companies();
    PrivateCompany company(const QString& id);

    // Filing feeds
    QVector<FormDFiling> recent_form_d(int days = 30);
    QVector<S1Filing>    ipo_pipeline();

    // Secondary prices
    SecondaryPrice secondary_price(const QString& company_id);

    // Comps
    QVector<QuoteData> public_comps(const QString& company_id);

    // Signals
signals:
    void new_form_d_filing(const FormDFiling&);
    void s1_filed(const S1Filing&);
    void secondary_price_updated(const QString& company_id, const SecondaryPrice&);
    void company_updated(const QString& id);
};
```

---

## Python Scripts Implementation

### `sec_form_d_data.py`

```python
# Polls SEC EDGAR full-text search for new Form D filings
# Endpoint: https://efts.sec.gov/LATEST/search-index
# Returns: list of FormD objects with company name, amount, date, exemption type
# Update cadence: every 4 hours

import requests, json

def fetch_form_d(days_back=1):
    url = "https://efts.sec.gov/LATEST/search-index"
    params = {
        "q": "\"Form D\"",
        "dateRange": "custom",
        "startdt": (date.today() - timedelta(days=days_back)).isoformat(),
        "forms": "D"
    }
    r = requests.get(url, params=params, timeout=10)
    hits = r.json().get("hits", {}).get("hits", [])
    return [parse_form_d(h) for h in hits]
```

### `sec_ipo_pipeline_data.py`

```python
# Monitors EDGAR for new S-1 and F-1 filings
# Endpoint: https://efts.sec.gov/LATEST/search-index?forms=S-1,F-1
# Also parses the prospectus for offering size, underwriters, use of proceeds
```

### `crunchbase_data.py`

```python
# Crunchbase Basic API (free tier): company profiles
# Crunchbase Enterprise: full funding rounds, valuations
# Falls back to static curated JSON if no API key configured
```

---

## Static Curated Universe (Phase 1 MVP)

While paid data APIs are being procured, maintain a manually curated JSON file:
`resources/pre_ipo/companies/_index.json`

Initial universe: ~100 well-known private companies

Categories:
- **Mega unicorns** ($10B+): Stripe, SpaceX, Databricks, OpenAI, Fanatics, Epic Games, Chime, Klarna, Revolut, Waymo, ByteDance subsidiaries
- **Notable unicorns** ($1B–$10B): Discord, Plaid, Brex, Ripple, Figma (acquired?), Canva, Loom, Gusto, Lattice, Scale AI
- **IPO imminent / filed**: Klarna, eToro, Medline, Panera (rumored)
- **YC standouts**: recent batches

---

## Phase Plan

### Phase 1 — MVP: Free Data (Weeks 1–3)

- [ ] Define `PreIpoTypes.h` data structures
- [ ] Create `PreIpoService` skeleton
- [ ] Build `sec_form_d_data.py` — poll EDGAR Form D feed
- [ ] Build `sec_ipo_pipeline_data.py` — monitor S-1/F-1 filings
- [ ] Create static curated universe JSON (~100 companies)
- [ ] Build `CompanyListPanel` — browsable company list with search/filter
- [ ] Build `CompanyDetailPanel` — static data + funding history (from curated JSON)
- [ ] Build `IpoPipelinePanel` — S-1 feed + manually curated IPO watch list
- [ ] Build `DealFlowFeed` — live Form D ticker from EDGAR
- [ ] Register screen as `pre_ipo` in `MainWindow.cpp`
- [ ] Add `PRE-IPO` to `FKeyBar` navigation

**Deliverable:** A working screen with curated company data, live Form D feed, and S-1 monitor. No secondary prices yet.

### Phase 2 — Secondary Prices & Comps (Weeks 4–6)

- [ ] Integrate Forge/Hiive data (scrape or API partnership)
- [ ] Build `SecondaryPrice` display in `CompanyDetailPanel`
- [ ] Build `FundingTimelineChart` — horizontal chart widget
- [ ] Build `CompsEngine` — auto-pull public comps via MarketDataService when S-1 filed
- [ ] Integrate Crunchbase free/paid API for richer funding data
- [ ] Build `secondary_price_history` chart (line chart over 24 months)

**Deliverable:** Secondary market prices, funding timeline visualization, auto-comps table.

### Phase 3 — Intelligence Layer (Weeks 7–9)

- [ ] Investor graph — cross-reference VCs with 13F data (public fund holdings)
- [ ] Congressional cross-reference — link to POWER TRADER data (which members hold PE positions)
- [ ] AI valuation narrative — "Based on comps and last round, implied range is $X–$Y at listing"
- [ ] Alerts — notify on new Form D, S-1, secondary price move >10%
- [ ] Mirror/track investor — "Show me all Sequoia portfolio companies"
- [ ] Sector filter — view pre-IPO by sector (AI, Fintech, Defense, Health, etc.)
- [ ] Crunchbase full API integration if subscribed

---

## Navigation & Integration

**Screen registration (`MainWindow.cpp`):**
```cpp
dock_router_->register_factory("pre_ipo", [this]() {
    auto* screen = new pre_ipo::PreIpoScreen;
    connect(screen, &pre_ipo::PreIpoScreen::navigate_to_screen, this,
            [this](const QString& id, const QString& ticker) {
                if (!ticker.isEmpty())
                    SymbolContext::instance().set_group_symbol(
                        SymbolGroup::A, SymbolRef::equity(ticker), nullptr);
                dock_router_->navigate(id);
            });
    return screen;
});
```

**FKeyBar addition:** Add `PRE-IPO` tab with screen ID `pre_ipo` at position after MARKETS or before PORTFOLIO.

**Cross-screen links:**
- From POWER TRADER: "This senator holds private equity in [company] → Open in PRE-IPO"
- From MARKETS: When a recent IPO is viewed, "Was tracked in PRE-IPO pipeline →"
- From KNOWLEDGE: `analyze-a-spac` playbook links to PRE-IPO screen

---

## Success Metrics

- 100+ companies in universe at launch
- Live Form D feed with <4hr latency
- S-1 filing alerts within 1hr of EDGAR publication
- Secondary prices for top 20 most-watched companies
- Auto-comps table generated for any S-1 filer within 30 seconds
- IPO pipeline tracks 10+ companies at any time
