// src/services/economics/EconomicsService.cpp
#include "services/economics/EconomicsService.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QTimer>

namespace finterm::services {

namespace {

// An answer that is shown but not held, so the next read asks again:
// - fred_data.py `multiple` with a refused series (one entry each, a refused
//   one carrying its own "error"; the others are good);
// - treasury_data.py `par_curve` with no rows (before Treasury publishes, or
//   a page that listed nothing) — holding it would say "no curve" for the
//   cache's ten minutes after the curve is out.
bool hold_back(const QString& command, const QJsonObject& data) {
    if (command == QLatin1String("par_curve"))
        return data.value(QLatin1String("rows")).toArray().isEmpty();
    if (command != QLatin1String("multiple"))
        return false;
    for (const auto& v : data.value(QLatin1String("data")).toArray())
        if (!v.toObject().value(QLatin1String("error")).toString().isEmpty())
            return true;
    return false;
}

// Why an answer without a top-level error still holds nothing usable, or
// empty. fred_economic_data.py's composite commands (the yield curve: one
// FRED series per tenor) report each series' failure inside it; when every
// one failed the answer is the refusal, not a curve.
QString unusable_reason(const QString& command, const QJsonObject& data) {
    if (command != QLatin1String("yield_curve"))
        return {};
    QString first_error;
    for (const char* block : {"yield_curve", "spreads"}) {
        const QJsonObject series = data.value(QLatin1String(block)).toObject();
        for (auto it = series.constBegin(); it != series.constEnd(); ++it) {
            const QJsonObject s = it.value().toObject();
            const QString e = s.value("error").toString();
            if (!e.isEmpty()) {
                if (first_error.isEmpty())
                    first_error = e;
                continue;
            }
            for (const auto& o : s.value("observations").toArray()) {
                bool num = false;
                o.toObject().value("value").toString().toDouble(&num); // FRED's "." = no value
                if (num)
                    return {};
            }
        }
    }
    return first_error.isEmpty() ? QStringLiteral("FRED returned no yield on any tenor")
                                 : QStringLiteral("FRED returned no yield on any tenor: ") + first_error;
}

} // namespace

EconomicsService& EconomicsService::instance() {
    static EconomicsService inst;
    return inst;
}

EconomicsService::EconomicsService(QObject* parent) : QObject(parent) {
    // Hydrate dispatch_records_ (which script + args answer each hub topic)
    // from cache.db, so a hub refresh can replay a request made in an earlier
    // session. The answers themselves are cache.db entries with their own TTL
    // and write time — there is no second copy to restore.
    QTimer::singleShot(0, this, [this]() {
        const auto stored = finterm::CacheManager::instance().get_prefix(QString::fromLatin1(kDispatchPrefix));
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            const QJsonObject root = QJsonDocument::fromJson(it.value().toUtf8()).object();
            DispatchRecord rec;
            rec.source_id = root.value("source_id").toString();
            rec.script = root.value("script").toString();
            rec.command = root.value("command").toString();
            for (const auto& v : root.value("args").toArray())
                rec.args << v.toString();
            rec.request_id = root.value("request_id").toString();
            if (rec.script.isEmpty() || rec.request_id.isEmpty())
                continue;
            dispatch_records_.insert(hub_topic(rec.source_id, rec.request_id), rec);
        }
    });
}

// ── Cache ─────────────────────────────────────────────────────────────────────

QString EconomicsService::cache_key(const QString& script, const QString& command, const QStringList& args) const {
    return "economics:" + script + ":" + command + ":" + args.join(",");
}

void EconomicsService::invalidate(const QString& script, const QString& command, const QStringList& args) {
    finterm::CacheManager::instance().remove(cache_key(script, command, args));
}

// ── Execute ───────────────────────────────────────────────────────────────────

void EconomicsService::run(const QString& script, const QString& command, const QStringList& args, RunDone done,
                           int timeout_ms) {
    const QString key = cache_key(script, command, args);
    {
        if (const auto cached = finterm::CacheManager::instance().try_get_aged(key)) {
            LOG_DEBUG("EconomicsService", "Cache hit: " + key);
            EconomicsResult res;
            res.success = true;
            res.data = QJsonDocument::fromJson(cached->value.toUtf8()).object();
            res.as_of = cached->written_at;
            done(res, false);
            return;
        }
    }

    auto& waiters = in_flight_[key];
    waiters.append(std::move(done));
    if (waiters.size() > 1)
        return; // the run already under way answers this caller too

    QStringList full_args;
    full_args << command << args;

    LOG_INFO("EconomicsService", QString("Run %1 %2 [%3]").arg(script, command, args.join(", ")));

    QPointer<EconomicsService> self = this;
    python::PythonRunner::instance().run(
        script, full_args,
        [self, key, script, command](python::PythonResult py) {
            if (!self)
                return;

            EconomicsResult res;
            if (!py.success) {
                res.error = python::failure_message(py);
                LOG_ERROR("EconomicsService", "Script failed: " + res.error);
            } else if (const QString json_str = python::extract_json(py.output); json_str.isEmpty()) {
                res.error = "No JSON output from script";
                LOG_ERROR("EconomicsService", res.error);
            } else {
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(json_str.toUtf8(), &err);
                if (doc.isNull()) {
                    res.error = "JSON parse error: " + err.errorString();
                    LOG_ERROR("EconomicsService", res.error);
                } else {
                    // If the root is an array, wrap it
                    res.data = doc.isArray() ? QJsonObject{{"data", doc.array()}} : doc.object();
                    // Treat script-level error field as failure
                    if (res.data.contains("error") && !res.data["error"].isNull() &&
                        !res.data["error"].toString().isEmpty() && !res.data.contains("data")) {
                        const QString error_code = res.data.value("error_code").toString();
                        const QString message = res.data["error"].toString();
                        // Prefix with [CODE] so panels can branch on it without a schema change.
                        res.error = error_code.isEmpty()
                                        ? message
                                        : (QStringLiteral("[") + error_code + QStringLiteral("] ") + message);
                    } else if (const QString why = unusable_reason(command, res.data); !why.isEmpty()) {
                        // Answered, but with nothing in it: every series inside
                        // carried FRED's error. A failure, never cached.
                        res.error = why;
                        LOG_WARN("EconomicsService", script + " " + command + ": " + why);
                    } else {
                        res.success = true;
                        res.as_of = QDateTime::currentDateTimeUtc();
                        if (!hold_back(command, res.data))
                            finterm::CacheManager::instance().put(
                                key,
                                QVariant(QString::fromUtf8(QJsonDocument(res.data).toJson(QJsonDocument::Compact))),
                                kCacheTtlSec, "economics");
                    }
                }
            }

            const QVector<RunDone> waiters = self->in_flight_.take(key);
            for (const auto& w : waiters)
                w(res, true);
        },
        {}, timeout_ms);
}

