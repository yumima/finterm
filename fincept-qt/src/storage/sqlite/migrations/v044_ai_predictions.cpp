// v044_ai_predictions — Immutable AI stock-forecast track record.
//
// Each row is one prediction the local AI made for a ticker at a point in time.
// The PREDICTION fields (direction, target, rationale, …) are written once and
// NEVER updated — that is the whole point of a track record. Only the
// RESOLUTION fields (price_at_resolve, actual_pct, correct, resolved_at) are
// filled in a single time, when the horizon elapses, by comparing against the
// real close. This lets the UI overlay "what the AI predicted" against "what
// the stock actually did" and show an honest, tamper-free hit rate.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

Result<void> apply_v044(QSqlDatabase& db) {
    auto r = sql(db,
        "CREATE TABLE IF NOT EXISTS ai_predictions ("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  ticker          TEXT    NOT NULL,"
        "  created_at      TEXT    NOT NULL,"   // ISO-8601 UTC — when predicted
        "  model           TEXT,"               // LLM model id used
        "  horizon_days    INTEGER NOT NULL,"   // calendar days to resolution
        "  resolve_date    TEXT    NOT NULL,"   // YYYY-MM-DD target date
        "  price_at_pred   REAL    NOT NULL,"   // real close at prediction time
        "  direction       TEXT    NOT NULL,"   // 'up' | 'down' | 'flat'
        "  target_price    REAL    DEFAULT 0,"  // predicted price at horizon
        "  predicted_pct   REAL    DEFAULT 0,"  // predicted % move
        "  confidence      INTEGER DEFAULT 0,"  // 0–100
        "  recommendation  TEXT,"               // buy|accumulate|hold|reduce|sell
        "  rationale       TEXT,"               // one-line why
        "  analysis        TEXT,"               // full analysis prose
        // ── resolution (filled once when the horizon passes) ──────────────────
        "  resolved          INTEGER NOT NULL DEFAULT 0,"
        "  price_at_resolve  REAL    DEFAULT 0,"
        "  actual_pct        REAL    DEFAULT 0,"
        "  correct           INTEGER DEFAULT 0,"  // 1 if the direction call hit
        "  resolved_at       TEXT"
        ")");
    if (r.is_err())
        return r;

    // Hot path is "all predictions for this ticker, newest first".
    return sql(db, "CREATE INDEX IF NOT EXISTS idx_ai_pred_ticker "
                   "ON ai_predictions(ticker, created_at DESC)");
}

} // anonymous namespace

void register_migration_v044() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({44, "ai_predictions", apply_v044});
}

} // namespace fincept
