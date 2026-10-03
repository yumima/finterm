// v052_earnings_outlook — record what the redesigned outlook said.
//
// The ER Earnings tab no longer issues a BUY / HOLD / SELL lean or a signed
// move estimate: walk-forward, neither beat a coin flip. What it states now is
// a beat probability and a one-session size forecast, and both are checkable —
// so both are written down before the print, the same way v046 wrote the lean.
//
// ADD COLUMN rather than a new table: old rows remain valid observations of
// the old model, and `model_version` keeps the two apart. NULL there means v1,
// whose expected_move_pct held the plain trailing average; from v2 it holds
// the volatility-blended forecast the tab displays.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace fincept {
namespace {

Result<void> add_column(QSqlDatabase& db, const char* column, const char* type) {
    QSqlQuery check(db);
    if (check.exec(QString("SELECT %1 FROM earnings_signal_records LIMIT 1").arg(column)))
        return Result<void>::ok();
    QSqlQuery q(db);
    if (!q.exec(QString("ALTER TABLE earnings_signal_records ADD COLUMN %1 %2").arg(column, type)))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

Result<void> apply_v052(QSqlDatabase& db) {
    for (const auto& [column, type] : {std::pair{"model_version", "INTEGER"},
                                       std::pair{"p_beat", "REAL"},
                                       std::pair{"implied_move_pct", "REAL"}}) {
        auto r = add_column(db, column, type);
        if (r.is_err())
            return r;
    }
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v052() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({52, "earnings_outlook", apply_v052});
}

} // namespace fincept
