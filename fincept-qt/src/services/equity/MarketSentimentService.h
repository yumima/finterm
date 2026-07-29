#pragma once

#include "services/equity/EquityResearchModels.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>

namespace fincept::services::equity {

class MarketSentimentService : public QObject {
    Q_OBJECT
  public:
    static MarketSentimentService& instance();

    void fetch_snapshot(const QString& symbol, int days = 7, bool force = false);
    bool is_configured() const;

  signals:
    void snapshot_loaded(QString symbol, fincept::services::equity::MarketSentimentSnapshot snapshot);
    void error_occurred(QString context, QString message);

  private:
    explicit MarketSentimentService(QObject* parent = nullptr);
    Q_DISABLE_COPY(MarketSentimentService)

    struct ConnectionConfig {
        bool configured = false;
        QString api_key;
    };

    ConnectionConfig load_connection() const;

    QNetworkRequest build_request(const QUrl& url, const QString& api_key) const;

    /// Every symbol held across the user's portfolios, read straight from the
    /// repository. Used to seed the request batch so the free tier's requests
    /// land on names the user actually owns.
    QStringList holdings_symbols() const;

    QNetworkAccessManager* nam_ = nullptr;
    quint64 active_request_id_ = 0;

    // Adanos' free tier allows 250 requests a month and this view spends one
    // per platform, so a 10-minute TTL — flipping between two tickers a few
    // times — could burn a month's budget in an afternoon. Sentiment here is a
    // rolling multi-day window that updates a few times a day; 6 hours loses
    // nothing real, and the REFRESH button still forces a live pull.
    static constexpr int kCacheTtlSec = 6 * 60 * 60;
    /// `/compare` accepts up to 10 tickers for the price of one request.
    static constexpr int kMaxBatchTickers = 10;

    /// Symbols recently opened in Equity Research, most-recent-first. A
    /// cache-filling request piggybacks these onto the batch: the four calls a
    /// single symbol would have cost now warm up to ten, which is the
    /// difference between ~62 and ~620 symbol-views a month on the free tier.
    QStringList recent_symbols_;
};

} // namespace fincept::services::equity
