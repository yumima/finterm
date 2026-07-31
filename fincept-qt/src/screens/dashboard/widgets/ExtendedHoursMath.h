// src/screens/dashboard/widgets/ExtendedHoursMath.h
#pragma once
#include <QJsonObject>
#include <QLatin1String>
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
};

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
/// `regular` is the most recent completed close: post-market trades against
/// that same close, so the pairing is sound, but pre-market trades against the
/// PREVIOUS close. Pairing this morning's pre-market print with this
/// afternoon's close computes the day's move backwards. So the fallback runs
/// one way only — before the open, falling back to the prior post-market is
/// fine because both sides reference the same close; after it, no post-market
/// print means no number.
inline std::optional<ExtQuote> quote_from_row(const QJsonObject& row, bool in_pre_session) {
    const QString session = row.value(QStringLiteral("session")).toString();
    const bool in_pre =
        session.isEmpty() ? in_pre_session : session == QLatin1String("PRE");

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
    return q;
}

/// Whether a quote was measured against the same close the row is showing.
///
/// When the daemon's reference and the quote feed's last price disagree by
/// more than a rounding difference they are describing different sessions, and
/// the percentage between them is a multi-day return wearing an after-hours
/// label — worse than blank, because it looks like an answer.
inline bool reference_agrees(const ExtQuote& q, double row_price, double tolerance) {
    if (row_price <= 0 || q.regular <= 0)
        return true;  // nothing to check it against; don't suppress on a guess
    return std::abs(q.regular - row_price) / row_price <= tolerance;
}

} // namespace fincept::screens::widgets::exthours
