# MARKETS — one tab, one header row of pages

Status: BUILT (2026-11-23). FUTURES is no longer a tab; it is part of MARKETS.

```
FINTERM MARKETS  OVERVIEW: MARKETS | EQUITY | RATES & MACRO | +
                 FUTURES: MAJORS | INDEX | RATES | ENERGY | METALS | GRAINS | SOFTS | LIVESTOCK | FX | CRYPTO
                 WORLD                                     NYSE status · REFRESH · + ADD · RESET
```

## The rule every page follows
- A page is a canvas of widgets (the dashboard's grid engine and widget
  registry): drag, resize, **+ ADD** (the dashboard's picker), a tile's ✕,
  **RESET** — exactly as on DASHBOARD. Pages fit the view (rows stretch and
  shrink): a trader reads a page without scrolling.
- Every page has an embedded chart that follows the clicks made **on that
  page only** (SelectionLink, one per canvas — Bloomberg's per-monitor
  security linking). The chart window opens only from a page without one.
- The same widget may sit on several pages; all read the same hub topics, so
  one fetch serves them all.

## Pages
- **OVERVIEW** — the user's screens (+ adds blank / from a template / resets
  all; right-click renames, duplicates, moves, deletes). Defaults:
  - MARKETS: a read per asset class, volatility (VIX/VVIX/VXN/MOVE), index
    conditions, FX, crypto, Strategy's premium to its bitcoin (mNAV).
  - EQUITY: SPDR sectors, heatmap, 63-day sector correlation, top movers.
  - RATES & MACRO: Treasury curve + spreads, 10Y–2Y (FRED T10Y2Y), credit
    stress (HY/IG OAS, MOVE), fed funds / CPI YoY / debt-GDP / CAPE,
    cross-asset correlation (S&P, 10Y, BTC, gold, oil, copper), sovereign
    10Y spreads (any OECD country).
- **FUTURES** — a page per group: the board (the header picks the group),
  the chart, the forward curve with its strip, the contract months, and the
  detail tabs (SPREAD · POSITIONING · TICKET · CONTRACT) — the old FUTURES
  screen as widgets. Data: FuturesHub (fut:curve/past/cot/spread topics).
- **WORLD** — the global board as tiles: sessions + overnight line, each
  region and section, the chart.

## Migration
- The user's old PANELS → a "MY PANELS" screen (one Quote List each).
- The interim OVERVIEW canvas → a "MY OVERVIEW" screen, as arranged.
- Every route to the old FUTURES tab → MARKETS' futures page of that market.
