#pragma once

#include <QMutex>
#include <QSqlDatabase>
#include <QString>

#include <functional>

namespace finterm {

/// One SQLite connection per thread, all on the same file.
///
/// Qt's rule is that a QSqlDatabase connection may only be used from the
/// thread that created it, and Qt's SQLite driver opens every connection with
/// SQLITE_OPEN_NOMUTEX (qsql_sqlite.cpp), so SQLite itself does no locking on
/// it. A single connection shared across threads with a mutex around exec() is
/// not enough: the QSqlQuery a caller gets back steps, reads columns and is
/// finalized after the lock is gone, and two threads doing that at once corrupt
/// the connection's allocator (the Windows crash in sqlite3DbMallocRawNN under
/// sqlite3DbStrNDup, 2026-11-26).
///
/// Here every thread gets its own connection, opened on first use. Under WAL
/// readers on different connections run concurrently and writers wait on
/// busy_timeout. A transaction belongs to the thread that began it.
///
/// set_path() moves every thread to a new file (or closes with an empty
/// path): it bumps a generation. A thread with an event loop drops its old
/// connection as soon as it returns to that loop; any thread drops it on its
/// next use; a thread without a loop that never uses the database again keeps
/// the old handle until it exits (QThreadPool threads expire after 30 s idle).
/// A statement another thread is already running when set_path() is called
/// still completes on the old file — switch files at login/logout only.
class ThreadConnections {
  public:
    /// `prefix` names the connections (prefix_N); `configure` runs on every
    /// new connection (per-connection PRAGMAs) and returns an error text or "".
    ThreadConnections(QString prefix, std::function<QString(QSqlDatabase&)> configure);

    /// Point every thread at `path`; empty closes. Opens the calling thread's
    /// connection at once and returns its error, or "" on success.
    QString set_path(const QString& path);
    QString path() const;

    /// The calling thread's connection, opened on demand. Invalid (not open)
    /// when no path is set or the open failed; `error` then says why.
    QSqlDatabase connection(QString* error = nullptr);

  private:
    void drop_current_thread_slot();
    void drop_if_stale(); ///< on the calling thread, if its connection is for an older generation

    const QString prefix_;
    const std::function<QString(QSqlDatabase&)> configure_;
    mutable QMutex mutex_; ///< guards path_ / generation_
    QString path_;
    quint64 generation_ = 0;
};

} // namespace finterm
