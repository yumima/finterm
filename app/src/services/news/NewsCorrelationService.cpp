#include "services/news/NewsCorrelationService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace finterm::services {

NewsCorrelationService& NewsCorrelationService::instance() {
    static NewsCorrelationService s;
    return s;
}

NewsCorrelationService::NewsCorrelationService() = default;

void NewsCorrelationService::detect_signals(const QVector<NewsArticle>& articles, SignalsCallback cb) {
    QJsonArray arr;
    for (const auto& a : articles) {
        QJsonObject obj;
        obj["id"] = a.id;
        obj["headline"] = a.headline;
        obj["summary"] = a.summary;
        obj["source"] = a.source;
        obj["category"] = a.category;
        obj["region"] = a.region;
        obj["sort_ts"] = static_cast<qint64>(a.sort_ts);
        arr.append(obj);
    }
    auto json_str = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    QPointer<NewsCorrelationService> self = this;
    python::PythonRunner::instance().run(
        "news_correlation.py", {"detect_signals", json_str}, [self, cb](python::PythonResult result) {
            if (!self)
                return;
            if (!result.success) {
                LOG_WARN("NewsCorrelation", "Signal detection failed: " + result.error);
                cb(false, {});
                return;
            }

            auto doc = QJsonDocument::fromJson(result.output.toUtf8());
            auto obj = doc.object();
            if (!obj["success"].toBool()) {
                cb(false, {});
                return;
            }

            QVector<CorrelationSignal> detected;
            for (const auto& v : obj["signals"].toArray()) {
                auto s = v.toObject();
                CorrelationSignal sig;
                sig.type = s["type"].toString();
                sig.category = s["category"].toString();
                sig.detail = s["detail"].toString();
                sig.severity = s["severity"].toString();
                sig.value = s["value"].toDouble();
                for (const auto& src : s["sources"].toArray())
                    sig.sources.append(src.toString());
                detected.append(sig);
            }

            cb(true, detected);
        });
}

void NewsCorrelationService::detect_deviations(const QMap<QString, int>& current_counts, DeviationCallback cb) {
    QJsonObject current;
    for (auto it = current_counts.begin(); it != current_counts.end(); ++it)
        current[it.key()] = it.value();

    QJsonObject baseline;
    for (auto it = baselines_.begin(); it != baselines_.end(); ++it) {
        QJsonObject bl;
        bl["mean"] = it.value().mean;
        bl["stddev"] = it.value().stddev;
        bl["sample_size"] = it.value().sample_size;
        baseline[it.key()] = bl;
    }

    auto cur_str = QString::fromUtf8(QJsonDocument(current).toJson(QJsonDocument::Compact));
    auto bl_str = QString::fromUtf8(QJsonDocument(baseline).toJson(QJsonDocument::Compact));

    python::PythonRunner::instance().run("news_correlation.py", {"detect_deviations", cur_str, bl_str},
                                         [cb](python::PythonResult result) {
                                             if (!result.success) {
                                                 cb(false, {});
                                                 return;
                                             }
                                             auto doc = QJsonDocument::fromJson(result.output.toUtf8());
                                             auto obj = doc.object();
                                             if (!obj["success"].toBool()) {
                                                 cb(false, {});
                                                 return;
                                             }

                                             QVector<QPair<QString, double>> deviations;
                                             for (const auto& v : obj["deviations"].toArray()) {
                                                 auto d = v.toObject();
                                                 deviations.append({d["category"].toString(), d["z_score"].toDouble()});
                                             }
                                             cb(true, deviations);
                                         });
}

} // namespace finterm::services
