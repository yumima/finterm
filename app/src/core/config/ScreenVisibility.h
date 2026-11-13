#pragma once
// ScreenVisibility.h — which screens the app offers by default.
//
// The screens listed below came with the upstream code and have not been
// audited: the ones that have been (Equity Research, Portfolio, News,
// Ownership, IPO Watch, Screener, Markets…) each turned up defects that put
// wrong numbers on screen, and these have had no such pass. They are hidden
// from every place a screen is offered — the Navigate menu, the tab bar, the
// command bar, the Component Browser, the agents' navigation tool — unless
// the user turns on Settings › Developer › "Show experimental screens".
//
// Hidden, not deleted: a saved layout that contains one still opens it, and
// the setting brings them all back. Takes effect on the next launch (menus
// are built once at startup).

#include "core/config/AppConfig.h"

#include <QSet>
#include <QString>

namespace finterm {

struct ScreenVisibility {
    static constexpr const char* kSettingKey = "ui/show_experimental_screens";

    static bool is_experimental(const QString& id) {
        static const QSet<QString> kExperimental = {
            // data catalogues
            "economics", "gov_data", "dbnomics", "akshare", "asia_markets", "maritime", "geopolitics",
            "relationship_map",
            // asset classes / analytics not yet audited
            "derivatives", "alt_investments", "ma_analytics", "quantlib",
            // trading and strategy tooling
            "crypto_trading", "equity_trading", "algo_trading", "backtesting", "alpha_arena", "ai_quant_lab",
            "trade_viz",
            // general-purpose tools that duplicate dedicated apps
            "node_editor", "code_editor", "excel", "file_manager", "notes", "report_builder", "data_mapping",
        };
        return kExperimental.contains(id);
    }

    /// Read once per process: the setting takes effect on the next launch, and
    /// a single read means no thread (the MCP worker lists screens too) ever
    /// touches AppConfig's QSettings after startup. The first call happens on
    /// the GUI thread when the menus are built.
    static bool show_experimental() {
        static const bool value = AppConfig::instance().get(kSettingKey, false).toBool();
        return value;
    }

    /// Whether `id` should appear in menus, tab bars and pickers.
    static bool listed(const QString& id) { return !is_experimental(id) || show_experimental(); }
};

} // namespace finterm
