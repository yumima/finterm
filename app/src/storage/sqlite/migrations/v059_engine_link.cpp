// v059_engine_link — who was using hearth keeps using it; a new user is asked.
//
// finterm used whatever answered at hearth's address without a question. Now
// the user's answer decides (EngineLinkRepository), so an update must give it
// for those who were using hearth: their chat was with it (its row is active,
// or no row is and it is what chat fell back to), a role is bound to it, their
// memory has been embedded, or they had finterm start it. Anybody else is asked
// when there is a hearth to connect to.
//
// It also leaves one active provider row, the one the chat was with.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace finterm {
namespace {

bool any(QSqlDatabase& db, const char* sql) {
    QSqlQuery q(db); // a table this database never had counts as none
    return q.exec(QString::fromLatin1(sql)) && q.next() && q.value(0).toInt() > 0;
}

constexpr const char* kHearth = "lower(provider) IN ('hearth','ollama')";

Result<void> apply_v059(QSqlDatabase& db) {
    const QString hearth = QString::fromLatin1(kHearth);
    auto count = [&db](const QString& sql) { return any(db, sql.toUtf8().constData()); };
    QSqlQuery q(db);
    auto run = [&q](const QString& sql) { return q.exec(sql); };
    auto failed = [&q]() { return Result<void>::err(q.lastError().text().toStdString()); };

    // A database made just now has nothing of a user's in it: the provider row
    // the migrations seed (and v002–v051 left active) is no choice of theirs.
    const bool fresh = !any(db, "SELECT COUNT(*) FROM settings");
    bool connected = false;
    if (!fresh) {
        const bool managed = any(db, "SELECT COUNT(*) FROM settings WHERE key = 'hearth.manage' AND value = 'true'");
        // The chat was with the first active row, or the first of all when none was.
        const QString first = "SELECT provider FROM llm_configs ORDER BY is_active DESC, provider LIMIT 1";
        const bool chat = !any(db, "SELECT COUNT(*) FROM llm_configs") ||
                          count("SELECT COUNT(*) FROM (" + first + ") WHERE " + hearth);
        const bool role = any(db, "SELECT COUNT(*) FROM llm_profile_assignments a JOIN llm_profiles p ON p.id = a.profile_id "
                                  "WHERE lower(p.provider) IN ('hearth','ollama')");
        const bool memory = any(db, "SELECT COUNT(*) FROM memory_entries WHERE embedding IS NOT NULL");
        connected = managed || chat || role || memory;
        if (connected && !run("INSERT OR IGNORE INTO settings (key, value, category) VALUES ('hearth.link', 'connected', 'ai')"))
            return failed();
        if (managed &&
            !run("INSERT OR IGNORE INTO settings (key, value, category) VALUES ('hearth.start_with_app', 'true', 'ai')"))
            return failed();
        if (!run("DELETE FROM settings WHERE key = 'hearth.manage'"))
            return failed();
    }

    // From here on exactly the active row chats: no "first one, if none is", by
    // which deleting or disconnecting one provider moved the chat to another.
    // (Saving one that was not the default used to make it a second active row.)
    if (!connected && !run("UPDATE llm_configs SET is_active = 0 WHERE " + hearth))
        return failed();
    const QString usable = connected ? QString() : " WHERE NOT (" + hearth + ")";
    if (!fresh && !run("UPDATE llm_configs SET is_active = CASE WHEN provider = (SELECT provider FROM llm_configs" + usable +
                       " ORDER BY is_active DESC, provider LIMIT 1) THEN 1 ELSE 0 END"))
        return failed();
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v059() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({59, "engine_link", apply_v059});
}

} // namespace finterm
