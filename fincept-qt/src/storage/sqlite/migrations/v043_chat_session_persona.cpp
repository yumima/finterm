// v043_chat_session_persona — Add persona_id to chat_sessions.
// Each conversation binds to a chat persona (ChatPersonas.h); existing rows
// default to 'general'. Lets each conversation/pane carry its own persona
// instead of the old global LlmService::set_persona singleton.

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

Result<void> apply_v043(QSqlDatabase& db) {
    // Add persona_id — defaults to 'general' for all existing conversations.
    auto r = sql(db, "ALTER TABLE chat_sessions ADD COLUMN persona_id TEXT DEFAULT 'general'");
    if (r.is_err()) {
        // Column may already exist if user ran a dev build — ignore that case.
        QSqlQuery check(db);
        check.exec("SELECT persona_id FROM chat_sessions LIMIT 1");
        if (check.lastError().isValid())
            return r; // real error
    }
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v043() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({43, "chat_session_persona", apply_v043});
}

} // namespace fincept
