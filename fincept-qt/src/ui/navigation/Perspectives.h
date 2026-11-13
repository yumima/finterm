#pragma once
// Perspectives.h — the Quick Switch layouts: which screens each one opens.
// The first screen leads (opens exclusive); the rest fill the grid. Shared by
// the View menu (which lists a layout only when its lead screen is offered,
// see ScreenVisibility) and MainWindow (which opens the offered screens).

#include "core/config/ScreenVisibility.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace finterm::ui {

inline const QMap<QString, QStringList>& perspective_layouts() {
    static const QMap<QString, QStringList> layouts = {
        {"perspective_trading", {"crypto_trading", "watchlist", "markets", "news"}},
        {"perspective_equity", {"equity_trading", "watchlist"}},
        {"perspective_algo", {"algo_trading", "backtesting"}},
        {"perspective_research", {"equity_research", "markets", "screener", "news"}},
        {"perspective_derivatives", {"derivatives", "surface_analytics"}},
        {"perspective_ma", {"ma_analytics", "news"}},
        {"perspective_portfolio", {"portfolio", "markets", "watchlist", "news"}},
        {"perspective_news", {"news", "markets"}},
        {"perspective_markets", {"markets", "watchlist"}},
        {"perspective_economics", {"economics", "dbnomics", "gov_data", "asia_markets"}},
        {"perspective_data", {"data_sources", "data_mapping"}},
        {"perspective_geopolitics", {"geopolitics", "maritime", "relationship_map", "news"}},
        {"perspective_quant", {"ai_quant_lab", "quantlib", "backtesting", "markets"}},
        {"perspective_ai", {"agent_config", "equity_research"}},
        {"perspective_tools", {"code_editor", "node_editor"}},
    };
    return layouts;
}

/// The screens `action` opens that are offered now; empty when its lead
/// screen is hidden (the layout is then not offered at all).
inline QStringList perspective_screens(const QString& action) {
    const QStringList all = perspective_layouts().value(action);
    if (all.isEmpty() || !ScreenVisibility::listed(all.first()))
        return {};
    QStringList out;
    for (const QString& id : all)
        if (ScreenVisibility::listed(id))
            out << id;
    return out;
}

} // namespace finterm::ui
