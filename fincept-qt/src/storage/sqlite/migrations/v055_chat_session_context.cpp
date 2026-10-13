// v055_chat_session_context — context tag on chat sessions.
//
// Embedded chat panes are scoped to a thing the user is looking at — a stock
// (Equity Research › AI) or a portfolio (Insights panel). Their threads are
// worth keeping ("what did I ask about NVDA last week"), but only if the pane
// can find its own thread again. The tag is that address: "symbol:NVDA",
// "portfolio:<id>", or '' for an untagged (global) session.
//
// A plain column + index rather than a new table: a tagged session IS an
// ordinary chat session — same messages, same Workbench recent-sessions view —
// it just also answers "whose pane do you belong to".

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v055(QSqlDatabase& db) {
    QSqlQuery probe(db);
    if (!probe.exec("SELECT context_tag FROM chat_sessions LIMIT 1")) {
        QSqlQuery alter(db);
        if (!alter.exec("ALTER TABLE chat_sessions ADD COLUMN context_tag TEXT NOT NULL DEFAULT ''"))
            return Result<void>::err(alter.lastError().text().toStdString());
    }
    QSqlQuery idx(db);
    if (!idx.exec("CREATE INDEX IF NOT EXISTS idx_chat_sessions_context "
                  "ON chat_sessions(context_tag, updated_at DESC)"))
        return Result<void>::err(idx.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v055() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({55, "chat_session_context", apply_v055});
}

} // namespace fincept
