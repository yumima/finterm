// v039_artefact_supersedes — Add chat_artefacts.supersedes_id so an
// artefact can explicitly point at its predecessor (Track 106).
//
// History: the Workbench Artefacts panel's Lineage view (Track 99)
// currently infers lineage by grouping artefacts that share the
// same (source_agent_id, source_skill, source_args_json) triple,
// because emit_artefact didn't carry a predecessor pointer.  That
// inference is good for "same dispatch handle, different runs",
// but it loses ordering precision when the same handle re-runs
// with different args.
//
// This migration adds an explicit `supersedes_id` column that
// re-run dispatches can set on the newly-emitted artefact.  The
// lineage view continues to fall back to the identity-based
// grouping for legacy rows where supersedes_id is NULL.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration039";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v039(QSqlDatabase& db) {
    if (auto r = sql(db,
            "ALTER TABLE chat_artefacts ADD COLUMN supersedes_id TEXT");
        r.is_err()) {
        const QString msg = QString::fromStdString(r.error());
        if (!msg.contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive))
            return r;
        LOG_INFO(kTag, "chat_artefacts.supersedes_id already present — skipping");
    }
    // Index the FK-shaped column for the lineage walk.
    if (auto r = sql(db,
            "CREATE INDEX IF NOT EXISTS idx_chat_artefacts_supersedes "
            "ON chat_artefacts(supersedes_id)"); r.is_err())
        LOG_WARN(kTag, QString("supersedes index create failed: %1").arg(QString::fromStdString(r.error())));
    LOG_INFO(kTag, "chat_artefacts.supersedes_id ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v039() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({39, "artefact_supersedes", apply_v039});
}

} // namespace fincept
