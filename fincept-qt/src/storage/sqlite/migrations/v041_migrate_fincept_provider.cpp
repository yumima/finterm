// v041_migrate_fincept_provider — retire the legacy "fincept" hosted-LLM row.
//
// The fincept hosted service was retired in R17. Any llm_configs row still
// carrying provider="fincept" is stale and confuses users (shows "finterm LLM"
// in LLM Config with an "API Key" placeholder pointing at an account that no
// longer serves inference).
//
// This migration:
//   1. Rewrites the fincept row to provider="ollama" with hearth's loopback
//      base URL (http://127.0.0.1:11435, the bare root — LlmService appends
//      /v1/...) and the "primary_chat" role alias, so the user immediately has
//      a working local-engine config.
//   2. Leaves is_active as it was on the fincept row (an UPDATE, not a
//      re-activation). v042 then activates the sole ollama row if nothing else
//      is active, so the user routes through hearth without touching Settings.
//   3. Leaves any other provider rows untouched.
//
// Idempotent: re-running is a no-op when no fincept row exists.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kMig041Tag = "Migration041";

static Result<void> sql(QSqlDatabase& db, const char* stmt,
                        const QVariantList& params = {}) {
    QSqlQuery q(db);
    q.prepare(stmt);
    for (int i = 0; i < params.size(); ++i)
        q.addBindValue(params[i]);
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static bool has_fincept(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM llm_configs WHERE provider = 'fincept'");
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toInt() > 0;
}

static bool has_ollama(QSqlDatabase& db) {
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM llm_configs WHERE provider = 'ollama'");
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toInt() > 0;
}

static Result<void> apply_v041(QSqlDatabase& db) {
    if (!has_fincept(db)) {
        LOG_INFO(kMig041Tag, "no fincept row — nothing to migrate");
        return Result<void>::ok();
    }

    if (has_ollama(db)) {
        // An ollama row already exists. Just delete the stale fincept row so
        // it doesn't clutter the provider list; the ollama row takes over.
        if (auto r = sql(db, "DELETE FROM llm_configs WHERE provider = 'fincept'");
            r.is_err())
            return r;
        LOG_INFO(kMig041Tag, "deleted stale fincept row (ollama row already present)");
        return Result<void>::ok();
    }

    // No ollama row yet: rewrite fincept → ollama-at-hearth so the user gets
    // a working local config immediately. base_url points at hearth's default
    // loopback port; model uses the primary_chat role alias.
    // base_url is the root without /v1 — LlmService appends /v1/chat/completions
    // and /v1/models itself, matching the convention used by Ollama (:11434).
    if (auto r = sql(db,
            "UPDATE llm_configs SET provider = 'ollama', "
            "  base_url = 'http://127.0.0.1:11435', "
            "  model    = 'primary_chat', "
            "  updated_at = datetime('now') "
            "WHERE provider = 'fincept'");
        r.is_err())
        return r;

    LOG_INFO(kMig041Tag,
        "migrated fincept provider row to ollama (hearth) with model=primary_chat");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v041() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({41, "migrate_fincept_provider", apply_v041});
}

} // namespace fincept
