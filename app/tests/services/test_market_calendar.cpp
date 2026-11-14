// test_market_calendar.cpp — NYSE holidays, early closes, options expiration
// and month/quarter ends, pinned against NYSE's published calendars.

#include "services/calendar/MarketCalendar.h"

#include <QTest>

using namespace finterm::market_calendar;

namespace {
QVector<QDate> holiday_dates(int y) {
    QVector<QDate> out;
    for (const auto& h : nyse_holidays(y))
        out << h.date;
    std::sort(out.begin(), out.end());
    return out;
}
QVector<QDate> early_closes(int y) {
    QVector<QDate> out;
    for (QDate d(y, 1, 1); d.year() == y; d = d.addDays(1))
        if (is_early_close(d))
            out << d;
    return out;
}
} // namespace

class TestMarketCalendar : public QObject {
    Q_OBJECT
  private slots:
    void easter_known_years() {
        QCOMPARE(easter(2024), QDate(2024, 3, 31));
        QCOMPARE(easter(2025), QDate(2025, 4, 20));
        QCOMPARE(easter(2026), QDate(2026, 4, 5));
        QCOMPARE(easter(2027), QDate(2027, 3, 28));
        QCOMPARE(easter(2028), QDate(2028, 4, 16));
        QCOMPARE(easter(2038), QDate(2038, 4, 25)); // latest possible
        QCOMPARE(easter(2285), QDate(2285, 3, 22)); // earliest possible
    }

    // NYSE published holiday calendars (nyse.com/markets/hours-calendars).
    void nyse_2025() {
        QCOMPARE(holiday_dates(2025),
                 (QVector<QDate>{{2025, 1, 1}, {2025, 1, 20}, {2025, 2, 17}, {2025, 4, 18}, {2025, 5, 26},
                                 {2025, 6, 19}, {2025, 7, 4}, {2025, 9, 1}, {2025, 11, 27}, {2025, 12, 25}}));
        QCOMPARE(early_closes(2025), (QVector<QDate>{{2025, 7, 3}, {2025, 11, 28}, {2025, 12, 24}}));
    }

    void nyse_2026_independence_day_on_saturday() {
        // Jul 4 is a Saturday: closed Friday Jul 3, and NO early close on Jul 2.
        QCOMPARE(holiday_dates(2026),
                 (QVector<QDate>{{2026, 1, 1}, {2026, 1, 19}, {2026, 2, 16}, {2026, 4, 3}, {2026, 5, 25},
                                 {2026, 6, 19}, {2026, 7, 3}, {2026, 9, 7}, {2026, 11, 26}, {2026, 12, 25}}));
        QCOMPARE(early_closes(2026), (QVector<QDate>{{2026, 11, 27}, {2026, 12, 24}}));
    }

    void nyse_2027_saturday_juneteenth_and_christmas() {
        // Juneteenth Sat → Fri Jun 18; Jul 4 Sun → Mon Jul 5; Christmas Sat → Fri Dec 24
        // (so Dec 24 is closed, not an early close, and Dec 23 trades a full day).
        QCOMPARE(holiday_dates(2027),
                 (QVector<QDate>{{2027, 1, 1}, {2027, 1, 18}, {2027, 2, 15}, {2027, 3, 26}, {2027, 5, 31},
                                 {2027, 6, 18}, {2027, 7, 5}, {2027, 9, 6}, {2027, 11, 25}, {2027, 12, 24}}));
        QCOMPARE(early_closes(2027), (QVector<QDate>{{2027, 11, 26}}));
    }

    void new_years_on_saturday_is_not_observed() {
        // 2022 and 2028: Jan 1 is a Saturday; NYSE trades Friday Dec 31 of the
        // year before and has no New Year holiday at all.
        QVERIFY(!holiday_dates(2022).contains(QDate(2021, 12, 31)));
        QVERIFY(is_trading_day(QDate(2021, 12, 31)));
        QVERIFY(is_trading_day(QDate(2027, 12, 31)));
        QCOMPARE(holiday_dates(2028).first(), QDate(2028, 1, 17)); // MLK
        // Sunday Jan 1 (2023) → Monday Jan 2.
        QVERIFY(is_holiday(QDate(2023, 1, 2)));
    }

