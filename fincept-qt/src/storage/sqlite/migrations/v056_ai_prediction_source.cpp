// v056_ai_prediction_source — which engine made the forecast.
//
// The Equity Research AI tab now records predictions from two engines: the
// quant read (fundamentals + price, JSON forecast — everything recorded before
// this migration) and the FinGPT news forecaster (weekly news windows). One
// track record scores both, so each row must say whose claim it grades —
// otherwise the hit rate blends two different methods into one meaningless
// number. Existing rows are all quant by construction.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v056(QSqlDatabase& db) {
    QSqlQuery probe(db);
    if (!probe.exec("SELECT source FROM ai_predictions LIMIT 1")) {
        QSqlQuery alter(db);
        if (!alter.exec("ALTER TABLE ai_predictions ADD COLUMN source TEXT NOT NULL DEFAULT 'quant'"))
            return Result<void>::err(alter.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v056() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({56, "ai_prediction_source", apply_v056});
}

} // namespace fincept
