#include "storage/sqlite/Database.h"

#include "core/logging/Logger.h"

#include <QFileInfo>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

#include <cstring>
#include "storage/sqlite/migrations/MigrationRunner.h"

namespace finterm {

Database& Database::instance() {
    static Database s;
    return s;
}

namespace {

/// The highest schema_version row of a database file, read-only; 0 when it
/// has none (or does not exist yet).
int stored_schema_version(const QString& path) {
    if (!QFileInfo::exists(path))
        return 0;
    const QString name = QStringLiteral("finterm_version_probe_") + QUuid::createUuid().toString(QUuid::Id128);
    int v = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'")) &&
                q.next() && q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) && q.next())
                v = q.value(0).toInt();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return v;
}

} // namespace

Database::Database() : conns_(QStringLiteral("finterm_main"), &Database::apply_pragmas) {}

Result<void> Database::open(const QString& path) {
    // Data a newer finterm wrote is refused before anything touches it (this
    // build's pragmas, its schema_version table): read with a connection of
    // its own, read-only. DataGate refuses it at start; this is the guard for
    // anything that slips past.
    // (The same rule as MigrationRunner::newer_than_build(), on a file not yet open.)
    if (const int stored = stored_schema_version(path); stored > MigrationRunner::newest_version())
        return Result<void>::err("This data was written by a newer version of finterm (data version " +
                                 std::to_string(stored) + "; this version reads up to " +
                                 std::to_string(MigrationRunner::newest_version()) + ")");
    // Behind this build while the copy that must come first could not be
    // made: not migrated in place (see MigrationRunner::hold).
    if (!MigrationRunner::hold().isEmpty() && QFileInfo::exists(path) &&
        stored_schema_version(path) < MigrationRunner::newest_version())
        return Result<void>::err(MigrationRunner::hold().toStdString());
    const QString error = conns_.set_path(path);
    if (!error.isEmpty()) {
        conns_.set_path({});
        return Result<void>::err(error.toStdString());
    }
    LOG_INFO("DB", "Opened database: " + path);

    // Run versioned migrations on this thread's connection.
    QSqlDatabase db = conns_.connection();
    MigrationRunner runner(db);
    return runner.run();
}

void Database::close() {
    // Every thread drops its connection on its next use; this thread's now.
    // A QSqlQuery a caller still holds stops working: call this only at
    // login/logout/account boundaries.
    conns_.set_path({});
}

Result<void> Database::reopen(const QString& path) {
    // Safe at login/logout boundaries, when no repository code is executing.
    conns_.set_path({});
    return open(path);
}

bool Database::is_open() const {
    return !conns_.path().isEmpty();
}

Result<QSqlQuery> Database::execute(const QString& sql, const QVariantList& params) {
    // Early-return before constructing a QSqlQuery against a closed database.
    // Qt's QSqlQuery::prepare unconditionally logs "database not open" via
    // the qt.sql.qsqlquery category — that noise hides real SQL errors at
    // startup, where SettingsRepository legitimately probes for legacy-
    // migration state before AuthService opens the user DB. Caller code
    // already handles the error result.
    QString error;
    QSqlDatabase db = conns_.connection(&error);
    if (!db.isOpen())
        return Result<QSqlQuery>::err(error.toStdString());
    QSqlQuery query(db);
    query.prepare(sql);
    for (int i = 0; i < params.size(); ++i) {
        query.bindValue(i, params[i]);
    }
    if (!query.exec()) {
        return Result<QSqlQuery>::err(query.lastError().text().toStdString());
    }
    return Result<QSqlQuery>::ok(std::move(query));
}

Result<void> Database::exec(const QString& sql) {
    QString error;
    QSqlDatabase db = conns_.connection(&error);
    if (!db.isOpen())
        return Result<void>::err(error.toStdString());
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        return Result<void>::err(query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> Database::begin_transaction() {
    return exec(QStringLiteral("BEGIN IMMEDIATE"));
}

Result<void> Database::commit() {
    return exec(QStringLiteral("COMMIT"));
}

Result<void> Database::rollback() {
    return exec(QStringLiteral("ROLLBACK"));
}

QString Database::apply_pragmas(QSqlDatabase& db) {
    const char* pragmas[] = {
        "PRAGMA journal_mode = WAL", "PRAGMA synchronous = NORMAL", "PRAGMA cache_size = -20000",
        "PRAGMA temp_store = MEMORY", "PRAGMA mmap_size = 268435456", "PRAGMA foreign_keys = ON",
        "PRAGMA busy_timeout = 5000",
        // Bound WAL growth. The default wal_autocheckpoint (1000 pages ≈
        // 4 MB) can drift much larger if any reader holds a connection
        // open across the checkpoint, which is exactly what happens here
        // — long-lived background pollers (trading, market data) keep
        // reader connections open continuously. We checkpoint more often
        // and hard-cap the on-disk WAL so a multi-day session can't
        // accumulate GBs of WAL behind a stalled reader.
        "PRAGMA wal_autocheckpoint = 400",      // checkpoint every ~1.5 MB
        "PRAGMA journal_size_limit = 67108864", // truncate WAL to 64 MB after checkpoint
    };
    // Per connection, so every thread's connection gets them. A failed PRAGMA
    // is logged and the connection still used, as before.
    for (auto* p : pragmas) {
        QSqlQuery q(db);
        // The large page cache is for the GUI thread's connection; every worker
        // thread has one too, and 2 MB each is plenty for what they run.
        if (std::strstr(p, "cache_size") && !QThread::isMainThread())
            p = "PRAGMA cache_size = -2000";
        if (!q.exec(QString::fromLatin1(p))) {
            LOG_WARN("DB", QString("PRAGMA failed: %1 — %2").arg(p, q.lastError().text()));
        }
    }
    return {};
}

} // namespace finterm
