#pragma once
// TreasuryCurve.h — the US Treasury daily par yield curve rows the WORLD
// board uses for the 2-year and the 2s10s spread (Yahoo has no cash 2-year).
// Produced by `scripts/treasury_data.py par_curve`; parsed here so the
// "absent is NaN, never 0" rule is unit-tested.

#include "screens/markets/world/WorldMarketsModel.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>

namespace finterm::screens::world {

struct TreasuryCurve {
    QDate date;      ///< the curve's date (Treasury publishes after the close)
    QDate prev_date; ///< the business day before it in the file
    double y2 = kNaN, y2_prev = kNaN;
    double y10 = kNaN, y10_prev = kNaN;
    QString error; ///< why there is no curve (the script's words)

    bool valid() const { return date.isValid() && std::isfinite(y2); }
    double y2_change_bp() const { return std::isfinite(y2_prev) ? (y2 - y2_prev) * 100.0 : kNaN; }
    double s2s10_bp() const { return spread_bp(y10, y2); }
    double s2s10_change_bp() const {
        const double now = s2s10_bp(), before = spread_bp(y10_prev, y2_prev);
        return std::isfinite(now) && std::isfinite(before) ? now - before : kNaN;
    }
};

namespace detail {
inline double json_num(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() > 0 ? v.toDouble() : kNaN;
}
} // namespace detail

/// {"rows": [{"date": "2026-11-13", "y2": 4.88, "y10": 5.15, ...}, ...]},
/// newest first. The newest row with a 2-year is the curve; the next row is
/// the day before. {"error": ...} or no usable row → invalid, error kept.
inline TreasuryCurve parse_treasury_curve(const QJsonObject& root) {
    TreasuryCurve c;
    if (root.contains(QLatin1String("error"))) {
        c.error = root.value(QLatin1String("error")).toString(QStringLiteral("Treasury curve unavailable"));
        return c;
    }
    const QJsonArray rows = root.value(QLatin1String("rows")).toArray();
    int i = 0;
    for (; i < rows.size(); ++i) {
        const QJsonObject o = rows[i].toObject();
        const QDate d = QDate::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
        const double y2 = detail::json_num(o, "y2");
        if (d.isValid() && std::isfinite(y2)) {
            c.date = d;
            c.y2 = y2;
            c.y10 = detail::json_num(o, "y10");
            break;
        }
    }
    if (!c.date.isValid()) {
        c.error = QStringLiteral("no Treasury curve row with a 2-year yield");
        return c;
    }
    for (++i; i < rows.size(); ++i) {
        const QJsonObject o = rows[i].toObject();
        const QDate d = QDate::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
        if (!d.isValid() || d >= c.date)
            continue;
        c.prev_date = d;
        c.y2_prev = detail::json_num(o, "y2");
        c.y10_prev = detail::json_num(o, "y10");
        break;
    }
    return c;
}

} // namespace finterm::screens::world
