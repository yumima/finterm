// src/services/economics/EconomicsService.cpp
#include "services/economics/EconomicsService.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "python/PythonRunner.h"
#include "services/util/DiskCache.h"
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

// Persistent on-disk cache for economic timeseries. One file per
// (script, command, args) tuple so cold launches can re-emit the last-known
// series for any panel that re-issues the same execute() call. Survives
// across runs; refreshes overwrite atomically.
finterm::services::util::DiskCache& disk_cache() {
    static finterm::services::util::DiskCache c(QStringLiteral("economics"));
    return c;
}

// Sanitize a logical key (source_id:script:command:args) into an alnum
// filename. We hash to keep the path bounded for long arg lists (some
// dbnomics queries serialize to >150 chars). request_id is intentionally
// EXCLUDED from the hash — it's a per-session identifier, so including
// it would make identical reruns from new sessions write fresh files and
// the cache dir would grow without bound.
QString file_for_dispatch(const QString& source_id, const QString& script, const QString& command,
                          const QStringList& args, const QString& /*request_id, unused on purpose*/) {
    const QString combined = source_id + "|" + script + "|" + command + "|" + args.join(",");
    const QByteArray digest = QCryptographicHash::hash(combined.toUtf8(), QCryptographicHash::Sha1).toHex();
    // Keep the script base + a short hash for some grep-ability in the
    // cache dir. Strip extension.
    QString script_stem = script;
    const int dot = script_stem.lastIndexOf('.');
    if (dot > 0)
        script_stem.truncate(dot);
    QString clean_stem;
    for (const QChar c : script_stem)
        if (c.isLetterOrNumber() || c == '_')
            clean_stem.append(c);
    if (clean_stem.isEmpty())
        clean_stem = QStringLiteral("econ");
    return clean_stem + QStringLiteral("_") + QString::fromLatin1(digest.left(16)) + QStringLiteral(".json");
}

} // namespace

EconomicsService& EconomicsService::instance() {
    static EconomicsService inst;
    return inst;
}

EconomicsService::EconomicsService(QObject* parent) : QObject(parent) {
    // Cap the cache dir at 500 distinct queries (per-(source, script,
    // command, args) tuple). Heavy users of the economics screen browse
    // dozens of series per session; 500 entries cover months of activity
    // and the ctor hydration stays bounded.
    disk_cache().trim_to(500);

    // Hydrate the dispatch_records_ map and CacheManager from every file on
    // disk. Deferred to the next event-loop tick so the synchronous file I/O
    // (up to 500 JSON files) stays off the startup path — that cold-cache read
    // is what hurts launch under memory pressure. Nothing reads these records
    // before a panel navigates, which can't happen before the event loop runs.
    QTimer::singleShot(0, this, [this]() {
        const QStringList files = disk_cache().files();
        int hydrated = 0;
        for (const QString& fname : files) {
            const QJsonDocument doc = disk_cache().load(fname);
            if (!doc.isObject())
                continue;
            const QJsonObject root = doc.object();
            // Expected schema written by execute(): { "source_id", "script",
            // "command", "args" (array), "request_id", "data" (the parsed obj) }
            DispatchRecord rec;
            rec.source_id = root.value("source_id").toString();
            rec.script = root.value("script").toString();
            rec.command = root.value("command").toString();
            for (const auto& v : root.value("args").toArray())
                rec.args << v.toString();
            rec.request_id = root.value("request_id").toString();
            const QJsonObject data = root.value("data").toObject();
            if (rec.script.isEmpty() || rec.request_id.isEmpty() || data.isEmpty())
                continue;

            const QString topic = hub_topic(rec.source_id, rec.request_id);
            dispatch_records_.insert(topic, rec);

            // Repopulate CacheManager so a subsequent execute() emits cached
            // immediately. Use full TTL since freshness is gated by execute()'s
            // own age check on next invocation.
            finterm::CacheManager::instance().put(
                cache_key(rec.script, rec.command, rec.args),
                QVariant(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))), kCacheTtlSec,
                "economics");
            ++hydrated;
        }
        if (hydrated > 0)
            LOG_INFO("EconomicsService", QString("Hydrated %1 series from disk cache").arg(hydrated));
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

void EconomicsService::run(const QString& script, const QString& command, const QStringList& args, RunDone done) {
    const QString key = cache_key(script, command, args);
    {
        const QVariant cached = finterm::CacheManager::instance().get(key);
        if (!cached.isNull()) {
            LOG_DEBUG("EconomicsService", "Cache hit: " + key);
            EconomicsResult res;
            res.success = true;
            res.data = QJsonDocument::fromJson(cached.toString().toUtf8()).object();
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
    python::PythonRunner::instance().run(script, full_args, [self, key](python::PythonResult py) {
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
                } else {
                    res.success = true;
                    finterm::CacheManager::instance().put(
                        key, QVariant(QString::fromUtf8(QJsonDocument(res.data).toJson(QJsonDocument::Compact))),
                        kCacheTtlSec, "economics");
                }
            }
        }

        const QVector<RunDone> waiters = self->in_flight_.take(key);
        for (const auto& w : waiters)
            w(res, true);
    });
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
            // Persist to disk so the next launch hydrates this series
            // immediately. We store the dispatch params alongside the data
            // so the constructor can rebuild dispatch_records_ for hub
            // refresh replay.
            const auto it = self->dispatch_records_.constFind(topic);
            if (it != self->dispatch_records_.constEnd()) {
                const auto& rec = it.value();
                QJsonObject root;
                root.insert("source_id", rec.source_id);
                root.insert("script", rec.script);
                root.insert("command", rec.command);
                QJsonArray args_arr;
                for (const auto& a : rec.args)
                    args_arr.append(a);
                root.insert("args", args_arr);
                root.insert("request_id", rec.request_id);
                root.insert("data", res.data);
                disk_cache().save(file_for_dispatch(rec.source_id, rec.script, rec.command, rec.args, rec.request_id),
                                  QJsonDocument(root));
            }
            LOG_INFO("EconomicsService", "Result ready: " + request_id);
        }
        emit self->result_ready(request_id, res);
        if (self->hub_registered_)
            finterm::datahub::DataHub::instance().publish(topic, QVariant::fromValue(res));
    });
}

void EconomicsService::fetch(const QString& script, const QString& command, const QStringList& args,
                             ResultCallback cb) {
    QPointer<EconomicsService> self = this;
    run(script, command, args, [self, cb = std::move(cb)](const EconomicsResult& res, bool) {
        if (!self)
            return;
        // Queued even when the run already finished asynchronously: one rule
        // for the caller, whatever the cache held.
        QMetaObject::invokeMethod(self, [cb, res]() { cb(res); }, Qt::QueuedConnection);
    });
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
