// tests/storage/test_db_threads.cpp
//
// Database and CacheDatabase are used from worker threads. Qt opens SQLite
// connections with SQLITE_OPEN_NOMUTEX, so one connection shared across threads
// (with a mutex only around exec) corrupted the connection's allocator: the
// QSqlQuery execute() hands back steps and finalizes after the lock is gone.
// The Windows build crashed in qsqlite.dll (sqlite3DbMallocRawNN) on
// 2026-11-26. Each thread now has its own connection; these pin that.

#include "storage/sqlite/CacheDatabase.h"
#include "storage/sqlite/Database.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <vector>

using finterm::CacheDatabase;
using finterm::Database;

class TestDbThreads : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void each_thread_has_its_own_connection();
    void threads_read_and_write_at_once();
    void a_transaction_belongs_to_its_thread();
    void reopen_moves_every_thread();

  private:
    QTemporaryDir dir_;
};

void TestDbThreads::initTestCase() {
    QVERIFY(dir_.isValid());
    auto r = Database::instance().open(dir_.filePath("main.db"));
    QVERIFY2(r.is_ok(), r.is_err() ? r.error().c_str() : "");
    QVERIFY(Database::instance().exec("CREATE TABLE t (k INTEGER, v TEXT)").is_ok());
    auto c = CacheDatabase::instance().open(dir_.filePath("cache.db"));
    QVERIFY2(c.is_ok(), c.is_err() ? c.error().c_str() : "");
}

void TestDbThreads::each_thread_has_its_own_connection() {
    const QString here = Database::instance().raw_db().connectionName();
    QString there;
    QString there_path;
    QThread* t = QThread::create([&] {
        there = Database::instance().raw_db().connectionName();
        there_path = Database::instance().raw_db().databaseName();
    });
    t->start();
    QVERIFY(t->wait(10000));
    delete t;
    QVERIFY(!here.isEmpty());
    QVERIFY(!there.isEmpty());
    QVERIFY(here != there);
    QCOMPARE(there_path, Database::instance().path());
    // The finished thread's connection is gone with it.
    QVERIFY(!QSqlDatabase::contains(there));
}

void TestDbThreads::threads_read_and_write_at_once() {
    // Exactly the crashing shape: execute() on several threads, each stepping
    // its QSqlQuery after execute() returned, while others write.
    constexpr int kThreads = 8, kRounds = 200;
    std::atomic<int> failures{0}, rows_seen{0};
    std::vector<QThread*> threads;
    for (int n = 0; n < kThreads; ++n) {
        threads.push_back(QThread::create([n, &failures, &rows_seen] {
            for (int i = 0; i < kRounds; ++i) {
                auto w = Database::instance().execute("INSERT INTO t (k, v) VALUES (?, ?)",
                                                      {n * kRounds + i, QString("row %1").arg(i)});
                if (w.is_err())
                    ++failures;
                auto r = Database::instance().execute("SELECT k, v FROM t ORDER BY k DESC LIMIT 20");
                if (r.is_err()) {
                    ++failures;
                    continue;
                }
                while (r.value().next())
                    if (!r.value().value(1).toString().startsWith("row "))
                        ++failures;
                    else
                        ++rows_seen;
                auto c = CacheDatabase::instance().execute(
                    "INSERT OR REPLACE INTO unified_cache (key, value, expires_at) "
                    "VALUES (?, 'x', datetime('now', '+1 hour'))",
                    {QString("k%1").arg(n)});
                if (c.is_err())
                    ++failures;
            }
        }));
    }
    for (auto* t : threads)
        t->start();
    for (auto* t : threads) {
        QVERIFY(t->wait(60000));
        delete t;
    }
    QCOMPARE(failures.load(), 0);
    QVERIFY(rows_seen.load() > 0);
    auto count = Database::instance().execute("SELECT COUNT(*) FROM t");
    QVERIFY(count.is_ok() && count.value().next());
    QCOMPARE(count.value().value(0).toInt(), kThreads * kRounds);
}

void TestDbThreads::a_transaction_belongs_to_its_thread() {
    // A row written by another thread while this one holds a transaction is
    // not rolled back with it (on a shared connection it was).
    QVERIFY(Database::instance().exec("DELETE FROM t").is_ok());
    QVERIFY(Database::instance().begin_transaction().is_ok());
    QVERIFY(Database::instance().execute("INSERT INTO t (k, v) VALUES (1, 'mine')").is_ok());
    std::atomic<bool> started{false}, done{false};
    bool other_ok = false;
    QThread* t = QThread::create([&] {
        started = true;
        // Waits on busy_timeout until this thread's transaction ends.
        other_ok = Database::instance().execute("INSERT INTO t (k, v) VALUES (2, 'theirs')").is_ok();
        done = true;
    });
    t->start();
    QTRY_VERIFY_WITH_TIMEOUT(started.load(), 5000);
    QTest::qWait(300);
    QVERIFY(!done.load()); // it is waiting on this thread's transaction, not inside it
    QVERIFY(Database::instance().rollback().is_ok());
    QVERIFY(t->wait(10000));
    delete t;
    QVERIFY(other_ok);
    auto r = Database::instance().execute("SELECT v FROM t ORDER BY k");
    QVERIFY(r.is_ok());
    QStringList got;
    while (r.value().next())
        got << r.value().value(0).toString();
    QCOMPARE(got, QStringList{"theirs"});
}

void TestDbThreads::reopen_moves_every_thread() {
    // A long-lived worker opened on the first file follows a reopen.
    QThread worker;
    worker.start();
    QObject ctx;
    ctx.moveToThread(&worker);
    auto path_on_worker = [&ctx] {
        QString p;
        QMetaObject::invokeMethod(&ctx, [&p] { p = Database::instance().raw_db().databaseName(); },
                                  Qt::BlockingQueuedConnection);
        return p;
    };
    QCOMPARE(path_on_worker(), dir_.filePath("main.db"));
    QString name_on_worker;
    QMetaObject::invokeMethod(&ctx, [&] { name_on_worker = Database::instance().raw_db().connectionName(); },
                              Qt::BlockingQueuedConnection);
    QVERIFY(QSqlDatabase::contains(name_on_worker));
    QVERIFY(Database::instance().reopen(dir_.filePath("other.db")).is_ok());
    // The idle worker lets go of main.db without touching the database again
    // (on Windows an open handle blocks renaming/deleting the file).
    QTRY_VERIFY_WITH_TIMEOUT(!QSqlDatabase::contains(name_on_worker), 5000);
    QCOMPARE(path_on_worker(), dir_.filePath("other.db"));
    Database::instance().close();
    QCOMPARE(path_on_worker(), QString());
    QVERIFY(!Database::instance().is_open());
    QVERIFY(Database::instance().execute("SELECT 1").is_err());
    worker.quit();
    worker.wait();
}

QTEST_MAIN(TestDbThreads)
#include "test_db_threads.moc"
