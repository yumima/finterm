#include "storage/sqlite/ThreadConnections.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutexLocker>
#include <QSqlError>
#include <QSet>
#include <QThread>
#include <QThreadStorage>

#include <atomic>

namespace finterm {

namespace {

std::atomic<quint64> g_next_id{0};

void remove_connection(const QString& name) {
    if (name.isEmpty() || !QSqlDatabase::contains(name))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(name, false);
        if (db.isOpen())
            db.close();
    } // the handle must be gone before removeDatabase
    QSqlDatabase::removeDatabase(name);
}

/// The calling thread's connections, one per ThreadConnections instance
/// (keyed by its prefix). Deleted by QThreadStorage when the thread finishes,
/// on that thread, which is where Qt wants a connection closed.
struct ThreadSlots {
    struct Entry {
        QString name;
        quint64 generation = 0;
    };
    QHash<QString, Entry> by_prefix;
    /// Lives on this thread when it runs an event loop: set_path() posts the
    /// drop of a stale connection to it, so an idle thread does not keep the
    /// previous file open (Windows then refuses to rename/delete it).
    QObject* reaper = nullptr;

    ~ThreadSlots();
};

QMutex g_reapers_mutex;
QSet<QObject*> g_reapers; ///< every live ThreadSlots::reaper

ThreadSlots::~ThreadSlots() {
    if (reaper) {
        {
            QMutexLocker lock(&g_reapers_mutex);
            g_reapers.remove(reaper);
        }
        delete reaper;
    }
    // At process exit (main thread, or pool threads joined by static
    // destructors) Qt's connection registry may already be gone; the OS
    // closes the files.
    if (!QCoreApplication::instance())
        return;
    for (const Entry& e : std::as_const(by_prefix))
        remove_connection(e.name);
}

QThreadStorage<ThreadSlots*>& thread_slots() {
    static QThreadStorage<ThreadSlots*> storage;
    return storage;
}

ThreadSlots& current_slots() {
    auto& storage = thread_slots();
    if (!storage.hasLocalData()) {
        auto* fresh = new ThreadSlots;
        // A thread without an event loop (QThreadPool worker, std::thread)
        // cannot take a posted call; it drops a stale connection on its next
        // use, and a pool thread exits after 30 s idle.
        if (QThread::currentThread()->eventDispatcher()) {
            fresh->reaper = new QObject;
            QMutexLocker lock(&g_reapers_mutex);
            g_reapers.insert(fresh->reaper);
        }
        storage.setLocalData(fresh);
    }
    return *storage.localData();
}

} // namespace

ThreadConnections::ThreadConnections(QString prefix, std::function<QString(QSqlDatabase&)> configure)
    : prefix_(std::move(prefix)), configure_(std::move(configure)) {}

QString ThreadConnections::set_path(const QString& path) {
    {
        QMutexLocker lock(&mutex_);
        path_ = path;
        ++generation_;
    }
    drop_current_thread_slot();
    {
        // Every other thread with an event loop drops its connection to the
        // old file as soon as it is idle (the posted call runs between events,
        // never inside a statement of that thread).
        QMutexLocker lock(&g_reapers_mutex);
        for (QObject* r : std::as_const(g_reapers)) {
            if (r->thread() == QThread::currentThread())
                continue;
            QMetaObject::invokeMethod(r, [this] { drop_if_stale(); }, Qt::QueuedConnection);
        }
    }
    if (path.isEmpty())
        return {};
    QString error;
    connection(&error);
    return error;
}

QString ThreadConnections::path() const {
    QMutexLocker lock(&mutex_);
    return path_;
}

void ThreadConnections::drop_if_stale() {
    quint64 generation = 0;
    {
        QMutexLocker lock(&mutex_);
        generation = generation_;
    }
    auto& mine = current_slots();
    auto it = mine.by_prefix.find(prefix_);
    if (it != mine.by_prefix.end() && it->generation != generation)
        drop_current_thread_slot();
}

void ThreadConnections::drop_current_thread_slot() {
    auto& mine = current_slots();
    auto it = mine.by_prefix.find(prefix_);
    if (it == mine.by_prefix.end())
        return;
    const QString name = it->name;
    mine.by_prefix.erase(it);
    remove_connection(name);
}

QSqlDatabase ThreadConnections::connection(QString* error) {
    QString path;
    quint64 generation = 0;
    {
        QMutexLocker lock(&mutex_);
        path = path_;
        generation = generation_;
    }
    auto& mine = current_slots();
    auto it = mine.by_prefix.find(prefix_);
    if (it != mine.by_prefix.end()) {
        if (it->generation == generation) {
            QSqlDatabase db = QSqlDatabase::database(it->name, false);
            if (db.isOpen())
                return db;
        }
        drop_current_thread_slot(); // another file now, or it was closed
    }
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("database not open");
        return {};
    }

    const QString name = QStringLiteral("%1_%2").arg(prefix_).arg(g_next_id.fetch_add(1));
    QString failure;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) {
            failure = db.lastError().text();
            if (failure.isEmpty())
                failure = QStringLiteral("cannot open ") + path;
        } else if (configure_) {
            failure = configure_(db);
        }
    }
    if (!failure.isEmpty()) {
        remove_connection(name);
        if (error)
            *error = failure;
        return {};
    }
    mine.by_prefix.insert(prefix_, {name, generation});
    return QSqlDatabase::database(name, false);
}

} // namespace finterm
