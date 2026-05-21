// v027_agent_schedule — Schema for AgentScheduler entries (Track 10).
//
// Holds (agent_id, skill, args_json, cron) entries the scheduler
// fires on its 60s tick.  Cron grammar (parsed by AgentScheduler::
// parse_cron) currently supports `@daily HH:MM` and `@every Nm /
// Nh`; full POSIX 5-field cron can be added later without a schema
// change.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration027";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v027(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS agent_schedule ("
            "  id TEXT PRIMARY KEY,"
            "  agent_id TEXT NOT NULL,"
            "  skill TEXT NOT NULL,"
            "  args_json TEXT NOT NULL DEFAULT '{}',"
            "  cron TEXT NOT NULL,"
            "  enabled INTEGER NOT NULL DEFAULT 1,"
            "  last_run_at TEXT,"
            "  last_status TEXT,"
            "  created_at TEXT DEFAULT (datetime('now')),"
            "  updated_at TEXT DEFAULT (datetime('now'))"
            ")"); r.is_err()) {
        return r;
    }

    // Index for the tick query: enabled rows ordered by id.  Tick
    // currently does a full scan; once entry counts grow past ~100
    // a "due_at" virtual column would beat the scan but until then
    // the linear scan is cheap and the SQL is simple.
    sql(db, "CREATE INDEX IF NOT EXISTS idx_agent_schedule_enabled "
            "ON agent_schedule(enabled)");

    LOG_INFO(kTag, "agent_schedule table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v027() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({27, "agent_schedule", apply_v027});
}

} // namespace fincept
