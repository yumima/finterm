// v022_llm_secure_keys — Move LLM api_key columns to SecureStorage.
//
// Before this migration, plaintext API keys lived in three columns:
//   llm_configs.api_key        (per provider)
//   llm_model_configs.api_key  (per custom model)
//   llm_profiles.api_key       (per profile)
//
// This migration:
//   1. Reads each row with a non-empty api_key, writes the value to
//      SecureStorage under llm.configs.<provider> / llm.models.<id> /
//      llm.profiles.<id>, then
//   2. NULLs every api_key column it copied.
//
// Idempotent: re-storing a key is an overwrite; UPDATEing already-NULL
// rows is a no-op.  Safe to re-run if a previous attempt rolled back.
//
// Columns are kept (not dropped) for schema stability across SQLite
// versions and to give still-old callers an empty-string read rather
// than a binding error.  A future migration can drop them once every
// read path is verified off the column.
//
// Per R16 in plans/ai-stack-free-local.md.

#include "core/logging/Logger.h"
#include "storage/secure/LlmSecureKeys.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration022";

struct Row {
    QString id;       // primary key (provider name, model id, profile id)
    QString api_key;
};

// Read rows with non-empty api_key from a table.  Returns empty result
// silently when the table doesn't yet exist — that's the case for
// llm_model_configs on databases that never ran v002's custom-model
// path, and for llm_profiles on pre-v010 databases.
static Result<QVector<Row>> read_keyed_rows(QSqlDatabase& db, const QString& table, const QString& id_col) {
    QSqlQuery q(db);
    if (!q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='" + table + "'")) {
        return Result<QVector<Row>>::err(q.lastError().text().toStdString());
    }
    if (!q.next()) {
        // Table absent — nothing to migrate.
        return Result<QVector<Row>>::ok({});
    }

    QSqlQuery rows(db);
    rows.prepare(QString("SELECT %1, api_key FROM %2 WHERE api_key IS NOT NULL AND api_key != ''").arg(id_col, table));
    if (!rows.exec()) {
        return Result<QVector<Row>>::err(rows.lastError().text().toStdString());
    }

    QVector<Row> out;
    while (rows.next()) {
        out.push_back({rows.value(0).toString(), rows.value(1).toString()});
    }
    return Result<QVector<Row>>::ok(std::move(out));
}

static Result<void> null_api_keys(QSqlDatabase& db, const QString& table) {
    QSqlQuery exists(db);
    if (!exists.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='" + table + "'")) {
        return Result<void>::err(exists.lastError().text().toStdString());
    }
    if (!exists.next())
        return Result<void>::ok(); // table absent

    QSqlQuery q(db);
    if (!q.exec(QString("UPDATE %1 SET api_key = NULL WHERE api_key IS NOT NULL").arg(table))) {
        return Result<void>::err(q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

static Result<void> apply_v022(QSqlDatabase& db) {
    struct TableSpec {
        QString table;
        QString id_col;
        QString (*key_fn)(const QString&);
    };

    const TableSpec specs[] = {
        {"llm_configs", "provider", &llm_secure_key_for_provider},
        {"llm_model_configs", "id", &llm_secure_key_for_model},
        {"llm_profiles", "id", &llm_secure_key_for_profile},
    };

    // Phase 1: write every non-empty api_key into SecureStorage.
    // We do all writes before any column NULLs so an early SecureStorage
    // failure leaves the SQL state untouched.
    int migrated = 0;
    for (const auto& spec : specs) {
        auto rows = read_keyed_rows(db, spec.table, spec.id_col);
        if (rows.is_err())
            return Result<void>::err(rows.error());

        for (const auto& row : rows.value()) {
            QString key = spec.key_fn(row.id);
            auto r = llm_secure_store(key, row.api_key);
            if (r.is_err()) {
                return Result<void>::err("SecureStorage::store(" + key.toStdString() + "): " + r.error());
            }
            ++migrated;
        }
    }

    // Phase 2: NULL every api_key column we copied.
    for (const auto& spec : specs) {
        auto r = null_api_keys(db, spec.table);
        if (r.is_err())
            return r;
    }

    LOG_INFO(kTag, QString("Moved %1 LLM api_key value(s) to SecureStorage").arg(migrated));
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v022() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({22, "llm_secure_keys", apply_v022});
}

} // namespace fincept
