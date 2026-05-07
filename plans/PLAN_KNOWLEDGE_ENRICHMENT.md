# KNOWLEDGE Tab — Enrichment Plan

**Status:** Planning
**Priority:** 1 (do first — no new infra, pure content + UX work)
**Estimated effort:** 3–5 weeks

---

## Background

The KNOWLEDGE tab is a 3-pane educational cockpit serving:
- **BASICS:** Glossary (46 terms), Abbreviations (500+)
- **CONTEXT:** Concepts (10 articles)
- **PRACTICE:** Cases (10), Tracks (8), Playbooks (10)

Total: 84 entries across 5 categories. The content architecture is mature — JSON index files + Markdown bodies, with a rich rail (live data, calculators, AI tutor, exposure scanner, pitfalls, rules of thumb). The gap is **content volume and breadth**, not infrastructure.

---

## Goals

1. Expand content to be genuinely reference-quality for all user levels
2. Add two new content categories (FORMULAS, CHARTS)
3. Deploy engagement mechanics (progress, quiz, daily widget)
4. Broaden `HelpHint` deep-link coverage across all screens
5. Surface KNOWLEDGE content passively throughout the app

---

## Phase 1 — Content Expansion (Weeks 1–2)

### 1A. Glossary: 46 → 150 terms

Add ~100 new entries covering gaps in:

**Macro & Rates**
- SOFR, Fed Funds Rate, QE/QT, TGA (Treasury General Account), RRP (Reverse Repo), TIPS, Breakeven Inflation, Yield Curve Control (YCC), Neutral Rate, Forward Guidance

**Options / Derivatives**
- Intrinsic Value, Extrinsic Value, Assignment, Exercise, Moneyness (ITM/OTM/ATM), Put-Call Parity, Synthetic Long/Short, Collar, Covered Call, Protective Put, Butterfly, Iron Condor, Calendar Spread, Skew, Vol Surface, Pinning

**Crypto-specific**
- Funding Rate, Liquidation Price, On-chain metrics, Hash Rate, Mempool, Gas Fee, MEV, Staking Yield, Realized Cap, MVRV Ratio, NVT Ratio

**ETF Mechanics**
- NAV Discount/Premium, Authorized Participant, Creation/Redemption, Tracking Error, TER, Smart Beta, Factor ETF

**Market Structure**
- NBBO, Market Maker, Payment for Order Flow (PFOF), Dark Pool, Lit Market, Reg NMS, Rule 10b-5, Short Interest, Short Squeeze, Days-to-Cover, Borrow Rate

**Corporate Finance**
- WACC, DCF, Terminal Value, Accretion/Dilution, LBO, EBITDA Bridge, Net Working Capital, Free Cash Flow Yield, Capital Allocation, Buyback Yield

**Risk**
- Tail Risk, CVaR, Max Drawdown, Ulcer Index, Correlation, Covariance, Tracking Error, Information Ratio, Sortino Ratio

Each entry follows the existing `KnowledgeEntry` schema with: difficulty tag, aliases, related entries, live_examples, rules_of_thumb, pitfalls, seen_in screen refs, and action CTAs.

### 1B. Concepts: 10 → 20 articles

New articles to write:

| ID | Title | Difficulty |
|---|---|---|
| `macro-cycles` | Macro Cycles: The Business Cycle, Credit Cycle & Market Regimes | intermediate |
| `reading-balance-sheet` | Reading a Balance Sheet: Assets, Liabilities & Equity | beginner |
| `earnings-season` | Earnings Season: EPS Beats, Revenue Surprises & Guidance | intermediate |
| `fed-policy` | Understanding Fed Policy: Rates, QE/QT & the Dual Mandate | intermediate |
| `sector-rotation` | Sector Rotation: Where Money Moves Through the Cycle | intermediate |
| `dark-pools-order-flow` | Dark Pools & Order Flow: What Retail Doesn't See | advanced |
| `crypto-on-chain` | Crypto On-Chain Fundamentals: Reading the Blockchain | intermediate |
| `commodities-basis` | Commodities Basis Trading: Cash vs. Futures | advanced |
| `credit-markets` | Credit Markets: IG, HY, CDS & Spreads | intermediate |
| `currency-fx` | Currency & FX: Carry Trades, PPP & Intervention | intermediate |

Each article includes: 800–1500 word markdown body, 3–5 live_examples, 3+ rules_of_thumb, 2–4 pitfalls, 2+ action CTAs to other screens.

### 1C. Cases: 10 → 20 case studies

New historical cases:

