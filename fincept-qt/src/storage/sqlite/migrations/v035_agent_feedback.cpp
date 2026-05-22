// v035_agent_feedback — Capture user judgement on agent dispatches
// (Track 7B).
//
// Each row pairs an `agent_traces.request_id` with a verdict + an
// optional one-line note.  Used by:
//   - the trace-detail dialog (user marks wrong / right after
//     reviewing what the agent did)
//   - the future skill-edit proposal flow (Track 7C) which reads
//     `verdict = 'wrong'` rows to seed "what should we change?"
//     prompts to the model
//
// Verdicts are free-form text but the UI restricts to:
//   - "wrong"   the answer was incorrect / harmful / unhelpful
//   - "right"   the answer was good — useful for positive examples
//   - "unsure"  user wanted to flag but doesn't know yet
//
// Multiple feedback rows per request_id are allowed — a user can
// update their take, and we keep the history rather than overwriting.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration035";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v035(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS agent_feedback ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  request_id TEXT NOT NULL,"
            "  verdict TEXT NOT NULL,"
            "  note TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"); r.is_err()) {
        return r;
    }
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_feedback_request_id "
            "ON agent_feedback(request_id)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_feedback_created_at "
            "ON agent_feedback(created_at DESC)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_feedback_verdict "
            "ON agent_feedback(verdict)");
    LOG_INFO(kTag, "agent_feedback table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v035() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({35, "agent_feedback", apply_v035});
}

} // namespace fincept
