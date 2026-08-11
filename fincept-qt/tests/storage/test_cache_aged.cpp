// tests/storage/test_cache_aged.cpp
//
// Pins the "how old is this cached value" contract.
//
// The bug these exist for: quote freshness was gated on a `cached_at` field
// that WRITERS stamped into the JSON payload. Two code paths wrote the same
// `market_last:` key, only one stamped it, and the reader treated a missing
// stamp as "age unknown ⇒ publish anyway". So merely opening the portfolio
// screen re-wrote the entry without a stamp and re-armed the stale-price bug
// for that symbol — a week-old price republished at cold start wearing a
// Fresh badge.
//
// Centralising to a single writer fixed the instance and left the class of
// bug alive for the next writer. The real fix is that write time is not the
// writer's to supply: it is derived from the cache row itself
// (expires_at − ttl_seconds), which put() sets unconditionally. These tests
// pin that, and specifically pin the property the old design could not hold —
// that a SECOND write by a different caller cannot un-age an entry.
//
// Runs against a real temp SQLite file, because the derivation is SQL plus
// timestamp parsing and that is exactly where it can go wrong.

#include "storage/cache/CacheManager.h"
#include "storage/sqlite/CacheDatabase.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using fincept::CacheManager;
using fincept::CacheDatabase;

class TestCacheAged : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase();
    void fresh_write_reports_now();
    void rewrite_by_another_caller_cannot_un_age();          // THE regression
    void written_at_is_not_expiry();
    void all_three_aged_getters_agree();
    void backdated_row_is_reported_old();
    void missing_key_is_absent_not_zero_aged();

  private:
    QTemporaryDir dir_;
};

void TestCacheAged::initTestCase() {
    QVERIFY(dir_.isValid());
    const auto r = CacheDatabase::instance().open(dir_.filePath("cache.db"));
    QVERIFY2(r.is_ok(), "cache database must open");
    QVERIFY(CacheDatabase::instance().is_open());
}

void TestCacheAged::fresh_write_reports_now() {
    CacheManager::instance().put("aged:fresh", QVariant(QStringLiteral("{\"price\":1}")),
                                 3600, "test");
    const auto got = CacheManager::instance().try_get_aged("aged:fresh");
    QVERIFY(got.has_value());
    QVERIFY(got->written_at.isValid());
    const qint64 age = QDateTime::currentSecsSinceEpoch() - got->written_at.toSecsSinceEpoch();
    // Generous bound: this asserts "roughly now", not clock precision.
    QVERIFY2(age >= -2 && age <= 10, qPrintable(QString("age was %1s").arg(age)));
}

// The exact shape of the shipped bug: writer A stores a quote, then writer B
// (a different code path, e.g. the on-demand batch fetch) re-stores the same
// key. Under the payload-stamp design B's write erased the age information.
// Here B cannot: it does not supply the timestamp at all.
void TestCacheAged::rewrite_by_another_caller_cannot_un_age() {
    // Writer A — full payload.
    CacheManager::instance().put("aged:rewrite",
                                 QVariant(QStringLiteral("{\"price\":10,\"cached_at\":12345}")),
                                 7 * 24 * 3600, "test");
    // Writer B — a payload with NO age field whatsoever, as flush_batch wrote.
    CacheManager::instance().put("aged:rewrite", QVariant(QStringLiteral("{\"price\":11}")),
                                 7 * 24 * 3600, "test");

    const auto got = CacheManager::instance().multi_get_aged({QStringLiteral("aged:rewrite")});
    QCOMPARE(got.size(), 1);
    const auto aged = got.value(QStringLiteral("aged:rewrite"));
    QCOMPARE(QJsonDocument::fromJson(aged.value.toUtf8()).object().value("price").toDouble(), 11.0);

    // The age must still be known and recent. Under the old design this read
    // as 0 / absent and the staleness gate silently disengaged.
    QVERIFY2(aged.written_at.isValid(), "a rewrite must not erase the write time");
    const qint64 age = QDateTime::currentSecsSinceEpoch() - aged.written_at.toSecsSinceEpoch();
    QVERIFY2(age >= -2 && age <= 10, qPrintable(QString("age was %1s").arg(age)));

    // And it must not have picked up the stale payload field either.
    QVERIFY2(aged.written_at.toSecsSinceEpoch() > 12345,
             "write time must come from the row, not a payload field");
}

