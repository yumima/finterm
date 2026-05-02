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

    /// Start the periodic refresh timer unconditionally. Safe to call from
    /// multiple screens. Prefer `retain()` / `release()` for visibility-
    /// aware lifetimes — those auto-pause the timer when no consumer is
    /// visible, eliminating background bandwidth use.
    void start_auto_refresh(int interval_ms = kFuturesRefreshIntervalMs);
    void stop_auto_refresh();

    /// Reference-counted timer lifecycle. Call from a consumer's showEvent
    /// (paired with `release()` in hideEvent). When the count goes 0→1 the
    /// timer starts and an immediate refresh is kicked; when 1→0 the
    /// timer stops. Multiple visible consumers share the same single
    /// refresh stream — no fan-out.
    void retain();
    void release();

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
    int                          retain_count_ = 0;
    int                          interval_ms_ = kFuturesRefreshIntervalMs;
};

} // namespace fincept::screens::futures
