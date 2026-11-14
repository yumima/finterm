// test_whats_on_model.cpp — News › WHAT'S ON: sources merged into one dated
// list, filtered, de-duplicated and bucketed TODAY / THIS WEEK / THIS MONTH /
// UPCOMING.

#include "screens/news/WhatsOnModel.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::whats_on;
namespace mc = finterm::macro_calendar;

namespace {
mc::MacroEvent ev(const QString& title, const QString& cty, int imp, const QDateTime& when) {
    mc::MacroEvent e;
    e.title = title;
    e.country = cty;
    e.importance = imp;
    e.when = when;
    e.forecast = "0.3%";
    e.previous = "0.4%";
    return e;
}
} // namespace

class TestWhatsOnModel : public QObject {
    Q_OBJECT
  private slots:
    void macro_default_filter_keeps_top_tier_us() {
        const QDateTime now(QDate(2026, 11, 10), QTime(9, 0));
        const QVector<mc::MacroEvent> events = {
            ev("CPI m/m", "USD", 3, now.addSecs(3600)),
            ev("Unemployment Claims", "USD", 2, now.addDays(2)),
            ev("Mortgage Delinquencies", "USD", 1, now.addDays(1)),
            ev("German ZEW", "EUR", 3, now.addDays(1)),
            ev("Retail Sales m/m", "USD", 3, now.addDays(-1)), // past
        };
        const auto top = items_from_macro(events, now, true);
        QCOMPARE(top.size(), 2);
        QCOMPARE(top[0].title, QStringLiteral("CPI m/m"));
        QCOMPARE(top[0].kind, QStringLiteral("INFLATION"));
        QCOMPARE(top[0].figures, QStringLiteral("F 0.3% · P 0.4%"));
        QCOMPARE(top[1].kind, QStringLiteral("LABOR"));
        const auto all = items_from_macro(events, now, false);
        QCOMPARE(all.size(), 4);
        QVERIFY(std::any_of(all.begin(), all.end(), [](const Item& i) { return i.title == "EUR German ZEW"; }));
    }

    void fomc_next_two_and_not_twice() {
        const QDate today(2026, 11, 13);
        const QVector<QDate> d = {QDate(2026, 10, 28), QDate(2026, 12, 9), QDate(2027, 1, 27), QDate(2027, 3, 17)};
        auto items = items_from_fomc(d, today, {});
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].when.date(), QDate(2026, 12, 9));
        QCOMPARE(items[0].when.toTimeZone(QTimeZone("America/New_York")).time(), QTime(14, 0));
        // The calendar already lists the decision that day: no duplicate row,
        // and it still counts as one of the next two meetings.
        Item rate;
        rate.title = "Federal Funds Rate";
        rate.when = QDateTime(QDate(2026, 12, 9), QTime(14, 0), QTimeZone("America/New_York"));
        items = items_from_fomc(d, today, {rate});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].when.date(), QDate(2027, 1, 27));
    }

    void ipo_upcoming_only() {
        const auto deals = QJsonDocument::fromJson(R"([
          {"deal_id":"d-42","company":"Acme Corp","ticker":"ACME","status":"upcoming","expected_date":"2026-11-18","price_range":"15.00-17.00","exchange":"NYSE"},
          {"company":"Old Co","ticker":"OLD","status":"upcoming","expected_date":"2026-11-01"},
          {"company":"Priced Inc","ticker":"PRC","status":"priced","expected_date":"2026-11-20"},
          {"company":"No Date","ticker":"ND","status":"upcoming","expected_date":""}
        ])").array();
        const auto items = items_from_ipo_deals(deals, QDate(2026, 11, 13));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].title, QStringLiteral("Acme Corp (ACME)"));
        QCOMPARE(items[0].key, QStringLiteral("ACME"));
        QCOMPARE(items[0].deal_id, QStringLiteral("d-42")); // IPO Watch selects by deal, not ticker
        QCOMPARE(items[0].figures, QStringLiteral("$15.00-17.00 · NYSE"));
    }

    void bucketize_orders_and_groups() {
        const QDate today(2026, 11, 13); // Friday
        auto mk = [](const QString& t, const QDateTime& w, bool timed) {
            Item i;
            i.title = t;
            i.when = w;
            i.timed = timed;
            return i;
        };
        const QVector<Item> items = {
            mk("later today", QDateTime(today, QTime(14, 0)), true),
            mk("all day today", QDateTime(today, QTime(0, 0)), false),
            mk("early today", QDateTime(today, QTime(8, 30)), true),
            mk("sunday", QDateTime(QDate(2026, 11, 15), QTime(9, 0)), true),
            mk("next week", QDateTime(QDate(2026, 11, 18), QTime(0, 0)), false),
            mk("december", QDateTime(QDate(2026, 12, 9), QTime(11, 0)), true),
            mk("yesterday", QDateTime(QDate(2026, 11, 12), QTime(11, 0)), true),
        };
        const auto b = bucketize(items, today);
        QCOMPARE(b.value(mc::Bucket::Today).size(), 3);
        QCOMPARE(b.value(mc::Bucket::Today)[0].title, QStringLiteral("all day today"));
        QCOMPARE(b.value(mc::Bucket::Today)[1].title, QStringLiteral("early today"));
        QCOMPARE(b.value(mc::Bucket::ThisWeek).size(), 1);
        QCOMPARE(b.value(mc::Bucket::ThisMonth)[0].title, QStringLiteral("next week"));
        QCOMPARE(b.value(mc::Bucket::Upcoming)[0].title, QStringLiteral("december"));
        QVERIFY(!b.contains(mc::Bucket::Past));
    }
};

QTEST_APPLESS_MAIN(TestWhatsOnModel)
#include "test_whats_on_model.moc"
