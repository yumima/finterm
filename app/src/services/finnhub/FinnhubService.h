// src/services/finnhub/FinnhubService.h
#pragma once
#include "services/query/QueryStore.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QVector>

namespace finterm::services::finnhub {

// ── Data models ──────────────────────────────────────────────────────────────
// Per-endpoint structs sized to what the UI actually consumes. We don't try
// to mirror Finnhub's full response — drop fields the screens never read.

/// One time-bucket from /stock/recommendation. Periods are YYYY-MM-DD
/// strings (start of the analyst-sentiment month).
struct FinnhubRecTrend {
    QString period;
    int strong_buy = 0;
    int buy = 0;
    int hold = 0;
    int sell = 0;
    int strong_sell = 0;
};

// ── Service ──────────────────────────────────────────────────────────────────

/// QueryStore-backed access layer for Finnhub endpoints not covered by Yahoo
/// or Stooq. Wired only to data that's genuinely Finnhub-unique among free
/// providers: the analyst-recommendation time series. Other endpoints in
/// finnhub_data.py (quote, profile, news) are duplicated by Yahoo so we
/// don't surface them here.
///
/// All endpoints gracefully no-op when FINNHUB_API_KEY is unset: the daemon
/// script returns {"error": "no api key", "needs_key": true}, the fetcher
/// resolves with an empty payload, and the UI just skips the Finnhub-backed
/// feature. has_api_key() lets the UI hide the affordance entirely when no
/// key is configured.
class FinnhubService : public QObject {
    Q_OBJECT
  public:
    static FinnhubService& instance();

    /// True if FINNHUB_API_KEY is reachable via SecureStorage. The actual
    /// Python subprocess env injection is handled by PythonRunner; this is
    /// a UI hint so screens can hide Finnhub-only features when no key.
    bool has_api_key() const;

    /// Analyst-recommendation monthly trend. State carries
    /// QVector<FinnhubRecTrend>.
    void subscribe_recommendations(QObject* owner, const QString& symbol, query::QueryStore::Callback cb);

  private:
    explicit FinnhubService(QObject* parent = nullptr);
    Q_DISABLE_COPY(FinnhubService)

    // Parsers — pure transformers, called from the QueryStore fetcher
    // closures after the Python subprocess result is decoded.
    QVector<FinnhubRecTrend> parse_recommendations(const QJsonArray& arr) const;

    // ── Cache TTLs ───────────────────────────────────────────────────────────
    // All Finnhub endpoints we use return slow-moving data; generous TTLs
    // keep network load light without sacrificing freshness.
    static constexpr int kRecTrendsTtlSec = 86400;  // 24h — monthly bucket
};

} // namespace finterm::services::finnhub

// ── QVariant interop for QueryStore ──────────────────────────────────────────
#include <QMetaType>
Q_DECLARE_METATYPE(QVector<finterm::services::finnhub::FinnhubRecTrend>)
