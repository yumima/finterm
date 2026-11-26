#pragma once
#include "core/result/Result.h"
#include "storage/sqlite/ThreadConnections.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

namespace finterm {

/// Separate SQLite database for ephemeral cache data.
/// Uses synchronous=NORMAL under WAL for safe, fast writes.
/// It is read/written from services whose callbacks may run on worker threads
/// (HttpClient, PythonRunner finished signals), so each thread works on its
/// own connection (ThreadConnections): a QSqlQuery from execute() belongs to
/// the calling thread.
class CacheDatabase {
  public:
    static CacheDatabase& instance();

    Result<void> open(const QString& path);
    void close();
    bool is_open() const;

    Result<QSqlQuery> execute(const QString& sql, const QVariantList& params = {});
    Result<void> exec(const QString& sql);

    /// The calling thread's connection (invalid when closed). This thread only.
    QSqlDatabase raw_db() { return conns_.connection(); }

  private:
    CacheDatabase();
    static QString apply_pragmas(QSqlDatabase& db);
    Result<void> create_tables();

    ThreadConnections conns_;
};

} // namespace finterm
