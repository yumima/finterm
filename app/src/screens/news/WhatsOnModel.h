#pragma once
// WhatsOnModel.h — the pure half of the News screen's WHAT'S ON drawer.
//
// Turns the app's existing free sources into one dated list and buckets it
// into TODAY / THIS WEEK / THIS MONTH / UPCOMING:
//   * macro releases — ForexFactory's weekly calendar (MacroCalendar.h),
//     filtered to top-tier US releases by default;
//   * FOMC decision days — the Fed's published meeting calendar;
//   * IPO pricings — the IPO Watch calendar (ipo_calendar.py deals).
// Header-only and Qt-Core-only; tests/screens/test_whats_on_model.cpp.

#include "services/calendar/MacroCalendar.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QStringList>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <algorithm>

namespace finterm::whats_on {

namespace mc = finterm::macro_calendar;

struct Item {
    enum class Source { Macro, Fomc, Ipo };
    Source source = Source::Macro;
    QDateTime when; ///< instant; for untimed items, local midnight of the day
    bool timed = false;
    QString kind;   ///< FED / LABOR / INFLATION / GROWTH / MACRO / IPO
    QString title;  ///< main text ("CPI m/m", "Acme Corp (ACME)")
    QString figures; ///< "F 0.3% · P 0.4%", "$15.00-17.00 · $200M" — may be empty
    QString detail; ///< multi-line detail shown on click
    QString key;    ///< IPO: ticker, else company (fallback IPO Watch selection key)
    QString deal_id; ///< IPO: the calendar's deal id — tickers repeat across re-filings

