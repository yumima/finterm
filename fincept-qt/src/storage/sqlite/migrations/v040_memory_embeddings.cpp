// v040_memory_embeddings — semantic memory search backing.
//
// Adds embedding storage to memory_entries so `memory_search` can do
// cosine similarity (via the local engine's /v1/embeddings, e.g. hearth →
// nomic-embed-text) instead of plain substring LIKE.  The tool contract
// (upsert / search / list with three scopes) is unchanged — agents don't
// know which backing they're hitting.
//
// Storage choice: the embedding is a float32 BLOB on the row, and the KNN
// is computed in C++ over the rows in a scope (see MemoryTools).  At
// finterm's memory scale (hundreds–thousands of rows per scope) brute-force
// cosine is ample and keeps us free of a native sqlite extension dependency.
// The column is forward-compatible: a future sqlite-vec `vec0` virtual table
// can be layered over the same BLOBs without touching the tool API or
// re-embedding.
//
// `embedding` NULL means "not yet embedded" (engine was down at upsert, or a
// row predates this migration) — search falls back to LIKE for those.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration040";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

// ALTER TABLE ... ADD COLUMN is idempotent-unfriendly (errors if the column
// exists), so guard each add by checking pragma table_info first.
static bool has_column(QSqlDatabase& db, const char* table, const char* col) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        return false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String(col))
            return true;
    }
    return false;
}

static Result<void> apply_v040(QSqlDatabase& db) {
    struct Col {
        const char* name;
        const char* ddl;
    };
    const Col cols[] = {
        {"embedding", "ALTER TABLE memory_entries ADD COLUMN embedding BLOB"},
        {"embedding_dim", "ALTER TABLE memory_entries ADD COLUMN embedding_dim INTEGER"},
        {"embedding_model", "ALTER TABLE memory_entries ADD COLUMN embedding_model TEXT"},
    };
    for (const auto& c : cols) {
        if (has_column(db, "memory_entries", c.name))
            continue;
        if (auto r = sql(db, c.ddl); r.is_err())
            return r;
    }
    LOG_INFO(kTag, "memory_entries embedding columns ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v040() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({40, "memory_embeddings", apply_v040});
}

} // namespace fincept
