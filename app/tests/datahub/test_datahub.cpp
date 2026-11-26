// finterm — DataHub Phase 1 unit tests
//
// Validates the core pub/sub primitive without any producers or
// app-wide plumbing. Uses Qt Test.

#include "datahub/DataHub.h"
#include "datahub/Producer.h"

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QVariant>

#include <atomic>

using finterm::datahub::DataHub;
using finterm::datahub::Producer;
using finterm::datahub::TopicPolicy;

namespace {

/// Minimal producer that records each `refresh()` call it receives and
/// lets the test drive what it publishes back.
class RecordingProducer : public Producer {
  public:
    explicit RecordingProducer(QStringList patterns, int rps = 0)
        : patterns_(std::move(patterns)), rps_(rps) {}

    QStringList topic_patterns() const override { return patterns_; }
    int max_requests_per_sec() const override { return rps_; }

    void refresh(const QStringList& topics) override {
        ++refresh_count;
        last_refresh_topics = topics;
        for (const auto& t : topics)
            all_requested_topics.append(t);
    }

    int refresh_count = 0;
    QStringList last_refresh_topics;
    QStringList all_requested_topics;

  private:
    QStringList patterns_;
    int rps_;
};

} // namespace

class TestDataHub : public QObject {
    Q_OBJECT

  private slots:

