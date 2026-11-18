// src/services/finnhub/FinnhubService.cpp
#include "services/finnhub/FinnhubService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"
#include "storage/secure/SecureStorage.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace finterm::services::finnhub {

namespace {
constexpr const char* kScript = "finnhub_data.py";

// Lift PythonRunner's "find last JSON line" helper for our subprocess
// results. The finnhub_data.py main() prints `json.dumps(result)` exactly
// once at the end, so parsing the full stdout works — but use extract_json
// for parity with how PreIpoService consumes its Python output (it tolerates
// stray log lines before the JSON).
QJsonDocument decode_result(const QString& stdout_text) {
    const QString js = python::extract_json(stdout_text);
    return QJsonDocument::fromJson(js.toUtf8());
}

// Most Finnhub responses are objects keyed by a data field; some are bare
// arrays. The fetchers below pass whatever shape the script returned through
// to the parser, which knows what to expect.
} // namespace

FinnhubService& FinnhubService::instance() {
    static FinnhubService inst;
    return inst;
}

FinnhubService::FinnhubService(QObject* parent) : QObject(parent) {}

bool FinnhubService::has_api_key() const {
    auto r = finterm::SecureStorage::instance().retrieve(QStringLiteral("FINNHUB_API_KEY"));
    return r.is_ok() && !r.value().isEmpty();
}

// ── Subscriptions ────────────────────────────────────────────────────────────
//
// Common shape for every subscribe_*: build a cache key, build a fetcher
// closure that runs the Python subprocess and resolves the QueryStore with
// the parsed payload, then delegate to QueryStore::subscribe.
//
// The Python script lives outside the persistent yfinance daemon, so each
// call is a fresh process spawn (~150ms Python import overhead amortized
// across the network call). Acceptable for hourly+ TTLs.

void FinnhubService::subscribe_recommendations(QObject* owner, const QString& symbol, query::QueryStore::Callback cb) {
    const QString sym = symbol.toUpper();
    const QString key = "finnhub:recommendations:" + sym;
    auto fetcher = [this, sym, key](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        const QVariant cv = finterm::CacheManager::instance().get(key);
        if (!cv.isNull()) {
            const auto doc = QJsonDocument::fromJson(cv.toString().toUtf8());
            // The endpoint returns a bare array — handle both shapes.
            const auto arr = doc.isArray()    ? doc.array()
                             : doc.isObject() ? doc.object().value("data").toArray()
                                              : QJsonArray{};
            resolve(QVariant::fromValue(parse_recommendations(arr)));
            return;
        }
        QPointer<FinnhubService> self(this);
        python::PythonRunner::instance().run(
            kScript, {QStringLiteral("recommendation_trends"), sym},
            [self, sym, resolve, reject](python::PythonResult result) {
                if (!self) {
                    reject("service destroyed");
                    return;
                }
                if (!result.success) {
                    reject(result.error);
                    return;
                }
                const auto doc = decode_result(result.output);
                if (doc.isObject() && doc.object().contains("error")) {
                    LOG_INFO("Finnhub", "recommendations: " + doc.object()["error"].toString());
                    resolve(QVariant::fromValue(QVector<FinnhubRecTrend>{}));
                    return;
                }
                const auto arr = doc.isArray() ? doc.array() : QJsonArray{};
                finterm::CacheManager::instance().put(
                    "finnhub:recommendations:" + sym,
                    QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))), kRecTrendsTtlSec,
                    "finnhub");
                resolve(QVariant::fromValue(self->parse_recommendations(arr)));
            });
    };
    query::QueryStore::instance().subscribe(owner, key, kRecTrendsTtlSec, kRecTrendsTtlSec * 3, std::move(cb),
                                            std::move(fetcher));
}

// ── Parsers ──────────────────────────────────────────────────────────────────

QVector<FinnhubRecTrend> FinnhubService::parse_recommendations(const QJsonArray& arr) const {
    QVector<FinnhubRecTrend> out;
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FinnhubRecTrend e;
        e.period = o.value("period").toString();
        e.strong_buy = o.value("strongBuy").toInt();
        e.buy = o.value("buy").toInt();
        e.hold = o.value("hold").toInt();
        e.sell = o.value("sell").toInt();
        e.strong_sell = o.value("strongSell").toInt();
        // Period is the time-bucket label — without it the row is meaningless.
        if (!e.period.isEmpty())
            out.append(e);
    }
    return out;
}

} // namespace finterm::services::finnhub
