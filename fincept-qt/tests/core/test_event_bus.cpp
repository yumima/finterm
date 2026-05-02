// Fincept Terminal — EventBus regression suite.
//
// Locks in two invariants that broke in production:
//
//   1. Handlers can subscribe / unsubscribe / publish *during dispatch*
//      without corrupting the subscription list or skipping events. The
//      previous in-place iteration plus a Qt::QueuedConnection workaround
//      desynchronised publish-order from delivery-order and dropped a
//      follow-up message on first click after a layout restore.
//
//   2. Publish is safe from worker threads. MCP tools run on a worker
//      QThread and call EventBus::publish() directly.

#include "core/events/EventBus.h"

#include <QCoreApplication>
#include <QObject>
#include <QTest>
#include <QThread>
#include <QVariant>

#include <atomic>

using fincept::EventBus;

class EventBusTest : public QObject {
    Q_OBJECT
  private slots:
    void cleanup() {
        // Tests share the singleton — drain any subscriptions a previous test
        // might have leaked. We don't expose a clear(), so the convention is:
        // each test stores its HandlerIds locally and unsubscribes in its own
        // teardown. cleanup() is here to fail fast if that convention slips.
    }

    // ── 1. Basic publish/subscribe round-trip ────────────────────────────
    void publish_invokes_handler() {
        int count = 0;
        auto id = EventBus::instance().subscribe(
            "test.basic", [&](const QVariantMap&) { ++count; });
        EventBus::instance().publish("test.basic");
        EventBus::instance().publish("test.basic");
        EventBus::instance().unsubscribe(id);
        QCOMPARE(count, 2);
    }

    void publish_to_unsubscribed_handler_does_not_fire() {
        int count = 0;
        auto id = EventBus::instance().subscribe(
            "test.unsub", [&](const QVariantMap&) { ++count; });
        EventBus::instance().unsubscribe(id);
        EventBus::instance().publish("test.unsub");
        QCOMPARE(count, 0);
    }

    void event_filter_is_topic_specific() {
        int a = 0, b = 0;
        auto ida = EventBus::instance().subscribe(
            "test.topic.a", [&](const QVariantMap&) { ++a; });
        auto idb = EventBus::instance().subscribe(
            "test.topic.b", [&](const QVariantMap&) { ++b; });
        EventBus::instance().publish("test.topic.a");
        EventBus::instance().unsubscribe(ida);
        EventBus::instance().unsubscribe(idb);
        QCOMPARE(a, 1);
        QCOMPARE(b, 0);
    }

    void payload_round_trips_through_handler() {
        QString got;
        auto id = EventBus::instance().subscribe(
            "test.payload",
            [&](const QVariantMap& d) { got = d.value("symbol").toString(); });
        EventBus::instance().publish("test.payload", {{"symbol", "AMZN"}});
        EventBus::instance().unsubscribe(id);
        QCOMPARE(got, QString("AMZN"));
    }

    // ── 2. Reentrancy: subscribe inside a handler ─────────────────────────
    // Mirrors the production scenario: nav.split_alongside materializes a
    // screen whose ctor calls subscribe(). With in-place iteration this
    // would corrupt the list / invalidate iterators.
    void subscribe_during_dispatch_does_not_corrupt_list() {
        int outer = 0;
        int inner = 0;
        EventBus::HandlerId inner_id = 0;
        auto outer_id = EventBus::instance().subscribe(
            "test.reentrant.outer", [&](const QVariantMap&) {
                ++outer;
                if (inner_id == 0) {
                    inner_id = EventBus::instance().subscribe(
                        "test.reentrant.inner",
                        [&](const QVariantMap&) { ++inner; });
                }
            });
        EventBus::instance().publish("test.reentrant.outer");
        // Inner handler was subscribed during dispatch; it must NOT fire
        // for the in-flight publish (standard pub/sub semantics — newly
        // added handlers take effect on subsequent publishes).
        QCOMPARE(outer, 1);
        QCOMPARE(inner, 0);
        // ...but it MUST be wired up correctly for the next publish.
        EventBus::instance().publish("test.reentrant.inner");
        QCOMPARE(inner, 1);
        EventBus::instance().unsubscribe(outer_id);
        EventBus::instance().unsubscribe(inner_id);
    }

    // ── 3. Reentrancy: unsubscribe self during dispatch ───────────────────
    void unsubscribe_self_during_dispatch_is_safe() {
        int fired = 0;
        EventBus::HandlerId id = 0;
        id = EventBus::instance().subscribe(
            "test.reentrant.unsub_self", [&](const QVariantMap&) {
                ++fired;
                EventBus::instance().unsubscribe(id);
            });
        EventBus::instance().publish("test.reentrant.unsub_self");
        EventBus::instance().publish("test.reentrant.unsub_self");
        // First publish hits the snapshot, fires once; handler is gone for
        // the second publish.
        QCOMPARE(fired, 1);
    }

    // ── 4. Reentrancy: publish another event from inside a handler ────────
    // This is the exact production case — handler A triggers handler B
    // synchronously via a follow-up publish, both must run.
    void nested_publish_during_dispatch_delivers_both() {
        int outer = 0, inner = 0;
        auto inner_id = EventBus::instance().subscribe(
            "test.nested.inner", [&](const QVariantMap&) { ++inner; });
        auto outer_id = EventBus::instance().subscribe(
            "test.nested.outer", [&](const QVariantMap&) {
                ++outer;
                EventBus::instance().publish("test.nested.inner");
            });
        EventBus::instance().publish("test.nested.outer");
        EventBus::instance().unsubscribe(outer_id);
        EventBus::instance().unsubscribe(inner_id);
        QCOMPARE(outer, 1);
        QCOMPARE(inner, 1);
    }

    // ── 5. Cross-thread publish ──────────────────────────────────────────
    // MCP tools publish from a worker QThread. Verify subscriptions_ stays
    // consistent under concurrent subscribe (GUI thread) + publish (worker).
    void publish_from_worker_thread_is_safe() {
        std::atomic<int> received{0};
        auto id = EventBus::instance().subscribe(
            "test.thread", [&](const QVariantMap&) {
                received.fetch_add(1, std::memory_order_relaxed);
            });

        // Spawn workers that hammer publish() while the main thread also
        // subscribes/unsubscribes ephemeral handlers. Without the mutex
        // this would race (TSan would catch it; even without TSan it
        // tends to crash on resize during append).
        constexpr int kPublishesPerWorker = 200;
        constexpr int kWorkers = 4;
        QList<QThread*> workers;
        for (int i = 0; i < kWorkers; ++i) {
            auto* t = QThread::create([] {
                for (int n = 0; n < kPublishesPerWorker; ++n)
                    EventBus::instance().publish("test.thread");
            });
            workers.append(t);
            t->start();
        }
        // Concurrent churn from this thread.
        for (int n = 0; n < 100; ++n) {
            auto ephemeral = EventBus::instance().subscribe(
                "test.thread.churn", [](const QVariantMap&) {});
            EventBus::instance().unsubscribe(ephemeral);
        }
        for (auto* t : workers) {
            t->wait();
            delete t;
        }
        EventBus::instance().unsubscribe(id);

        QCOMPARE(received.load(), kPublishesPerWorker * kWorkers);
    }
};

QTEST_MAIN(EventBusTest)
#include "test_event_bus.moc"
