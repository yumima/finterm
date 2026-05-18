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

namespace fincept::services::finnhub {

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
    auto r = fincept::SecureStorage::instance().retrieve(QStringLiteral("FINNHUB_API_KEY"));
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

void FinnhubService::subscribe_ipo_calendar(QObject* owner, const QDate& from, const QDate& to,
                                              query::QueryStore::Callback cb) {
    const QString from_s = from.toString(Qt::ISODate);
    const QString to_s   = to.toString(Qt::ISODate);
    const QString key = QString("finnhub:ipo_calendar:%1:%2").arg(from_s, to_s);
    auto fetcher = [this, from_s, to_s](query::QueryStore::Resolver resolve,
                                          query::QueryStore::Rejecter reject) {
        // Local cache check first.
        const QVariant cv = fincept::CacheManager::instance().get(
            "finnhub:ipo_calendar:" + from_s + ":" + to_s);
        if (!cv.isNull()) {
            const auto doc = QJsonDocument::fromJson(cv.toString().toUtf8());
            if (doc.isObject()) {
                resolve(QVariant::fromValue(parse_ipo_calendar(doc.object())));
                return;
            }
        }
        QPointer<FinnhubService> self(this);
        python::PythonRunner::instance().run(
            kScript, {QStringLiteral("ipo_calendar"), from_s, to_s},
            [self, from_s, to_s, resolve, reject](python::PythonResult result) {
                if (!self) { reject("service destroyed"); return; }
                if (!result.success) { reject(result.error); return; }
                const auto doc = decode_result(result.output);
                if (!doc.isObject()) {
                    reject("invalid JSON from finnhub_data.py");
                    return;
                }
                const auto obj = doc.object();
                if (obj.contains("error")) {
                    // Common case: "no api key" → resolve empty so the UI
                    // doesn't error-banner. Real errors still resolve empty
                    // for the same reason; logs capture the detail.
                    LOG_INFO("Finnhub", "ipo_calendar: " + obj["error"].toString());
                    resolve(QVariant::fromValue(QVector<FinnhubIPO>{}));
                    return;
                }
                fincept::CacheManager::instance().put(
                    "finnhub:ipo_calendar:" + from_s + ":" + to_s,
                    QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                    kIpoCalendarTtlSec, "finnhub");
                resolve(QVariant::fromValue(self->parse_ipo_calendar(obj)));
            });
    };
    query::QueryStore::instance().subscribe(owner, key, kIpoCalendarTtlSec,
                                            kIpoCalendarTtlSec * 4,
                                            std::move(cb), std::move(fetcher));
}

void FinnhubService::subscribe_lockups(QObject* owner, const QDate& from, const QDate& to,
                                        const QString& symbol,
                                        query::QueryStore::Callback cb) {
    const QString from_s = from.toString(Qt::ISODate);
    const QString to_s   = to.toString(Qt::ISODate);
    const QString sym    = symbol.toUpper();
    const QString key = QString("finnhub:lockups:%1:%2:%3").arg(from_s, to_s, sym);
    auto fetcher = [this, from_s, to_s, sym](query::QueryStore::Resolver resolve,
                                              query::QueryStore::Rejecter reject) {
        const QVariant cv = fincept::CacheManager::instance().get(
            "finnhub:lockups:" + from_s + ":" + to_s + ":" + sym);
        if (!cv.isNull()) {
            const auto doc = QJsonDocument::fromJson(cv.toString().toUtf8());
            if (doc.isObject()) {
                resolve(QVariant::fromValue(parse_lockups(doc.object())));
                return;
            }
        }
        QPointer<FinnhubService> self(this);
        QStringList args = {QStringLiteral("lockup_calendar"), from_s, to_s};
        if (!sym.isEmpty()) args << sym;
        python::PythonRunner::instance().run(
            kScript, args,
            [self, from_s, to_s, sym, resolve, reject](python::PythonResult result) {
                if (!self) { reject("service destroyed"); return; }
                if (!result.success) { reject(result.error); return; }
                const auto doc = decode_result(result.output);
                if (!doc.isObject()) { reject("invalid JSON"); return; }
                const auto obj = doc.object();
                if (obj.contains("error")) {
                    LOG_INFO("Finnhub", "lockup_calendar: " + obj["error"].toString());
                    resolve(QVariant::fromValue(QVector<FinnhubLockup>{}));
                    return;
                }
                fincept::CacheManager::instance().put(
                    "finnhub:lockups:" + from_s + ":" + to_s + ":" + sym,
                    QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                    kLockupTtlSec, "finnhub");
                resolve(QVariant::fromValue(self->parse_lockups(obj)));
            });
    };
    query::QueryStore::instance().subscribe(owner, key, kLockupTtlSec, kLockupTtlSec * 4,
                                            std::move(cb), std::move(fetcher));
}

void FinnhubService::subscribe_insider_tx(QObject* owner, const QString& symbol,
                                            const QDate& from, const QDate& to,
                                            query::QueryStore::Callback cb) {
    const QString sym    = symbol.toUpper();
    const QString from_s = from.isValid() ? from.toString(Qt::ISODate) : QString();
    const QString to_s   = to.isValid()   ? to.toString(Qt::ISODate)   : QString();
    const QString key = QString("finnhub:insider_tx:%1:%2:%3").arg(sym, from_s, to_s);
    auto fetcher = [this, sym, from_s, to_s](query::QueryStore::Resolver resolve,
                                              query::QueryStore::Rejecter reject) {
        const QVariant cv = fincept::CacheManager::instance().get(
            "finnhub:insider_tx:" + sym + ":" + from_s + ":" + to_s);
        if (!cv.isNull()) {
            const auto doc = QJsonDocument::fromJson(cv.toString().toUtf8());
            if (doc.isObject()) {
                resolve(QVariant::fromValue(parse_insider_tx(doc.object())));
                return;
            }
        }
        QPointer<FinnhubService> self(this);
        QStringList args = {QStringLiteral("insider_tx"), sym};
        if (!from_s.isEmpty()) args << from_s;
        if (!to_s.isEmpty())   args << to_s;
        python::PythonRunner::instance().run(
            kScript, args,
            [self, sym, from_s, to_s, resolve, reject](python::PythonResult result) {
                if (!self) { reject("service destroyed"); return; }
                if (!result.success) { reject(result.error); return; }
                const auto doc = decode_result(result.output);
                if (!doc.isObject()) { reject("invalid JSON"); return; }
                const auto obj = doc.object();
                if (obj.contains("error")) {
                    LOG_INFO("Finnhub", "insider_tx: " + obj["error"].toString());
                    resolve(QVariant::fromValue(QVector<FinnhubInsiderTx>{}));
                    return;
                }
                fincept::CacheManager::instance().put(
                    "finnhub:insider_tx:" + sym + ":" + from_s + ":" + to_s,
                    QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                    kInsiderTxTtlSec, "finnhub");
                resolve(QVariant::fromValue(self->parse_insider_tx(obj)));
            });
    };
    query::QueryStore::instance().subscribe(owner, key, kInsiderTxTtlSec,
                                            kInsiderTxTtlSec * 4,
                                            std::move(cb), std::move(fetcher));
}

void FinnhubService::subscribe_recommendations(QObject* owner, const QString& symbol,
                                                 query::QueryStore::Callback cb) {
    const QString sym = symbol.toUpper();
    const QString key = "finnhub:recommendations:" + sym;
    auto fetcher = [this, sym, key](query::QueryStore::Resolver resolve,
                                     query::QueryStore::Rejecter reject) {
        const QVariant cv = fincept::CacheManager::instance().get(key);
        if (!cv.isNull()) {
            const auto doc = QJsonDocument::fromJson(cv.toString().toUtf8());
            // The endpoint returns a bare array — handle both shapes.
            const auto arr = doc.isArray()  ? doc.array()
                          : doc.isObject() ? doc.object().value("data").toArray()
                                           : QJsonArray{};
            resolve(QVariant::fromValue(parse_recommendations(arr)));
            return;
        }
        QPointer<FinnhubService> self(this);
        python::PythonRunner::instance().run(
            kScript, {QStringLiteral("recommendation_trends"), sym},
            [self, sym, resolve, reject](python::PythonResult result) {
                if (!self) { reject("service destroyed"); return; }
                if (!result.success) { reject(result.error); return; }
                const auto doc = decode_result(result.output);
                if (doc.isObject() && doc.object().contains("error")) {
                    LOG_INFO("Finnhub", "recommendations: " +
                                doc.object()["error"].toString());
                    resolve(QVariant::fromValue(QVector<FinnhubRecTrend>{}));
                    return;
                }
                const auto arr = doc.isArray() ? doc.array() : QJsonArray{};
                fincept::CacheManager::instance().put(
                    "finnhub:recommendations:" + sym,
                    QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
                    kRecTrendsTtlSec, "finnhub");
                resolve(QVariant::fromValue(self->parse_recommendations(arr)));
            });
    };
    query::QueryStore::instance().subscribe(owner, key, kRecTrendsTtlSec,
                                            kRecTrendsTtlSec * 3,
                                            std::move(cb), std::move(fetcher));
}

// ── Parsers ──────────────────────────────────────────────────────────────────

QVector<FinnhubIPO> FinnhubService::parse_ipo_calendar(const QJsonObject& root) const {
    QVector<FinnhubIPO> out;
    const auto arr = root.value("ipoCalendar").toArray();
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FinnhubIPO e;
        e.symbol      = o.value("symbol").toString();
        e.name        = o.value("name").toString();
        e.date        = QDate::fromString(o.value("date").toString(), Qt::ISODate);
        e.exchange    = o.value("exchange").toString();
        e.status      = o.value("status").toString();
        // Finnhub returns price either as a single-value or "lo-hi" string.
        const QString price = o.value("price").toString();
        if (price.contains('-')) {
            const auto parts = price.split('-');
            if (parts.size() == 2) {
                e.price_low  = parts[0].trimmed().toDouble();
                e.price_high = parts[1].trimmed().toDouble();
            }
        } else {
            e.price_low = e.price_high = price.toDouble();
        }
        e.shares      = static_cast<qint64>(o.value("numberOfShares").toDouble());
        e.total_value = static_cast<qint64>(o.value("totalSharesValue").toDouble());
        if (!e.symbol.isEmpty())
            out.append(e);
    }
    return out;
}

