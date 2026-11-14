#pragma once
// MacroCalendar.h — economic-calendar parsing, filtering and date bucketing.
//
// Shared by the dashboard's ECONOMIC CALENDAR widget and the News screen's
// WHAT'S ON drawer. Header-only, Qt-Core-only, every function pure (the
// current time is always a parameter), so tests/services/test_macro_calendar
// can pin the rules.
//
// Source: ForexFactory's free weekly JSON (nfs.faireconomy.media). Its "date"
// is an ISO-8601 instant with offset ("2026-11-08T17:30:00-05:00"). The widget
// used to parse it as "MMM d yyyy" — every date came back invalid, so the raw
// ISO string was displayed, the chronological sort compared invalid dates
// (a no-op), and the first 25 rows of the week (Sunday's) showed all week.

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTimeZone>
#include <QVector>

#include <algorithm>

namespace finterm::macro_calendar {

struct MacroEvent {
    QString title;
    QString country;  ///< currency code as the source gives it ("USD", "EUR")
    QDateTime when;   ///< the event instant (invalid when the source gave none)
    bool timed = true; ///< false for all-day / tentative items (no clock time)
    int importance = 0; ///< 3 high, 2 medium, 1 low, 0 holiday/unknown
    QString forecast, previous, actual;
};

/// "High"/"Medium"/"Low"/"Holiday" → 3/2/1/0.
inline int importance_from_label(const QString& s) {
    if (s.startsWith(QLatin1Char('H'), Qt::CaseInsensitive) && !s.startsWith(QLatin1String("Hol"), Qt::CaseInsensitive))
        return 3;
    if (s.startsWith(QLatin1Char('M'), Qt::CaseInsensitive))
        return 2;
    if (s.startsWith(QLatin1Char('L'), Qt::CaseInsensitive))
        return 1;
    return 0;
}

/// One ForexFactory row. Accepts the ISO instant the feed sends today and,
/// for robustness, the older "Nov 13 2026" + separate "time" form.
inline MacroEvent event_from_ff(const QJsonObject& o) {
    MacroEvent e;
    e.title = o.value(QLatin1String("title")).toString().trimmed();
    e.country = o.value(QLatin1String("country")).toString().trimmed().toUpper();
    e.importance = importance_from_label(o.value(QLatin1String("impact")).toString());
    e.forecast = o.value(QLatin1String("forecast")).toString().trimmed();
    e.previous = o.value(QLatin1String("previous")).toString().trimmed();
    e.actual = o.value(QLatin1String("actual")).toString().trimmed();
    const QString date = o.value(QLatin1String("date")).toString().trimmed();
    e.when = QDateTime::fromString(date, Qt::ISODate);
    if (!e.when.isValid()) {
        const QDate d = QLocale(QLocale::English).toDate(date, QStringLiteral("MMM d yyyy"));
        const QTime t = QLocale(QLocale::English).toTime(o.value(QLatin1String("time")).toString().trimmed(),
                                                         QStringLiteral("h:mmap"));
        if (d.isValid()) {
            e.timed = t.isValid();
            // The old feed's times were US Eastern.
            e.when = QDateTime(d, t.isValid() ? t : QTime(0, 0), QTimeZone("America/New_York"));
        }
    }
    // The feed marks all-day and tentative items with midnight local to the
    // feed's zone and a "Holiday"/"All Day"/"Tentative" time in the old form.
    const QString time_label = o.value(QLatin1String("time")).toString();
    if (time_label.contains(QLatin1String("Day"), Qt::CaseInsensitive) ||
        time_label.contains(QLatin1String("Tentative"), Qt::CaseInsensitive) || e.importance == 0)
        e.timed = false;
    return e;
}

inline QVector<MacroEvent> parse_ff(const QJsonArray& rows) {
    QVector<MacroEvent> out;
    out.reserve(rows.size());
    for (const auto& v : rows) {
        MacroEvent e = event_from_ff(v.toObject());
        if (!e.title.isEmpty())
            out.append(std::move(e));
    }
    return out;
}

/// Kind of a US release, for grouping and colour. Empty when none applies.
inline QString kind_of(const QString& title) {
    const QString t = title.toLower();
    if (t.contains(QLatin1String("fomc")) || t.contains(QLatin1String("federal funds")) ||
        t.contains(QLatin1String("fed chair")) || t.contains(QLatin1String("powell")) ||
        t.contains(QLatin1String("fed ")) || t.contains(QLatin1String("monetary policy")))
        return QStringLiteral("FED");
    if (t.contains(QLatin1String("non-farm")) || t.contains(QLatin1String("nonfarm")) ||
        t.contains(QLatin1String("unemployment")) || t.contains(QLatin1String("jobless")) ||
        t.contains(QLatin1String("payroll")) || t.contains(QLatin1String("jolts")) ||
        t.contains(QLatin1String("average hourly")) || t.contains(QLatin1String("employment")))
        return QStringLiteral("LABOR");
    if (t.contains(QLatin1String("cpi")) || t.contains(QLatin1String("ppi")) || t.contains(QLatin1String("pce")) ||
        t.contains(QLatin1String("inflation")))
        return QStringLiteral("INFLATION");
    if (t.contains(QLatin1String("gdp")) || t.contains(QLatin1String("retail sales")) ||
        t.contains(QLatin1String("ism ")) || t.contains(QLatin1String("pmi")))
        return QStringLiteral("GROWTH");
    return {};
}

/// The default "top-tier US" filter: US high impact, plus the medium-impact
/// releases the WHAT'S ON panel is about (Fed speakers, jobless claims, PPI…).
inline bool is_top_tier_us(const MacroEvent& e) {
    if (e.country != QLatin1String("USD") && e.country != QLatin1String("US"))
        return false;
    if (e.importance >= 3)
        return true;
    return e.importance == 2 && !kind_of(e.title).isEmpty();
}

/// Events from the start of `today` (local) onward, chronological; untimed
/// items sort at the start of their day.
inline QVector<MacroEvent> today_onward(QVector<MacroEvent> events, const QDateTime& now) {
    const QDate today = now.toLocalTime().date();
    QVector<MacroEvent> out;
    for (auto& e : events) {
        if (e.when.isValid() && e.when.toLocalTime().date() >= today)
            out.append(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MacroEvent& a, const MacroEvent& b) { return a.when < b.when; });
    return out;
}

enum class Bucket { Past, Today, ThisWeek, ThisMonth, Upcoming };

/// Mutually exclusive horizon of `d` relative to `today`: TODAY; THIS WEEK =
/// after today through Sunday of today's ISO week; THIS MONTH = after that,
/// still in today's month; UPCOMING = later. A week that runs into next month
/// keeps its days in THIS WEEK.
inline Bucket bucket_for(const QDate& d, const QDate& today) {
    if (!d.isValid() || d < today)
        return Bucket::Past;
    if (d == today)
        return Bucket::Today;
    const QDate week_end = today.addDays(7 - today.dayOfWeek()); // Sunday
    if (d <= week_end)
        return Bucket::ThisWeek;
    if (d.year() == today.year() && d.month() == today.month())
        return Bucket::ThisMonth;
    return Bucket::Upcoming;
}

inline QString bucket_title(Bucket b) {
    switch (b) {
    case Bucket::Today:
        return QStringLiteral("TODAY");
    case Bucket::ThisWeek:
        return QStringLiteral("THIS WEEK");
    case Bucket::ThisMonth:
        return QStringLiteral("THIS MONTH");
    case Bucket::Upcoming:
        return QStringLiteral("UPCOMING");
    case Bucket::Past:
        break;
    }
    return QStringLiteral("EARLIER");
}

/// Short local label: "Today 08:30", "Tue 08:30" (within 6 days), "Nov 20
/// 08:30"; untimed items drop the clock ("Tue", "Nov 20").
inline QString short_when(const QDateTime& when, bool timed, const QDate& today) {
    if (!when.isValid())
        return QStringLiteral("—");
    const QDateTime local = when.toLocalTime();
    const QDate d = local.date();
    const QLocale en(QLocale::English);
    QString day;
    if (d == today)
        day = QStringLiteral("Today");
    else if (d > today && today.daysTo(d) < 7)
        day = en.toString(d, QStringLiteral("ddd"));
    else
        day = en.toString(d, QStringLiteral("MMM d"));
    return timed ? day + QLatin1Char(' ') + local.toString(QStringLiteral("HH:mm")) : day;
}

/// FOMC decision days (the meeting's last day) from the Fed's
/// fomccalendars.htm. Each year's panel lists "<month>" + "27-28" rows; a
/// meeting spanning a month boundary is written "Apr/May" + "30-1". A
/// trailing "*" (Summary of Economic Projections) or "(notation vote)" text is
/// ignored. Returns sorted, de-duplicated dates.
inline QVector<QDate> parse_fomc_calendar(const QString& html) {
    static const QRegularExpression kYear(QStringLiteral("(\\d{4}) FOMC Meetings"));
    static const QRegularExpression kRow(
        QStringLiteral("fomc-meeting__month[^>]*>\\s*<strong>([^<]+)</strong>.*?fomc-meeting__date[^>]*>\\s*([^<]+)<"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QStringList kMonths = {"january", "february", "march",     "april",   "may",      "june",
                                        "july",    "august",   "september", "october", "november", "december"};
    auto month_of = [](QString m) {
        m = m.trimmed().toLower();
        for (int i = 0; i < 12; ++i)
            if (kMonths[i].startsWith(m.left(3)) && m.size() >= 3)
                return i + 1;
        return 0;
    };
    QVector<QDate> out;
    QVector<std::pair<qsizetype, int>> years;
    for (auto it = kYear.globalMatch(html); it.hasNext();) {
        const auto m = it.next();
        years.append({m.capturedStart(), m.captured(1).toInt()});
    }
    std::sort(years.begin(), years.end());
    for (int yi = 0; yi < years.size(); ++yi) {
        const qsizetype from = years[yi].first;
        const qsizetype to = yi + 1 < years.size() ? years[yi + 1].first : html.size();
        const QString seg = html.mid(from, to - from);
        for (auto it = kRow.globalMatch(seg); it.hasNext();) {
            const auto m = it.next();
            const QStringList months = m.captured(1).split(QLatin1Char('/'));
            static const QRegularExpression kDays(QStringLiteral("(\\d{1,2})(?:\\s*-\\s*(\\d{1,2}))?"));
            const auto dm = kDays.match(m.captured(2));
            if (!dm.hasMatch())
                continue;
            const int last_day = dm.captured(2).isEmpty() ? dm.captured(1).toInt() : dm.captured(2).toInt();
            const int mon = month_of(months.last());
            if (mon == 0)
                continue;
            const QDate d(years[yi].second, mon, last_day);
            if (d.isValid())
                out.append(d);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace finterm::macro_calendar
