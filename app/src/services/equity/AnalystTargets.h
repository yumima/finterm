// src/services/equity/AnalystTargets.h
#pragma once
// How current the analysts' price targets are.
//
// The "mean target" is an average of one number per firm, and every one of
// those numbers has its own date. After a sharp fall the mean still carries
// targets set before it, so "+104% upside" beside a STRONG SELL on the tape
// reads as a contradiction when it is partly a lag: IONQ on 2026-11-16 had a
// $54 mean against a $26.6 price — with 4 of 12 targets under a month old,
// every one of the 7 changes in ninety days a cut, and the price down 24%
// since the newest of them.
//
// Checked the same day against a second vendor (Nasdaq/FactSet: mean $57.08,
// low $40, high $70 — Yahoo: $54.28, $40, $70): the consensus shown was
// current. Nothing fresher exists to fetch; analysts had simply not caught
// up with the price. So the gap is shown for what it is.
//
// The consensus is not corrected or re-weighted here: it is the analysts'
// number. What is added is its age and its direction, so the reader can
// weigh it. Pure (QtCore only).

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finterm::services::equity {

struct FirmTarget {
    QString firm;
    QDate date; ///< the day this target was published
    double target = std::numeric_limits<double>::quiet_NaN();
    double prior = std::numeric_limits<double>::quiet_NaN(); ///< the firm's target before; NaN on a first one
    QString action; ///< "Raises" | "Lowers" | "Maintains" | "Announces", as the vendor words it
    QString grade;

    bool raised() const { return std::isfinite(prior) && target > prior; }
    bool lowered() const { return std::isfinite(prior) && target < prior; }
};

inline QVector<FirmTarget> parse_firm_targets(const QJsonObject& o) {
    QVector<FirmTarget> out;
    for (const auto& v : o.value(QLatin1String("firms")).toArray()) {
        const QJsonObject r = v.toObject();
        FirmTarget t;
        t.firm = r.value(QLatin1String("firm")).toString();
        t.date = QDate::fromString(r.value(QLatin1String("date")).toString(), Qt::ISODate);
        if (r.value(QLatin1String("target")).isDouble())
            t.target = r.value(QLatin1String("target")).toDouble();
        if (r.value(QLatin1String("prior")).isDouble())
            t.prior = r.value(QLatin1String("prior")).toDouble();
        t.action = r.value(QLatin1String("action")).toString();
        t.grade = r.value(QLatin1String("grade")).toString();
        if (!t.firm.isEmpty() && t.date.isValid() && t.target > 0)
            out.append(t);
    }
    return out;
}

struct TargetFreshness {
    int firms = 0;
    int newest_days = -1; ///< age of the most recent target
    int fresh_30d = 0;  ///< targets published in the last 30 days
    int raised_90d = 0; ///< firms whose latest target, set in the last 90 days, was a raise
    int lowered_90d = 0;
    QDate newest_date;
    /// Mean of the targets published in the last 30 days; NaN with fewer than
    /// three — two opinions are not a consensus.
    double fresh_mean = std::numeric_limits<double>::quiet_NaN();
    /// The share price move since the newest target was published, percent;
    /// NaN without the two prices.
    double price_since_newest_pct = std::numeric_limits<double>::quiet_NaN();
    bool valid() const { return firms > 0; }
};

/// `close_at_newest` is the last close the analyst behind the newest target
/// had seen when it was published (NaN if not known).
inline TargetFreshness target_freshness(const QVector<FirmTarget>& targets, const QDate& today, double price_now,
                                        double close_at_newest) {
    TargetFreshness f;
    QVector<int> ages;
    double fresh_sum = 0;
    for (const auto& t : targets) {
        // Dated in New York; a reader a time zone ahead or behind is at most
        // a day out, and a target is never from the future.
        const int age = std::max(0, static_cast<int>(t.date.daysTo(today)));
        ages.append(age);
        if (age <= 30) {
            ++f.fresh_30d;
            fresh_sum += t.target;
        }
        if (age <= 90) {
            f.raised_90d += t.raised() ? 1 : 0;
            f.lowered_90d += t.lowered() ? 1 : 0;
        }
        if (!f.newest_date.isValid() || t.date > f.newest_date)
            f.newest_date = t.date;
    }
    f.firms = ages.size();
    if (ages.isEmpty())
        return f;
    f.newest_days = *std::min_element(ages.begin(), ages.end());
    if (f.fresh_30d >= 3)
        f.fresh_mean = fresh_sum / f.fresh_30d;
    if (std::isfinite(close_at_newest) && close_at_newest > 0 && std::isfinite(price_now) && price_now > 0)
        f.price_since_newest_pct = (price_now - close_at_newest) / close_at_newest * 100.0;
    return f;
}

/// A fall this large since the newest target means the published upside is
/// measured from a price that no longer exists.
inline constexpr double kTargetsOvertakenPct = 15.0;
inline bool targets_overtaken(const TargetFreshness& f) {
    return std::isfinite(f.price_since_newest_pct) && std::abs(f.price_since_newest_pct) >= kTargetsOvertakenPct;
}

/// The card's lines, in plain words. Empty when there is nothing to say.
inline QStringList target_age_lines(const TargetFreshness& f) {
    QStringList out;
    if (!f.valid())
        return out;
    const QString ago = f.newest_days == 0   ? QStringLiteral("today")
                        : f.newest_days == 1 ? QStringLiteral("yesterday")
                                             : QStringLiteral("%1 days ago").arg(f.newest_days);
    out << QStringLiteral("Newest target %1 (%2) · %3 of %4 firms updated in the last 30 days")
               .arg(QLocale::c().toString(f.newest_date, QStringLiteral("d MMM")), ago)
               .arg(f.fresh_30d)
               .arg(f.firms);
    if (f.raised_90d + f.lowered_90d > 0)
        out << QStringLiteral("Last 90 days: %1 cut, %2 raised").arg(f.lowered_90d).arg(f.raised_90d);
    if (std::isfinite(f.price_since_newest_pct) && std::abs(f.price_since_newest_pct) >= 1.0)
        out << QStringLiteral("Shares %1%2% since the newest target%3")
                   .arg(f.price_since_newest_pct > 0 ? QStringLiteral("+") : QString())
                   .arg(f.price_since_newest_pct, 0, 'f', 1)
                   // Says only what the dates say: the targets are older than
                   // the move. Not that nobody has written since — a rating
                   // change with no target attached is not in this list.
                   .arg(targets_overtaken(f) ? QStringLiteral(" — the targets above predate it") : QString());
    return out;
}

} // namespace finterm::services::equity