QVector<FinnhubLockup> FinnhubService::parse_lockups(const QJsonObject& root) const {
    QVector<FinnhubLockup> out;
    const auto arr = root.value("data").toArray();
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FinnhubLockup e;
        e.symbol          = o.value("symbol").toString();
        e.expiration_date = QDate::fromString(o.value("expirationDate").toString(), Qt::ISODate);
        e.shares          = static_cast<qint64>(o.value("share").toDouble());
        e.related_event   = o.value("relatedEvent").toString();
        if (!e.symbol.isEmpty())
            out.append(e);
    }
    return out;
}

QVector<FinnhubInsiderTx> FinnhubService::parse_insider_tx(const QJsonObject& root) const {
    QVector<FinnhubInsiderTx> out;
    const auto arr = root.value("data").toArray();
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FinnhubInsiderTx e;
        e.name              = o.value("name").toString();
        e.shares            = static_cast<qint64>(o.value("share").toDouble());
        e.change            = static_cast<qint64>(o.value("change").toDouble());
        e.transaction_date  = QDate::fromString(o.value("transactionDate").toString(), Qt::ISODate);
        e.transaction_price = o.value("transactionPrice").toDouble();
        e.code              = o.value("transactionCode").toString();
        out.append(e);
    }
    return out;
}

QVector<FinnhubRecTrend> FinnhubService::parse_recommendations(const QJsonArray& arr) const {
    QVector<FinnhubRecTrend> out;
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FinnhubRecTrend e;
        e.period       = o.value("period").toString();
        e.strong_buy   = o.value("strongBuy").toInt();
        e.buy          = o.value("buy").toInt();
        e.hold         = o.value("hold").toInt();
        e.sell         = o.value("sell").toInt();
        e.strong_sell  = o.value("strongSell").toInt();
        out.append(e);
    }
    return out;
}

} // namespace fincept::services::finnhub