void EconomicsService::execute(const QString& source_id, const QString& script, const QString& command,
                               const QStringList& args, const QString& request_id) {
    // Remember the dispatch so hub-driven refresh() can replay it later.
    const QString topic = hub_topic(source_id, request_id);
    dispatch_records_.insert(topic, DispatchRecord{source_id, script, command, args, request_id});

    QPointer<EconomicsService> self = this;
    run(script, command, args, [self, source_id, request_id, topic](const EconomicsResult& r, bool fresh) {
        if (!self)
            return;
        EconomicsResult res = r;
        res.source_id = source_id;
        if (!res.success) {
            emit self->result_ready(request_id, res);
            return;
        }
        if (fresh) {
            // Remember which script + args answer this topic, for a hub
            // refresh in a later session (the answer is already cached).
            const auto it = self->dispatch_records_.constFind(topic);
            if (it != self->dispatch_records_.constEnd()) {
                const auto& rec = it.value();
                QJsonArray args_arr;
                for (const auto& a : rec.args)
                    args_arr.append(a);
                const QJsonObject root{{"source_id", rec.source_id}, {"script", rec.script},
                                       {"command", rec.command},     {"args", args_arr},
                                       {"request_id", rec.request_id}};
                finterm::CacheManager::instance().put(
                    QString::fromLatin1(kDispatchPrefix) + topic,
                    QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)), kDispatchTtlSec,
                    "economics");
            }
            LOG_INFO("EconomicsService", "Result ready: " + request_id);
        }
        emit self->result_ready(request_id, res);
        if (self->hub_registered_)
            finterm::datahub::DataHub::instance().publish(topic, QVariant::fromValue(res));
    });
}

void EconomicsService::fetch(const QString& script, const QString& command, const QStringList& args, ResultCallback cb,
                             int timeout_ms) {
    QPointer<EconomicsService> self = this;
    run(
        script, command, args,
        [self, cb = std::move(cb)](const EconomicsResult& res, bool) {
            if (!self)
                return;
            // Queued even when the run already finished asynchronously: one rule
            // for the caller, whatever the cache held.
            QMetaObject::invokeMethod(self, [cb, res]() { cb(res); }, Qt::QueuedConnection);
        },
        timeout_ms);
}

// ── DataHub producer wiring ─────────────────────────────────────────────────

QString EconomicsService::hub_topic(const QString& source_id, const QString& request_id) {
    return QStringLiteral("econ:") + source_id + QLatin1Char(':') + request_id;
}

QStringList EconomicsService::topic_patterns() const {
    // Single broad pattern — individual source refactor to explicit
    // patterns can happen later once panels migrate. Keeps producer
    // registration simple across 32 upstream sources.
    return {QStringLiteral("econ:*")};
}

void EconomicsService::refresh(const QStringList& topics) {
    // Replay the last-known dispatch for each topic. If we've never
    // seen an execute() for this topic, nothing to do — we don't know
    // which script + args produce it.
    for (const auto& topic : topics) {
        auto it = dispatch_records_.constFind(topic);
        if (it == dispatch_records_.constEnd()) {
            LOG_DEBUG("EconomicsService", "refresh() for unknown topic (no prior execute): " + topic);
            continue;
        }
        const DispatchRecord rec = it.value();
        // Force re-fetch: clear CacheManager entry so execute() goes
        // through the Python path rather than returning the cached copy.
        finterm::CacheManager::instance().remove(cache_key(rec.script, rec.command, rec.args));
        execute(rec.source_id, rec.script, rec.command, rec.args, rec.request_id);
    }
}

int EconomicsService::max_requests_per_sec() const {
    return 2; // Python spawn pacing — upstream rate limits are usually higher
}

void EconomicsService::ensure_registered_with_hub() {
    if (hub_registered_)
        return;
    auto& hub = finterm::datahub::DataHub::instance();
    hub.register_producer(this);

    // 1-hour TTL, 60s min interval — economics series move slowly.
    finterm::datahub::TopicPolicy policy;
    policy.ttl_ms = 60 * 60 * 1000;
    policy.min_interval_ms = 60 * 1000;
    policy.push_only = false;
    hub.set_policy_pattern(QStringLiteral("econ:*"), policy);

    hub_registered_ = true;
    LOG_INFO("EconomicsService", "Registered with DataHub (econ:*)");
}

} // namespace finterm::services
