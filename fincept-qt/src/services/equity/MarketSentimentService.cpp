#include "services/equity/MarketSentimentService.h"

#include "core/logging/Logger.h"
#include "services/equity/MarketSentimentSupport.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/DataSourceRepository.h"
#include "storage/repositories/PortfolioRepository.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkReply>
#include <QUrlQuery>

#include <memory>
#include <utility>

namespace fincept::services::equity {

namespace {

constexpr auto kApiBase = "https://api.adanos.org";

struct PendingSnapshotRequest {
    quint64 request_id = 0;
    QString symbol;      // the ticker the UI is waiting on
    QStringList batch;   // every ticker this request paid for
    int days = 7;
    int pending = 0;
    QMap<QString, QJsonObject> payloads; // raw compare payload per source
};

QString cache_key_for_symbol(const QString& symbol, int days) {
    return QString("equity:adanos:sentiment:%1:%2").arg(symbol.toUpper(), QString::number(days));
}

QByteArray variant_to_json_bytes(const QVariant& value) {
    if (value.canConvert<QByteArray>()) {
        return value.toByteArray();
    }
    return value.toString().toUtf8();
}

} // namespace

MarketSentimentService& MarketSentimentService::instance() {
    static MarketSentimentService service;
    return service;
}

MarketSentimentService::MarketSentimentService(QObject* parent) : QObject(parent) {
    nam_ = new QNetworkAccessManager(this);
}

bool MarketSentimentService::is_configured() const {
    return load_connection().configured;
}

MarketSentimentService::ConnectionConfig MarketSentimentService::load_connection() const {
    const auto result = DataSourceRepository::instance().list_all();
    if (result.is_err()) {
        return {};
    }

    for (const auto& source : result.value()) {
        if (source.provider != sentiment::kProviderId || !source.enabled) {
            continue;
        }
        const auto config = QJsonDocument::fromJson(source.config.toUtf8()).object();
        const auto api_key = config.value("apiKey").toString().trimmed();
        if (!api_key.isEmpty()) {
            return {true, api_key};
        }
    }

    return {};
}

QNetworkRequest MarketSentimentService::build_request(const QUrl& url, const QString& api_key) const {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "finterm/4.0");
    request.setRawHeader("X-API-Key", api_key.toUtf8());
    return request;
}

QStringList MarketSentimentService::holdings_symbols() const {
    QStringList symbols;
    auto& repo = PortfolioRepository::instance();
    const auto portfolios = repo.list_portfolios();
    if (portfolios.is_err()) {
        return symbols;
    }
    for (const auto& portfolio : portfolios.value()) {
        const auto assets = repo.get_assets(portfolio.id);
        if (assets.is_err()) {
            continue;
        }
        for (const auto& asset : assets.value()) {
            const QString symbol = asset.symbol.trimmed().toUpper();
            // A closed or short row isn't a holding to watch sentiment on.
            if (symbol.isEmpty() || asset.quantity <= 0 || symbols.contains(symbol)) {
                continue;
            }
            symbols.append(symbol);
        }
    }
    return symbols;
}