    QDate local_date() const { return when.toLocalTime().date(); }
};

inline QString macro_figures(const mc::MacroEvent& e) {
    QStringList parts;
    if (!e.actual.isEmpty())
        parts << QStringLiteral("A %1").arg(e.actual);
    if (!e.forecast.isEmpty())
        parts << QStringLiteral("F %1").arg(e.forecast);
    if (!e.previous.isEmpty())
        parts << QStringLiteral("P %1").arg(e.previous);
    return parts.join(QStringLiteral(" · "));
}

/// Macro events from today onward; `top_tier_us_only` applies the default
/// importance filter (mc::is_top_tier_us).
inline QVector<Item> items_from_macro(const QVector<mc::MacroEvent>& events, const QDateTime& now,
                                      bool top_tier_us_only) {
    QVector<Item> out;
    for (const auto& e : mc::today_onward(events, now)) {
        if (top_tier_us_only && !mc::is_top_tier_us(e))
            continue;
        if (e.importance == 0 && top_tier_us_only)
            continue; // holidays
        Item it;
        it.source = Item::Source::Macro;
        it.when = e.when;
        it.timed = e.timed;
        it.kind = mc::kind_of(e.title);
        if (it.kind.isEmpty())
            it.kind = QStringLiteral("MACRO");
        it.title = e.country == QLatin1String("USD") ? e.title : e.country + QLatin1Char(' ') + e.title;
        it.figures = macro_figures(e);
        const QString imp = e.importance >= 3 ? QStringLiteral("High") : e.importance == 2 ? QStringLiteral("Medium")
                                                                     : e.importance == 1  ? QStringLiteral("Low")
                                                                                          : QStringLiteral("Holiday");
        it.detail = QStringLiteral("%1 — %2\n%3 · impact %4\nActual %5 · Forecast %6 · Previous %7\nSource: ForexFactory "
                                   "weekly calendar")
                        .arg(e.country, e.title,
                             e.when.toLocalTime().toString(e.timed ? QStringLiteral("ddd d MMM yyyy HH:mm t")
                                                                   : QStringLiteral("ddd d MMM yyyy")),
                             imp, e.actual.isEmpty() ? QStringLiteral("—") : e.actual,
                             e.forecast.isEmpty() ? QStringLiteral("—") : e.forecast,
                             e.previous.isEmpty() ? QStringLiteral("—") : e.previous);
        out.append(it);
    }
    return out;
}

/// The next `max_count` FOMC decision days from `today` on (the Fed page
/// lists a year and more ahead; the rest would bury UPCOMING). The statement
/// is released at 14:00 Eastern. A day the macro calendar already covers with
/// a rate-decision row ("Federal Funds Rate", "FOMC Statement") is not listed
/// twice.
inline QVector<Item> items_from_fomc(const QVector<QDate>& decisions, const QDate& today,
                                     const QVector<Item>& macro_items, int max_count = 2) {
    QVector<Item> out;
    int taken = 0;
    for (const QDate& d : decisions) {
        if (d < today)
            continue;
        if (taken++ >= max_count)
            break;
        const QDateTime when(d, QTime(14, 0), QTimeZone("America/New_York"));
        const bool covered = std::any_of(macro_items.begin(), macro_items.end(), [&](const Item& m) {
            return m.local_date() == when.toLocalTime().date() &&
                   (m.title.contains(QLatin1String("Federal Funds Rate")) ||
                    m.title.contains(QLatin1String("FOMC Statement")));
        });
        if (covered)
            continue;
        Item it;
        it.source = Item::Source::Fomc;
        it.when = when;
        it.timed = true;
        it.kind = QStringLiteral("FED");
        it.title = QStringLiteral("FOMC rate decision");
        it.detail = QStringLiteral("FOMC meeting ends %1; statement 14:00 ET (%2 local)\nSource: federalreserve.gov "
                                   "meeting calendar")
                        .arg(QLocale(QLocale::English).toString(d, QStringLiteral("ddd d MMM yyyy")),
                             when.toLocalTime().toString(QStringLiteral("HH:mm")));
        out.append(it);
    }
    return out;
}

/// Expected IPO pricings (status "upcoming") dated today or later, from the
/// ipo_calendar.py "deals" array IPO Watch uses.
inline QVector<Item> items_from_ipo_deals(const QJsonArray& deals, const QDate& today) {
    QVector<Item> out;
    for (const auto& v : deals) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("status")).toString() != QLatin1String("upcoming"))
            continue;
        const QDate d = QDate::fromString(o.value(QLatin1String("expected_date")).toString(), Qt::ISODate);
        if (!d.isValid() || d < today)
            continue;
        const QString company = o.value(QLatin1String("company")).toString().trimmed();
        const QString ticker = o.value(QLatin1String("ticker")).toString().trimmed();
        if (company.isEmpty())
            continue;
        Item it;
        it.source = Item::Source::Ipo;
        it.when = QDateTime(d, QTime(0, 0)); // local day; pricing time is not published
        it.timed = false;
        it.kind = QStringLiteral("IPO");
        it.title = ticker.isEmpty() ? company : QStringLiteral("%1 (%2)").arg(company, ticker);
        const QString range = o.value(QLatin1String("price_range")).toString().trimmed();
        const QString exch = o.value(QLatin1String("exchange")).toString().trimmed();
        QStringList fig;
        if (!range.isEmpty())
            fig << QStringLiteral("$") + range;
        if (!exch.isEmpty())
            fig << exch;
        it.figures = fig.join(QStringLiteral(" · "));
        it.key = ticker.isEmpty() ? company : ticker;
        it.deal_id = o.value(QLatin1String("deal_id")).toString();
        it.detail = QStringLiteral("Expected to price %1").arg(QLocale(QLocale::English).toString(d, "ddd d MMM yyyy"));
        out.append(it);
    }
    return out;
}

/// Items grouped by horizon, each group chronological; past items dropped.
inline QMap<mc::Bucket, QVector<Item>> bucketize(QVector<Item> items, const QDate& today) {
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.local_date() != b.local_date())
            return a.local_date() < b.local_date();
        // Untimed (all-day) items first within a day, then by clock.
        if (a.timed != b.timed)
            return !a.timed;
        return a.when < b.when;
    });
    QMap<mc::Bucket, QVector<Item>> out;
    for (auto& it : items) {
        const auto b = mc::bucket_for(it.local_date(), today);
        if (b != mc::Bucket::Past)
            out[b].append(std::move(it));
    }
    return out;
}

} // namespace finterm::whats_on
