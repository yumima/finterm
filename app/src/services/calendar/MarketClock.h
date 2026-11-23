#pragma once
// MarketClock.h — the US equity market's clock (NYSE / Nasdaq share the
// session): the time in New York, whether the regular session is open, and
// how long until it closes — or, when it is shut, until it next opens.
//
// Regular session 09:30–16:00 ET (13:00 on an early close); trading days,
// holidays and early closes from MarketCalendar. Pure: the caller passes the
// instant, so tests pin it. Extended hours are not "open" here — the clock
// counts to the regular bell, which is what "time to close" means on a desk.

#include "services/calendar/MarketCalendar.h"

#include <QDateTime>
#include <QString>
#include <QTime>
#include <QTimeZone>

namespace finterm::market_clock {

enum class Session {
    PreOpen, ///< a trading day, before 09:30 ET
    Open,    ///< regular session
    Closed,  ///< after the close, a weekend or a holiday
};

struct Reading {
    bool valid = false;    ///< false when this system has no America/New_York zone
    QDateTime et;          ///< now, in New York
    Session session = Session::Closed;
    qint64 secs_to_event = 0; ///< to the close when Open, else to the next open
    QDateTime next_event;     ///< that close / open, in New York
    bool early_close = false; ///< today closes at 13:00
};

inline const QTimeZone& new_york() {
    static const QTimeZone tz("America/New_York");
    return tz;
}

inline QTime open_time() {
    return QTime(9, 30);
}

inline QTime close_time(const QDate& d) {
    return market_calendar::is_early_close(d) ? QTime(13, 0) : QTime(16, 0);
}

/// The first trading day on or after `d`.
inline QDate next_trading_day(QDate d) {
    for (int i = 0; i < 15 && !market_calendar::is_trading_day(d); ++i)
        d = d.addDays(1);
    return d;
}

inline Reading read(const QDateTime& now) {
    Reading r;
    if (!new_york().isValid())
        return r; // no tz database entry: no clock rather than a wrong one
    r.et = now.toTimeZone(new_york());
    r.valid = r.et.isValid();
    if (!r.valid)
        return r;
    const QDate today = r.et.date();
    r.early_close = market_calendar::is_early_close(today);
    if (market_calendar::is_trading_day(today)) {
        const QDateTime open(today, open_time(), new_york());
        const QDateTime close(today, close_time(today), new_york());
        if (r.et < open) {
            r.session = Session::PreOpen;
            r.next_event = open;
        } else if (r.et < close) {
            r.session = Session::Open;
            r.next_event = close;
        }
    }
    if (!r.next_event.isValid()) {
        // After today's close, or no session today (before-open is PreOpen):
        // the next trading day's open.
        r.session = Session::Closed;
        r.next_event = QDateTime(next_trading_day(today.addDays(1)), open_time(), new_york());
    }
    // Rounded up: beside the time shown (to the second, truncated) the
    // countdown lands on the bell — 13:12:05.9 shows 02:47:55, not :54.
    r.secs_to_event = std::max<qint64>(0, (r.et.msecsTo(r.next_event) + 999) / 1000);
    return r;
}

/// "02:47:55", or "1d 19:17:55" past a day.
inline QString countdown(qint64 secs) {
    const qint64 d = secs / 86400;
    secs %= 86400;
    const QString hms = QStringLiteral("%1:%2:%3")
                            .arg(secs / 3600, 2, 10, QLatin1Char('0'))
                            .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
                            .arg(secs % 60, 2, 10, QLatin1Char('0'));
    return d > 0 ? QStringLiteral("%1d %2").arg(d).arg(hms) : hms;
}

} // namespace finterm::market_clock
