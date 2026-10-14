// v057_saved_strategies — the table F&O Builder's SAVE writes to.
//
// StrategiesRepository and the Builder's save/load have existed for a while,
// but no migration ever created `strategies`, so every save failed with "no
// such table" and a strategy the user built was gone when they left the tab.
// One row per saved strategy; the legs travel as JSON because a leg's shape
// (type, strike, expiry, lots, entry price, IV at entry) is the Builder's,
// and the repository already serialises it.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v057(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS strategies ("
                "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  name        TEXT NOT NULL,"
                "  underlying  TEXT NOT NULL,"
                "  expiry      TEXT,"
                "  created_at  TEXT NOT NULL,"
                "  modified_at TEXT NOT NULL,"
                "  notes       TEXT,"
                "  legs_json   TEXT NOT NULL DEFAULT '[]'"
                ")"))
        return Result<void>::err(q.lastError().text().toStdString());
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_strategies_underlying ON strategies(underlying, modified_at)"))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v057() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({57, "saved_strategies", apply_v057});
}

} // namespace fincept
