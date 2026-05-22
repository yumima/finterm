// v023_llm_profile_runtime — Add a first-class `runtime` column to
// llm_profiles so the agent dispatch path can pick between the two
// runtimes finterm now ships (Anthropic via claude-agent-sdk, and
// the minimal local OpenAI-compatible loop) without re-deriving it
// from the legacy `provider` string at every call site.
//
// Per R1 / R3 in plans/ai-stack.md.
//
// Backfill from existing provider values:
//   provider = 'anthropic' → runtime = 'anthropic'
//   provider = 'ollama'    → runtime = 'local'
//   anything else          → runtime = 'external'
//
// 'external' covers the dormant provider adapters (R2): the row
// keeps loading so legacy config doesn't break, but no agent
// dispatch is wired to those values until/unless they are
// reactivated by a future track.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration023";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v023(QSqlDatabase& db) {
    // llm_profiles arrived in v010.  A fresh DB will pass through v010
    // before reaching here so the table is guaranteed to exist.
    QSqlQuery exists(db);
    if (!exists.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='llm_profiles'"))
        return Result<void>::err(exists.lastError().text().toStdString());
    if (!exists.next()) {
        LOG_WARN(kTag, "llm_profiles table missing — skipping v023");
        return Result<void>::ok();
    }

    // SQLite ALTER TABLE ADD COLUMN is forwards-compatible across the
    // versions we target.  Default '' so existing rows pass NOT NULL.
    if (auto r = sql(db, "ALTER TABLE llm_profiles ADD COLUMN runtime TEXT NOT NULL DEFAULT ''");
        r.is_err()) {
        // ALTER will fail if the column already exists (re-run).  Skip.
        const std::string& err = r.error();
        if (err.find("duplicate column") == std::string::npos &&
            err.find("already exists") == std::string::npos) {
            return r;
        }
    }

    // Backfill from provider.  Idempotent: only touches blank runtimes.
    if (auto r = sql(db,
            "UPDATE llm_profiles SET runtime = "
            "  CASE LOWER(provider) "
            "    WHEN 'anthropic' THEN 'anthropic' "
            "    WHEN 'ollama' THEN 'local' "
            "    ELSE 'external' "
            "  END "
            "WHERE runtime IS NULL OR runtime = ''");
        r.is_err())
        return r;

    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v023() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({23, "llm_profile_runtime", apply_v023});
}

} // namespace fincept
