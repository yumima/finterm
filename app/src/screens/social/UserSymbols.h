#pragma once
// The user's own names — every watchlist and portfolio — for the private part
// of the weekly brief. Local SQLite reads; they go to the local build only.

#include "storage/repositories/PortfolioRepository.h"
#include "storage/repositories/WatchlistRepository.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace finterm::screens {

/// null when any of it could not be read: what was sent before then stands,
/// rather than a part of it.
inline QJsonValue user_symbols() {
    QSet<QString> syms;
    const auto wl = WatchlistRepository::instance().list_all();
    const auto pf = PortfolioRepository::instance().list_portfolios();
    if (!wl.is_ok() || !pf.is_ok())
        return QJsonValue::Null;
    for (const auto& w : wl.value()) {
        const auto st = WatchlistRepository::instance().get_stocks(w.id);
        if (!st.is_ok())
            return QJsonValue::Null;
        for (const auto& s : st.value())
            syms.insert(s.symbol);
    }
    for (const auto& p : pf.value()) {
        const auto as = PortfolioRepository::instance().get_assets(p.id);
        if (!as.is_ok())
            return QJsonValue::Null;
        for (const auto& a : as.value())
            syms.insert(a.symbol);
    }
    QJsonArray out;
    for (const auto& s : std::as_const(syms))
        out.append(s);
    return out;
}

} // namespace finterm::screens
