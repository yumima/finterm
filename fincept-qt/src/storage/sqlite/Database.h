#pragma once
#include "core/result/Result.h"

#include <QMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

namespace fincept {

/// SQLite database wrapper with WAL mode, prepared statements, and transaction support.
class Database {
  public:
    static Database& instance();

    Result<void> open(const QString& path);
    /// Close the current connection and reopen at a new path, running
    /// migrations on the new database. Safe to call while logged out.
    Result<void> reopen(const QString& path);
    void close();
    bool is_open() const;

    Result<QSqlQuery> execute(const QString& sql, const QVariantList& params = {});
    Result<void> exec(const QString& sql);

    // Transaction support
    Result<void> begin_transaction();
    Result<void> commit();
    Result<void> rollback();

    QSqlDatabase& raw_db() { return db_; }

    /// Returns the file path of the open database (empty if not open).
    QString path() const { return db_.databaseName(); }

  private:
    Database() = default;
    Result<void> apply_pragmas();

    QSqlDatabase db_;
    // Serialises access to db_ across threads.
    // Qt's rule: a QSqlDatabase connection may only be used from the thread
    // that created it. In practice the SQLite driver is compiled with
    // SQLITE_THREADSAFE=1 (serialised mode), so the C-level access is safe,
    // but the Qt C++ wrappers (QSqlQuery, QSqlDatabase) are not. This mutex
    // ensures we never touch db_ concurrently from two threads.
    mutable QMutex mutex_;
};

} // namespace fincept
