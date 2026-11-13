#pragma once
// LiveQuoteFollower.h — keep a changing set of rows on their live quotes.
//
// For surfaces whose ROWS come from somewhere slow (a screener, a scan, an
// IPO list) but whose prices should move with the tape: hand it the symbols
// currently shown and it holds a market:quote:<sym> subscription for each —
// which also puts them on the push stream — calling `on_quote` with every
// update. A separate QObject so its subscriptions never collide with the
// owning widget's own (DataHub keys subscriptions by owner + topic).
//
// Only a quote polled or ticked THIS process is passed on: the value the hub
// hands back on subscribe can be a disk-hydrated quote from an earlier
// session, older than the row it would overwrite.

#include "services/markets/MarketDataService.h"

#include <QObject>
#include <QSet>
#include <QStringList>

#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace fincept::services {

class LiveQuoteFollower : public QObject {
  public:
    LiveQuoteFollower(QObject* parent, std::function<void(const QuoteData&)> on_quote)
        : QObject(parent), on_quote_(std::move(on_quote)) {}
    ~LiveQuoteFollower() override;

    /// Follow exactly `symbols` (diffed: unchanged ones keep their subscription).
    void set_symbols(const QStringList& symbols);
    void clear();

    bool following(const QString& symbol) const { return subs_.contains(symbol); }

    /// Lay the live quote's price fields on a row that came from elsewhere
    /// (keeps the row's own name and anything else it carries). Price and
    /// change move as a pair: a live quote that doesn't know its change (NaN)
    /// leaves the row alone rather than show a new price beside an old %.
    static void merge(QuoteData& row, const QuoteData& live) {
        if (!(live.price > 0) || !std::isfinite(live.change) || !std::isfinite(live.change_pct))
            return;
        row.price = live.price;
        row.change = live.change;
        row.change_pct = live.change_pct;
        if (live.high > 0)
            row.high = live.high;
        if (live.low > 0)
            row.low = live.low;
        if (std::isfinite(live.volume) && live.volume > 0)
            row.volume = live.volume;
    }

    /// The hub's current quote for `symbol` when it is live (polled or ticked
    /// this process), else nullopt.
    static std::optional<QuoteData> live_quote(const QString& symbol);

    /// Movers: lay `q` on its row(s) and re-rank — gainers by % down, losers
    /// by % up. False when `q` is not a listed mover.
    static bool merge_movers(MarketDataService::TopMovers& movers, const QuoteData& q);
    /// Movers: lay every row's live quote on a freshly fetched list (the
    /// screener's snapshot can be older than the tape already shown).
    static void refresh_movers(MarketDataService::TopMovers& movers);

  private:
    std::function<void(const QuoteData&)> on_quote_;
    QSet<QString> subs_;
};

} // namespace fincept::services
