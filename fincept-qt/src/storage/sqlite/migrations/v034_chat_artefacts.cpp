// v034_chat_artefacts — Typed slash output as first-class entities.
//
// When an agent emits a `chat_artefact` (via the `emit_artefact`
// internal MCP tool), the result lands here instead of being
// rendered into a one-shot chat bubble.  The Workbench surfaces
// the artefacts list; users can re-run, export, or supersede
// each one without going back through chat.
//
// `kind` is free-form to keep the schema flexible — initial values:
//   - "table"   — markdown / CSV table (comps, peers, screen results)
//   - "model"   — DCF / LBO / 3-statement model snapshot
//   - "memo"    — IC memo, initiating-coverage memo, morning note
//   - "report"  — multi-section research write-up
//
// `source_*` columns capture the slash invocation that produced the
// artefact so re-runs can dispatch with the same agent + skill +
// args.  `payload_json` is the artefact's structured content as
// JSON; the kind dictates the schema inside.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration034";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v034(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS chat_artefacts ("
            "  id TEXT PRIMARY KEY,"
            "  kind TEXT NOT NULL,"
            "  title TEXT NOT NULL,"
            "  payload_json TEXT NOT NULL,"
            "  source_request_id TEXT,"
            "  source_agent_id TEXT,"
            "  source_skill TEXT,"
            "  source_args_json TEXT,"
            "  status TEXT NOT NULL DEFAULT 'final',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"); r.is_err()) {
        return r;
    }
    sql(db, "CREATE INDEX IF NOT EXISTS idx_chat_artefacts_created_at "
            "ON chat_artefacts(created_at DESC)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_chat_artefacts_request_id "
            "ON chat_artefacts(source_request_id)");
    sql(db, "CREATE INDEX IF NOT EXISTS idx_chat_artefacts_status "
            "ON chat_artefacts(status)");
    LOG_INFO(kTag, "chat_artefacts table ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v034() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({34, "chat_artefacts", apply_v034});
}

} // namespace fincept
