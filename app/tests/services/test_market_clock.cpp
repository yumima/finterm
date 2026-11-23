// tests/services/test_market_clock.cpp
//
// The toolbar's US market clock: New York time, the regular session's state
// and the countdown to the bell — in summer time and across the autumn clock
// change, a weekend, a holiday, an early close. Instants are given in UTC (what the machine clock is).

#include "services/calendar/MarketClock.h"

#include <QtTest/QtTest>

using namespace finterm::market_clock;

namespace {
QDateTime utc(int y, int m, int d, int hh, int mm, int ss = 0) {
    return QDateTime(QDate(y, m, d), QTime(hh, mm, ss), QTimeZone::UTC);
}
} // namespace

class TestMarketClock : public QObject {
    Q_OBJECT
  private slots:
    void open_counts_to_the_close() {
        // Mon 23 Nov 2026, 13:12:05 ET (EST, UTC-5) → closes 16:00.
        const auto r = read(utc(2026, 11, 23, 18, 12, 5));
        QCOMPARE(r.et.time(), QTime(13, 12, 5));
        QCOMPARE(r.session, Session::Open);
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("02:47:55"));
    }

    void before_the_open() {
        // Mon 23 Nov 2026, 08:00 ET → opens 09:30.
        const auto r = read(utc(2026, 11, 23, 13, 0));
        QCOMPARE(r.session, Session::PreOpen);
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("01:30:00"));
    }

    void after_the_close_counts_to_tomorrow() {
        // Mon 23 Nov 2026, 16:00:00 ET exactly → closed; Tue 09:30.
        const auto r = read(utc(2026, 11, 23, 21, 0));
        QCOMPARE(r.session, Session::Closed);
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("17:30:00"));
    }

    void weekend_counts_to_monday() {
        // Sat 21 Nov 2026, 12:00 ET → Mon 23 Nov 09:30 = 1d 21:30.
        const auto r = read(utc(2026, 11, 21, 17, 0));
        QCOMPARE(r.session, Session::Closed);
        QCOMPARE(r.next_event.date(), QDate(2026, 11, 23));
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("1d 21:30:00"));
    }

    void holiday_and_early_close() {
        // Thanksgiving Thu 26 Nov 2026, noon ET: closed → Fri 27 09:30.
        auto r = read(utc(2026, 11, 26, 17, 0));
        QCOMPARE(r.session, Session::Closed);
        QCOMPARE(r.next_event, QDateTime(QDate(2026, 11, 27), QTime(9, 30), new_york()));
        // Fri 27 Nov 2026, 12:00 ET: early close at 13:00.
        r = read(utc(2026, 11, 27, 17, 0));
        QCOMPARE(r.session, Session::Open);
        QVERIFY(r.early_close);
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("01:00:00"));
        // 13:30 ET that day: closed → Mon 30 Nov 09:30.
        r = read(utc(2026, 11, 27, 18, 30));
        QCOMPARE(r.session, Session::Closed);
        QCOMPARE(r.next_event.date(), QDate(2026, 11, 30));
    }

    void across_the_clock_change() {
        // Sat 31 Oct 2026 12:00 EDT → Mon 2 Nov 09:30 EST: clocks fall back on
        // Sun 1 Nov, so the wait is 46 h 30 m, not 45 h 30 m.
        const auto r = read(utc(2026, 10, 31, 16, 0));
        QCOMPARE(r.et.time(), QTime(12, 0));
        QCOMPARE(r.next_event, QDateTime(QDate(2026, 11, 2), QTime(9, 30), new_york()));
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("1d 22:30:00"));
    }

    void countdown_meets_the_bell() {
        // 13:12:05.900 ET shows 13:12:05 and 02:47:55 → 16:00:00.
        const QDateTime t = utc(2026, 11, 23, 18, 12, 5).addMSecs(900);
        const auto r = read(t);
        QCOMPARE(r.et.toString(QStringLiteral("HH:mm:ss")), QStringLiteral("13:12:05"));
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("02:47:55"));
    }

    void summer_time() {
        // Wed 1 Jul 2026, 10:00 ET is 14:00 UTC (EDT, UTC-4).
        const auto r = read(utc(2026, 7, 1, 14, 0));
        QCOMPARE(r.et.time(), QTime(10, 0));
        QCOMPARE(r.session, Session::Open);
        QCOMPARE(countdown(r.secs_to_event), QStringLiteral("06:00:00"));
    }
};

QTEST_GUILESS_MAIN(TestMarketClock)
#include "test_market_clock.moc"
