#pragma once
#include "screens/futures/FuturesDataService.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

namespace fincept::screens::futures {

/// Default refresh cadence for the quote cache and the Futures screen's own
/// refresh tick. One source of truth so they don't drift.
inline constexpr int kFuturesRefreshIntervalMs = 60'000;

/// Process-wide singleton holding the most recent quote for every contract
/// in the catalog. Backed by one batched yfinance fetch — far cheaper than
/// each panel making its own per-class request.
///
/// Class-switching panels read synchronously via `quotes_for_class()` and
/// re-render in response to the `quotes_updated` signal. Async drift is
/// impossible because there is no per-panel request to lose track of.
class FuturesQuoteCache : public QObject {
    Q_OBJECT
  public:
    static FuturesQuoteCache& instance();

    /// Synchronous read — returns whatever was last written. Empty until
    /// the first refresh resolves.
    QVector<FuturesQuote> quotes_for_class(const QString& asset_class) const;
    QVector<FuturesQuote> all_quotes() const;
    QHash<QString, QVector<FuturesQuote>> grouped_by_class() const;

    /// Whether we've seen at least one successful response.
    bool has_data() const { return last_refresh_.isValid(); }
    QString last_source() const { return last_source_; }
    QDateTime last_refresh() const { return last_refresh_; }

    /// Kick off a refresh now. Idempotent — if one is already in flight,
    /// the call is dropped (the in-flight request will broadcast on return).
    /// Auto-coalesces to at most one fetch per `min_interval_ms`.
    void refresh();

    /// Start the periodic refresh timer. Safe to call from multiple screens.
    void start_auto_refresh(int interval_ms = kFuturesRefreshIntervalMs);
    void stop_auto_refresh();

  signals:
    void quotes_updated();
    void refresh_failed(const QString& error);

  private:
    FuturesQuoteCache();

    QHash<QString, FuturesQuote> rows_;          // keyed by symbol
    QString                      last_source_;
    QDateTime                    last_refresh_;
    QDateTime                    last_request_;
    bool                         in_flight_ = false;
    QTimer*                      timer_     = nullptr;
    int                          min_interval_ms_ = 5'000;
};

} // namespace fincept::screens::futures