void MarketSentimentService::fetch_snapshot(const QString& symbol, int days, bool force) {
    const QString normalized_symbol = symbol.trimmed().toUpper();
    if (normalized_symbol.isEmpty()) {
        return;
    }

    const auto connection = load_connection();
    if (!connection.configured) {
        MarketSentimentSnapshot snapshot;
        snapshot.symbol = normalized_symbol;
        snapshot.status = "not_configured";
        snapshot.message = "Configure Adanos Market Sentiment in Data Sources → Alternative Data to enable this view.";
        emit snapshot_loaded(normalized_symbol, snapshot);
        return;
    }

    const QString key = cache_key_for_symbol(normalized_symbol, days);
    if (!force && CacheManager::instance().has(key)) {
        const auto cached = variant_to_json_bytes(CacheManager::instance().get(key));
        const auto doc = QJsonDocument::fromJson(cached);
        if (doc.isObject()) {
            auto snapshot = sentiment::snapshot_from_json(doc.object());
            snapshot.configured = true;
            emit snapshot_loaded(normalized_symbol, snapshot);
            return;
        }
    }

    // Remember what the user looks at, so the next paid request can warm those
    // symbols too.
    recent_symbols_.removeAll(normalized_symbol);
    recent_symbols_.prepend(normalized_symbol);
    while (recent_symbols_.size() > kMaxBatchTickers * 2) {
        recent_symbols_.removeLast();
    }

    // One request per platform covers up to ten tickers, so fill the batch with
    // symbols the user is likely to open next. They cost nothing extra and
    // spare four requests each next time.
    //
    // Holdings first: a position the user owns is a better bet than a ticker
    // they glanced at, and this is what makes the very first request useful —
    // with browsing history alone the opening fetch warms exactly one symbol.
    // Read straight from the repository (a local SQLite read, no quotes, no
    // network) rather than going through PortfolioService, which would drag a
    // quote refresh along behind it.
    QStringList batch{normalized_symbol};
    const auto append_candidate = [&](const QString& raw) {
        const QString candidate = raw.trimmed().toUpper();
        if (candidate.isEmpty() || batch.size() >= kMaxBatchTickers || batch.contains(candidate)) {
            return;
        }
        // Symbols with no coverage cache an "unavailable" snapshot like any
        // other, so money-market funds and the like drop out of the batch
        // after their first appearance instead of squatting a slot forever.
        if (CacheManager::instance().has(cache_key_for_symbol(candidate, days))) {
            return;
        }
        batch.append(candidate);
    };

    for (const auto& symbol_from_book : holdings_symbols()) {
        append_candidate(symbol_from_book);
    }
    for (const auto& candidate : std::as_const(recent_symbols_)) {
        append_candidate(candidate);
    }

    active_request_id_ += 1;
    const auto request_id = active_request_id_;

    auto pending = std::make_shared<PendingSnapshotRequest>();
    pending->request_id = request_id;
    pending->symbol = normalized_symbol;
    pending->batch = batch;
    pending->days = days;
    pending->pending = sentiment::source_ids().size();

    auto finalize = [this, pending]() {
        // Note there is no early-out on a superseded request here. The rows
        // have already been paid for out of a 250-a-month budget, so they get
        // cached regardless; only the emit is suppressed, so a stale response
        // can't paint over the symbol the user has since moved to.
        const bool is_current = (pending->request_id == active_request_id_);

        const auto ordered_sources = sentiment::source_ids();

        // Every ticker the response carried is cached, not just the one that
        // was asked for — the request has already been paid for either way.
        QStringList tickers = pending->batch;
        for (auto it = pending->payloads.cbegin(); it != pending->payloads.cend(); ++it) {
            for (const auto& ticker : sentiment::tickers_in_compare_payload(it.value())) {
                if (!tickers.contains(ticker)) {
                    tickers.append(ticker);
                }
            }
        }

        for (const auto& ticker : std::as_const(tickers)) {
            MarketSentimentSnapshot snapshot;
            snapshot.symbol = ticker;
            snapshot.configured = true;

            double total_buzz = 0.0;
            double total_bullish = 0.0;
            int coverage = 0;

            snapshot.sources.reserve(ordered_sources.size());
            for (const auto& source_id : ordered_sources) {
                auto source = sentiment::parse_compare_payload_for(source_id, ticker,
                                                                   pending->payloads.value(source_id));
                if (source.source_id.isEmpty()) {
                    source.source_id = source_id;
                    source.label = sentiment::source_label(source_id);
                }
                snapshot.sources.append(source);
                if (source.available) {
                    total_buzz += source.buzz_score;
                    total_bullish += source.bullish_pct;
                    coverage += 1;
                }
            }

            snapshot.coverage = coverage;
            snapshot.source_alignment = sentiment::compute_source_alignment(snapshot.sources);
            snapshot.fetched_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

            if (coverage > 0) {
                snapshot.available = true;
                snapshot.status = "ok";
                snapshot.average_buzz = total_buzz / coverage;
                snapshot.average_bullish_pct = total_bullish / coverage;
            } else {
                snapshot.available = false;
                snapshot.status = "unavailable";
                snapshot.message = "No Adanos market sentiment snapshot is available for this symbol yet.";
            }

            CacheManager::instance().put(
                cache_key_for_symbol(ticker, pending->days),
                QVariant(QJsonDocument(sentiment::snapshot_to_json(snapshot)).toJson(QJsonDocument::Compact)),
                kCacheTtlSec,
                "equity");

            if (is_current && ticker == pending->symbol) {
                emit snapshot_loaded(pending->symbol, snapshot);
            }
        }
    };

    for (const auto& source_id : sentiment::source_ids()) {
        QUrl url(QString("%1/%2/stocks/v1/compare").arg(kApiBase, source_id));
        QUrlQuery query;
        query.addQueryItem("tickers", batch.join(QLatin1Char(',')));
        query.addQueryItem("days", QString::number(days));
        url.setQuery(query);

        auto* reply = nam_->get(build_request(url, connection.api_key));
        connect(reply, &QNetworkReply::finished, this, [this, reply, pending, source_id, finalize]() {
            const QUrl request_url = reply->request().url();
            const int status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();
            reply->deleteLater();

            if (reply->error() == QNetworkReply::NoError) {
                const auto doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    pending->payloads.insert(source_id, doc.object());
                }
            } else {
                LOG_WARN(
                    "MarketSentiment",
                    QString("Adanos request failed for %1 (%2): %3")
                        .arg(source_id, request_url.toString(QUrl::RemoveQuery), reply->errorString()));
                if (pending->request_id == active_request_id_) {
                    emit error_occurred(
                        "market_sentiment",
                        QString("Adanos source %1 returned HTTP %2.").arg(source_id, QString::number(status_code)));
                }
            }

            pending->pending -= 1;
            if (pending->pending == 0) {
                finalize();
            }
        });
    }
}

} // namespace fincept::services::equity
