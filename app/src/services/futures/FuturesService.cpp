// src/services/futures/FuturesService.cpp
#include "services/futures/FuturesService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace finterm::services::futures {

namespace {

constexpr qint64 kCotKeepSecs = 6 * 3600;

struct CachedCot {
    CotReport report;
    qint64 fetched_at = 0;
};

QHash<QString, CachedCot>& cot_cache() {
    static QHash<QString, CachedCot> cache;
    return cache;
}

/// The daemon's reason for a refusal: the frame's error, else the body's.
QString failure_text(const QJsonObject& result, const QString& err) {
    const QString body = result.value(QStringLiteral("error")).toString();
    return (err.isEmpty() ? body : err).left(160);
}

} // namespace

void fetch_curve(const QString& root, QObject* ctx, std::function<void(bool, Curve, QString)> cb) {
    QPointer<QObject> guard(ctx);
    python::PythonWorker::instance().submit(
        QStringLiteral("futures_curve"), QJsonObject{{"root", root}},
        [guard, cb, root](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            if (!ok || result.contains(QStringLiteral("error"))) {
                const QString why = failure_text(result, err);
                LOG_WARN("Futures", QStringLiteral("curve %1 failed: %2").arg(root, why));
                cb(false, {}, why.isEmpty() ? QStringLiteral("no answer") : why);
                return;
            }
            Curve curve = parse_curve(result);
            if (curve.root != root || curve.contracts.isEmpty()) {
                cb(false, {}, QStringLiteral("no contract months quoted for %1").arg(root));
                return;
            }
            cb(true, curve, {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void fetch_curve_history(const QStringList& contract_symbols, QObject* ctx,
                         std::function<void(bool, QVector<PastCurve>, QString)> cb) {
    QPointer<QObject> guard(ctx);
    python::PythonWorker::instance().submit(
        QStringLiteral("futures_curve_history"),
        QJsonObject{{"symbols", QJsonArray::fromStringList(contract_symbols)}},
        [guard, cb](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            if (!ok || result.contains(QStringLiteral("error"))) {
                cb(false, {}, failure_text(result, err));
                return;
            }
            cb(true, parse_curve_history(result), {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void fetch_cot(const QString& cftc_code, QObject* ctx, std::function<void(bool, CotReport, QString)> cb) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto hit = cot_cache().constFind(cftc_code);
    if (hit != cot_cache().constEnd() && now - hit->fetched_at < kCotKeepSecs) {
        cb(true, hit->report, {});
        return;
    }
    QPointer<QObject> guard(ctx);
    python::PythonRunner::instance().run(
        QStringLiteral("futures_cot.py"), {cftc_code, QStringLiteral("156")},
        [guard, cb, cftc_code](python::PythonResult res) {
            if (!res.success) {
                if (guard)
                    cb(false, {}, res.error.left(160));
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8()).object();
            if (o.contains(QStringLiteral("error")) || o.isEmpty()) {
                if (guard)
                    cb(false, {}, o.value(QStringLiteral("error")).toString(QStringLiteral("unreadable reply")).left(160));
                return;
            }
            CotReport rep = parse_cot(o);
            if (rep.rows.isEmpty()) {
                if (guard)
                    cb(false, {}, QStringLiteral("the CFTC reports no positions for this market"));
                return;
            }
            cot_cache().insert(cftc_code, {rep, QDateTime::currentSecsSinceEpoch()});
            if (guard)
                cb(true, rep, {});
        },
        {}, 45'000);
}

} // namespace finterm::services::futures
