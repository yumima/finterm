// test_macro_calendar.cpp — economic-calendar parsing, filter and bucketing.
//
// Regression (2026-11-13): the ECONOMIC CALENDAR widget showed raw
// "2026-11-08T17:30:00-05:00" strings and, on Friday Nov 13, Sunday Nov 8's
// events — the feed's ISO dates were parsed as "MMM d yyyy" and came back
// invalid, so nothing sorted and nothing was filtered.

#include "services/calendar/MacroCalendar.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::macro_calendar;

namespace {
QJsonArray ff_week() {
    return QJsonDocument::fromJson(R"([
      {"title":"RBA Assist Gov Hunter Speaks","country":"AUD","date":"2026-11-08T17:30:00-05:00","impact":"Low","forecast":"","previous":""},
      {"title":"CPI m/m","country":"USD","date":"2026-11-13T08:30:00-05:00","impact":"High","forecast":"0.3%","previous":"0.4%"},
      {"title":"Unemployment Claims","country":"USD","date":"2026-11-12T08:30:00-05:00","impact":"Medium","forecast":"220K","previous":"218K"},
      {"title":"FOMC Member Waller Speaks","country":"USD","date":"2026-11-14T10:00:00-05:00","impact":"Medium","forecast":"","previous":""},
      {"title":"Bank Holiday","country":"USD","date":"2026-11-11T00:00:00-05:00","impact":"Holiday","forecast":"","previous":""},
      {"title":"Mortgage Delinquencies","country":"USD","date":"2026-11-13T10:00:00-05:00","impact":"Low","forecast":"","previous":""},
      {"title":"German ZEW","country":"EUR","date":"2026-11-13T05:00:00-05:00","impact":"High","forecast":"","previous":""}
    ])").array();
}
} // namespace

class TestMacroCalendar : public QObject {
    Q_OBJECT
  private slots:
    void parses_iso_instants_from_the_feed() {
        const auto ev = parse_ff(ff_week());
        QCOMPARE(ev.size(), 7);
        QVERIFY(ev[1].when.isValid());
        QCOMPARE(ev[1].when.toUTC(), QDateTime(QDate(2026, 11, 13), QTime(13, 30), QTimeZone::UTC));
        QCOMPARE(ev[1].importance, 3);
        QCOMPARE(ev[1].forecast, QStringLiteral("0.3%"));
        QCOMPARE(ev[4].importance, 0); // Holiday is not High
        QVERIFY(!ev[4].timed);
    }

    void today_onward_drops_past_days_and_sorts() {
        // Friday 2026-11-13 09:00 New York.
        const QDateTime now(QDate(2026, 11, 13), QTime(9, 0), QTimeZone("America/New_York"));
        const auto ev = today_onward(parse_ff(ff_week()), now);
        for (const auto& e : ev)
            QVERIFY2(e.when.toLocalTime().date() >= QDate(2026, 11, 13), qPrintable(e.title));
        QCOMPARE(ev.first().title, QStringLiteral("German ZEW")); // 05:00 < 08:30
        QCOMPARE(ev.last().title, QStringLiteral("FOMC Member Waller Speaks"));
    }

    void top_tier_us_filter() {
        const auto ev = parse_ff(ff_week());
        QStringList kept;
        for (const auto& e : ev)
            if (is_top_tier_us(e))
                kept << e.title;
        QCOMPARE(kept, QStringList({"CPI m/m", "Unemployment Claims", "FOMC Member Waller Speaks"}));
    }

    void kinds() {
        QCOMPARE(kind_of("Non-Farm Employment Change"), QStringLiteral("LABOR"));
        QCOMPARE(kind_of("Unemployment Rate"), QStringLiteral("LABOR"));
        QCOMPARE(kind_of("Core PPI m/m"), QStringLiteral("INFLATION"));
        QCOMPARE(kind_of("Federal Funds Rate"), QStringLiteral("FED"));
        QCOMPARE(kind_of("Fed Chair Powell Speaks"), QStringLiteral("FED"));
        QCOMPARE(kind_of("Mortgage Delinquencies"), QString());
    }

    void buckets() {
        const QDate fri(2026, 11, 13); // Friday
        QCOMPARE(bucket_for(QDate(2026, 11, 12), fri), Bucket::Past);
        QCOMPARE(bucket_for(fri, fri), Bucket::Today);
        QCOMPARE(bucket_for(QDate(2026, 11, 15), fri), Bucket::ThisWeek); // Sunday
        QCOMPARE(bucket_for(QDate(2026, 11, 16), fri), Bucket::ThisMonth); // next Monday
        QCOMPARE(bucket_for(QDate(2026, 11, 30), fri), Bucket::ThisMonth);
        QCOMPARE(bucket_for(QDate(2026, 12, 1), fri), Bucket::Upcoming);
        // A week running into the next month keeps its days in THIS WEEK.
        const QDate wed(2026, 9, 30);
        QCOMPARE(bucket_for(QDate(2026, 10, 2), wed), Bucket::ThisWeek);
        QCOMPARE(bucket_for(QDate(2026, 10, 5), wed), Bucket::Upcoming);
        QCOMPARE(bucket_for(QDate(), fri), Bucket::Past);
    }

    void short_labels() {
        const QDate today(2026, 11, 13);
        const QDateTime t(QDate(2026, 11, 13), QTime(8, 30)); // local time
        QCOMPARE(short_when(t, true, today), QStringLiteral("Today 08:30"));
        QCOMPARE(short_when(t.addDays(4), true, today), QStringLiteral("Tue 08:30"));
        QCOMPARE(short_when(t.addDays(7), false, today), QStringLiteral("Nov 20"));
        QVERIFY(!short_when(t, true, today).contains(QLatin1Char('T') + QStringLiteral("08")));
    }

    void fomc_calendar() {
        const QString html = QStringLiteral(
            "<h4><a id='1'>2026 FOMC Meetings</a></h4>"
            "<div class=\"fomc-meeting__month col-xs-5\"><strong>October</strong></div>\n"
            "   <div class=\"fomc-meeting__date col-xs-4\">27-28</div>"
            "<div class=\"fomc-meeting__month col-xs-5\"><strong>December</strong></div>"
            "<div class=\"fomc-meeting__date col-xs-4\">8-9*</div>"
            "<h4><a id='2'>2025 FOMC Meetings</a></h4>"
            "<div class=\"fomc-meeting__month\"><strong>Apr/May</strong></div>"
            "<div class=\"fomc-meeting__date\">30-1</div>");
        const auto d = parse_fomc_calendar(html);
        QCOMPARE(d, QVector<QDate>({QDate(2025, 5, 1), QDate(2026, 10, 28), QDate(2026, 12, 9)}));
    }
};

QTEST_APPLESS_MAIN(TestMacroCalendar)
#include "test_macro_calendar.moc"
