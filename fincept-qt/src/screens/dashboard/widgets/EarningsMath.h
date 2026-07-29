// src/screens/dashboard/widgets/EarningsMath.h
#pragma once
#include <QString>

#include <algorithm>
#include <cmath>

/// Pure helpers behind the Earnings Calendar widget's numbers. Kept free of Qt
/// widget types so the rules can be tested without standing up a UI.
namespace fincept::screens::widgets::earnings {

/// Nasdaq money strings: "$1.88", "($0.12)" for negatives, "" when absent.
/// Returns false when the field carries no usable number.
inline bool parse_money(const QString& raw, double* out) {
    QString s = raw.trimmed();
    if (s.isEmpty() || s == QLatin1String("N/A"))
        return false;
    const bool negative = s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')'));
    s.remove(QLatin1Char('(')).remove(QLatin1Char(')'));
    s.remove(QLatin1Char('$')).remove(QLatin1Char(','));
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok)
        return false;
    *out = negative ? -v : v;
    return true;
}

inline QString fmt_eps(double v) {
    return (v < 0 ? QStringLiteral("-$%1") : QStringLiteral("$%1")).arg(std::abs(v), 0, 'f', 2);
}

enum class Growth { Unknown, Up, Flat, Down };

/// Growth call for "consensus vs the year-ago quarter", with a dead band.
///
/// Two consensus panels disagree by a few cents on the same print — Nasdaq had
/// META's June-2026 quarter at $7.10 while Yahoo had $7.22, either side of the
/// $7.14 actually reported a year earlier. Painting a sub-1% gap as growth or
/// decline claims a precision the inputs don't have, so anything inside the
/// band reads flat. The band is relative, with an absolute floor so a
/// near-zero year-ago EPS can't make every comparison look enormous.
/// Sequential change: the coming quarter's consensus against what the last
/// reported quarter actually printed. Returns false when the base is ~zero —
/// the percentage is meaningless there, and dividing by it would produce an
/// infinity the UI would happily render.
///
/// The denominator is |prev_actual| so a loss-making base keeps the sign
/// meaningful: a $0.25 estimate after a -$0.75 quarter is an improvement (+),
/// not a decline, which a signed denominator would claim.
inline bool sequential_pct(double estimate, double prev_actual, double* out) {
    constexpr double kZeroBase = 0.005; // half a cent
    if (std::abs(prev_actual) < kZeroBase)
        return false;
    *out = (estimate - prev_actual) / std::abs(prev_actual) * 100.0;
    return true;
}

inline Growth growth_verdict(double estimate, double year_ago) {
    constexpr double kFlatBand = 0.01; // 1%
    constexpr double kScaleFloor = 0.05;
    const double scale = std::max(std::abs(year_ago), kScaleFloor);
    const double rel = (estimate - year_ago) / scale;
    if (std::abs(rel) < kFlatBand)
        return Growth::Flat;
    return rel > 0 ? Growth::Up : Growth::Down;
}

} // namespace fincept::screens::widgets::earnings
