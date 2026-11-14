#pragma once
// MarketCalendar.h — US equity market-structure dates, computed locally.
//
// NYSE full-day holidays and 13:00 ET early closes, monthly equity-options
// expiration, quarterly "quad witching", and month/quarter-end trading days.
// Nothing here is fetched: the rules are published (NYSE Rule 7.2 holiday
// list, OCC expiration standard) and stable, so a computed calendar cannot be
// "unavailable". Header-only, Qt-Core-only, every function pure;
// tests/services/test_market_calendar.cpp pins it against NYSE's published
// calendars for 2022–2028.
//
// Rules:
//   * New Year's Day, Juneteenth (from 2022), Independence Day, Christmas:
//     Saturday → observed Friday, Sunday → observed Monday — EXCEPT New
//     Year's Day on a Saturday, which NYSE does not observe on the Friday
//     before (that Friday is the last trading day of the year: 2022, 2028).
//   * MLK Day 3rd Mon Jan, Washington's Birthday 3rd Mon Feb, Good Friday
//     (Easter − 2, Gregorian computus), Memorial Day last Mon May, Labor Day
//     1st Mon Sep, Thanksgiving 4th Thu Nov.
//   * Early close (13:00 ET): the day after Thanksgiving; Dec 24 when it is a
//     weekday trading day; Jul 3 when it is a weekday trading day (not when
//     Independence Day itself is observed on Jul 3 — NYSE did not close early
//     on Jul 2 in 2015 or 2020).
//   * Monthly options expire on the 3rd Friday; when that Friday is a market
//     holiday (Good Friday) expiration moves to the Thursday before.
//   * Quad witching = the March/June/September/December monthly expiration.
//   * Month/quarter end = the month's last TRADING day.
// One-off closures (national days of mourning, 2012's storm) are not rules
// and are not here.

#include <QDate>
#include <QLocale>
#include <QString>
#include <QVector>

#include <algorithm>

