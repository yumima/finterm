#include "services/markets/LiveQuoteFollower.h"

#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace fincept::services {

LiveQuoteFollower::~LiveQuoteFollower() {
    clear();
}

void LiveQuoteFollower::set_symbols(const QStringList& symbols) {
    auto& hub = datahub::DataHub::instance();
    const QSet<QString> want(symbols.begin(), symbols.end());
    for (auto it = subs_.begin(); it != subs_.end();) {
        if (!want.contains(*it)) {
            hub.unsubscribe(this, QStringLiteral("market:quote:") + *it);
            it = subs_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& sym : want) {
        if (sym.isEmpty() || subs_.contains(sym))
            continue;
        subs_.insert(sym);
        hub.subscribe(this, QStringLiteral("market:quote:") + sym, [this, sym](const QVariant& v) {
            if (v.canConvert<QuoteData>() && MarketDataService::instance().has_live_quote(sym))
                on_quote_(v.value<QuoteData>());
        });
    }
}

void LiveQuoteFollower::clear() {
    if (subs_.isEmpty())
        return;
    datahub::DataHub::instance().unsubscribe(this);
    subs_.clear();
}

std::optional<QuoteData> LiveQuoteFollower::live_quote(const QString& symbol) {
    if (!MarketDataService::instance().has_live_quote(symbol))
        return std::nullopt;
    const QVariant v = datahub::DataHub::instance().peek_raw(QStringLiteral("market:quote:") + symbol);
    if (!v.canConvert<QuoteData>())
        return std::nullopt;
    return v.value<QuoteData>();
}

namespace {
void rank(MarketDataService::TopMovers& m) {
    auto pct = [](const QuoteData& r) { return std::isfinite(r.change_pct) ? r.change_pct : 0.0; };
    std::stable_sort(m.gainers.begin(), m.gainers.end(), [&](const auto& a, const auto& b) { return pct(a) > pct(b); });
    std::stable_sort(m.losers.begin(), m.losers.end(), [&](const auto& a, const auto& b) { return pct(a) < pct(b); });
}
} // namespace

bool LiveQuoteFollower::merge_movers(MarketDataService::TopMovers& movers, const QuoteData& q) {
    bool hit = false;
    for (auto* list : {&movers.gainers, &movers.losers})
        for (auto& row : *list)
            if (row.symbol == q.symbol) {
                merge(row, q);
                hit = true;
            }
    if (hit)
        rank(movers);
    return hit;
}

void LiveQuoteFollower::refresh_movers(MarketDataService::TopMovers& movers) {
    for (auto* list : {&movers.gainers, &movers.losers})
        for (auto& row : *list)
            if (const auto q = live_quote(row.symbol))
                merge(row, *q);
    rank(movers);
}

} // namespace fincept::services
