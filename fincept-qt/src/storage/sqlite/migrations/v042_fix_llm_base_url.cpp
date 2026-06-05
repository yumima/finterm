// v042_fix_llm_base_url — strip stale /v1 suffix from ollama base_url rows.
//
// v041 (migrate_fincept_provider) incorrectly stored base_url with a /v1
// suffix (e.g. "http://127.0.0.1:11435/v1"). LlmService::get_endpoint_url()
// already appends /v1/chat/completions to whatever is in base_url_, so the
// actual request URL became .../v1/v1/chat/completions → 404.
//
// The correct convention (matching the pre-existing Ollama row stored as
// "http://localhost:11434") is to store the ROOT without /v1.
//
// This migration:
//   1. Strips a trailing /v1 or /v1/ from any ollama base_url that has one.
//   2. Also ensures that the ollama row is marked active when it is the only
//      provider (the case where v041 migrated fincept→ollama but the row had
//      is_active=0, leaving no active provider).
//
// Idempotent: already-correct rows are unchanged; no fincept rows → no-op.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>

namespace fincept {
namespace {

constexpr const char* kMig042Tag = "Migration042";

static Result<void> sql_exec(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v042(QSqlDatabase& db) {
    // 1. Strip trailing /v1 or /v1/ from ollama base_url values.
    // SQLite's RTRIM(str, chars) treats the second arg as a SET of characters,
    // not a literal string — RTRIM(url, '/v1') would strip all '/', 'v', '1'
    // chars from the right, leaving a bare host. Use SUBSTR+LENGTH instead:
    // strip a trailing '/v1/' first (4 chars), then '/v1' (3 chars).
    {
        // Step 1: trailing /v1/ → remove 4 chars
        QSqlQuery fix1(db);
        fix1.prepare(
            "UPDATE llm_configs "
            "SET base_url = SUBSTR(base_url, 1, LENGTH(base_url) - 4), "
            "    updated_at = datetime('now') "
            "WHERE provider = 'ollama' AND base_url LIKE '%/v1/'");
        if (!fix1.exec())
            return Result<void>::err(fix1.lastError().text().toStdString());

        // Step 2: trailing /v1 (without slash) → remove 3 chars
        QSqlQuery fix2(db);
        fix2.prepare(
            "UPDATE llm_configs "
            "SET base_url = SUBSTR(base_url, 1, LENGTH(base_url) - 3), "
            "    updated_at = datetime('now') "
            "WHERE provider = 'ollama' AND base_url LIKE '%/v1'");
        if (!fix2.exec())
            return Result<void>::err(fix2.lastError().text().toStdString());

        // Step 3: any remaining trailing slash
        QSqlQuery fix3(db);
        fix3.prepare(
            "UPDATE llm_configs "
            "SET base_url = SUBSTR(base_url, 1, LENGTH(base_url) - 1), "
            "    updated_at = datetime('now') "
            "WHERE provider = 'ollama' AND base_url LIKE '%/'");
        if (!fix3.exec())
            return Result<void>::err(fix3.lastError().text().toStdString());

        const int affected = fix1.numRowsAffected() + fix2.numRowsAffected() + fix3.numRowsAffected();
        if (affected > 0)
            LOG_INFO(kMig042Tag,
                QString("stripped /v1 suffix from ollama base_url (%1 step(s))")
                    .arg(affected));
    }

    // 2. If no active provider exists but exactly one ollama row does, make it
    //    active so LLM Config UI shows a ✓ and LlmService uses it directly.
    {
        QSqlQuery active_count(db);
        active_count.prepare(
            "SELECT COUNT(*) FROM llm_configs WHERE is_active = 1");
        if (!active_count.exec() || !active_count.next())
            return Result<void>::ok();
        if (active_count.value(0).toInt() > 0)
            return Result<void>::ok(); // already have an active row — done

        QSqlQuery ollama_count(db);
        ollama_count.prepare(
            "SELECT COUNT(*) FROM llm_configs WHERE provider = 'ollama'");
        if (!ollama_count.exec() || !ollama_count.next())
            return Result<void>::ok();
        if (ollama_count.value(0).toInt() != 1)
            return Result<void>::ok(); // ambiguous — don't guess

        if (auto r = sql_exec(db,
                "UPDATE llm_configs SET is_active = 1 WHERE provider = 'ollama'");
            r.is_err())
            return r;
        LOG_INFO(kMig042Tag, "activated the sole ollama row as the active provider");
    }

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v042() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({42, "fix_llm_base_url", apply_v042});
}

} // namespace fincept
