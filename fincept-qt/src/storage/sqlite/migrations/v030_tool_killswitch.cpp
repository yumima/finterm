// v030_tool_killswitch — Per-tool kill-switch table (Track 14 #39, R22).
//
// Holds a row per tool the user has explicitly disabled.  McpService
// checks the set at the top of execute_tool() and refuses to dispatch.
// Empty table = everything enabled — the default state.
//
// `tool_name` is the wire-form key (`<server_id>__<tool_name>`), which
// is what McpService::execute_tool dispatches against — matching the
// same identifier the agent's `allow_tools` allowlist filters on
// (Track 8).  We keep the kill-switch independent of the allowlist
// because the two answer different questions:
//
//   - allow_tools  = "what's this agent allowed to call?"  (per-agent)
//   - kill-switch  = "what's globally disabled right now?" (per-tool, system-wide)
//
// A tool disabled by the kill-switch is refused even for agents that
// have it in their allowlist — kill-switch wins.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration030";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v030(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS tool_killswitch ("
            "  tool_name TEXT PRIMARY KEY,"
            "  reason TEXT,"
            "  disabled_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"); r.is_err()) {
        return r;
    }
    LOG_INFO(kTag, "tool_killswitch table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v030() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({30, "tool_killswitch", apply_v030});
}

} // namespace fincept
