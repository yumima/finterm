#pragma once
// ExtendedSessions.h — the latest pre-market, after-hours and overnight
// session of one symbol, for the ER Overview's Extended Hours panel.
//
// Two sources, joined in with_live_readings():
//   * the daemon's `extended_sessions` action (scripts/extended_sessions.py):
//     1-minute bars — each session's range, active minutes and volume;
//   * services::ExtendedHoursService — the portfolio EXT% source: each
//     session's latest print and the close it is measured against, the
//     overnight session included (it comes only from the push stream).
// The headline price and move are the service's whenever it has that session,
// so the panel and the portfolio cannot show two numbers for one session.
//
// Absent is NaN, never 0: a session with no volume figure prints a dash, not
// "0 shares".

#include "screens/dashboard/widgets/ExtendedHoursMath.h"

#include <QDate>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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
    bool volume_partial = false; ///< read while the session traded; the total was never seen
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
    ExtSession overnight;     ///< from the live stream only (no bars carry it)
    bool unsupported = false; ///< the instrument has no US extended session at all
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
    s.volume_partial = o.value(QLatin1String("volume_partial")).toBool();
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

namespace ext_detail {
namespace xh = finterm::screens::widgets::exthours;

/// One session's headline from the service's reading. The bars' range,
/// volume and minutes stay only when they describe the SAME session (same ET
/// date); otherwise they belong to another day and are dropped, and the
/// session counts as trading while it keeps printing (15 minutes).
inline void take_reading(ExtSession& s, const std::optional<xh::Reading>& r, qint64 now_secs) {
    if (!r || !(r->price > 0) || !(r->regular > 0))
        return;
    // The bars already hold a later session than the reading: keep theirs.
    if (s.valid && s.date.isValid() && r->day.isValid() && s.date > r->day)
        return;
    const bool same = s.valid && s.date == r->day;
    ExtSession out = same ? s : ExtSession{};
    if (!same)
        out.live = r->print_ts > 0 && now_secs - r->print_ts < 15 * 60;
    out.valid = true;
    out.date = r->day;
    out.last = r->price;
    out.last_ts = r->print_ts;
    out.ref_close = r->regular;
    out.ref_date = r->regular_day.isValid() ? r->regular_day
                   : same && xh::same_close(s.ref_close, r->regular) ? s.ref_date
                                                                     : QDate();
    out.pct = r->pct();
    // The range is of prints the bars saw; the headline print is one too.
    if (same && std::isfinite(out.high) && std::isfinite(out.low)) {
        out.high = std::max(out.high, out.last);
        out.low = std::min(out.low, out.last);
    }
    s = out;
}
} // namespace ext_detail

/// The panel's sessions: the bars' pre-market and after-hours with each
/// headline taken from `live` (ExtendedHoursService::sessions), plus the
/// overnight session when it is the current one (printed after the last
/// close — an older overnight is history no other source can show beside it).
/// `now_secs` decides whether a print-only session is still trading.
inline ExtSessions with_live_readings(ExtSessions bars, const finterm::screens::widgets::exthours::SymbolExt& live,
                                      qint64 now_secs) {
    namespace xh = finterm::screens::widgets::exthours;
    if (live.declined)
        return bars; // the close is stale: no reading to take
    ext_detail::take_reading(bars.pre_market, live.at(xh::ExtSide::Pre), now_secs);
    ext_detail::take_reading(bars.after_hours, live.at(xh::ExtSide::Post), now_secs);
    bars.overnight = {};
    if (const auto& on = live.at(xh::ExtSide::Overnight); on && xh::is_current(live, *on))
        ext_detail::take_reading(bars.overnight, on, now_secs);
    return bars;
}

} // namespace finterm::services::equity
