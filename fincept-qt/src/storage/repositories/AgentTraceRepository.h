#pragma once
// AgentTraceRepository.h — read/write the agent_traces table (v029).
//
// Track 14 #38: one row per AgentService::run_agent invocation.
// AgentService::run_agent calls `create(...)` on dispatch and
// `finish(...)` when the python callback returns.  Read APIs back
// the future Workbench System tab.

#include "core/result/Result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace fincept {

struct AgentTraceRow {
    qint64 id = 0;
    QString request_id;
    QString agent_id;
    QString runtime;     ///< anthropic | local | agno | unknown
    QString source;      ///< chat | chat_slash | scheduler | api | ...
    QString query;
    QString config_json;
    QString started_at;
    QString finished_at; ///< empty until finish() called
    QString status;      ///< in_progress | success | error
    QString response;
    QString error;
    std::optional<int> latency_ms;
    std::optional<int> tokens_in;
    std::optional<int> tokens_out;
    std::optional<double> cost_usd;
    /// v036: JSON array of tool invocations recorded during the
    /// turn.  Each element: {name, args, duration_ms, ok,
    /// result_preview}.  Empty / NULL on runtimes that don't surface
    /// the tool loop (e.g. Anthropic SDK paths today).
    QString tool_calls_json;
};

/// Args for create().  Pass an aggregate so the call site stays
/// readable as the trace columns grow.
struct AgentTraceCreate {
    QString request_id;
    QString agent_id;     ///< empty allowed (general router)
    QString runtime;      ///< empty if unknown at dispatch time
    QString source;       ///< empty allowed
    QString query;
    QJsonObject config;   ///< serialised as compact JSON into config_json
};

/// Args for finish().  Optional fields only set when the runtime
/// reports them.
struct AgentTraceFinish {
    QString request_id;
    bool success = false;
    QString response;
    QString error;
    std::optional<int> latency_ms;
    std::optional<int> tokens_in;
    std::optional<int> tokens_out;
    std::optional<double> cost_usd;
    /// JSON array of tool invocations — see AgentTraceRow::tool_calls_json.
    /// Empty string leaves the column NULL.
    QString tool_calls_json;
};

class AgentTraceRepository {
  public:
    static AgentTraceRepository& instance();

    Result<void> create(const AgentTraceCreate& c);

    /// Mark an existing row finished.  No-op (with warn log) when
    /// request_id wasn't seen — avoids the data-loss trap where a
    /// late finish for a row that never got create() silently drops.
    Result<void> finish(const AgentTraceFinish& f);

    /// Most recent N traces, newest first.  For the Workbench System
    /// tab.  `limit` is clamped to a sane upper bound.
    Result<QVector<AgentTraceRow>> list_recent(int limit = 100);

    /// One row by request_id, or empty optional if not found.
    Result<std::optional<AgentTraceRow>> get(const QString& request_id);

    AgentTraceRepository(const AgentTraceRepository&) = delete;
    AgentTraceRepository& operator=(const AgentTraceRepository&) = delete;

  private:
    AgentTraceRepository() = default;
};

} // namespace fincept
