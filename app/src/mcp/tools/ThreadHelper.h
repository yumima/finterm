#pragma once
// ThreadHelper.h — shared thread-marshaling helpers for MCP tool handlers.
//
// MCP tool handlers run on a QtConcurrent::run worker thread (off the main
// UI thread). Calling a Qt service that owns a QNetworkAccessManager (or
// any QObject parented on a different thread) directly from a worker is a
// Qt invariant violation: signals fire on the owning thread, child QObjects
// get the wrong parent thread, and the app crashes a few seconds later
// with `QObject: Cannot create children for a parent that is in a different
// thread` warnings. We've seen this pattern crash `get_news`, `refresh_news`,
// and is latent in 7+ other tool .cpp files (Edgar, Forum, Profile, MA,
// AltInvestments, PythonTools async runner, DataHub subscribe).
//
// The fix is always the same: post the work to the target's thread, sleep
// the worker on a wait condition, wake when the callback fires. These two
// helpers consolidate that pattern so each tool file doesn't re-implement
// it (poorly).

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QWaitCondition>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace finterm::mcp::tools::detail {

// ── run_on_target_thread_sync ────────────────────────────────────────────────
//
// Run `fn` on `target`'s thread and block the calling thread until it
// returns. If we're already on the target's thread we just call directly.
//
// Use this when the target call is itself synchronous on the main thread —
// e.g. `service->some_getter()` — and you want the result back on the
// worker without races.
// false = it was not run: the target is gone, or finterm is closing and nobody
// runs posted events any more. (A BlockingQueuedConnection would wait for ever there, and the exit waits
// for the pool this runs in.)
template <typename F>
bool run_on_target_thread_sync(QObject* target, F&& fn) {
    if (!target || QThread::currentThread() == target->thread()) {
        std::forward<F>(fn)();
        return true;
    }
    struct State {
        QMutex m;
        QWaitCondition cv;
        bool started = false, done = false, abandoned = false, dropped = false;
    };
    auto st = std::make_shared<State>();
    // With the event: run or not, its end is seen (a target that is deleted takes
    // its events with it).
    std::shared_ptr<void> event(nullptr, [st](void*) {
        QMutexLocker lock(&st->m);
        st->dropped = true;
        st->cv.wakeAll();
    });
    QMetaObject::invokeMethod(
        target,
        [st, &fn, event]() {
            {
                QMutexLocker lock(&st->m);
                if (st->abandoned)
                    return; // fn is gone
                st->started = true;
            }
            fn();
            QMutexLocker lock(&st->m);
            st->done = true;
            st->cv.wakeAll();
        },
        Qt::QueuedConnection);
    event.reset();
    QMutexLocker lock(&st->m);
    while (!st->done) {
        if (!st->dropped)
            st->cv.wait(&st->m, 200);
        if (!st->started && (st->dropped || QCoreApplication::closingDown())) {
            st->abandoned = true;
            return false;
        }
    }
    return true;
}

// ── run_async_wait ───────────────────────────────────────────────────────────
//
// Simpler, more useful variant. Caller writes the result into outer storage
// inside their start lambda (which captures by reference); the start lambda
// is invoked on the target's thread; the worker blocks until `signal_done`
// is called (from any thread).
//
//   QVector<NewsArticle> articles;
//   bool ok = false;
//   run_async_wait(svc, [&](auto signal_done) {
//       svc->fetch_all_news(force, [&, signal_done](bool s, auto a) {
//           ok = s;
//           articles = std::move(a);
//           signal_done();
//       });
//   });
//
// This is the pattern actually used by the existing fixed code in
// NewsTools::fetch_articles_sync. We just generalize it so each tool file
// doesn't re-roll its own QMutex/QWaitCondition.
template <typename Start>
void run_async_wait(QObject* target, Start&& start) {
    struct Wait {
        QMutex m;
        QWaitCondition cv;
        bool done = false;
    };
    auto w = std::make_shared<Wait>();

    auto signal_done = [w]() {
        QMutexLocker lock(&w->m);
        w->done = true;
        w->cv.wakeAll();
    };

    if (!target || QThread::currentThread() == target->thread()) {
        std::forward<Start>(start)(signal_done);
    } else {
        QMetaObject::invokeMethod(
            target, [start = std::forward<Start>(start), signal_done]() mutable { start(signal_done); },
            Qt::QueuedConnection);
    }

    QMutexLocker lock(&w->m);
    while (!w->done)
        w->cv.wait(&w->m);
}

} // namespace finterm::mcp::tools::detail
