#pragma once
// ExtendedSessions.h — the latest after-hours and pre-market session of one
// symbol, as the daemon's `extended_sessions` action reports them
// (scripts/extended_sessions.py).
//
// Each session carries the close it is measured against: after-hours against
// its own day's close, pre-market against the close before it. Nothing here
// pairs a print with a close — that is done once, where the bars are.
//
// Absent is NaN, never 0: a session with no volume figure prints a dash, not
// "0 shares".

#include <QDate>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>

#include <cmath>
#include <limits>

namespace finterm::services::equity {

struct ExtSession {
    bool valid = false;
    bool live = false; ///< still trading when the daemon answered
    QDate date;        ///< the session's calendar date (ET)
    QDate ref_date;    ///< the date of the close it is measured against
    double ref_close = std::numeric_limits<double>::quiet_NaN();
    double last = std::numeric_limits<double>::quiet_NaN();
    qint64 last_ts = 0; ///< when the last print happened — a real instant
    double pct = std::numeric_limits<double>::quiet_NaN();
    double high = std::numeric_limits<double>::quiet_NaN();
    double low = std::numeric_limits<double>::quiet_NaN();
    int minutes = 0; ///< minutes of the session with at least one print
    double volume = std::numeric_limits<double>::quiet_NaN();
    double avg_volume = std::numeric_limits<double>::quiet_NaN(); ///< average FULL-day volume, 20 sessions

    /// The session's volume as a share of an average full day, in percent.
    double volume_pct_of_day() const {
        if (!std::isfinite(volume) || !std::isfinite(avg_volume) || avg_volume <= 0)
            return std::numeric_limits<double>::quiet_NaN();
        return volume / avg_volume * 100.0;
    }
};

struct ExtSessions {
    ExtSession after_hours;
    ExtSession pre_market;
    bool unsupported = false; ///< the instrument has no US extended session at all

    /// True when the pre-market session is the more recent of the two.
    bool pre_is_latest() const {
        if (!pre_market.valid)
            return false;
        return !after_hours.valid || pre_market.last_ts > after_hours.last_ts;
    }
};

namespace ext_detail {
inline double num(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}
inline ExtSession session(const QJsonValue& v) {
    ExtSession s;
    if (!v.isObject())
        return s;
    const QJsonObject o = v.toObject();
    s.last = num(o, "last");
    s.date = QDate::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
    if (!std::isfinite(s.last) || !s.date.isValid())
        return s;
    s.valid = true;
    s.live = o.value(QLatin1String("live")).toBool();
    s.ref_date = QDate::fromString(o.value(QLatin1String("ref_date")).toString(), Qt::ISODate);
    s.ref_close = num(o, "ref_close");
    // Derived here from the two prices on the row rather than read from the
    // row's own `pct`: one number is stored, the rest follow from it.
    if (std::isfinite(s.ref_close) && s.ref_close > 0)
        s.pct = (s.last - s.ref_close) / s.ref_close * 100.0;
    const double ts = num(o, "last_ts");
    s.last_ts = std::isfinite(ts) ? static_cast<qint64>(ts) : 0;
    s.high = num(o, "high");
    s.low = num(o, "low");
    const double m = num(o, "minutes");
    s.minutes = std::isfinite(m) ? static_cast<int>(m) : 0;
    s.volume = num(o, "volume");
    s.avg_volume = num(o, "avg_volume");
    return s;
}
} // namespace ext_detail

inline ExtSessions parse_extended_sessions(const QJsonObject& o) {
    ExtSessions r;
    r.unsupported = o.value(QLatin1String("unsupported")).toBool();
    r.after_hours = ext_detail::session(o.value(QLatin1String("after_hours")));
    r.pre_market = ext_detail::session(o.value(QLatin1String("pre_market")));
    return r;
}

} // namespace finterm::services::equity
