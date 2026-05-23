// v038_team_mode — Rename `teams.strategy` to `teams.mode` and remap
// existing values to agno's actual coordination modes (Track 105).
//
// v037 shipped a `strategy` column with the values 'sequential' /
// 'parallel' that the Workbench dropdown surfaced but the dispatch
// layer never read — agno's TeamModule.from_config only reads
// `mode` (coordinate / route / collaborate).  This migration:
//
//   1. Renames the column to `mode` so the schema reflects what
//      agno actually consumes.
//   2. Remaps existing values:
//        sequential → coordinate  (round-table-then-synthesise)
//        parallel   → collaborate (everyone works concurrently)
//      Anything else (including the new 'coordinate' / 'route' /
//      'collaborate' values themselves) is preserved.
//
// SQLite 3.25+ supports `ALTER TABLE … RENAME COLUMN`; the bundled
// build (3.46+) is well past that.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration038";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v038(QSqlDatabase& db) {
    // RENAME COLUMN is forward-only — re-applying would fail with
    // "no such column: strategy".  Detect the current shape via
    // PRAGMA table_info and only rename if `strategy` is still
    // present.
    bool has_strategy = false;
    {
        QSqlQuery q(db);
        if (!q.exec("PRAGMA table_info(teams)"))
            return Result<void>::err(q.lastError().text().toStdString());
        while (q.next()) {
            if (q.value(1).toString() == QStringLiteral("strategy")) {
                has_strategy = true;
                break;
            }
        }
    }
    if (has_strategy) {
        if (auto r = sql(db, "ALTER TABLE teams RENAME COLUMN strategy TO mode"); r.is_err())
            return r;
    } else {
        LOG_INFO(kTag, "teams.strategy already renamed to mode — skipping ALTER");
    }
    // Remap legacy values regardless (idempotent — UPDATEs nothing
    // when the values are already correct).
    if (auto r = sql(db,
            "UPDATE teams SET mode = 'coordinate' WHERE mode = 'sequential'"); r.is_err())
        return r;
    if (auto r = sql(db,
            "UPDATE teams SET mode = 'collaborate' WHERE mode = 'parallel'"); r.is_err())
        return r;
    LOG_INFO(kTag, "teams.mode ready (renamed from strategy + values remapped)");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v038() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({38, "team_mode", apply_v038});
}

} // namespace fincept
