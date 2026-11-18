// v058_drop_context_recordings — drop the context-recording tables.
//
// v003 created recorded_contexts and recording_sessions for a "record tab
// data for the AI" feature that was never wired: the only code that touched
// them was a repository nothing called (removed), and Settings > Storage
// counted and cleared tables that nothing could fill.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace finterm {
namespace {

Result<void> apply_v058(QSqlDatabase& db) {
    QSqlQuery q(db);
    for (const char* stmt : {"DROP INDEX IF EXISTS idx_contexts_tab", "DROP TABLE IF EXISTS recorded_contexts",
                             "DROP TABLE IF EXISTS recording_sessions"}) {
        if (!q.exec(QString::fromLatin1(stmt)))
            return Result<void>::err(q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v058() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({58, "drop_context_recordings", apply_v058});
}

} // namespace finterm