// written_at = expires_at − ttl. A long TTL must not push the reported write
// time into the future; dropping the subtraction is the obvious way to break
// this and it would make every entry look permanently fresh.
void TestCacheAged::written_at_is_not_expiry() {
    CacheManager::instance().put("aged:longttl", QVariant(QStringLiteral("x")),
                                 30 * 24 * 3600, "test");
    const auto got = CacheManager::instance().try_get_aged("aged:longttl");
    QVERIFY(got.has_value());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY2(got->written_at.toSecsSinceEpoch() <= now + 2,
             "write time must not be in the future for a long-TTL entry");
    QVERIFY2(now - got->written_at.toSecsSinceEpoch() <= 10, "and must still be ~now");
}

// Three getters derive the same field; they must not drift. MarketDataService
// hydrates via the prefix form while PortfolioService reads via the batch
// form, and the two disagreeing is what put one holding on screen at two
// different prices.
void TestCacheAged::all_three_aged_getters_agree() {
    CacheManager::instance().put("agedpfx:AAPL", QVariant(QStringLiteral("{\"price\":1}")),
                                 3600, "test");

    const auto single = CacheManager::instance().try_get_aged("agedpfx:AAPL");
    const auto batch  = CacheManager::instance().multi_get_aged({QStringLiteral("agedpfx:AAPL")});
    const auto prefix = CacheManager::instance().get_prefix_aged("agedpfx:");

    QVERIFY(single.has_value());
    QCOMPARE(batch.size(), 1);
    QCOMPARE(prefix.size(), 1);

    const qint64 a = single->written_at.toSecsSinceEpoch();
    const qint64 b = batch.value(QStringLiteral("agedpfx:AAPL")).written_at.toSecsSinceEpoch();
    const qint64 c = prefix.value(QStringLiteral("agedpfx:AAPL")).written_at.toSecsSinceEpoch();
    QCOMPARE(b, a);
    QCOMPARE(c, a);
}

// The gate has to be able to actually fire. Backdate the row so it is old but
// NOT expired — the 7-day quote fallback is exactly this state — and confirm
// the reported age crosses a multi-day threshold. Without this, every test
// above would still pass if written_at were hardcoded to "now".
void TestCacheAged::backdated_row_is_reported_old() {
    CacheManager::instance().put("aged:old", QVariant(QStringLiteral("{\"price\":5}")),
                                 7 * 24 * 3600, "test");

    // Written 6 days ago ⇒ expires 1 day from now. Still live, clearly stale.
    QSqlQuery q(CacheDatabase::instance().raw_db());
    QVERIFY(q.exec("UPDATE unified_cache "
                   "SET expires_at = datetime('now', '+1 day') "
                   "WHERE key = 'aged:old'"));

    const auto got = CacheManager::instance().multi_get_aged({QStringLiteral("aged:old")});
    QCOMPARE(got.size(), 1);   // still unexpired, so still returned
    const auto aged = got.value(QStringLiteral("aged:old"));
    QVERIFY(aged.written_at.isValid());

    const qint64 age = QDateTime::currentSecsSinceEpoch() - aged.written_at.toSecsSinceEpoch();
    const qint64 six_days = 6 * 24 * 3600;
    QVERIFY2(qAbs(age - six_days) < 3600,
             qPrintable(QString("expected ~6d, got %1s").arg(age)));

    // And it must be caught by the hydration threshold the app actually uses.
    QVERIFY2(age > 4 * 24 * 3600, "a 6-day-old quote must fail a 4-day freshness gate");
}

void TestCacheAged::missing_key_is_absent_not_zero_aged() {
    const auto batch = CacheManager::instance().multi_get_aged({QStringLiteral("aged:nope")});
    QVERIFY2(batch.isEmpty(), "a missing key must be absent, not present with a zero age");
    QVERIFY(!CacheManager::instance().try_get_aged("aged:nope").has_value());
    QVERIFY(CacheManager::instance().get_prefix_aged("aged:nope").isEmpty());
}

QTEST_MAIN(TestCacheAged)
#include "test_cache_aged.moc"