| ID | Title | Theme |
|---|---|---|
| `case-yen-carry-2024` | Yen Carry Trade Unwind (2024) | FX / carry |
| `case-uk-gilt-2022` | UK Gilt Crisis & LDI (2022) | Rates / systemic |
| `case-archegos-2021` | Archegos Capital (2021) | Prime brokerage / leverage |
| `case-terra-luna-2022` | Terra/Luna Collapse (2022) | Crypto / algorithmic stablecoin |
| `case-enron-2001` | Enron Collapse (2001) | Accounting fraud |
| `case-dotcom-2000` | Dot-com Bubble (1999–2002) | Valuation / narrative |
| `case-asian-crisis-1997` | Asian Financial Crisis (1997) | FX / contagion |
| `case-flash-crash-2010` | Flash Crash (2010) | Market microstructure / HFT |
| `case-nickel-lme-2022` | LME Nickel Short Squeeze (2022) | Commodities / margin |
| `case-fed-2022` | Fed Inflation Response (2022) | Rates / macro |

### 1D. Tracks: 8 → 14 learning paths

New tracks:

| ID | Title | Target user |
|---|---|---|
| `track-crypto-trader` | Crypto Trader | Crypto-native learner |
| `track-macro-investor` | Macro Investor | Top-down, rates/FX focus |
| `track-options-income` | Options Income Strategies | Income/yield-focused |
| `track-quant-systematic` | Quant & Systematic Trading | Technical/algo-inclined |
| `track-day-trader` | Day Trader Foundations | Active intraday trader |
| `track-long-term-investor` | Long-Term & Retirement | Buy-and-hold, tax-aware |

Each track lists 6–12 entries (glossary + concepts + playbooks) in a sequenced curriculum with difficulty arc.

### 1E. Playbooks: 10 → 20 how-to guides

New playbooks:

| ID | Title |
|---|---|
| `trade-earnings-event` | How to Trade an Earnings Event |
| `read-fed-statement` | How to Read a Fed Statement |
| `manage-losing-trade` | How to Manage a Losing Trade |
| `understand-merger-arb` | How Merger Arbitrage Works |
| `analyze-a-spac` | How to Analyze a SPAC |
| `read-crypto-onchain` | How to Read On-Chain Crypto Data |
| `build-options-strategy` | How to Build a Multi-Leg Options Strategy |
| `research-a-sector` | How to Research a Sector from Scratch |
| `analyze-credit` | How to Analyze a Bond / Credit |
| `build-macro-view` | How to Build a Macro Investment View |

---

## Phase 2 — New Content Categories (Week 2–3)

### 2A. FORMULAS category

An interactive formula library — every key financial formula with:
- Variable definitions and typical value ranges
- Step-by-step worked example with real numbers
- A live calculator (extending the existing `Calculator` struct)
- Common mistakes section

**Initial set (~25 formulas):**
P/E, Forward P/E, PEG, EV/EBITDA, P/B, P/FCF, Dividend Yield, Sharpe Ratio, Sortino Ratio, Treynor Ratio, Information Ratio, Kelly Criterion, VaR (historical + parametric), CVaR, Beta (CAPM), WACC, DCF (Gordon Growth), Duration (Macaulay + Modified), Convexity, Black-Scholes (simplified), Put-Call Parity, Delta approximation, R-Multiple, Expectancy, Position Size formula

**UI placement:** Add as a 4th GroupedPane tab in the BASICS pane, or as a standalone column alongside Glossary.

### 2B. CHARTS category (Pattern Recognition)

Annotated chart pattern reference using real historical price data:

**Pattern types (~20):**
- Trend-following: Ascending Triangle, Descending Triangle, Symmetrical Triangle, Wedge (rising/falling), Flag, Pennant, Channel
- Reversal: Head & Shoulders, Inverse H&S, Double Top, Double Bottom, Triple Top, Rounding Bottom (Cup), Evening Star, Morning Star
- Continuation: Rectangle, Measured Move
- Volume-based: Volume Climax, Accumulation/Distribution

Each entry includes: pattern image/chart (using historical OHLCV via MarketDataService), detection criteria, typical measured move target, failure rate note, and a live example ticker.

**Implementation note:** Chart rendering can reuse the existing charting infrastructure; pattern images can be static SVGs or real historical data renders.

---

## Phase 3 — Engagement Mechanics (Week 3–4)

### 3A. Progress Tracking

- Persist read status per entry in user settings/local DB
- Show read/unread indicator on category column rows
- Track completion % per Track curriculum
- "Continue track" shortcut on KNOWLEDGE home (highlight where user left off)

**Data to persist (per entry):** `read: bool`, `read_at: datetime`, `quiz_score: int`, `bookmarked: bool`

### 3B. Quiz / Flashcard Mode

