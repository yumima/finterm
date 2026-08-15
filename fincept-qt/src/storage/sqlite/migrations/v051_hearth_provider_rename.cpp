// v051_hearth_provider_rename — call the local gateway what it is.
//
// The provider id was "ollama", but that names the wrong thing: hearth is the
// provider — the OpenAI-compatible gateway on 127.0.0.1:11435 — and Ollama is
// one engine it drives behind that. The Models tab therefore read as though
// finterm talked to Ollama directly, and it cost a real bug: a model field was
// filled in with "hearth", because that is what the provider actually is, and
// the resulting requests failed with "the local model returned nothing".
//
// Rewrites existing rows to the canonical id. is_hearth_provider() still
// accepts "ollama" everywhere, so a row this migration misses (an older
// profile, a hand-edited config) keeps resolving rather than silently losing
// its provider.

#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

Result<void> apply_v051(QSqlDatabase& db) {
    QSqlQuery q(db);
    // llm_configs.provider is the primary key, so a rename can collide with an
    // existing "hearth" row. Drop that case rather than fail the migration.
    if (!q.exec("DELETE FROM llm_configs WHERE provider = 'hearth' "
                "  AND EXISTS (SELECT 1 FROM llm_configs WHERE provider = 'ollama')"))
        return Result<void>::err(q.lastError().text().toStdString());
    if (!q.exec("UPDATE llm_configs SET provider = 'hearth' WHERE provider = 'ollama'"))
        return Result<void>::err(q.lastError().text().toStdString());
    if (!q.exec("UPDATE llm_profiles SET provider = 'hearth', "
                "  name = replace(name, 'ollama · ', 'hearth · ') WHERE provider = 'ollama'"))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v051() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({51, "hearth_provider_rename", apply_v051});
}

} // namespace fincept
