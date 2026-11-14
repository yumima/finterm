#pragma once
// MarketSessions.h — exchange session state computed locally from each
// exchange's own IANA time zone: OPEN / PRE / BREAK / POST / CLOSED and the
// time to the next open or close. Pure (Qt Core only) so it is unit-tested
// (tests/screens/test_world_markets.cpp).
//
// LIMIT: exchange HOLIDAYS are not modelled. The repo has no C++ exchange
// holiday calendar and no non-US one at all (scripts/exchange_sessions.py has
// a US-only Python calendar for its day-change references). On a holiday an
// exchange shows its normal weekday state; half-days are not modelled either.
// Every surface that shows a state says so in its tooltip.

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <algorithm>

namespace finterm::screens::world {

enum class Phase { Closed, Pre, Open, Break, Post };

/// How an exchange's week is shaped.
enum class WeekShape {
    Cash,   ///< Mon–Fri, one or two regular segments plus optional PRE / POST windows
    Globex, ///< CME Globex: Sun 17:00 – Fri 16:00 Chicago, daily halt 16:00–17:00
    Fx24,   ///< spot FX: Sun 17:00 – Fri 17:00 New York, no daily halt
    Always, ///< crypto: never closes
};

/// Local times are minutes after the exchange's local midnight; -1 = absent.
struct ExchangeHours {
    const char* id;    ///< "NYC"
    const char* city;  ///< "NEW YORK" — what the strip shows
    const char* venue; ///< "NYSE / Nasdaq"
    const char* tz;    ///< IANA zone the hours are written in
    WeekShape shape = WeekShape::Cash;
    int pre = -1; ///< PRE runs pre .. open1
    int open1 = -1, close1 = -1;
    int open2 = -1, close2 = -1; ///< second segment after a lunch BREAK
    int post = -1;               ///< POST runs (last close) .. post
    /// B3 moves its close by an hour with US daylight saving: close1 applies
    /// while New York is on DST, this one while New York is on standard time.
    int close1_us_std = -1;
};

constexpr int hm(int h, int m) {
    return h * 60 + m;
}

/// Every exchange a WORLD row or the sessions strip refers to.
inline const QVector<ExchangeHours>& exchange_table() {
    static const QVector<ExchangeHours> v = {
        // Asia-Pacific
        {"TYO", "TOKYO", "TSE", "Asia/Tokyo", WeekShape::Cash, -1, hm(9, 0), hm(11, 30), hm(12, 30), hm(15, 30)},
        {"HKG", "HONG KONG", "HKEX", "Asia/Hong_Kong", WeekShape::Cash, hm(9, 0), hm(9, 30), hm(12, 0), hm(13, 0),
         hm(16, 0), hm(16, 10)}, // pre-opening session 09:00, closing auction to 16:10
        {"SHA", "SHANGHAI", "SSE", "Asia/Shanghai", WeekShape::Cash, hm(9, 15), hm(9, 30), hm(11, 30), hm(13, 0),
         hm(15, 0)}, // opening call auction from 09:15
        {"SYD", "SYDNEY", "ASX", "Australia/Sydney", WeekShape::Cash, hm(7, 0), hm(10, 0), hm(16, 0)},
        {"SEO", "SEOUL", "KRX", "Asia/Seoul", WeekShape::Cash, -1, hm(9, 0), hm(15, 30)},
        {"TPE", "TAIPEI", "TWSE", "Asia/Taipei", WeekShape::Cash, -1, hm(9, 0), hm(13, 30)},
        // Europe
        {"FRA", "FRANKFURT", "Xetra", "Europe/Berlin", WeekShape::Cash, -1, hm(9, 0), hm(17, 30)},
        {"LON", "LONDON", "LSE", "Europe/London", WeekShape::Cash, hm(7, 50), hm(8, 0), hm(16, 30), -1, -1,
         hm(16, 35)}, // opening auction 07:50, closing auction to 16:35
        {"PAR", "PARIS", "Euronext Paris", "Europe/Paris", WeekShape::Cash, hm(7, 15), hm(9, 0), hm(17, 30), -1, -1,
         hm(17, 35)}, // pre-opening from 07:15, closing auction to 17:35
        {"AMS", "AMSTERDAM", "Euronext Amsterdam", "Europe/Amsterdam", WeekShape::Cash, -1, hm(9, 0), hm(17, 30)},
        {"MAD", "MADRID", "BME", "Europe/Madrid", WeekShape::Cash, -1, hm(9, 0), hm(17, 30)},
        {"MIL", "MILAN", "Borsa Italiana", "Europe/Rome", WeekShape::Cash, -1, hm(9, 0), hm(17, 30)},
        {"ZRH", "ZURICH", "SIX", "Europe/Zurich", WeekShape::Cash, -1, hm(9, 0), hm(17, 30)},
        // Americas
        {"NYC", "NEW YORK", "NYSE / Nasdaq", "America/New_York", WeekShape::Cash, hm(4, 0), hm(9, 30), hm(16, 0), -1,
         -1, hm(20, 0)}, // extended hours 04:00 – 20:00
        {"TOR", "TORONTO", "TSX", "America/Toronto", WeekShape::Cash, -1, hm(9, 30), hm(16, 0)},
        {"SAO", "SAO PAULO", "B3", "America/Sao_Paulo", WeekShape::Cash, -1, hm(10, 0), hm(17, 0), -1, -1, -1,
         hm(18, 0)},
        {"CME", "CME GLOBEX", "CME Globex equity-index futures", "America/Chicago", WeekShape::Globex},
        {"FX", "FX", "Spot FX (interbank)", "America/New_York", WeekShape::Fx24},
        {"24/7", "CRYPTO", "Crypto (always trading)", "UTC", WeekShape::Always},
    };
    return v;
}

inline const ExchangeHours* find_exchange(const QString& id) {
    for (const auto& e : exchange_table())
        if (id == QLatin1String(e.id))
            return &e;
    return nullptr;
}

/// The strip's order: Asia, Europe, Americas, then the futures clock.
inline QStringList strip_exchange_ids() {
    return {"TYO", "HKG", "SHA", "SYD", "FRA", "LON", "PAR", "NYC", "CME"};
}

inline QString region_of(const QString& id) {
    static const QStringList asia{"TYO", "HKG", "SHA", "SYD", "SEO", "TPE"};
    static const QStringList europe{"FRA", "LON", "PAR", "AMS", "MAD", "MIL", "ZRH"};
    if (asia.contains(id))
        return QStringLiteral("ASIA");
    if (europe.contains(id))
        return QStringLiteral("EUROPE");
    return QStringLiteral("AMERICAS");
}

struct Span {
    int start = 0; ///< minutes after local midnight
    int end = 0;   ///< exclusive; 1440 = the next midnight
    Phase phase = Phase::Closed;
};

/// The non-closed spans of one LOCAL calendar day.
inline QVector<Span> spans_on(const ExchangeHours& ex, const QDate& local_day) {
    QVector<Span> out;
    const int dow = local_day.dayOfWeek(); // 1 = Mon .. 7 = Sun
    switch (ex.shape) {
    case WeekShape::Always:
        out.append({0, 1440, Phase::Open});
        return out;
    case WeekShape::Globex:
        // A CME trading day opens 17:00 the evening before; 16:00–17:00 is
        // the daily maintenance halt; Friday 16:00 to Sunday 17:00 is shut.
        if (dow >= 1 && dow <= 5)
            out.append({0, hm(16, 0), Phase::Open});
        if (dow >= 1 && dow <= 4)
            out.append({hm(16, 0), hm(17, 0), Phase::Break});
        if (dow == 7 || (dow >= 1 && dow <= 4))
            out.append({hm(17, 0), 1440, Phase::Open});
        return out;
    case WeekShape::Fx24:
        if (dow == 7)
            out.append({hm(17, 0), 1440, Phase::Open});
        else if (dow >= 1 && dow <= 4)
            out.append({0, 1440, Phase::Open});
        else if (dow == 5)
            out.append({0, hm(17, 0), Phase::Open});
        return out;
    case WeekShape::Cash:
        break;
    }
    if (dow > 5)
        return out;
    int close1 = ex.close1;
    if (ex.close1_us_std >= 0) {
        const QTimeZone ny("America/New_York");
        if (!ny.isDaylightTime(QDateTime(local_day, QTime(12, 0), ny)))
            close1 = ex.close1_us_std;
    }
    if (ex.pre >= 0)
        out.append({ex.pre, ex.open1, Phase::Pre});
    out.append({ex.open1, close1, Phase::Open});
    int last_close = close1;
    if (ex.open2 >= 0) {
        out.append({close1, ex.open2, Phase::Break});
        out.append({ex.open2, ex.close2, Phase::Open});
        last_close = ex.close2;
    }
    if (ex.post >= 0)
        out.append({last_close, ex.post, Phase::Post});
    return out;
}

struct SessionInfo {
    Phase phase = Phase::Closed;
    QDateTime local_now; ///< now in the exchange's zone
    /// When the current phase ends (any phase), and what follows it.
    QDateTime phase_end_utc;
    Phase next_phase = Phase::Closed;
    /// The next start of a regular OPEN segment strictly after now (invalid
    /// for an exchange that never closes).
    QDateTime next_open_utc;
};

/// The exchange's state at `now_utc`. Looks up to 10 days ahead, which covers
/// every weekend; holidays are not known (see the file comment).
inline SessionInfo session_at(const ExchangeHours& ex, const QDateTime& now_utc) {
    const QTimeZone tz(QByteArray(ex.tz));
    SessionInfo info;
    info.local_now = now_utc.toTimeZone(tz);
    if (ex.shape == WeekShape::Always) {
        info.phase = Phase::Open; // never closes: no phase end, no next open
        return info;
    }

    struct Abs {
        QDateTime start, end;
        Phase phase;
    };
    auto at = [&tz](const QDate& d, int minutes) {
        if (minutes >= 1440)
            return QDateTime(d.addDays(1), QTime(0, 0), tz).toUTC();
        return QDateTime(d, QTime(minutes / 60, minutes % 60), tz).toUTC();
    };
    // Spans on an absolute timeline, adjacent same-phase spans merged (a
    // Globex session crosses midnight; FX runs Monday through Thursday).
    QVector<Abs> line;
    const QDate today = info.local_now.date();
    for (int i = -1; i <= 10; ++i) {
        const QDate d = today.addDays(i);
        for (const Span& s : spans_on(ex, d)) {
            Abs a{at(d, s.start), at(d, s.end), s.phase};
            if (!line.isEmpty() && line.last().phase == a.phase && line.last().end == a.start)
                line.last().end = a.end;
            else
                line.append(a);
        }
    }
    const QDateTime now = now_utc.toUTC();
    for (int i = 0; i < line.size(); ++i) {
        const Abs& a = line[i];
        if (a.start <= now && now < a.end) {
            info.phase = a.phase;
            info.phase_end_utc = a.end;
            info.next_phase =
                (i + 1 < line.size() && line[i + 1].start == a.end) ? line[i + 1].phase : Phase::Closed;
            break;
        }
    }
    if (info.phase == Phase::Closed) {
        // Closed until the next span of any kind begins.
        for (const Abs& a : line)
            if (a.start > now) {
                info.phase_end_utc = a.start;
                info.next_phase = a.phase;
                break;
            }
    }
    for (const Abs& a : line)
        if (a.phase == Phase::Open && a.start > now) {
            info.next_open_utc = a.start;
            break;
        }
    return info;
}

inline QString phase_label(Phase p) {
    switch (p) {
    case Phase::Open:
        return QStringLiteral("OPEN");
    case Phase::Pre:
        return QStringLiteral("PRE");
    case Phase::Post:
        return QStringLiteral("POST");
    case Phase::Break:
        return QStringLiteral("BREAK");
    case Phase::Closed:
        break;
    }
    return QStringLiteral("CLOSED");
}

/// "45m", "2h 14m", "1d 3h". Rounds UP to the minute so a countdown never
/// reads "0m" before the bell.
inline QString format_countdown(qint64 secs) {
    if (secs < 0)
        secs = 0;
    const qint64 mins = (secs + 59) / 60;
    if (mins < 60)
        return QStringLiteral("%1m").arg(mins);
    if (mins < 24 * 60)
        return QStringLiteral("%1h %2m").arg(mins / 60).arg(mins % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1d %2h").arg(mins / (24 * 60)).arg((mins % (24 * 60)) / 60);
}

/// What the strip prints under the state: "closes in 2h 14m",
/// "break in 1h 05m", "opens in 5h 03m". Empty for an exchange that never
/// closes.
inline QString next_change_text(const SessionInfo& s, const QDateTime& now_utc) {
    if (s.phase == Phase::Open) {
        if (!s.phase_end_utc.isValid())
            return {};
        const qint64 secs = now_utc.secsTo(s.phase_end_utc);
        const QString verb = s.next_phase == Phase::Break ? QStringLiteral("break in") : QStringLiteral("closes in");
        return verb + QLatin1Char(' ') + format_countdown(secs);
    }
    if (!s.next_open_utc.isValid())
        return {};
    return QStringLiteral("opens in ") + format_countdown(now_utc.secsTo(s.next_open_utc));
}

/// "09:00–11:30, 12:30–15:30 Asia/Tokyo, Mon–Fri" for tooltips.
inline QString hours_text(const ExchangeHours& ex) {
    auto t = [](int m) {
        return QStringLiteral("%1:%2").arg(m / 60, 2, 10, QLatin1Char('0')).arg(m % 60, 2, 10, QLatin1Char('0'));
    };
    switch (ex.shape) {
    case WeekShape::Always:
        return QStringLiteral("Trades around the clock, every day.");
    case WeekShape::Globex:
        return QStringLiteral("Sun 17:00 – Fri 16:00 America/Chicago; daily halt 16:00–17:00 Mon–Thu.");
    case WeekShape::Fx24:
        return QStringLiteral("Sun 17:00 – Fri 17:00 America/New_York, around the clock in between.");
    case WeekShape::Cash:
        break;
    }
    QString s = t(ex.open1) + QStringLiteral("–") + t(ex.close1);
    if (ex.close1_us_std >= 0)
        s += QStringLiteral(" (to %1 while New York is on standard time)").arg(t(ex.close1_us_std));
    if (ex.open2 >= 0)
        s += QStringLiteral(", ") + t(ex.open2) + QStringLiteral("–") + t(ex.close2);
    s += QStringLiteral(" %1, Mon–Fri").arg(QString::fromLatin1(ex.tz));
    if (ex.pre >= 0)
        s += QStringLiteral("\nPRE from %1").arg(t(ex.pre));
    if (ex.post >= 0)
        s += QStringLiteral("\nPOST until %1").arg(t(ex.post));
    return s;
}

} // namespace finterm::screens::world