    void juneteenth_from_2022_only() {
        QVERIFY(!is_holiday(QDate(2021, 6, 18)));
        QVERIFY(is_holiday(QDate(2022, 6, 20))); // Sunday → Monday
        QVERIFY(is_holiday(QDate(2023, 6, 19)));
    }

    void early_close_rules() {
        QVERIFY(is_early_close(QDate(2023, 7, 3)));   // Monday before a Tuesday 4th
        QVERIFY(!is_early_close(QDate(2022, 7, 1)));  // 4th on Monday: Friday is a full day
        QVERIFY(!is_early_close(QDate(2020, 7, 2)));  // 4th Saturday, observed Friday
        QVERIFY(is_early_close(QDate(2018, 12, 24))); // Monday Christmas Eve
        QVERIFY(!is_early_close(QDate(2022, 12, 23))); // Christmas Sunday → Monday 26th closed
        QVERIFY(is_holiday(QDate(2022, 12, 26)));
        QVERIFY(is_early_close(QDate(2024, 11, 29)));
        QVERIFY(!is_early_close(QDate(2024, 11, 30))); // a Saturday is not a session
    }

    void opex_third_friday_or_thursday_before_a_holiday() {
        QCOMPARE(monthly_opex(2026, 11), QDate(2026, 11, 20));
        QCOMPARE(monthly_opex(2026, 12), QDate(2026, 12, 18));
        QCOMPARE(monthly_opex(2026, 5), QDate(2026, 5, 15));
        // Good Friday on the 3rd Friday: Apr 18 2025 → Thursday Apr 17.
        QCOMPARE(monthly_opex(2025, 4), QDate(2025, 4, 17));
        // 1st of the month on a Friday: the 3rd Friday is the 15th.
        QCOMPARE(monthly_opex(2027, 1), QDate(2027, 1, 15));
    }

    void last_trading_day_skips_weekends_and_holidays() {
        QCOMPARE(last_trading_day(2026, 10), QDate(2026, 10, 30)); // Oct 31 Saturday
        QCOMPARE(last_trading_day(2026, 12), QDate(2026, 12, 31));
        QCOMPARE(last_trading_day(2027, 12), QDate(2027, 12, 31)); // Friday; Jan 1 2028 Saturday
        QCOMPARE(last_trading_day(2026, 5), QDate(2026, 5, 29));   // 31st Sunday
    }

    void market_dates_window() {
        const auto d = market_dates(QDate(2026, 11, 13), QDate(2026, 12, 31));
        QStringList got;
        for (const auto& m : d)
            got << m.date.toString(Qt::ISODate) + QLatin1Char(' ') + QString::number(int(m.kind));
        using K = MarketDate::Kind;
        const QStringList want = {
            "2026-11-20 " + QString::number(int(K::Opex)),       "2026-11-26 " + QString::number(int(K::Holiday)),
            "2026-11-27 " + QString::number(int(K::EarlyClose)), "2026-11-30 " + QString::number(int(K::MonthEnd)),
            "2026-12-18 " + QString::number(int(K::QuadWitching)), "2026-12-24 " + QString::number(int(K::EarlyClose)),
            "2026-12-25 " + QString::number(int(K::Holiday)),    "2026-12-31 " + QString::number(int(K::QuarterEnd)),
        };
        QCOMPARE(got, want);
        QVERIFY(d[1].title.contains(QStringLiteral("Thanksgiving")));
        QVERIFY(market_dates(QDate(2026, 12, 1), QDate(2026, 11, 1)).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestMarketCalendar)
#include "test_market_calendar.moc"
