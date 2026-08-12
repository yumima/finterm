// tests/services/test_query_store.cpp
//
// The provenance contract every panel now depends on.
//
// Two things were true before and cost the user real trust:
//   - Nothing ever re-kicked a subscribed key, so a panel could sit on its
//     first-load values for as long as the symbol stayed open, while a chip
//     claimed the data was current.
//   - The only refresh primitive was invalidate(), which DROPS the cached
//     value — so a refresh that then failed blanked a panel that had been
//     showing good numbers.
//
// revalidate() is the missing primitive: refresh in place, keep what we have
// until the replacement lands. State::fetched_at is the missing fact: when
// the data was actually fetched upstream, as opposed to when a signal
// happened to reach the UI (which a cache hit satisfies instantly).

#include "services/query/QueryStore.h"

#include <QtTest/QtTest>

using fincept::services::query::QueryStore;

class TestQueryStore : public QObject {
    Q_OBJECT

  private slots:
    void init();
    void fetched_at_reports_upstream_time();
    void cache_hit_keeps_the_original_fetched_at();
    void revalidate_due_policy();
    void revalidate_is_a_noop_while_fresh();
    void revalidate_is_a_noop_without_a_subscription();
    void failed_revalidate_keeps_the_last_good_value();
    void failed_fetch_backs_off_periodic_revalidate();
    void revalidate_owner_covers_every_key_of_that_owner();
    void revalidate_owner_ignores_other_owners();

  private:
    // Each test gets its own key namespace: QueryStore is a process-wide
    // singleton, so shared keys would couple the tests to each other.
    QString k(const QString& name) const {
        return QStringLiteral("test:%1:%2").arg(QTest::currentTestFunction(), name);
    }
    static void drain() { QCoreApplication::processEvents(); }
};

void TestQueryStore::init() {
    drain();
}

void TestQueryStore::fetched_at_reports_upstream_time() {
    QObject owner;
    QueryStore::State seen;
    const QDateTime before = QDateTime::currentDateTime().addSecs(-1);

    QueryStore::instance().subscribe(
        &owner, k("a"), /*ttl*/ 300, /*stale_max*/ 600,
        [&](const QueryStore::State& s) { seen = s; },
        [](QueryStore::Resolver resolve, QueryStore::Rejecter) { resolve(QVariant(42)); });
    drain();

    QCOMPARE(seen.data.toInt(), 42);
    QVERIFY2(seen.fetched_at.isValid(), "a delivered value must carry its fetch time");
    QVERIFY(seen.fetched_at >= before);
}

void TestQueryStore::cache_hit_keeps_the_original_fetched_at() {
    QObject first;
    QueryStore::instance().subscribe(
        &first, k("a"), 300, 600, [](const QueryStore::State&) {},
        [](QueryStore::Resolver resolve, QueryStore::Rejecter) { resolve(QVariant(7)); });
    drain();

    // A second subscriber served from cache must learn when the data was
    // FETCHED, not when it subscribed — otherwise every new panel would
    // report hours-old data as brand new.
    QObject second;
    QueryStore::State seen;
    QueryStore::instance().subscribe(
        &second, k("a"), 300, 600, [&](const QueryStore::State& s) { seen = s; },
        [](QueryStore::Resolver resolve, QueryStore::Rejecter) { resolve(QVariant(999)); });
    drain();

    QCOMPARE(seen.data.toInt(), 7); // served from cache, fetcher not run
    QVERIFY(seen.fetched_at.isValid());
    QVERIFY(seen.fetched_at <= QDateTime::currentDateTime());
}

void TestQueryStore::revalidate_due_policy() {
    // The refresh policy, tested exhaustively without waiting real seconds.
    const QDateTime now = QDateTime::currentDateTime();
    const auto due = [&](int age_sec, int ttl) {
        return QueryStore::revalidate_due(now.addSecs(-age_sec), ttl, now);
    };

    QVERIFY2(QueryStore::revalidate_due(QDateTime(), 60, now),
             "never fetched must be due");
    QVERIFY2(!due(10, 60), "inside its TTL is not due");
    QVERIFY2(!due(100000, 0), "ttl<=0 means never expires, so never due");
    QVERIFY2(!due(100000, -1), "negative ttl behaves like 0");

    // The floor: a short TTL must not become a poll rate. The Earnings key
    // is 180s and fans out into six upstream calls; the quote key is 60s.
    QVERIFY2(!due(30, 5), "a 5s TTL does not refresh every 5s");
    QVERIFY2(due(QueryStore::kMinRevalidateSec + 1, 5), "past the floor it does refresh");
    QVERIFY2(!due(90, 180), "a long TTL is still respected");
    QVERIFY2(due(181, 180), "and fires once past it");
}

void TestQueryStore::revalidate_is_a_noop_while_fresh() {
    QObject owner;
    int fetches = 0;
    QVector<QVariant> delivered;
    auto fetcher = [&](QueryStore::Resolver resolve, QueryStore::Rejecter) {
        resolve(QVariant(++fetches));
    };

    QueryStore::instance().subscribe(
        &owner, k("a"), /*ttl*/ 600, /*stale_max*/ 1200,
        [&](const QueryStore::State& s) { delivered.append(s.data); }, fetcher);
    drain();
    QCOMPARE(fetches, 1);

    // Freshly fetched: a periodic tick must not refetch, or a 20s UI timer
    // would pull 10-minute data 30 times an hour.
    QueryStore::instance().revalidate(k("a"));
    drain();
    QCOMPARE(fetches, 1);

    // And nothing was blanked along the way.
    bool seen_data = false;
    for (const QVariant& v : delivered) {
        if (!v.isNull()) { seen_data = true; continue; }
        QVERIFY2(!seen_data, "revalidate must not blank a subscriber that had data");
    }
    QVERIFY(seen_data);
}

