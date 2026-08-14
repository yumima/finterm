// v050_llm_assignment_dedupe — one LLM role binding per slot, and no dangling ones.
//
// llm_profile_assignments carries UNIQUE(context_type, context_id), which made
// "INSERT OR REPLACE" look like an upsert. It wasn't: type-level bindings store
// context_id as NULL, and SQLite treats NULLs as DISTINCT inside a UNIQUE
// constraint, so ('news', NULL) never collided with itself. Every re-bind
// appended another row, and get_assignment()'s unordered "LIMIT 1" then handed
// back the OLDEST — so changing a role's model appeared to save and was
// permanently ignored.
//
// The write path now deletes the slot before inserting, and the read path
// orders newest-first. This migration repairs the rows those older builds left:
//
//   1. Collapse duplicate slots to the newest row (highest id = latest bind).
//   2. Drop assignments pointing at a profile that no longer exists. The FK
//      declares ON DELETE CASCADE, but SQLite only enforces foreign keys when
//      PRAGMA foreign_keys is ON, so deleted profiles left assignments behind
//      that resolve to nothing — a bound role silently falling back.
//
// Both are idempotent, so re-running is harmless.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v050(QSqlDatabase& db) {
    QSqlQuery q(db);

    // 1. Keep only the newest assignment per (context_type, context_id).
    //    IS NOT DISTINCT FROM would be cleaner but SQLite lacks it; comparing
    //    COALESCE against a sentinel groups the NULL rows correctly.
    if (!q.exec("DELETE FROM llm_profile_assignments "
                "WHERE id NOT IN ("
                "  SELECT MAX(id) FROM llm_profile_assignments "
                "  GROUP BY context_type, COALESCE(context_id, char(31))"
                ")"))
        return Result<void>::err(q.lastError().text().toStdString());

    // 2. Drop bindings whose profile is gone.
    if (!q.exec("DELETE FROM llm_profile_assignments "
                "WHERE profile_id NOT IN (SELECT id FROM llm_profiles)"))
        return Result<void>::err(q.lastError().text().toStdString());

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v050() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({50, "llm_assignment_dedupe", apply_v050});
}

} // namespace fincept
