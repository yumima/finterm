#include "storage/sqlite/Database.h"

#include "core/logging/Logger.h"

#include <QThread>

#include <cstring>
#include "storage/sqlite/migrations/MigrationRunner.h"

namespace finterm {

Database& Database::instance() {
    static Database s;
    return s;
}

Database::Database() : conns_(QStringLiteral("finterm_main"), &Database::apply_pragmas) {}

Result<void> Database::open(const QString& path) {
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
