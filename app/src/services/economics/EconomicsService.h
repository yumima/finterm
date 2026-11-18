// src/services/economics/EconomicsService.h
// Singleton service for all economics data sources.
// Wraps PythonRunner, caches results, emits result_ready.
#pragma once

#include "datahub/Producer.h"
#include "python/PythonRunner.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace finterm::services {

struct EconomicsResult {
    bool success = false;
    QJsonObject data;
    QString error;
    QString source_id; // which provider produced this
    QDateTime as_of;   // when the script produced `data` (a cache hit keeps its original time)
};

/// Phase 6 — DataHub producer for `econ:<source>:<request_id>` topics.
/// The service is a thin Python-script dispatcher; hub topics are keyed
/// by (source_id, request_id) matching the legacy `result_ready` signal
/// tuple. Panels can migrate to `hub.subscribe("econ:fred:gdp_quarterly",
/// ...)` and the service fans out dual — hub publish + legacy signal —
/// so migration is incremental.
class EconomicsService : public QObject, public finterm::datahub::Producer {
    Q_OBJECT
  public:
    static EconomicsService& instance();

    /// Run a script command asynchronously.
    /// @param source_id  caller tag echoed back in result_ready
    /// @param script     python script filename
    /// @param command    first CLI argument
    /// @param args       additional arguments
    /// @param request_id unique id to correlate response
    void execute(const QString& source_id, const QString& script, const QString& command, const QStringList& args,
                 const QString& request_id);

    /// The same run answered to ONE caller: the cache and the in-flight
    /// request execute() uses (a panel and a workflow asking for the same
    /// series share one script run), never a second copy. Always answers
    /// after the call returns, cache hit included. `source_id` is empty.
    using ResultCallback = std::function<void(const EconomicsResult&)>;
    /// `timeout_ms`: the script's process deadline (a run already out keeps its own).
    void fetch(const QString& script, const QString& command, const QStringList& args, ResultCallback cb,
               int timeout_ms = python::PythonRunner::kProcessTimeoutMs);

    /// The US Treasury daily par yield curve (treasury_data.py par_curve —
    /// the 2-year Yahoo does not carry): data = {"rows": [{date, y2, y10, …}]},
    /// newest first. fetch()'s cache and coalescing; a refusal is a failure.
    void fetch_treasury_par_curve(ResultCallback cb) {
        fetch(QStringLiteral("treasury_data.py"), QStringLiteral("par_curve"), {}, std::move(cb));
    }

    /// Drop the cached answer for (script, command, args) so the next
    /// execute() of it re-runs the script (a forced refresh).
    void invalidate(const QString& script, const QString& command, const QStringList& args);

    /// Register with the hub + install econ:* policies. Idempotent.
    void ensure_registered_with_hub();

    // ── finterm::datahub::Producer ────────────────────────────────────────
    /// Patterns: `econ:*`. Individual series topic shape is
    /// `econ:<source>:<request_id>` (request_id is caller-assigned and
    /// usually carries the series id or query hash).
    QStringList topic_patterns() const override;
    /// Split topic by `:` → source, request_id. If a pending dispatch
    /// record exists for that topic (registered via `remember_topic_*`),
    /// re-execute it. Otherwise no-op — scheduler-driven refresh without
    /// a prior execute() call is a no-op since we don't know the script.
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override; // 2 — Python spawn pacing

  signals:
    void result_ready(const QString& request_id, const finterm::services::EconomicsResult& result);

  private:
    explicit EconomicsService(QObject* parent = nullptr);
    Q_DISABLE_COPY(EconomicsService)

    static constexpr int kCacheTtlSec = 10 * 60; // 10 min — passed to CacheManager
    // Which script + args answer each hub topic, kept across sessions so a
    // refresh can replay it (cache.db "econ_dispatch:<topic>").
    static constexpr const char* kDispatchPrefix = "econ_dispatch:";
    static constexpr int kDispatchTtlSec = 90 * 24 * 3600;

    QString cache_key(const QString& script, const QString& command, const QStringList& args) const;

    /// Core of execute() and fetch(): a cache hit answers inside the call
    /// (`fresh` = false); otherwise the script runs once per cache key, every
    /// caller waiting on it is answered from that run (`fresh` = true), and a
    /// success is cached.
    using RunDone = std::function<void(const EconomicsResult&, bool fresh)>;
    void run(const QString& script, const QString& command, const QStringList& args, RunDone done,
             int timeout_ms = python::PythonRunner::kProcessTimeoutMs);
    QHash<QString, QVector<RunDone>> in_flight_; // cache key -> callers waiting on the one run

    /// Compose hub topic: `econ:<source>:<request_id>`.
    static QString hub_topic(const QString& source_id, const QString& request_id);
    /// Remember dispatch params so `refresh(topic)` can replay the call.
    struct DispatchRecord {
        QString source_id;
        QString script;
        QString command;
        QStringList args;
        QString request_id;
    };
    QHash<QString, DispatchRecord> dispatch_records_; // topic -> last dispatch
    bool hub_registered_ = false;
};

} // namespace finterm::services

#include <QMetaType>
Q_DECLARE_METATYPE(finterm::services::EconomicsResult)