namespace finterm::market_calendar {

/// Easter Sunday (Gregorian), anonymous computus.
inline QDate easter(int y) {
    const int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
    const int f = (b + 8) / 25, g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31, day = ((h + l - 7 * m + 114) % 31) + 1;
    return QDate(y, month, day);
}

/// The n-th (1-based) `weekday` (1=Mon … 7=Sun) of the month; n = -1 → last.
inline QDate nth_weekday(int y, int month, int weekday, int n) {
    if (n > 0) {
        QDate d(y, month, 1);
        d = d.addDays((weekday - d.dayOfWeek() + 7) % 7);
        return d.addDays(7 * (n - 1));
    }
    QDate d(y, month, QDate(y, month, 1).daysInMonth());
    return d.addDays(-((d.dayOfWeek() - weekday + 7) % 7));
}

/// Saturday → Friday, Sunday → Monday.
inline QDate observed(const QDate& d) {
    if (d.dayOfWeek() == 6)
        return d.addDays(-1);
    if (d.dayOfWeek() == 7)
        return d.addDays(1);
    return d;
}

struct Holiday {
    QDate date;
    QString name;
};

/// NYSE full-day closures whose OBSERVED date falls in year `y`, in date order.
inline QVector<Holiday> nyse_holidays(int y) {
    QVector<Holiday> out;
    // New Year's Day: a Saturday Jan 1 is not observed (the Friday before is
    // in the previous year and trades). A Sunday Jan 1 → Monday Jan 2.
    const QDate ny(y, 1, 1);
    if (ny.dayOfWeek() != 6)
        out.append({observed(ny), QStringLiteral("New Year's Day")});
    out.append({nth_weekday(y, 1, 1, 3), QStringLiteral("Martin Luther King Jr. Day")});
    out.append({nth_weekday(y, 2, 1, 3), QStringLiteral("Washington's Birthday")});
    out.append({easter(y).addDays(-2), QStringLiteral("Good Friday")});
    out.append({nth_weekday(y, 5, 1, -1), QStringLiteral("Memorial Day")});
    if (y >= 2022)
        out.append({observed(QDate(y, 6, 19)), QStringLiteral("Juneteenth")});
    out.append({observed(QDate(y, 7, 4)), QStringLiteral("Independence Day")});
    out.append({nth_weekday(y, 9, 1, 1), QStringLiteral("Labor Day")});
    out.append({nth_weekday(y, 11, 4, 4), QStringLiteral("Thanksgiving Day")});
    out.append({observed(QDate(y, 12, 25)), QStringLiteral("Christmas Day")});
    return out;
}

inline bool is_holiday(const QDate& d, QString* name = nullptr) {
    if (!d.isValid())
        return false;
    for (const auto& h : nyse_holidays(d.year())) {
        if (h.date == d) {
            if (name)
                *name = h.name;
            return true;
        }
    }
    return false;
}

inline bool is_trading_day(const QDate& d) {
    return d.isValid() && d.dayOfWeek() <= 5 && !is_holiday(d);
}

/// 13:00 ET close. `why` gets the occasion.
inline bool is_early_close(const QDate& d, QString* why = nullptr) {
    if (!is_trading_day(d))
        return false;
    const int y = d.year();
    if (d == nth_weekday(y, 11, 4, 4).addDays(1)) {
        if (why)
            *why = QStringLiteral("day after Thanksgiving");
        return true;
    }
    if (d.month() == 12 && d.day() == 24) {
        if (why)
            *why = QStringLiteral("Christmas Eve");
        return true;
    }
    if (d.month() == 7 && d.day() == 3) {
        if (why)
            *why = QStringLiteral("eve of Independence Day");
        return true;
    }
    return false;
}

/// Monthly equity-options expiration: 3rd Friday, or the Thursday before
/// when that Friday is a holiday.
inline QDate monthly_opex(int y, int month) {
    QDate d = nth_weekday(y, month, 5, 3);
    while (!is_trading_day(d))
        d = d.addDays(-1);
    return d;
}

inline QDate last_trading_day(int y, int month) {
    QDate d(y, month, QDate(y, month, 1).daysInMonth());
    while (!is_trading_day(d))
        d = d.addDays(-1);
    return d;
}

struct MarketDate {
    enum class Kind { Holiday, EarlyClose, Opex, QuadWitching, MonthEnd, QuarterEnd };
    QDate date;
    Kind kind = Kind::Holiday;
    QString title;  ///< "Market closed — Thanksgiving Day", "Quad witching"…
    QString detail;
};

/// Every market-structure date in [from, to], chronological.
inline QVector<MarketDate> market_dates(const QDate& from, const QDate& to) {
    QVector<MarketDate> out;
    if (!from.isValid() || !to.isValid() || to < from)
        return out;
    for (int y = from.year(); y <= to.year(); ++y) {
        for (const auto& h : nyse_holidays(y)) {
            if (h.date >= from && h.date <= to)
                out.append({h.date, MarketDate::Kind::Holiday, QStringLiteral("US markets closed — %1").arg(h.name),
                            QStringLiteral("NYSE and Nasdaq closed all day (%1). Bond market (SIFMA) schedule may "
                                           "differ.")
                                .arg(h.name)});
        }
        for (int m = 1; m <= 12; ++m) {
            const QDate first(y, m, 1);
            if (first > to || QDate(y, m, first.daysInMonth()) < from)
                continue;
            // Early closes in this month.
            for (QDate d = first; d.month() == m; d = d.addDays(1)) {
                QString why;
                if (d >= from && d <= to && is_early_close(d, &why))
                    out.append({d, MarketDate::Kind::EarlyClose, QStringLiteral("Early close 13:00 ET"),
                                QStringLiteral("NYSE closes at 13:00 ET (%1); eligible options at 13:15 ET.")
                                    .arg(why)});
            }
            const QDate ox = monthly_opex(y, m);
            const bool quad = m % 3 == 0;
            if (ox >= from && ox <= to)
                out.append({ox, quad ? MarketDate::Kind::QuadWitching : MarketDate::Kind::Opex,
                            quad ? QStringLiteral("Quad witching — quarterly expiration")
                                 : QStringLiteral("Monthly options expiration"),
                            quad ? QStringLiteral("Stock options, index options, index futures and single-stock "
                                                  "futures expire together; S&P index rebalances take effect at "
                                                  "this close.")
                                 : QStringLiteral("Standard monthly equity and index options expire (3rd Friday).")});
            const QDate last = last_trading_day(y, m);
            if (last >= from && last <= to)
                out.append({last, quad ? MarketDate::Kind::QuarterEnd : MarketDate::Kind::MonthEnd,
                            quad ? QStringLiteral("Quarter end — last trading day of Q%1").arg(m / 3)
                                 : QStringLiteral("Month end — last trading day"),
                            QStringLiteral("Last trading session of %1; window-dressing and rebalancing flows.")
                                .arg(QLocale(QLocale::English).toString(first, QStringLiteral("MMMM yyyy")))});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const MarketDate& a, const MarketDate& b) { return a.date < b.date; });
    return out;
}

} // namespace finterm::market_calendar
