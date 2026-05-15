#include "screens/dashboard/canvas/DashboardTemplates.h"

namespace fincept::screens {

// Helper: create a GridItem with type + 12-col layout (no instance_id)
static GridItem gi(const char* id, int x, int y, int w, int h, int mw = 2, int mh = 3) {
    GridItem item;
    item.id = id;
    item.cell = {x, y, w, h, mw, mh};
    return item;
}

QVector<DashboardTemplate> all_dashboard_templates() {
    return {

        // ── Portfolio Manager ────────────────────────────────────────────────
        // Layout (12-col, 15 rows, ~1038 px at 60 px/row — fits 1080p+):
        //   Row  0–4 : Indices [4] | Commodities [4] | Margin [2] | Today P&L [2]
        //   Row  4–8 : Sector Heatmap [6] | Econ Calendar [3] | Quick Trade [3]
        //   Row  8–12: Portfolio Summary [8] | Watchlist [4]
        //   Row 12–16: News [6] | Live TV [6]
        // open_positions/working_orders omitted — require broker account (none configured).
        // Sidebar already shows Fear/Greed, Top Movers and VIX.
        {"portfolio_manager",
         "Portfolio Manager",
         "Markets, portfolio holdings, watchlist and live news",
         {
             gi("indices",           0,  0, 4, 4, 3, 4),
             gi("commodities",       4,  0, 4, 4, 2, 3),
             gi("margin_usage",      8,  0, 2, 4, 2, 3),
             gi("today_pnl",        10,  0, 2, 4, 2, 3),
             gi("sector_heatmap",    0,  4, 6, 4, 3, 4),
             gi("econ_calendar",     6,  4, 3, 4, 2, 3),
             gi("quick_trade",       9,  4, 3, 4, 2, 3),
             gi("portfolio_summary", 0,  8, 8, 4, 3, 3),
             gi("watchlist",         8,  8, 4, 4, 2, 3),
             gi("news",              0, 12, 6, 4, 3, 3),
             gi("video_player",      6, 12, 6, 4, 3, 4),
         }},

        // ── Hedge Fund ────────────────────────────────────────────────────────
        {"hedge_fund",
         "Hedge Fund",
         "Sector heatmap, screener, risk and macro",
         {
             gi("sector_heatmap", 0, 0, 6, 5, 3, 4),
             gi("screener", 6, 0, 6, 5, 3, 4),
             gi("risk_metrics", 0, 5, 4, 4),
             gi("performance", 4, 5, 4, 4),
             gi("econ_calendar", 8, 5, 4, 4),
             gi("news", 0, 9, 12, 4),
         }},

        // ── Crypto Trader ─────────────────────────────────────────────────────
        {"crypto_trader",
         "Crypto Trader",
         "Crypto prices, quick trade, sentiment and movers",
         {
             gi("crypto", 0, 0, 6, 4),
             gi("top_movers", 6, 0, 6, 5, 3, 4),
             gi("quick_trade", 0, 4, 4, 5),
             gi("watchlist", 4, 4, 8, 5),
             gi("sentiment", 0, 9, 6, 4),
             gi("news", 6, 9, 6, 4),
         }},

        // ── Equity Trader ─────────────────────────────────────────────────────
        {"equity_trader",
         "Equity Trader",
         "Indices, forex, commodities, screener and movers",
         {
             gi("indices", 0, 0, 4, 5, 3, 4),
             gi("forex", 4, 0, 4, 4),
             gi("commodities", 8, 0, 4, 4),
             gi("stock_quote", 0, 5, 4, 5),
             gi("screener", 4, 4, 8, 5, 3, 4),
             gi("top_movers", 0, 10, 6, 4),
             gi("news", 6, 9, 6, 4),
         }},

        // ── Macro Economist ───────────────────────────────────────────────────
        {"macro_economist",
         "Macro Economist",
         "Economic calendar, indices, commodities and news",
         {
             gi("econ_calendar", 0, 0, 6, 5, 3, 4),
             gi("indices", 6, 0, 6, 4),
             gi("commodities", 6, 4, 6, 4),
             gi("performance", 0, 5, 6, 4),
             gi("news", 0, 9, 8, 5),
             gi("risk_metrics", 8, 8, 4, 5),
         }},

        // ── Geopolitics Analyst ───────────────────────────────────────────────
        {"geopolitics",
         "Geopolitics Analyst",
         "News, sentiment, economic calendar and screener",
         {
             gi("news", 0, 0, 8, 5),
             gi("sentiment", 8, 0, 4, 4),
             gi("econ_calendar", 8, 4, 4, 5),
             gi("screener", 0, 5, 6, 5, 3, 4),
             gi("indices", 6, 5, 6, 4),
         }},
    };
}

} // namespace fincept::screens
