// v029_agent_traces — Unified trace + audit schema (Track 14 #38).
//
// One row per AgentService::run_agent call.  Both runtimes
// (anthropic, local) write here; legacy agno-backed runs do too via
// the same dispatch path.  This is the foundation table for:
//
//   - Workbench → System tab (Track 13): "Recent agent activity"
//   - Cost / budget tracking (Track 14 #41)
//   - Eval result storage (Track 14 #37)
//   - Audit log for compliance — `agent_traces` joins with
//     security_audit_log (v020) on request_id where applicable.
//
// Token / cost columns are nullable: only populated when the
// runtime actually reports them.  Anthropic SDK reports both; the
// local OpenAI-compat runtime reports tokens but cost depends on
// the user-configured pricing per profile.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration029";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v029(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS agent_traces ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  request_id TEXT NOT NULL UNIQUE,"
            "  agent_id TEXT,"
            "  runtime TEXT,"        // anthropic | local | agno | unknown
            "  source TEXT,"         // chat | chat_slash | scheduler | api | ...
            "  query TEXT,"
            "  config_json TEXT,"
            "  started_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  finished_at TEXT,"
            "  status TEXT NOT NULL DEFAULT 'in_progress',"  // in_progress|success|error
            "  response TEXT,"
            "  error TEXT,"
            "  latency_ms INTEGER,"
            "  tokens_in INTEGER,"
            "  tokens_out INTEGER,"
            "  cost_usd REAL"
            ")"); r.is_err()) {
        return r;
    }

    // Index hot read paths: System-tab tail by started_at, per-agent
    // filter, dispatcher lookup by request_id (already UNIQUE so a
    // dedicated index is redundant — sqlite covers it via the unique
    // constraint's implicit index).
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_traces_started_at "
            "ON agent_traces(started_at DESC)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_traces_agent_id "
            "ON agent_traces(agent_id)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_traces_status "
            "ON agent_traces(status)");

    LOG_INFO(kTag, "agent_traces table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v029() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({29, "agent_traces", apply_v029});
}

} // namespace fincept
