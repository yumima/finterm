// v036_agent_trace_tool_calls — Add `tool_calls_json` column to
// agent_traces so the trace drill-down can render the per-turn tool
// timeline (Track 96 / "audit pillar" follow-up).
//
// The column carries a JSON array of objects, one per tool invocation
// the agent made during the turn:
//
//   [{"name": "int__get_quote", "args": {...}, "duration_ms": 280,
//     "ok": true, "result_preview": "AAPL: $187.42"},
//    {"name": "int__fd_filings_get", "args": {...}, "duration_ms": 1450,
//     "ok": false, "result_preview": "404: filing not found"},
//    …]
//
// Nullable: existing rows + dispatches from runtimes that don't surface
// tool calls (e.g. Anthropic SDK paths where we can't introspect the
// loop today) leave it NULL, and the UI shows "(no tool log)".
//
// Populated by AgentService::finish() when the Python runtime returns
// a `tool_calls` array in its result JSON.  Migration is forward-only;
// no destructive ALTER.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration036";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v036(QSqlDatabase& db) {
    if (auto r = sql(db,
            "ALTER TABLE agent_traces ADD COLUMN tool_calls_json TEXT");
        r.is_err()) {
        // SQLite has no IF NOT EXISTS for ADD COLUMN.  If the column
        // already exists (rerun / hand-applied), pretend success — the
        // alternative is the whole migration runner halting on a
        // re-apply that's actually a no-op.
        const QString msg = QString::fromStdString(r.error());
        if (!msg.contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive))
            return r;
        LOG_INFO(kTag, "agent_traces.tool_calls_json already present — skipping");
    }
    LOG_INFO(kTag, "agent_traces.tool_calls_json ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v036() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({36, "agent_trace_tool_calls", apply_v036});
}

} // namespace fincept
