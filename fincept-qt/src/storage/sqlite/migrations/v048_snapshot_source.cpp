// v048_snapshot_source — provenance for portfolio NAV snapshots.
//
// portfolio_snapshots held two kinds of row under one key with no way to tell
// them apart: real end-of-day valuations written by build_summary from live
// quotes, and synthetic rows written by backfill_history. Because
// save_snapshot was INSERT OR REPLACE on (portfolio_id, snapshot_date), any
// backfill request — including the one the perf chart fires when the user
// clicks a longer period — overwrote every genuine snapshot the app had
// accumulated.
//
// This column is the fix's foundation: 'live' rows are real observations and
// immutable to backfill; 'backfill' rows are estimates that later backfills
// (and the daily live write) may correct. The CHECK matches the
// transaction_type convention on the sibling table — a writer inserting
// 'Live' or 'live ' would otherwise mint rows the source <> 'live' guard
// happily destroys.
//
// Existing rows: created_at discriminates. A real daily snapshot is written
// on the day it describes, so created_at and snapshot_date agree; the old
// backfill wrote weeks or years of past dates in one run, so its rows carry
// a created_at far after their snapshot_date. Rows written 2+ days after the
// date they claim are reclassified 'backfill' — correctable by the next
// (now historically-accurate) backfill instead of frozen. The one-day slack
// keeps timezone skew (created_at is UTC, snapshot_date the user's local
// date) from misfiling a genuine late-evening live row; the ambiguous band
// defaults to 'live' because destroying a real observation is irreversible
// while keeping a stale estimate is merely stale.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v048(QSqlDatabase& db) {
    // Tolerate the column already existing so a re-run cannot fail the chain.
    QSqlQuery check(db);
    if (check.exec("SELECT source FROM portfolio_snapshots LIMIT 1"))
        return Result<void>::ok();

    QSqlQuery q(db);
    if (!q.exec("ALTER TABLE portfolio_snapshots "
                "ADD COLUMN source TEXT NOT NULL DEFAULT 'live' "
                "CHECK(source IN ('live','backfill'))"))
        return Result<void>::err(q.lastError().text().toStdString());

    QSqlQuery reclass(db);
    if (!reclass.exec("UPDATE portfolio_snapshots SET source = 'backfill' "
                      "WHERE created_at IS NOT NULL "
                      "  AND substr(created_at, 1, 10) > date(substr(snapshot_date, 1, 10), '+1 day')"))
        return Result<void>::err(reclass.lastError().text().toStdString());

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v048() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({48, "snapshot_source", apply_v048});
}

} // namespace fincept