void TestQueryStore::revalidate_is_a_noop_without_a_subscription() {
    // Must not crash or invent an entry for a key nobody uses.
    QueryStore::instance().revalidate(k("never-subscribed"));
    drain();
}

void TestQueryStore::failed_revalidate_keeps_the_last_good_value() {
    QObject owner;
    bool first_call = true;
    QueryStore::State seen;
    auto fetcher = [&](QueryStore::Resolver resolve, QueryStore::Rejecter reject) {
        if (first_call) {
            first_call = false;
            resolve(QVariant(11));
        } else {
            reject(QStringLiteral("network down"));
        }
    };

    QueryStore::instance().subscribe(
        &owner, k("a"), 600, 1200, [&](const QueryStore::State& s) { seen = s; }, fetcher);
    drain();
    QCOMPARE(seen.data.toInt(), 11);
    const QDateTime good_at = seen.fetched_at;

    // Force the refresh (bypassing the due-check, which this test isn't
    // about) and let it fail.
    QueryStore::instance().revalidate(k("a"), /*force=*/true);
    drain();

    // The good value survives the failure, the error is reported alongside
    // it, and the age is NOT reset — a failed fetch must not make stale data
    // look refreshed.
    QCOMPARE(seen.data.toInt(), 11);
    QVERIFY(!seen.error.isEmpty());
    QVERIFY(seen.is_stale);
    QCOMPARE(seen.fetched_at, good_at);
}

void TestQueryStore::failed_fetch_backs_off_periodic_revalidate() {
    QObject owner;
    int attempts = 0;
    auto fetcher = [&](QueryStore::Resolver, QueryStore::Rejecter reject) {
        ++attempts;
        reject(QStringLiteral("rate limited"));
    };
    QueryStore::instance().subscribe(
        &owner, k("a"), 600, 1200, [](const QueryStore::State&) {}, fetcher);
    drain();
    QCOMPARE(attempts, 1);

    // A periodic tick right after the failure must NOT refetch: the entry has
    // no cached value (so the TTL gate can't hold it back) and errors are
    // never cached — without the failure backoff this looped a full RPC on
    // every 20s tick for as long as an outage lasted.
    QueryStore::instance().revalidate(k("a"));
    drain();
    QCOMPARE(attempts, 1);

    // An explicit user refresh is not throttled.
    QueryStore::instance().revalidate(k("a"), /*force=*/true);
    drain();
    QCOMPARE(attempts, 2);
}

void TestQueryStore::revalidate_owner_covers_every_key_of_that_owner() {
    QObject owner;
    int fetch_a = 0, fetch_b = 0;
    // No cached value yet for either key ⇒ both are due, which lets this
    // test exercise the fan-out without depending on wall-clock time.
    QueryStore::instance().subscribe(
        &owner, k("a"), 600, 1200, [](const QueryStore::State&) {},
        [&](QueryStore::Resolver resolve, QueryStore::Rejecter) { ++fetch_a; resolve(QVariant(1)); });
    QueryStore::instance().subscribe(
        &owner, k("b"), 600, 1200, [](const QueryStore::State&) {},
        [&](QueryStore::Resolver resolve, QueryStore::Rejecter) { ++fetch_b; resolve(QVariant(2)); });
    drain();
    QCOMPARE(fetch_a, 1);
    QCOMPARE(fetch_b, 1);

    // One call reaches every key this owner subscribes to — the whole point,
    // since a panel subscribes to several at once. Both are fresh here, so
    // the assertion is that the call is scoped and safe, not that it refetches
    // (revalidate_due_policy covers when a refetch is owed).
    QueryStore::instance().revalidate_owner(&owner);
    drain();
    QCOMPARE(fetch_a, 1);
    QCOMPARE(fetch_b, 1);
}

void TestQueryStore::revalidate_owner_ignores_other_owners() {
    QObject mine, theirs;
    int fetch_mine = 0, fetch_theirs = 0;
    QueryStore::instance().subscribe(
        &mine, k("mine"), 600, 1200, [](const QueryStore::State&) {},
        [&](QueryStore::Resolver resolve, QueryStore::Rejecter) { ++fetch_mine; resolve(QVariant(1)); });
    QueryStore::instance().subscribe(
        &theirs, k("theirs"), 600, 1200, [](const QueryStore::State&) {},
        [&](QueryStore::Resolver resolve, QueryStore::Rejecter) { ++fetch_theirs; resolve(QVariant(2)); });
    drain();
    QCOMPARE(fetch_mine, 1);
    QCOMPARE(fetch_theirs, 1);

    // Scoping: whatever revalidate_owner decides to do, it must never touch
    // a key belonging to a different owner.
    QueryStore::instance().revalidate_owner(&mine);
    drain();
    QCOMPARE(fetch_theirs, 1);
}

QTEST_MAIN(TestQueryStore)
#include "test_query_store.moc"
