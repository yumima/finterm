// v031_memory_entries — Stub schema for the MemoryTools MCP family
// (Track 9).
//
// Anthropic profile uses the SDK's native memory tool — this table
// is for the local profile.  Today it's a plain SQLite keyword
// search.  When sqlite-vec lands, a follow-up migration adds an
// embedding column and rewires `memory_search` to do cosine search;
// the contract (`upsert / search / list` with three scopes) stays
// identical so agents don't need to know which backing implementation
// they're hitting.
//
// Three intended scopes (free-form strings, not an enum so the
// future per-team / per-skill scopes don't need a migration):
//   - "thesis"     — pinned to one open thesis row
//   - "workspace"  — per finterm workspace (account)
//   - "global"     — cross-thesis / cross-workspace

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration032";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v032(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS memory_entries ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  scope TEXT NOT NULL,"
            "  key TEXT NOT NULL,"
            "  content TEXT NOT NULL,"
            "  metadata_json TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  UNIQUE(scope, key)"
            ")"); r.is_err()) {
        return r;
    }
    sql(db, "CREATE INDEX IF NOT EXISTS idx_memory_entries_scope ON memory_entries(scope)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_memory_entries_updated_at "
            "ON memory_entries(updated_at DESC)");
    LOG_INFO(kTag, "memory_entries table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v032() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({32, "memory_entries", apply_v032});
}

} // namespace fincept
