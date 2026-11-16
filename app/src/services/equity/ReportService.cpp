// src/services/equity/ReportService.cpp
#include "services/equity/ReportService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "storage/cache/CacheManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace finterm::services::equity {

namespace {

constexpr int kFilingsTtlSec = 24 * 3600;
// One of the two filings could not be read. That may be a fact (a foreign
// issuer files no 10-K) or a passing EDGAR error, and the script cannot tell
// which — so it is kept just long enough not to refetch on every tab switch.
constexpr int kPartialFilingsTtlSec = 20 * 60;
constexpr int kNarrativeTtlSec = 24 * 3600;
// v3: segment facts tagged on a label-only axis are read (v2 returned an
// empty breakdown for them, and that emptiness would be served for a day).
// v2: adds revenue_breakdown, customers, the filing url and 20-F reports.
// v1: {annual, quarterly} with cleaned, cut sections.
const QString kFilingsKey = QStringLiteral("equity:report_filings:v3:");
// v2: keyed by section, symbol and day, holding {reply, model, written_at}.
// (v1 was keyed by prompt and held raw replies, readable or not.)
const QString kNarrativeKey = QStringLiteral("equity:report_narrative:v2:");

QString narrative_key(const QString& section, const QString& symbol) {
    return kNarrativeKey + section + QLatin1Char(':') + symbol + QLatin1Char(':') +
           QDate::currentDate().toString(Qt::ISODate);
}

} // namespace

void fetch_report_extras(const QString& symbol, QObject* ctx, ReportJsonCallback cb) {
    QPointer<QObject> guard(ctx);
    python::PythonWorker::instance().submit(
        QStringLiteral("report_extras"), QJsonObject{{"symbol", symbol}},
        [guard, cb, symbol](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            if (!ok || result.contains(QStringLiteral("error"))) {
                const QString body = result.value(QStringLiteral("error")).toString();
                const QString why = (err.isEmpty() ? body : err).left(160);
                LOG_WARN("Report", QStringLiteral("extras %1 failed: %2").arg(symbol, why));
                cb(false, {}, why.isEmpty() ? QStringLiteral("no answer") : why);
                return;
            }
            cb(true, result, {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void fetch_industry_peers(const QString& symbol, QObject* ctx,
                          std::function<void(bool, QStringList, QString)> cb) {
    QPointer<QObject> guard(ctx);
    python::PythonWorker::instance().submit(
        QStringLiteral("industry_peers"), QJsonObject{{"symbol", symbol}},
        [guard, cb](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            QStringList peers;
            for (const auto& v : result.value(QStringLiteral("peers")).toArray())
                if (!v.toString().isEmpty())
                    peers << v.toString();
            if (!ok || result.contains(QStringLiteral("error")) || peers.isEmpty()) {
                const QString body = result.value(QStringLiteral("error")).toString();
                cb(false, {}, (err.isEmpty() ? body : err).left(160));
                return;
            }
            cb(true, peers, result.value(QStringLiteral("industry")).toString());
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void fetch_report_filings(const QString& symbol, QObject* ctx, ReportJsonCallback cb) {
    const QString key = kFilingsKey + symbol;
    if (const auto hit = CacheManager::instance().try_get(key)) {
        const QJsonObject o = QJsonDocument::fromJson(hit->toUtf8()).object();
        if (!o.isEmpty()) {
            cb(true, o, {});
            return;
        }
    }
    QPointer<QObject> guard(ctx);
    python::PythonRunner::instance().run(
        QStringLiteral("report_filings.py"), {symbol},
        [guard, cb, key](python::PythonResult res) {
            if (!res.success) {
                if (guard)
                    cb(false, {}, res.error.left(160));
                return;
            }
            const QString json = python::extract_json(res.output);
            const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
            if (o.isEmpty() || o.contains(QStringLiteral("error"))) {
                // Not kept: a refusal or a missing contact is not "no filings".
                if (guard)
                    cb(false, {}, o.value(QStringLiteral("error")).toString(QStringLiteral("unreadable reply")).left(200));
                return;
            }
            // Something that should have been there is missing: the annual
            // report, its XBRL figures, or — for a domestic filer — the
            // quarterly one. A foreign issuer (20-F, 40-F) files no 10-Q, so
            // its missing quarter is the whole answer, not a gap.
            const QString annual_form = o.value(QStringLiteral("annual")).toObject().value(QStringLiteral("form")).toString();
            const bool partial = o.contains(QStringLiteral("annual_error")) || o.contains(QStringLiteral("breakdown_error")) ||
                                 (o.contains(QStringLiteral("quarterly_error")) && annual_form == QLatin1String("10-K"));
            CacheManager::instance().put(key, json, partial ? kPartialFilingsTtlSec : kFilingsTtlSec,
                                         QStringLiteral("equity"));
            if (guard)
                cb(true, o, {});
        },
        {}, 150'000);
}

KeptNarrative kept_narrative(const QString& section, const QString& symbol) {
    KeptNarrative out;
    if (const auto hit = CacheManager::instance().try_get(narrative_key(section, symbol))) {
        const QJsonObject o = QJsonDocument::fromJson(hit->toUtf8()).object();
        out.reply = o.value(QStringLiteral("reply")).toString();
        out.model = o.value(QStringLiteral("model")).toString();
        out.written_at = QDateTime::fromString(o.value(QStringLiteral("written_at")).toString(), Qt::ISODate);
    }
    return out;
}

void keep_narrative(const QString& section, const QString& symbol, const QString& reply, const QString& model) {
    if (reply.trimmed().isEmpty())
        return;
    const QJsonObject o{{"reply", reply},
                        {"model", model},
                        {"written_at", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    CacheManager::instance().put(narrative_key(section, symbol),
                                 QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)), kNarrativeTtlSec,
                                 QStringLiteral("equity"));
}

void forget_narrative(const QString& section, const QString& symbol) {
    CacheManager::instance().remove(narrative_key(section, symbol));
}

} // namespace finterm::services::equity
