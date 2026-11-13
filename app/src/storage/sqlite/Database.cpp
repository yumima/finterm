#include "storage/sqlite/Database.h"

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

namespace finterm {

Database& Database::instance() {
    static Database s;
    return s;
}

Result<void> Database::open(const QString& path) {
    {
        // Workers in execute()/exec() read db_ under mutex_; swap it under
        // the same lock. Released before apply_pragmas(), which takes it.
        QMutexLocker lock(&mutex_);
        db_ = QSqlDatabase::addDatabase("QSQLITE", "finterm_main");
        db_.setDatabaseName(path);
        if (!db_.open()) {
            return Result<void>::err(db_.lastError().text().toStdString());
        }
    }
    LOG_INFO("DB", "Opened database: " + path);

    auto pr = apply_pragmas();
    if (pr.is_err())
        return pr;

    // Run versioned migrations
    MigrationRunner runner(db_);
    return runner.run();
}

void Database::close() {
    // Under mutex_ so a worker inside execute()/exec() finishes the statement
    // it is running before the connection goes away, and none starts against
    // a half-closed handle. It does NOT cover a QSqlQuery a caller keeps
    // iterating after execute() returned (the lock is released by then):
    // like reopen(), call this only at login/logout/account boundaries.
    // Nothing that holds mutex_ calls close(), so this cannot self-deadlock.
    QMutexLocker lock(&mutex_);
    close_locked();
}

void Database::close_locked() {
    if (db_.isOpen())
        db_.close();
}

Result<void> Database::reopen(const QString& path) {
    // Safe only at login/logout boundaries when no repository code is
    // executing. QSqlDatabase::removeDatabase() is undefined if any
    // QSqlQuery objects still hold a reference to the connection; callers
    // must ensure all queries are finished before calling reopen().
    {
        QMutexLocker lock(&mutex_);
        close_locked();
        db_ = QSqlDatabase(); // release the handle before removing the connection
        if (QSqlDatabase::contains("finterm_main"))
            QSqlDatabase::removeDatabase("finterm_main");
    }
    return open(path);
}

bool Database::is_open() const {
    QMutexLocker lock(&mutex_); // db_ is swapped under this lock by open()/reopen()
    return db_.isOpen();
}

Result<QSqlQuery> Database::execute(const QString& sql, const QVariantList& params) {
    QMutexLocker lock(&mutex_);
    // Early-return before constructing a QSqlQuery against a closed db_.
    // Qt's QSqlQuery::prepare unconditionally logs "database not open" via
    // the qt.sql.qsqlquery category — that noise hides real SQL errors at
    // startup, where SettingsRepository legitimately probes for legacy-
    // migration state before AuthService opens the user DB. Caller code
    // already handles the error result.
    if (!db_.isOpen())
        return Result<QSqlQuery>::err("database not open");
    QSqlQuery query(db_);
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
    QMutexLocker lock(&mutex_);
    if (!db_.isOpen())
        return Result<void>::err("database not open");
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        return Result<void>::err(query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> Database::begin_transaction() {
    if (!db_.isOpen())
        return Result<void>::err("database not open");
    QSqlQuery q(db_);
    if (!q.exec("BEGIN IMMEDIATE")) {
        return Result<void>::err(q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> Database::commit() {
    if (!db_.isOpen())
        return Result<void>::err("database not open");
    QSqlQuery q(db_);
    if (!q.exec("COMMIT")) {
        return Result<void>::err(q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> Database::rollback() {
    if (!db_.isOpen())
        return Result<void>::err("database not open");
    QSqlQuery q(db_);
    if (!q.exec("ROLLBACK")) {
        return Result<void>::err(q.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

Result<void> Database::apply_pragmas() {
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
    for (auto* p : pragmas) {
        auto r = exec(p);
        if (r.is_err()) {
            LOG_WARN("DB", QString("PRAGMA failed: %1 — %2").arg(p, QString::fromStdString(r.error())));
        }
    }
    LOG_INFO("DB", "PRAGMAs applied (WAL, foreign_keys, cache_size=20MB, "
                   "wal_autocheckpoint=400, journal_size_limit=64MB)");
    return Result<void>::ok();
}

} // namespace finterm
