// A tool's worker asking something of another thread: done there, or given up
// when nobody will do it.

#include "mcp/tools/ThreadHelper.h"

#include <QtConcurrent>
#include <QtTest/QtTest>

using finterm::mcp::tools::detail::run_on_target_thread_sync;

class TestThreadHelper : public QObject {
    Q_OBJECT

  private slots:
    void it_is_done_on_the_targets_thread() {
        QThread* where = nullptr;
        auto f = QtConcurrent::run([this, &where]() {
            return run_on_target_thread_sync(this, [&where]() { where = QThread::currentThread(); });
        });
        QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
        QVERIFY(f.result());
        QCOMPARE(where, QThread::currentThread());
    }

    void on_its_own_thread_at_once() {
        int n = 0;
        QVERIFY(run_on_target_thread_sync(this, [&n]() { ++n; }));
        QVERIFY(run_on_target_thread_sync(nullptr, [&n]() { ++n; }));
        QCOMPARE(n, 2);
    }

    void a_target_that_goes_away_is_not_waited_for() {
        auto* target = new QObject;
        bool ran = false;
        auto f = QtConcurrent::run([target, &ran]() { return run_on_target_thread_sync(target, [&ran]() { ran = true; }); });
        QTest::qSleep(300); // posted, and this thread runs no events meanwhile
        delete target;
        QVERIFY(QTest::qWaitFor([&f]() { return f.isFinished(); }, 5000));
        QVERIFY(!f.result());
        QVERIFY(!ran);
    }
};

// The case it exists for: the application ends with one waiting. It must end.
int main(int argc, char** argv) {
    int rc = 0;
    bool ran = false, answer = true;
    {
        QCoreApplication app(argc, argv);
        TestThreadHelper t;
        rc = QTest::qExec(&t, argc, argv);
        QObject* target = &app;
        (void)QtConcurrent::run([target, &ran, &answer]() {
            answer = run_on_target_thread_sync(target, [&ran]() { ran = true; });
        });
        QTest::qSleep(300);
    } // ~QCoreApplication waits for the pool (ctest's timeout, if it cannot end)
    if (ran || answer) {
        qCritical("the application ended, and the waiting call said it had been run");
        rc = 1;
    }
    return rc;
}

#include "test_thread_helper.moc"
