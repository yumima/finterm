// v047_earnings_predicted_move — the engine's own point estimate for the move.
//
// v046 recorded what the scorecard said (a BUY/HOLD/SELL lean and the size of
// this name's typical print reaction) but never the two combined into a single
// signed number. Without that there is nothing to place beside the realised
// move and call right or wrong — a lean and a magnitude are not a prediction
// until something multiplies them.
//
// ADD COLUMN rather than a rewrite: v046 may already be live, and its rows are
// still perfectly good observations. They simply carry NULL here, which reads
// as "no estimate was recorded", not "an estimate of zero".

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v047(QSqlDatabase& db) {
    // Tolerate the column already existing: a build that ran an in-development
    // v046 carrying it must not fail the whole migration chain on startup.
    QSqlQuery check(db);
    if (check.exec("SELECT predicted_move_pct FROM earnings_signal_records LIMIT 1"))
        return Result<void>::ok();

    QSqlQuery q(db);
    if (!q.exec("ALTER TABLE earnings_signal_records ADD COLUMN predicted_move_pct REAL"))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v047() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({47, "earnings_predicted_move", apply_v047});
}

} // namespace fincept
