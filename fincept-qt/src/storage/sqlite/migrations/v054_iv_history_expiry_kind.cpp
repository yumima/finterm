// v054_iv_history_expiry_kind — daily ATM IV keyed by expiry kind.
//
// IvHistoryRepository wrote to `iv_history_daily` keyed only on
// (underlying, date), so whichever expiry was viewed last that day won, and
// the F&O IV-percentile pill ranked a weekly's IV against a monthly's. No
// migration ever created the table, so every upsert failed silently.
//
// The table is now keyed on (underlying, expiry_kind, date_iso), where
// expiry_kind is "front" (nearest listed expiry) or "monthly" (nearest
// month-end expiry), and records the actual expiry string for audit. Any
// pre-existing table without expiry_kind mixes expiries and is not
// comparable; it is renamed aside rather than dropped.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v054(QSqlDatabase& db) {
    QSqlQuery probe(db);
    const bool exists = probe.exec("SELECT 1 FROM iv_history_daily LIMIT 1");
    if (exists) {
        QSqlQuery has_kind(db);
        if (!has_kind.exec("SELECT expiry_kind FROM iv_history_daily LIMIT 1")) {
            QSqlQuery rename(db);
            if (!rename.exec("ALTER TABLE iv_history_daily RENAME TO iv_history_daily_unkeyed_legacy"))
                return Result<void>::err(rename.lastError().text().toStdString());
        }
    }

    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS iv_history_daily ("
                "  underlying  TEXT NOT NULL,"
                "  expiry_kind TEXT NOT NULL,"
                "  date_iso    TEXT NOT NULL,"
                "  expiry      TEXT NOT NULL,"
                "  atm_iv      REAL NOT NULL,"
                "  updated_at  TEXT,"
                "  PRIMARY KEY (underlying, expiry_kind, date_iso)"
                ")"))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v054() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({54, "iv_history_expiry_kind", apply_v054});
}

} // namespace fincept
