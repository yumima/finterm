# MARKETS — one tab, one header row of pages

Status: BUILT (2026-11-23). FUTURES is no longer a tab; it is part of MARKETS.

```
FINTERM MARKETS  MY VIEW +  │  MARKETS  EQUITY  RATES & MACRO  WORLD  │
                 FUTURES  MAJORS  INDEX  RATES  ENERGY  METALS  GRAINS  SOFTS  LIVESTOCK  FX  CRYPTO
                                                     NYSE status · REFRESH · + ADD · RESET
```

Three groups, set apart by a rule rather than named — the pages say what
they are. FUTURES keeps its word: its RATES, FX and CRYPTO are futures, not
the market pages.

## The rule every page follows
- A page is a canvas of widgets (the dashboard's grid engine and widget
  registry): drag, resize, **+ ADD** (the dashboard's picker), a tile's ✕,
  **RESET** — exactly as on DASHBOARD.
- A page fits the view: it is divided into a FIXED number of rows (saved with
  the layout). Resizing a tile changes no other tile's size — a tile it
  pushes past the last row gives up height instead (down to two rows), and
  the resize stops when nothing can give. Positions pack upward on their own.
- Every tile shows its resize grip (bottom-right; amber under the pointer).
- Every page has an embedded chart that follows the clicks made **on that
  page only** (SelectionLink, one per canvas). The row the chart draws is
  marked — an amber band and bar over the row's own colours — in every list
  on the page, and only there; before any click, the chart's own symbol is
  the selection.
- The same widget may sit on several pages; all read the same hub topics, so
  one fetch serves them all.

## Pages
- **MY VIEW …** — the user's screens, first. **+** adds one (blank, or a copy
  of any page as it is now) or resets all of MARKETS; right-click renames,
  duplicates, moves, deletes. MY VIEW's default is the desk's own screen.
- **MARKETS · EQUITY · RATES & MACRO · WORLD** — fixed market pages (their
  arrangement is the user's; RESET returns the default):
  - MARKETS: a read per asset class, volatility, index conditions (every
    major index), FX, crypto, Strategy's premium to its bitcoin (mNAV).
  - EQUITY: ETFs and SPDR sectors, heatmap, sector correlation, top movers.
  - RATES & MACRO: Treasury curve + spreads, bonds and credit (OAS, yields,
    Treasury / credit ETFs), fed funds / CPI YoY / debt-GDP / CAPE,
    cross-asset correlation, sovereign 10Y spreads.
  - WORLD: sessions + overnight line, each region and section, the chart.
- **FUTURES** — a page per group: the board, the chart, the forward curve
  with its strip, the contract months, and the detail tabs (SPREAD ·
  POSITIONING · TICKET · CONTRACT). Data: FuturesHub.

Default arrangements are the ones the desk settled on (2026-11-23), copied
from its saved pages into MarketTemplates.cpp; a dated contract a chart had
been left on is replaced by its continuous root (it would expire).

## Migration (once, markets_pages_version = 2)
- The old OVERVIEW list → the user's screens minus the market pages (fixed
  now) and PANELS; "MY OVERVIEW" is renamed MY VIEW; none left → MY VIEW.
- PANELS is gone: its lists are the default lists of the widgets that read
  the same markets (indices → INDEX CONDITIONS, FX → FX Spot, commodities →
  Commodity Conditions, bonds → Bonds & Credit, ETFs → ETFs & Sectors,
  crypto → Crypto Spot). A saved INDEX CONDITIONS gains the index rows it
  lacked. The three single-stock lists (US, China, Europe) were the app's
  stock defaults and have no market-page counterpart; they are not carried.
- Every route to the old FUTURES tab → MARKETS' futures page of that market.
