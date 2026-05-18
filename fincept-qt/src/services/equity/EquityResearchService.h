// src/services/equity/EquityResearchService.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

namespace fincept::services::equity {

class EquityResearchService : public QObject {
    Q_OBJECT
  public:
    static EquityResearchService& instance();

    // ── Async API — results delivered via signals ─────────────────────────────
    void search_symbols(const QString& query);
    void schedule_search(const QString& query); // debounced entry point

    /// Fetches quote + info + historical in parallel (three Python calls)
    void load_symbol(const QString& symbol, const QString& period = "1y");

    /// Quote-only refresh — used by the 20s refresh timer and showEvent.
    /// load_symbol() unnecessarily re-fires info + historical on every tick
    /// (info hits cache cheaply but historical periodically misses TTL,
    /// causing a visible chart hiccup every ~10min). The refresh path should
    /// only freshen the quote; info/historical refresh on their own slower
    /// cadence or on explicit user action.
    void fetch_quote(const QString& symbol);

    // ── QueryStore-backed subscription API ────────────────────────────────────
    // Per-subscriber stream replacing the broadcast quote_loaded /
    // info_loaded / historical_loaded signals. Consumers receive a
    // QueryStore::State on every transition (loading / data / error). The
    // broadcast signals continue to fire alongside — non-migrated tabs
    // (Analysis, Technicals, News, …) keep working until their own
    // migration PR lands.

    /// Subscribe to the quote stream for `symbol`. State carries a
    /// QuoteData via `state.data.value<QuoteData>()` once available.
    void subscribe_quote(QObject* owner, const QString& symbol,
                         query::QueryStore::Callback cb);
    /// Subscribe to the info stream for `symbol`. State carries StockInfo.
    void subscribe_info(QObject* owner, const QString& symbol,
                        query::QueryStore::Callback cb);
    /// Subscribe to the historical-candles stream for (`symbol`, `period`).
    /// State carries QVector<Candle>.
    void subscribe_historical(QObject* owner, const QString& symbol, const QString& period,
                              query::QueryStore::Callback cb);

    /// Warm the historical cache for (`symbol`, `period`) without binding a
    /// subscriber. No-op if already cached fresh or a fetch is in flight.
    /// Used by the ER overview tab on symbol-land / period-switch to make
    /// an adjacent period (e.g. 5Y when viewing 1Y) instant on next click.
    void prefetch_historical(const QString& symbol, const QString& period);

    void fetch_financials(const QString& symbol);
    void fetch_technicals(const QString& symbol, const QString& period = "1y");
    void fetch_peers(const QString& symbol, const QStringList& peer_symbols);
    void fetch_news(const QString& symbol, int count = 20);

    /// TALIpp tab: fetch historical then run equity_talipp.py
    void compute_talipp(const QString& symbol, const QString& indicator, const QVariantMap& params,
                        const QString& period = "2y");

  signals:
    void search_results_loaded(QVector<fincept::services::equity::SearchResult> results);
    void quote_loaded(fincept::services::equity::QuoteData quote);
    void info_loaded(fincept::services::equity::StockInfo info);
    // period is one of "1mo", "3mo", "6mo", "1y", "5y" — receivers must
    // filter to ignore stale emissions from a different period than the
    // one currently selected (e.g. a refresh-timer reload that uses the
    // default "1y" must not overwrite a chart the user has just switched
    // to "1mo").
    void historical_loaded(QString symbol, QString period, QVector<fincept::services::equity::Candle> candles);
    void financials_loaded(fincept::services::equity::FinancialsData data);
    void technicals_loaded(fincept::services::equity::TechnicalsData data);
    void peers_loaded(QString symbol, QVector<fincept::services::equity::PeerData> peers);
    void news_loaded(QString symbol, QVector<fincept::services::equity::NewsArticle> articles);
    void talipp_result(QString indicator, QVector<double> values, QVector<qint64> timestamps);
    void error_occurred(QString symbol, QString context, QString message);

  private:
    explicit EquityResearchService(QObject* parent = nullptr);
    Q_DISABLE_COPY(EquityResearchService)

    // ── Python helpers ────────────────────────────────────────────────────────
    void run_python(const QString& script, const QStringList& args, std::function<void(bool, const QString&)> cb);
    /// Persistent yfinance daemon path — same shape as run_python but skips
    /// the ~1.5s pandas/yfinance cold-import per call. Callback receives the
    /// daemon's `result` field already parsed as a QJsonObject (or wrapped
    /// under "_value" if the action returns a non-object).
    void run_daemon(const QString& action, const QJsonObject& payload,
                    std::function<void(bool, QJsonObject, QString)> cb);

    /// In-flight request dedup. Returns true if the key was successfully
    /// acquired (caller proceeds with the daemon request). Returns false
    /// if a request for the same key is already in flight — the caller
    /// should return early; the existing in-flight request will fan out
    /// its result via the matching `*_loaded` signal so all subscribers
    /// (including this caller's tab) still get the data.
    bool acquire_inflight(const QString& key);
    void release_inflight(const QString& key);

    // ── Parsers ───────────────────────────────────────────────────────────────
    QuoteData parse_quote(const QJsonObject& obj) const;
    StockInfo parse_info(const QJsonObject& obj) const;
    QVector<Candle> parse_candles(const QJsonArray& arr) const;
    FinancialsData parse_financials(const QJsonObject& obj) const;
    TechnicalsData parse_technicals(const QString& symbol, const QString& period, const QJsonArray& rows) const;
    TechSignal score_indicator(const QString& name, double value, double sma20, double sma50) const;
    QVector<PeerData> parse_peers(const QJsonArray& arr) const;
    QVector<NewsArticle> parse_news(const QJsonArray& arr) const;

    // ── Cache TTLs — delegated to CacheManager ───────────────────────────────
    static constexpr int kQuoteTtlSec = 60;
    static constexpr int kInfoTtlSec = 1800;       // fundamentals barely change minute-to-minute
    static constexpr int kHistoricalTtlSec = 600;  // daily candles stable within 10 min
    static constexpr int kTechnicalsTtlSec = 600; // computed — more expensive than raw candles
    static constexpr int kNewsTtlSec = 180;
    // Financials are quarterly — no need to refetch within a session.
    static constexpr int kFinancialsTtlSec = 3600;
    // Peer fundamentals (P/E, P/B, ROE, …) move slowly — same hour-long TTL.
    static constexpr int kPeersTtlSec = 3600;

    // ── In-flight dedup state ─────────────────────────────────────────────────
    QSet<QString> in_flight_keys_;

    // ── Debounce ──────────────────────────────────────────────────────────────
    static constexpr int kDebounceMs = 350;
    QTimer* search_debounce_ = nullptr;
    QString pending_query_;
};

} // namespace fincept::services::equity
