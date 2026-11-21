# MARKETS + FUTURES — one tab

Status: design, 2026-11-20. Implements the user's ask: merge FUTURES into
MARKETS, keep the futures screen's list + chart + curve way of reading a
market, make the panel view a configurable canvas like the dashboard, and put
the rest in tabs.

## What the industry does

| Product | How a market is read |
|---|---|
| Bloomberg Launchpad | User-built **monitors** (quote lists) + charts in one canvas; **security linking**: clicking a row in a monitor drives every linked chart/curve. Futures: CT (contract table) + curve (CRVF/GC). |
| Refinitiv Workspace | Quote lists + linked chart; futures chain + forward curve in one app. |
| TradingView | Watchlist (left/right rail) → **inline chart** follows the selection; details below in tabs. No popups. |
| CME Group / Barchart | A futures market = contract table (months) + forward curve + price chart, side by side. |
| Koyfin | Dashboards of configurable widgets; any symbol click updates linked widgets. |

Common pattern: **a configurable overview of lists, with selection linking to an
inline chart**, and a **deep single-market view** (list + chart + curve + tabs)
for the instrument class that needs it (futures). Popups are a fallback.

## Design

MARKETS header: `OVERVIEW · FUTURES · WORLD` + clocks, refresh, and (OVERVIEW)
`+ WIDGET`, `RESET`.

### OVERVIEW — the configurable canvas
- The dashboard's own grid canvas (drag, resize, add, remove, per-widget config,
  saved per user under its own key). Same widget registry as DASHBOARD, so any
  dashboard widget can go here and every Markets widget can go on DASHBOARD.
- **Quote Panel** widget replaces the PANELS tiles: a named, user-edited list
  (+ ADD / − / right-click move, columns chooser) on the live quote feed.
  Existing user panels migrate one-to-one into Quote Panel widgets.
- The CONDITIONS page's widgets (futures majors, ETFs, vol, curve, credit,
  macro, commodities, FX, crypto, correlation, odds) live here too.
- **Chart** widget (inline, the chart dialog's view): follows the selection —
  clicking a row in any widget on the same canvas loads it (security linking).
  Same periods, overlays and condition strip as the dialog. A futures symbol
  shows `CURVE ▸` which opens the FUTURES page on that market.
- Linking is generic: `MarketChartDialog::show_for()` first asks the nearest
  canvas for a visible linked Chart widget; only if there is none does the
  dialog open. So the DASHBOARD gains linking too, and every other screen keeps
  click-to-chart unchanged.

### FUTURES — the futures screen, now a page of MARKETS
Unchanged inside: board (list) · chart · forward curve · contract months ·
spread / COT / terms / ticket tabs. Reached from the header, from `CURVE ▸`,
and from every old route to "futures" (top nav, F-key, command bar, MCP
navigation, saved layouts and perspectives) — those now open MARKETS ▸ FUTURES.

### WORLD — unchanged.

## Rules kept
- One source per number: quotes `market:quote:*`, readings `cond:*`, futures
  curve the FUTURES service — no widget fetches its own copy.
- Cache first: every widget paints its last answer at once, refreshes behind.
- Live: hub topics refresh while on screen; stop when hidden.
- No fake data; a missing feed is said, not filled.

## Migration
- `markets_panels` (MarketPanelStore) → OVERVIEW default layout built from the
  user's panels (positions by column), then the condition widgets + a Chart.
- `markets_view` = panels/conditions → overview; world stays world.
- FUTURES screen state (selected market, layout) is kept under MARKETS'.
