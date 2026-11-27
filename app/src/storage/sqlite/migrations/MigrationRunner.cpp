#include "storage/sqlite/migrations/MigrationRunner.h"

#include "core/logging/Logger.h"

#include <algorithm>

namespace finterm {

// ── Static migration registry ────────────────────────────────────────────────

static QVector<Migration>& migration_registry() {
    static QVector<Migration> registry;
    return registry;
}

void MigrationRunner::register_migration(Migration m) {
    auto& reg = migration_registry();
    reg.append(std::move(m));
    // Keep sorted by version
    std::sort(reg.begin(), reg.end(), [](const Migration& a, const Migration& b) { return a.version < b.version; });
}

const QVector<Migration>& MigrationRunner::all_migrations() {
    return migration_registry();
}

namespace {
QString& hold_reason() {
    static QString reason;
    return reason;
}
} // namespace

void MigrationRunner::set_hold(const QString& reason) {
    hold_reason() = reason;
}

QString MigrationRunner::hold() {
    return hold_reason();
}

int MigrationRunner::newest_version() {
    const auto& all = all_migrations();
    return all.isEmpty() ? 0 : all.last().version;
}

bool MigrationRunner::external_after(int version) {
    const auto& all = all_migrations();
    return std::any_of(all.begin(), all.end(), [version](const Migration& m) { return m.version > version && m.external; });
}

// ── Instance methods ─────────────────────────────────────────────────────────

MigrationRunner::MigrationRunner(QSqlDatabase& db) : db_(db) {}

Result<void> MigrationRunner::run() {
    auto r = ensure_schema_version_table();
    if (r.is_err())
        return r;

    int current = read_current_version();
    const auto& migrations = all_migrations();
    // Written by a newer finterm: its tables are not what this build's code
    // reads and writes, and running on them could damage them.
    if (newer_than_build())
        return Result<void>::err("This data was written by a newer version of finterm (data version " +
                                 std::to_string(current) + "; this version reads up to " +
                                 std::to_string(migrations.last().version) + ")");

    for (const auto& m : migrations) {
        if (m.version <= current)
            continue;

        LOG_INFO("DB", QString("Applying migration v%1: %2").arg(m.version).arg(m.name));

        // Begin transaction for this migration
        QSqlQuery q(db_);
        if (!q.exec("BEGIN IMMEDIATE")) {
            return Result<void>::err("Failed to begin transaction for migration v" + std::to_string(m.version) + ": " +
                                     q.lastError().text().toStdString());
        }

        // Apply
        auto ar = apply_migration(m);
        if (ar.is_err()) {
            q.exec("ROLLBACK");
            LOG_ERROR("DB", QString("Migration v%1 failed: %2").arg(m.version).arg(QString::fromStdString(ar.error())));
            return ar;
        }

        // Record version
        auto vr = record_version(m.version, m.name);
        if (vr.is_err()) {
            q.exec("ROLLBACK");
            return vr;
        }

        // Commit
        if (!q.exec("COMMIT")) {
            return Result<void>::err("Failed to commit migration v" + std::to_string(m.version) + ": " +
                                     q.lastError().text().toStdString());
        }

        LOG_INFO("DB", QString("Migration v%1 applied successfully").arg(m.version));
    }

    if (!migrations.isEmpty()) {
        LOG_INFO("DB", QString("Schema at version %1").arg(read_current_version()));
    }
    return Result<void>::ok();
}

bool MigrationRunner::newer_than_build() {
    const auto& migrations = all_migrations();
    return !migrations.isEmpty() && read_current_version() > migrations.last().version;
}

// ── Private helpers ──────────────────────────────────────────────────────────

Result<void> MigrationRunner::ensure_schema_version_table() {
    QSqlQuery q(db_);
    if (!q.exec("CREATE TABLE IF NOT EXISTS schema_version ("
                "  version INTEGER PRIMARY KEY,"
                "  name TEXT NOT NULL,"
                "  applied_at TEXT DEFAULT (datetime('now'))"
                ")")) {
        return Result<void>::err("Failed to create schema_version table: " + q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> MigrationRunner::apply_migration(const Migration& m) {
    return m.apply(db_);
}

Result<void> MigrationRunner::record_version(int version, const QString& name) {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO schema_version (version, name) VALUES (?, ?)");
    q.bindValue(0, version);
    q.bindValue(1, name);
    if (!q.exec()) {
        return Result<void>::err("Failed to record migration version: " + q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

int MigrationRunner::read_current_version() {
    QSqlQuery q(db_);
    if (!q.exec("SELECT MAX(version) FROM schema_version")) {
        return 0;
    }
    if (!q.next())
        return 0;
    return q.value(0).toInt(); // returns 0 if NULL
}

} // namespace finterm
