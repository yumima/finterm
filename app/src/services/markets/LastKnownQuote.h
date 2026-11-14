#pragma once
// Last-known quotes for a view that (re)subscribes to market:quote:* topics.
//
// DataHub::subscribe() hands a new subscriber the cached value only while it is
// younger than the topic's TTL (30 s for quotes). A view that subscribes later
// than that — the Markets PANELS page opened behind WORLD, a dashboard tab
// shown a minute after start — receives nothing for any symbol whose refresh
// is failing, and a list built from deliveries simply leaves those rows out.
// 2026-11-13, during a Yahoo 429 burst: the STOCK INDICES panel showed one
// row, ^AXJO, the only index whose quote had been published in the 30 s before
// PANELS was clicked; every other index had a (hydrated) value in the hub that
// was older than the TTL and a refresh that kept being refused.
//
// Seed from the hub's last value regardless of age, and mark it: a quote not
// polled in this process (hydrated from an earlier session's cache) or whose
// last refresh failed is STALE, never shown as current.

#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "services/markets/MarketDataService.h"

#include <QHash>
#include <QStringList>

namespace finterm::services {

/// Fill `into` with the hub's last value for every symbol it does not already
/// hold. Returns how many were added.
inline int seed_last_known_quotes(const QStringList& symbols, QHash<QString, QuoteData>& into) {
    auto& hub = datahub::DataHub::instance();
    int added = 0;
    for (const QString& sym : symbols) {
        if (into.contains(sym))
            continue;
        const QVariant v = hub.peek_raw(QStringLiteral("market:quote:") + sym);
        if (!v.canConvert<QuoteData>())
            continue;
        into.insert(sym, v.value<QuoteData>());
        ++added;
    }
    return added;
}

/// A row's quote is current only when it was polled in this process
/// (MarketDataService::has_live_quote) and its topic's last refresh did not
/// fail. Same rule as the WORLD board's row_status().
inline bool quote_is_stale(bool polled_this_process, datahub::SubscriptionState state) {
    return !polled_this_process || state == datahub::SubscriptionState::Error;
}

/// The hub's current state for `sym`'s quote topic. Read it here rather than
/// from a view's own error subscription: DataHub does not replay an error to a
/// subscriber that arrives after it, so a seeded quote would read as healthy.
inline datahub::SubscriptionState quote_topic_state(const QString& sym) {
    return datahub::DataHub::instance().subscription_state(QStringLiteral("market:quote:") + sym);
}

/// quote_is_stale() for `sym` as the app sees it now.
inline bool last_known_is_stale(const QString& sym) {
    return quote_is_stale(MarketDataService::instance().has_live_quote(sym), quote_topic_state(sym));
}

} // namespace finterm::services
