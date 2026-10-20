#pragma once
// SectorResolver.h — Singleton that maps symbols to sector labels.
//
// Resolution order per symbol:
//   1. In-memory cache (populated from `sector_cache` SQLite table at startup).
//   2. Explicit override passed via remember().
//   3. yfinance `info` lookup through MarketDataService. We retry with a `.TO`
//      suffix for Canadian TSX symbols that didn't resolve bare.
//   4. Persistent fallback "Unclassified".
//
// Negative hits (yfinance returns N/A or request fails) are cached with a
// 7-day TTL so we don't hammer the script every launch for tickers that
// genuinely don't have a sector (mutual funds, cash, obscure ETFs).
//
// The resolver is sync-for-cache-hits and async-for-misses. Consumers should
// either:
//   - call sector_for() (returns empty for unknowns, triggers async fetch)
//   - subscribe to sector_resolved() to be notified when the fetch lands.

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

namespace fincept::services {

class SectorResolver : public QObject {
    Q_OBJECT

  public:
    static SectorResolver& instance();

    // Synchronous lookup. Returns the cached sector or an empty string if
    // unknown. When empty, triggers an async fetch (unless already in flight)
    // — subscribe to sector_resolved() to get the result.
    QString sector_for(const QString& symbol);

    // Remember an authoritative sector (e.g. from portfolio import JSON).
    // Overwrites any cached value and persists to disk.
    void remember(const QString& symbol, const QString& sector);

    // Kick off resolution for multiple symbols at once. Results fan out via
    // sector_resolved() as they come back.
    void prefetch(const QStringList& symbols);

  signals:
    void sector_resolved(QString symbol, QString sector);

  private:
    SectorResolver();
    ~SectorResolver() override = default;
    SectorResolver(const SectorResolver&) = delete;
    SectorResolver& operator=(const SectorResolver&) = delete;

    void load_cache();
    void persist(const QString& symbol, const QString& sector, const QString& industry,
                 const QString& quote_type);
    void resolve_async(const QString& symbol);
    static QString normalize(const QString& symbol);
    static QString fallback_for_unresolvable(const QString& symbol);
    // No answer from Yahoo is not "no sector": nothing is cached or stored,
    // and the symbol is asked again after `secs`.
    void defer(const QString& symbol, qint64 secs);

    mutable QMutex mutex_;
    QHash<QString, QString> cache_;   // symbol → sector
    QSet<QString> inflight_;           // symbols currently being fetched
    QHash<QString, qint64> retry_after_; // symbol → epoch secs: unanswered, ask again then
};

} // namespace fincept::services
