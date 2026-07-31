// v046_earnings_signal_records — What the pre-earnings scorecard said, before
// the print, kept so it can be held against what actually happened.
//
// The scorecard has never been checkable. Yahoo publishes estimates and
// revisions as a point-in-time snapshot with no history, so there is no way to
// reconstruct what the panel would have shown a week before a past report —
// which means the only honest way to find out whether the signal is any good
// is to start writing it down. This table is that record, and it necessarily
// begins empty: nothing before the day it ships can be recovered.
//
// One row per (symbol, report, calendar day observed). Daily rows rather than
// one-per-report on purpose — the trajectory is the interesting part, since a
// verdict that swings from BUY to SELL in the last week says something a
// single snapshot cannot.
//
// The observation columns are write-once. Only the outcome columns are filled
// later, exactly once, when the print lands and its reaction settles — the
// same discipline as ai_predictions (v044), and for the same reason: a track
// record you can edit is not a track record.

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

Result<void> apply_v046(QSqlDatabase& db) {
    auto r = sql(db,
        "CREATE TABLE IF NOT EXISTS earnings_signal_records ("
        "  id               INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  symbol           TEXT    NOT NULL,"
        "  report_ts        INTEGER NOT NULL,"   // the print this is about
        "  observed_on      TEXT    NOT NULL,"   // YYYY-MM-DD, market time
        "  captured_at      TEXT    NOT NULL,"   // ISO-8601 UTC
        "  days_to_report   INTEGER NOT NULL,"
        // ── what the scorecard said, write-once ─────────────────────────────
        "  verdict          TEXT    NOT NULL,"   // BUY | HOLD | SELL
        "  score            REAL    NOT NULL,"   // -100 … +100
        "  confidence       REAL    NOT NULL,"   // 0 … 1
        "  setup_score      REAL,"               // NULL when that axis had no data
        "  bar_score        REAL,"
        // The scorecard does not forecast a move. What it offers is a
        // direction lean (verdict) and an expected MAGNITUDE from history —
        // both stored, and neither dressed up as a price target.
        "  expected_move_pct REAL,"              // mean |1-day move| over past prints
        "  consensus_eps    REAL,"
        "  dispersion_pct   REAL,"
        "  runup_5d_pct     REAL,"
        "  price_at_capture REAL,"
        // ── what happened, filled once ──────────────────────────────────────
        "  resolved         INTEGER NOT NULL DEFAULT 0,"
        "  actual_eps       REAL,"
        "  surprise_pct     REAL,"
        "  actual_move_pct  REAL,"               // realised close-to-close reaction
        "  direction_hit    INTEGER,"            // 1 when the lean matched the move
        "  resolved_at      TEXT"
        ")");
    if (r.is_err())
        return r;

    // One observation per symbol per report per day. Re-opening the tab five
    // times in an afternoon must not five-count that day's reading and tilt
    // any hit rate computed from these rows.
    r = sql(db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_esr_unique "
                "ON earnings_signal_records(symbol, report_ts, observed_on)");
    if (r.is_err())
        return r;

    // Hot paths: the track record for one symbol, and the resolution sweep
    // looking for prints that have since happened.
    r = sql(db, "CREATE INDEX IF NOT EXISTS idx_esr_symbol "
                "ON earnings_signal_records(symbol, report_ts DESC)");
    if (r.is_err())
        return r;
    return sql(db, "CREATE INDEX IF NOT EXISTS idx_esr_unresolved "
                   "ON earnings_signal_records(resolved, report_ts)");
}

} // anonymous namespace

void register_migration_v046() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({46, "earnings_signal_records", apply_v046});
}

} // namespace fincept
