#pragma once
#include "core/result/Result.h"
#include "storage/sqlite/ThreadConnections.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

namespace finterm {

/// SQLite database wrapper with WAL mode, prepared statements, and transaction support.
/// Each thread works on its own connection to the file (ThreadConnections): a
/// QSqlQuery from execute() belongs to the calling thread, and so does a
/// transaction begun with begin_transaction().
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

    /// The calling thread's connection (invalid when the database is closed).
    /// Use it on this thread only.
    QSqlDatabase raw_db() { return conns_.connection(); }

    /// Returns the file path of the open database (empty if not open).
    QString path() const { return conns_.path(); }

  private:
    Database();
    static QString apply_pragmas(QSqlDatabase& db);

    ThreadConnections conns_;
};

} // namespace finterm
