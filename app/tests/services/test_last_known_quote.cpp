// A quote view that subscribes after its topics' TTL keeps every row it has a
// last-known value for (services/markets/LastKnownQuote.h).
//
// 2026-11-13, during a Yahoo 429 burst: the Markets STOCK INDICES panel showed
// only ^AXJO. The panels subscribed when PANELS was clicked (WORLD had become
// the default page), minutes after startup hydration had published every
// index's cached quote. DataHub::subscribe() replays a value only while it is
// younger than the TTL, every refresh was refused (nothing published), so the
// only index delivered was ^AXJO — the one quote that had landed in the 30 s
// before the click. This test replays that sequence against the real DataHub.

#include "datahub/DataHub.h"
#include "datahub/Producer.h"
#include "datahub/TopicPolicy.h"
#include "services/markets/LastKnownQuote.h"

#include <QCoreApplication>
#include <cmath>
#include <QTest>

using finterm::datahub::DataHub;
using finterm::datahub::SubscriptionState;
using finterm::services::QuoteData;

namespace {

/// Yahoo refusing everything: refresh() is called, nothing is ever published.
class RefusingProducer : public finterm::datahub::Producer {
  public:
    QStringList topic_patterns() const override { return {QStringLiteral("market:quote:*")}; }
    void refresh(const QStringList& topics) override { asked += topics; }
    QStringList asked;
};

QuoteData quote(const QString& sym, double price) {
    QuoteData q;
    q.symbol = sym;
    q.name = sym;
    q.price = price;
    q.change = 1.0;
    q.change_pct = 0.1;
    return q;
}

void pump() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

} // namespace

class TestLastKnownQuote : public QObject {
    Q_OBJECT

  private slots:
    void initTestCase() {
        qRegisterMetaType<QuoteData>();
        DataHub::instance().register_producer(&producer_);
        // The real quote topics use the same rule with a 30 s TTL; 40 ms keeps
        // the test fast. The seam is market:quote:<sym>, so publish there.
        finterm::datahub::TopicPolicy p;
        p.ttl_ms = 40;
        p.min_interval_ms = 0;
        DataHub::instance().set_policy_pattern(QStringLiteral("market:quote:*"), p);
    }

    // The incident: a hydrated ^GSPC older than the TTL, a just-polled ^AXJO.
    void late_subscriber_keeps_every_last_known_row() {
        auto& hub = DataHub::instance();
        const QStringList syms{QStringLiteral("^GSPC"), QStringLiteral("^N225"), QStringLiteral("^AXJO"),
                               QStringLiteral("^NEVER")};
        hub.publish(QStringLiteral("market:quote:^GSPC"), QVariant::fromValue(quote("^GSPC", 6700.5)));
        hub.publish(QStringLiteral("market:quote:^N225"), QVariant::fromValue(quote("^N225", 50100.0)));
        QTest::qWait(80); // past the TTL; every refresh since was refused
        hub.publish(QStringLiteral("market:quote:^AXJO"), QVariant::fromValue(quote("^AXJO", 8800.0)));

        // A panel subscribing now, the way MarketPanel::hub_resubscribe does.
        QObject panel;
        QHash<QString, QuoteData> rows;
        for (const QString& sym : syms)
            hub.subscribe(&panel, QStringLiteral("market:quote:") + sym,
                          [&rows, sym](const QVariant& v) { rows.insert(sym, v.value<QuoteData>()); });
        pump();
        // What subscribe() alone delivers: the bug's single row.
        QCOMPARE(rows.keys(), QStringList{QStringLiteral("^AXJO")});

        // The fix: every symbol the hub holds a value for gets its row back.
        QCOMPARE(finterm::services::seed_last_known_quotes(syms, rows), 2);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.value(QStringLiteral("^GSPC")).price, 6700.5);
        QCOMPARE(rows.value(QStringLiteral("^N225")).price, 50100.0);
        QVERIFY(!rows.contains(QStringLiteral("^NEVER"))); // never quoted: no invented row value
        hub.unsubscribe(&panel);
        pump();
    }

    // A delivered (newer) quote is never replaced by the hub's copy.
    void seed_does_not_overwrite_a_delivered_quote() {
        auto& hub = DataHub::instance();
        hub.publish(QStringLiteral("market:quote:^FTSE"), QVariant::fromValue(quote("^FTSE", 9000.0)));
        QHash<QString, QuoteData> rows;
        rows.insert(QStringLiteral("^FTSE"), quote("^FTSE", 9001.0));
        QCOMPARE(finterm::services::seed_last_known_quotes({QStringLiteral("^FTSE")}, rows), 0);
        QCOMPARE(rows.value(QStringLiteral("^FTSE")).price, 9001.0);
    }

    // Seeded rows must be marked: a hydrated quote (not polled this process)
    // or one whose refresh failed is STALE; only a polled, healthy one is not.
    void stale_rule() {
        using finterm::services::quote_is_stale;
        QVERIFY(quote_is_stale(false, SubscriptionState::Fresh));
        QVERIFY(quote_is_stale(false, SubscriptionState::Refreshing));
        QVERIFY(quote_is_stale(true, SubscriptionState::Error));
        QVERIFY(!quote_is_stale(true, SubscriptionState::Fresh));
        QVERIFY(!quote_is_stale(true, SubscriptionState::Refreshing));
    }

    // An unchanged quote is not re-delivered — including one whose open /
    // prev_close the feed left out (NaN). With a defaulted operator== NaN
    // never equals itself, so every poll and tick repainted every subscriber.
    void unchanged_quote_with_unknown_fields_is_not_redelivered() {
        QuoteData q = quote("^NAN", 100.0);
        QVERIFY(std::isnan(q.open) && std::isnan(q.prev_close));
        QVERIFY(q == q);
        auto& hub = DataHub::instance();
        const QString topic = QStringLiteral("market:quote:^NAN");
        QObject owner;
        int delivered = 0;
        hub.subscribe(&owner, topic, [&delivered](const QVariant&) { ++delivered; });
        hub.publish(topic, QVariant::fromValue(q));
        pump();
        const int after_first = delivered;
        QVERIFY(after_first >= 1);
        hub.publish(topic, QVariant::fromValue(q));
        pump();
        QCOMPARE(delivered, after_first);
    }

  private:
    RefusingProducer producer_;
};

QTEST_MAIN(TestLastKnownQuote)
#include "test_last_known_quote.moc"