    // Drain any queued invocations between tests so timer fires from one
    // test don't leak into the next. DataHub is a singleton so state is
    // shared across the whole run; we reset per-test side-effects here.
    void cleanup() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }

    // Core flow — subscribe, publish, slot invoked.
    void subscribe_publish_delivers() {
        auto& hub = DataHub::instance();
        QObject owner;
        QVariant received;
        hub.subscribe(&owner, "test:basic:1",
                      [&](const QVariant& v) { received = v; });

        hub.publish("test:basic:1", QVariant(42));
        QTRY_COMPARE(received.toInt(), 42);
    }

    // Subscriber receives cached value immediately if one exists + is fresh.
    void subscribe_receives_initial_value() {
        auto& hub = DataHub::instance();

        // Seed a value first, then subscribe.
        hub.publish("test:initial:1", QVariant(QString("hello")));

        QObject owner;
        QVariant received;
        hub.subscribe(&owner, "test:initial:1",
                      [&](const QVariant& v) { received = v; });

        // Initial delivery is queued — pump the event loop.
        QTRY_COMPARE(received.toString(), QString("hello"));
    }

    // Destroying the owner auto-unsubscribes.
    void destroyed_owner_auto_unsubscribes() {
        auto& hub = DataHub::instance();
        int call_count = 0;

        {
            QObject owner;
            hub.subscribe(&owner, "test:destroy:1",
                          [&](const QVariant&) { ++call_count; });
            hub.publish("test:destroy:1", QVariant(1));
            QTRY_COMPARE(call_count, 1);
        } // owner destroyed here

        // Give the queued destroyed() → on_owner_destroyed a chance to run.
        // processEvents once may only deliver the first queued item; run a
        // couple to drain all cascaded queued invocations.
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        hub.publish("test:destroy:1", QVariant(2));
        QCoreApplication::processEvents();

        // call_count must remain 1 — subscriber is gone.
        QCOMPARE(call_count, 1);

        // Subscriber count for this topic should have dropped to 0.
        // After unsubscribe the subscriptions_ bucket is erased, so stats()
        // reports the default zero (via missing bucket).
        const auto stats = hub.stats();
        for (const auto& s : stats)
            if (s.topic == "test:destroy:1")
                QCOMPARE(s.subscriber_count, 0);
    }

    // Wildcard subscription receives every matching topic.
    void wildcard_pattern_matches() {
        auto& hub = DataHub::instance();
        QObject owner;
        QStringList seen;
        hub.subscribe_pattern(&owner, "test:wild:*",
            [&](const QString& t, const QVariant&) { seen.append(t); });

        hub.publish("test:wild:alpha", QVariant(1));
        hub.publish("test:wild:beta",  QVariant(2));
        hub.publish("test:other:gamma", QVariant(3));

        QCoreApplication::processEvents();
        QCOMPARE(seen.size(), 2);
        QVERIFY(seen.contains("test:wild:alpha"));
        QVERIFY(seen.contains("test:wild:beta"));
        QVERIFY(!seen.contains("test:other:gamma"));
    }

    // The no-change skip is for subscribers that show the value. One that
    // joined while it was stale (no initial delivery) gets the refresh even
    // when it republishes the same value — else it waits forever.
    void unchanged_republish_reaches_a_late_subscriber() {
        auto& hub = DataHub::instance();
        const QString topic = "test:unchanged:1";
        TopicPolicy p;
        p.ttl_ms = 1;
        hub.set_policy(topic, p);

        QObject early;
        int early_calls = 0;
        hub.subscribe(&early, topic, [&](const QVariant&) { ++early_calls; });
        hub.publish(topic, QVariant(7));
        QTRY_COMPARE(early_calls, 1);

        QTest::qWait(5); // the value goes stale: a new subscriber gets no initial delivery
        QObject late;
        QVariant late_got;
        hub.subscribe(&late, topic, [&](const QVariant& v) { late_got = v; });
        QCoreApplication::processEvents();
        QVERIFY(!late_got.isValid());

        hub.publish(topic, QVariant(7)); // the refresh: same value
        QCOMPARE(late_got.toInt(), 7);
        QCOMPARE(early_calls, 1); // already shows 7: still skipped

        late_got = QVariant();
        hub.publish(topic, QVariant(7)); // primed now: skipped for both
        QVERIFY(!late_got.isValid());
        QCOMPARE(early_calls, 1);
    }

    // A forced request is a caller waiting for the answer: a subscriber that
    // set aside the value it was handed (a restored quote) gets the refresh
    // even when it repeats that value.
    void forced_request_answer_reaches_subscribers_holding_that_value() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:forced:*"});
        hub.register_producer(&prod);
        const QString topic = "test:forced:1";
        hub.publish(topic, QVariant(5)); // fresh: handed to the subscriber below

        QObject owner;
        int calls = 0;
        hub.subscribe(&owner, topic, [&](const QVariant&) { ++calls; });
        QTRY_COMPARE(calls, 1);

        hub.publish(topic, QVariant(5)); // unchanged, nobody asked: skipped
        QCOMPARE(calls, 1);

        hub.request(topic, /*force=*/true);
        hub.publish(topic, QVariant(5)); // the forced refresh's answer, same value
        QCOMPARE(calls, 2);
        hub.publish(topic, QVariant(5)); // and only that one
        QCOMPARE(calls, 2);
        hub.unregister_producer(&prod);
    }

    // One forced request for a list asks its producer for all of it in one
    // refresh — the rate limit gates the call, not each topic after the first.
    void forced_list_request_is_one_refresh_of_every_topic() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:list:*"}, /*rps=*/2);
        hub.register_producer(&prod);
        const QStringList topics{"test:list:a", "test:list:b", "test:list:c"};
        hub.request(topics, /*force=*/true);
        QTRY_COMPARE(prod.refresh_count, 1);
        QCOMPARE(QSet<QString>(prod.last_refresh_topics.begin(), prod.last_refresh_topics.end()),
                 QSet<QString>(topics.begin(), topics.end()));
        hub.unregister_producer(&prod);
    }

    // A forced ask the rate limit turned away is not dropped: the next
    // scheduler pass sends it even though the topic's value is inside TTL.
    void rate_limited_forced_request_is_sent_by_the_scheduler() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:gated:*"}, /*rps=*/10);
        hub.register_producer(&prod);
        QObject owner;
        hub.publish("test:gated:b", QVariant(1)); // fresh: no cold-start fetch
        hub.subscribe(&owner, "test:gated:b", [](const QVariant&) {});
        hub.request("test:gated:a", /*force=*/true); // opens and closes the 100 ms gate
        hub.request("test:gated:b", /*force=*/true); // turned away
        QTRY_COMPARE(prod.refresh_count, 1);
        QCOMPARE(prod.last_refresh_topics, QStringList{"test:gated:a"});
        QTest::qWait(150);
        hub.tick_scheduler();
        QTRY_VERIFY(prod.all_requested_topics.contains("test:gated:b"));
        hub.unregister_producer(&prod);
    }

    // A subscriber is primed exactly when it is handed the value: at the
    // TTL boundary it must never be left primed with nothing delivered (it
    // would then miss an unchanged refresh for good).
    void primed_iff_initial_value_delivered() {
        auto& hub = DataHub::instance();
        const QString topic = "test:boundary:1";
        TopicPolicy p;
        p.ttl_ms = 1;
        hub.set_policy(topic, p);
        for (int i = 0; i < 300; ++i) {
            hub.publish(topic, QVariant(9));
            QObject owner;
            int got = 0;
            hub.subscribe(&owner, topic, [&](const QVariant&) { ++got; });
            QCoreApplication::processEvents(); // the initial delivery, if any
            hub.publish(topic, QVariant(9));   // unchanged refresh
            // The initial delivery is a queued event: one processEvents() pass
            // does not always drain it on macOS's event loop (CI, 2026-11-26,
            // iteration 71 of 300), so wait for it — a primed subscriber that
            // is never handed the value still fails here.
            QTRY_VERIFY2_WITH_TIMEOUT(got >= 1, qPrintable(QStringLiteral("iteration %1: nothing delivered").arg(i)),
                                      200);
            hub.unsubscribe(&owner);
        }
    }

    // Scheduler skips push-only topics.
    void push_only_skips_scheduler() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:push:*"});
        hub.register_producer(&prod);

        TopicPolicy p;
        p.push_only = true;
        hub.set_policy_pattern("test:push:*", p);

        QObject owner;
        hub.subscribe(&owner, "test:push:live",
                      [](const QVariant&) {});

        // Drive the scheduler a handful of times — it must not call refresh()
        // on push-only topics.
        for (int i = 0; i < 5; ++i) hub.tick_scheduler();

        QCOMPARE(prod.refresh_count, 0);
        hub.unregister_producer(&prod);
    }

    // request() respects in-flight dedup: two back-to-back requests for
    // the same topic trigger only one refresh().
    void request_dedups_in_flight() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:flight:*"});
        hub.register_producer(&prod);

        // Disable coalesce window for deterministic synchronous testing —
        // request() then dispatches on the next event-loop iteration instead
        // of waiting ~100ms. Restored at end of test.
        const int saved_coalesce = hub.coalesce_window_ms();
        hub.set_coalesce_window_ms(0);

        // Pin an explicit policy. TopicPolicy defaults to min_interval_ms
        // = 5000, which would gate the post-publish request() below on the
        // interval rather than on in-flight — the test would then fail while
        // the dedup logic it is actually checking works fine. The long TTL
        // keeps the 1 s scheduler tick from injecting refreshes of its own
        // while QTRY_COMPARE spins an event loop.
        hub.clear_policy_pattern("test:flight:*");
        TopicPolicy p;
        p.min_interval_ms = 0;
        p.ttl_ms = 60'000;
        hub.set_policy_pattern("test:flight:*", p);

        QObject owner;
        hub.subscribe(&owner, "test:flight:1",
                      [](const QVariant&) {});

        hub.request("test:flight:1");
        hub.request("test:flight:1");  // should be deduped — already in-flight

        // Let the zero-ms coalesce timer fire once.
        QTRY_COMPARE(prod.refresh_count, 1);

        // Once the producer publishes, the in-flight flag clears and the
        // next request() should go through again.
        hub.publish("test:flight:1", QVariant(1));
        QCoreApplication::processEvents();

        hub.request("test:flight:1");
        QTRY_COMPARE(prod.refresh_count, 2);

        hub.unregister_producer(&prod);
        hub.set_coalesce_window_ms(saved_coalesce);
        hub.clear_policy_pattern("test:flight:*");
    }

    // Cross-thread publish() must marshal to the hub thread safely.
    void cross_thread_publish_marshals() {
        auto& hub = DataHub::instance();
        QObject owner;
        std::atomic<int> received{0};
        hub.subscribe(&owner, "test:xthread:1",
            [&](const QVariant& v) { received = v.toInt(); });

        // Publish from a worker thread.
        QThread* worker = QThread::create([&] {
            hub.publish("test:xthread:1", QVariant(77));
        });
        worker->start();
        worker->wait();

        QTRY_COMPARE(received.load(), 77);
        worker->deleteLater();
    }

    // peek() reads without subscribing and returns the last published value.
    void peek_reads_without_subscribing() {
        auto& hub = DataHub::instance();
        QCOMPARE(hub.peek("test:peek:unset").isValid(), false);
        hub.publish("test:peek:set", QVariant(QString("seeded")));
        QCoreApplication::processEvents();
        QCOMPARE(hub.peek("test:peek:set").toString(), QString("seeded"));
    }

    // Scheduler respects ttl_ms (does not refresh inside the TTL window).
    void scheduler_respects_ttl() {
        auto& hub = DataHub::instance();
        RecordingProducer prod({"test:ttl:*"});
        hub.register_producer(&prod);

        // Clear any stale pattern policy from prior tests + use sync coalesce.
        hub.clear_policy_pattern("test:ttl:*");
        const int saved_coalesce = hub.coalesce_window_ms();
        hub.set_coalesce_window_ms(0);

        TopicPolicy p;
        p.ttl_ms = 60'000;
        p.min_interval_ms = 0;
        hub.set_policy_pattern("test:ttl:*", p);

        QObject owner;
        hub.subscribe(&owner, "test:ttl:1", [](const QVariant&) {});

        // subscribe() auto cold-starts a forced request — give the coalesce
        // flush a chance to run and drive refresh_count to 1.
        QTRY_COMPARE(prod.refresh_count, 1);

        // Producer publishes; TTL is 60 s so next tick should skip.
        hub.publish("test:ttl:1", QVariant(1));
        QCoreApplication::processEvents();

        hub.tick_scheduler();
        QCOMPARE(prod.refresh_count, 1);  // unchanged

        hub.unregister_producer(&prod);
        hub.set_coalesce_window_ms(saved_coalesce);
    }

    // peek() returns invalid when value is past TTL; peek_raw() returns anyway.
    void peek_respects_ttl() {
        auto& hub = DataHub::instance();

        TopicPolicy p;
        p.ttl_ms = 1;  // effectively immediate expiry
        p.min_interval_ms = 0;
        hub.clear_policy_pattern(QStringLiteral("test:peek:*"));  // idempotent if absent
        hub.set_policy_pattern(QStringLiteral("test:peek:*"), p);

        hub.publish(QStringLiteral("test:peek:ttl:1"), QVariant(QString("stale")));
        QCoreApplication::processEvents();

        QVERIFY(hub.peek_raw(QStringLiteral("test:peek:ttl:1")).isValid());
        QTest::qWait(5);  // exceed ttl
        QVERIFY(!hub.peek(QStringLiteral("test:peek:ttl:1")).isValid());   // stale → invalid
        QVERIFY(hub.peek_raw(QStringLiteral("test:peek:ttl:1")).isValid()); // raw still there
    }

    // publish_live(): a stream tick refining a POLLED value. Delivered and
    // cached like publish(), but freshness is untouched — a topic kept moving by
    // ticks must still go stale on its TTL, or its poll would never run again.
    void publish_live_delivers_without_refreshing() {
        auto& hub = DataHub::instance();
        TopicPolicy p;
        p.ttl_ms = 40;
        p.min_interval_ms = 0;
        hub.clear_policy_pattern(QStringLiteral("test:live:*"));
        hub.set_policy_pattern(QStringLiteral("test:live:*"), p);

        QObject owner;
        QVariant got;
        hub.subscribe(&owner, QStringLiteral("test:live:1"), [&](const QVariant& v) { got = v; });

        // Nothing polled yet: a live refinement has nothing to refine — dropped.
        hub.publish_live(QStringLiteral("test:live:1"), QVariant(1));
        QCoreApplication::processEvents();
        QVERIFY(!got.isValid());
        QVERIFY(!hub.peek_raw(QStringLiteral("test:live:1")).isValid());

        hub.publish(QStringLiteral("test:live:1"), QVariant(10)); // the poll
        QTRY_COMPARE(got.toInt(), 10);
        QTest::qWait(25);
        hub.publish_live(QStringLiteral("test:live:1"), QVariant(11)); // a tick
        QTRY_COMPARE(got.toInt(), 11);
        QCOMPARE(hub.peek_raw(QStringLiteral("test:live:1")).toInt(), 11);
        // 25 + 25 ms after the POLL: stale on the poll's clock, tick or not.
        QTest::qWait(25);
        QVERIFY(!hub.peek(QStringLiteral("test:live:1")).isValid());
    }
};

QTEST_MAIN(TestDataHub)
#include "test_datahub.moc"