The `difficulty` field and structured content already support this. Add a quiz panel:
- Flashcard front: term name + "What does this measure?"
- Flashcard back: subtitle + 1–2 key rules_of_thumb
- Multi-choice mode: 4 options, pull distractors from `related` entries
- Score tracked per category session
- Surfaced as a "Quiz this track" button at the bottom of any Track view

### 3C. Daily Term Widget (Dashboard integration)

- A new `KnowledgeDailyWidget` for the DASHBOARD screen
- Rotates daily: one glossary term, one case of the week, one playbook recommendation
- "Learn more →" deep-links to the full entry in KNOWLEDGE
- Shows a live data point from the term's `live_examples` (e.g., "Today's VIX: 18.4 — see: Volatility")

### 3D. Recently Viewed Rail

- Show last 10 viewed entries on KNOWLEDGE home (above the category panes)
- Persist in session; restore across restarts
- One-click re-open

---

## Phase 4 — Cross-App Integration (Week 4–5)

### 4A. Expand HelpHint Coverage

The `HelpHint` widget deep-links from any screen to the relevant KNOWLEDGE entry. Currently used sparsely. Audit and add to:

| Screen | Context | Links to entry |
|---|---|---|
| MARKETS stock detail | P/E field | `pe-ratio` |
| MARKETS stock detail | Beta field | `beta` |
| MARKETS stock detail | Volume field | `volume` |
| FUTURES panel | Open Interest field | `open-interest` |
| FUTURES panel | Contango/backwardation indicator | `contango`, `backwardation` |
| OPTIONS chain | Delta column | `delta` |
| OPTIONS chain | Theta column | `theta` |
| OPTIONS chain | IV column | `implied-volatility` |
| PORTFOLIO blotter | Sharpe Ratio | `sharpe-ratio` |
| PORTFOLIO blotter | Beta | `beta` |
| PORTFOLIO blotter | Drawdown | `drawdown` |
| BACKTEST results | Sharpe, Max DD, Expectancy | respective entries |
| CRYPTO | Funding rate | (new entry) |

### 4B. AI Tutor Expansion

The `AITutorPanel` exists. Extend it:
- **Level selector:** "Explain to me as a: Beginner / Trader / CFA / Quant" — passes user level as prompt context
- **Follow-up Q&A:** Allow 2–3 follow-up questions in same panel without leaving the entry
- **"Test me" mode:** AI generates a question based on the entry content; user types answer; AI grades it
- **"Real world example":** AI pulls a current news headline and explains how it relates to the concept

---

## File & Code Locations

| Component | Path |
|---|---|
| Content JSON indexes | `fincept-qt/resources/knowledge/*/` |
| Entry markdown bodies | `fincept-qt/resources/knowledge/*/` |
| ContentLoader | `fincept-qt/src/screens/knowledge/ContentLoader.h/.cpp` |
| KnowledgeTypes | `fincept-qt/src/screens/knowledge/KnowledgeTypes.h` |
| KnowledgeScreen | `fincept-qt/src/screens/knowledge/KnowledgeScreen.h/.cpp` |
| RailWidget | `fincept-qt/src/screens/knowledge/RailWidget.h/.cpp` |
| AITutorPanel | `fincept-qt/src/screens/knowledge/AITutorPanel.h/.cpp` |
| GroupedPane | `fincept-qt/src/screens/knowledge/GroupedPane.h/.cpp` |
| HelpHint | `fincept-qt/src/screens/knowledge/HelpHint.h/.cpp` |
| Dashboard widget target | `fincept-qt/src/screens/dashboard/` |

---

## Effort Summary

| Phase | Work | Effort |
|---|---|---|
| 1A–E Content expansion | JSON + Markdown authoring | ~2 weeks |
| 2A–B New categories | Schema extension + C++ widget + content | ~1 week |
| 3A Progress tracking | Local persistence + UI indicators | ~3 days |
| 3B Quiz mode | New panel widget + score logic | ~4 days |
| 3C Daily widget | Dashboard widget + daily rotation | ~2 days |
| 3D Recently viewed | Session state + rail UI | ~1 day |
| 4A HelpHint audit | Connecting existing widget to new targets | ~3 days |
| 4B AI tutor expansion | Prompt engineering + follow-up Q&A UI | ~3 days |

**Total: ~3.5–5 weeks**

---

## Success Metrics

- Glossary: 150+ entries
- Concepts: 20+ articles
- Cases: 20+ studies
- Tracks: 14+ curricula
- Playbooks: 20+ guides
- FORMULAS: 25+ entries with live calculators
- CHARTS: 20+ pattern entries with real chart examples
- HelpHint: deployed on 30+ fields across 8+ screens
- Daily widget: live on DASHBOARD
- Quiz mode: available on all tracks
