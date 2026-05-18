// src/services/finnhub/FinnhubService.h
#pragma once
#include "services/query/QueryStore.h"

#include <QDate>
#include <QObject>
#include <QString>
#include <QVector>

namespace fincept::services::finnhub {

// ── Data models ──────────────────────────────────────────────────────────────
// Per-endpoint structs sized to what the UI actually consumes. We don't try
// to mirror Finnhub's full response — drop fields the screens never read.

/// One row from /calendar/ipo. `status` ∈ {filed, expected, priced, withdrawn}.
struct FinnhubIPO {
    QString symbol;
    QString name;
    QDate   date;              // expected pricing date (or actual if status="priced")
    QString exchange;          // "NYSE", "NASDAQ Global", "AMEX", …
    QString status;            // filed / expected / priced / withdrawn
    double  price_low = 0.0;
    double  price_high = 0.0;
    qint64  shares = 0;
    qint64  total_value = 0;   // shares × midpoint price (Finnhub-provided)
};

/// One row from /stock/lockup. `related_event` describes the trigger
/// (e.g. "ipo"), `share` is the number of insider shares unlocking.
struct FinnhubLockup {
    QString symbol;
    QDate   expiration_date;
    qint64  shares = 0;
    QString related_event;
};

/// SEC Form 4 row from /stock/insider-transactions.
/// `code` is the SEC transaction code: S=sale, P=purchase, F=tax-related
/// disposition, M=option exercise, G=gift, etc.
struct FinnhubInsiderTx {
    QString name;              // insider name
    qint64  shares = 0;        // resulting holdings after this transaction
    qint64  change = 0;        // signed delta (negative = sale)
    QDate   transaction_date;
    double  transaction_price = 0.0;
    QString code;              // SEC code
};

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
/// providers: multi-exchange IPO calendar, lockup expiries, SEC Form 4
/// insider transactions, analyst-recommendation time series. Other endpoints
/// in finnhub_data.py (quote, profile, news) are duplicated by Yahoo so we
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

    /// IPO calendar between two dates. State carries QVector<FinnhubIPO>.
    void subscribe_ipo_calendar(QObject* owner, const QDate& from, const QDate& to,
                                 query::QueryStore::Callback cb);

    /// Lockup expiries between two dates (optionally filtered to one symbol).
    /// State carries QVector<FinnhubLockup>.
    void subscribe_lockups(QObject* owner, const QDate& from, const QDate& to,
                           const QString& symbol,
                           query::QueryStore::Callback cb);

    /// SEC Form 4 transactions for `symbol`. State carries
    /// QVector<FinnhubInsiderTx>.
    void subscribe_insider_tx(QObject* owner, const QString& symbol,
                              const QDate& from, const QDate& to,
                              query::QueryStore::Callback cb);

    /// Analyst-recommendation monthly trend. State carries
    /// QVector<FinnhubRecTrend>.
    void subscribe_recommendations(QObject* owner, const QString& symbol,
                                    query::QueryStore::Callback cb);

  private:
    explicit FinnhubService(QObject* parent = nullptr);
    Q_DISABLE_COPY(FinnhubService)

    // Parsers — pure transformers, called from the QueryStore fetcher
    // closures after the Python subprocess result is decoded.
    QVector<FinnhubIPO>        parse_ipo_calendar(const QJsonObject& root) const;
    QVector<FinnhubLockup>     parse_lockups(const QJsonObject& root) const;
    QVector<FinnhubInsiderTx>  parse_insider_tx(const QJsonObject& root) const;
    QVector<FinnhubRecTrend>   parse_recommendations(const QJsonArray& arr) const;

    // ── Cache TTLs ───────────────────────────────────────────────────────────
    // All Finnhub endpoints we use return slow-moving data; generous TTLs
    // keep network load light without sacrificing freshness.
    static constexpr int kIpoCalendarTtlSec  = 3600;       // 1h — calendar updates daily
    static constexpr int kLockupTtlSec       = 3600;       // 1h
    static constexpr int kInsiderTxTtlSec    = 1800;       // 30min — Form 4s file in bursts
    static constexpr int kRecTrendsTtlSec    = 86400;      // 24h — monthly bucket
};

} // namespace fincept::services::finnhub

// ── QVariant interop for QueryStore ──────────────────────────────────────────
#include <QMetaType>
Q_DECLARE_METATYPE(QVector<fincept::services::finnhub::FinnhubIPO>)
Q_DECLARE_METATYPE(QVector<fincept::services::finnhub::FinnhubLockup>)
Q_DECLARE_METATYPE(QVector<fincept::services::finnhub::FinnhubInsiderTx>)
Q_DECLARE_METATYPE(QVector<fincept::services::finnhub::FinnhubRecTrend>)
