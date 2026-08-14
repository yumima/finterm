// src/screens/dashboard/widgets/ExtendedHoursMath.h
#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QLatin1String>
#include <QTimeZone>
#include <QtGlobal>
#include <QString>

#include <cmath>
#include <optional>

/// Pure helpers behind the AFT% column. Kept free of Qt widget types so the
/// rules can be tested against real daemon payloads without standing up a UI —
/// this arithmetic has produced two visibly wrong numbers on screen (a sign
/// inversion and a stale reference), and neither was catchable by reading it.
namespace fincept::screens::widgets::exthours {

/// One symbol's extended-hours reading, already reduced to what the cell needs.
struct ExtQuote {
    double pct = 0;        // signed move against the regular close
    double price = 0;      // the extended-hours print
    double regular = 0;    // the close it is measured against
    bool   from_pre = false;
    qint64 regular_ts = 0; // when each bar printed; 0 when the daemon didn't say
    qint64 ext_ts = 0;
};

/// True when today's closing bell has not rung yet, so the last completed close
/// — the reference an extended print is measured against — is YESTERDAY's.
///
/// This is the `in_pre_session` hint for `quote_from_row`, and it has to agree
/// with the rule that function applies to the daemon's `session` field: PRE and
/// REGULAR both mean "the reference is the previous close". A window that
/// stopped at 09:30 would take the post-market branch all through the session
/// and surface a day-old number. Only reached for payloads with no `session`
/// field at all, which the daemon always sends — a fallback, kept consistent so
/// it can't quietly contradict the primary rule.
inline bool before_the_close_et(const QDateTime& now = QDateTime::currentDateTime()) {
    const QDateTime et = now.toTimeZone(QTimeZone("America/New_York"));
    if (et.date().dayOfWeek() >= 6)
        return false;
    const QTime t = et.time();
    return t >= QTime(4, 0) && t < QTime(16, 0);
}

inline std::optional<double> json_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (v.isNull() || v.isUndefined() || !v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

/// Reduce one `extended_hours` row to a displayable quote.
///
/// `in_pre_session` says whether the ET clock is currently in the pre-market
/// window; the daemon's own `session` field takes precedence when present.
///
/// Which extended price may be paired with `regular` depends on the session,
/// and getting it wrong yields a wrong number rather than a stale one.
/// `regular` is the most recent COMPLETED close. Before today's bell — PRE and
/// REGULAR — that is yesterday's close, so today's pre-market print is measured
/// against exactly the right reference and is also the most recent extended
/// move there is. Once the bell has rung — POST, and the CLOSED stretch after
/// 20:00 ET — `regular` is today's close, and today's pre-market print belongs
/// to the previous one; pairing them computes the day's move backwards. So the
/// fallback runs one way only: before the close, falling back to the prior
/// post-market is fine because both sides reference the same close; after it,
/// no post-market print means no number.
inline std::optional<ExtQuote> quote_from_row(const QJsonObject& row, bool in_pre_session) {
    const QString session = row.value(QStringLiteral("session")).toString();
    const bool in_pre = session.isEmpty()
                            ? in_pre_session
                            : (session == QLatin1String("PRE") ||
                               session == QLatin1String("REGULAR"));

    // An explicit null `ext_price` is the daemon saying "these prints cannot be
    // honestly paired with this close" — a reference bar from an earlier day,
    // typically. Re-deriving the percentage from the raw prints anyway would
    // reconstruct exactly the number it withheld, and the timestamp check below
    // won't object: a two-day-old reference is well inside its tolerance. An
    // ABSENT key means nothing was said, so it isn't judged.
    const auto ext_field = row.value(QLatin1String("ext_price"));
    if (ext_field.isNull())
        return std::nullopt;

    const auto pre = json_num(row, "pre_market");
    const auto post = json_num(row, "post_market");
    const auto regular = json_num(row, "regular");
    const auto ext = in_pre ? (pre ? pre : post) : post;

    if (!ext || !regular || *regular <= 0)
        return std::nullopt;

    ExtQuote q;
    q.regular = *regular;
    q.price = *ext;
    // Base is the CLOSE, never the extended print. Dividing by the extended
    // price instead inverts the sign of every move and shrinks its magnitude —
    // a stock down 6.5% after hours reads as up 7%, which is not a degraded
    // answer but the opposite one.
    q.pct = (*ext - *regular) / *regular * 100.0;
    q.from_pre = in_pre && pre.has_value();
    const auto reg_ts = json_num(row, "regular_ts");
    const auto ext_ts = json_num(row, q.from_pre ? "pre_market_ts" : "post_market_ts");
    q.regular_ts = reg_ts ? static_cast<qint64>(*reg_ts) : 0;
    q.ext_ts = ext_ts ? static_cast<qint64>(*ext_ts) : 0;
    return q;
}

/// Whether the two prices in a quote can honestly be compared.
///
/// Checked on TIME, deliberately. An earlier version of this guard compared
/// the daemon's close against the price the row was quoting and suppressed the
/// pair when they differed by more than a few percent. That premise was wrong:
/// the quote feed rolls over to extended-hours prices, so once a stock had
/// moved after the bell its row price WAS the extended print, and the
/// "mismatch" the guard measured was the after-hours move itself. It hid every
/// symbol that moved — the exact rows the column exists for, and only those.
///
/// Timestamps can't be fooled that way. The extended print has to come after
/// the close it is measured against, and that close has to be the most recent
/// one rather than a bar left over from days ago. A payload with no times is
/// not judged: silence is not evidence of a bad pairing.
inline bool pairing_is_sound(const ExtQuote& q, qint64 now_secs, int max_close_age_days = 5) {
    if (q.regular_ts <= 0 || q.ext_ts <= 0)
        return true;
    if (q.ext_ts <= q.regular_ts)
        return false;
    // Five days spans a long weekend with a holiday attached; beyond that the
    // "close" is from a different week and the percentage is a multi-day
    // return, not an after-hours move.
    return (now_secs - q.regular_ts) <= static_cast<qint64>(max_close_age_days) * 86400;
}

} // namespace fincept::screens::widgets::exthours
