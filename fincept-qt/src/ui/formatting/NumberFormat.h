#pragma once
#include <QDateTime>
#include <QLocale>
#include <QString>

#include <cmath>

namespace fincept::ui::formatting {

// ─────────────────────────────────────────────────────────────────────────────
//  Shared number / money / percent / date formatting
//
//  Why this exists: before this layer, money / percent / date were rolled inline
//  per-screen with DIVERGENT conventions — K/M/B decimals disagreed (1.2K but
//  3.40M), percent precision differed between sibling tabs, currency symbols were
//  hardcoded per module (₹ in F&O, $ in crypto, locale-aware in portfolio), and
//  missing/invalid sentinels were inconsistent (—, --, N/A, ∞, bare 0). Worse,
//  real 0.0 values were sometimes hidden as "missing".
//
//  This header is the ONE place those conventions live. Pick a formatter, pass a
//  raw double, get a consistent string. All return QString (never std::string).
//
//  CONVENTIONS (the single chosen rule for each):
//    * Compact magnitude  : K / M / B / T, ONE decimal by default (1.2K, 3.4M,
//                           1.1B, 2.6T). Bloomberg-style, negative-safe.
//    * Money              : currency symbol from a small code→symbol map, then
//                           2dp non-compact ($1,234.50) or compact magnitude at
//                           1dp ($1.2B).
//    * Percent            : 2dp by default, optional leading sign ("+1.23%").
//    * Missing/unavailable: ONE sentinel, the em-dash "—" (placeholder()).
//                           A real 0.0 is NOT missing — callers must gate on
//                           NaN / optional, never on `== 0.0`.
//    * Date / datetime    : a couple of standard renderings so screens stop each
//                           inventing their own.
// ─────────────────────────────────────────────────────────────────────────────

/// The single canonical "missing / unavailable" sentinel. Use this everywhere a
/// value is genuinely absent (NaN, unset optional). Do NOT use it for a real 0.0
/// — zero is a valid value and must render as "0" / "0.00%" / "$0.00".
inline QString placeholder() {
    return QStringLiteral("—");
}

/// Compact magnitude: K / M / B / T with a single decimal convention.
///   format_compact(1234)       -> "1.2K"
///   format_compact(3.4e6)      -> "3.4M"
///   format_compact(1.1e9)      -> "1.1B"
///   format_compact(2.6e12)     -> "2.6T"
///   format_compact(-4.2e6)     -> "-4.2M"   (negative-safe)
///   format_compact(950)        -> "950"     (below 1K: plain integer)
///
/// `dp` is the decimal places used for the K/M/B/T tiers (default 1). Values
/// below 1000 render as a plain integer (no suffix, no decimals). NaN/inf yield
/// placeholder(). Zero is a real value and renders as "0".
inline QString format_compact(double v, int dp = 1) {
    if (!std::isfinite(v))
        return placeholder();
    const QString sign = v < 0 ? QStringLiteral("-") : QString();
    const double a = std::abs(v);
    if (a < 1e3)
        return sign + QString::number(static_cast<qint64>(a));

    double  scale  = a >= 1e12 ? 1e12 : a >= 1e9 ? 1e9 : a >= 1e6 ? 1e6 : 1e3;
    QString suffix = scale >= 1e12 ? QStringLiteral("T")
                   : scale >= 1e9  ? QStringLiteral("B")
                   : scale >= 1e6  ? QStringLiteral("M")
                                   : QStringLiteral("K");
    // Tier-boundary rollover: if rounding the mantissa to `dp` decimals carries
    // it to 1000 (e.g. 999_950 -> "1.0M", not "1000.0K"), step up one tier.
    const double pw = std::pow(10.0, dp);
    if (scale < 1e12 && std::round(a / scale * pw) / pw >= 1000.0) {
        scale *= 1000.0;
        suffix = scale >= 1e12 ? QStringLiteral("T")
               : scale >= 1e9  ? QStringLiteral("B")
                               : QStringLiteral("M");
    }
    return sign + QString::number(a / scale, 'f', dp) + suffix;
}

/// Compact volume / size formatting. Kept for existing callers; delegates to
/// format_compact so volume and every other compact number share one convention.
///
/// Historical contract preserved: negative volume is meaningless, so it (like
/// NaN/inf) renders as the missing sentinel rather than a negative magnitude.
/// Zero renders as "0" (valid market state: pre-open, halt, no trades yet).
inline QString format_compact_volume(double volume) {
    if (!std::isfinite(volume) || volume < 0.0)
        return placeholder();
    return format_compact(volume, 1);
}

namespace detail {
/// Currency code → symbol. Unknown codes fall back to the code + a space
/// (e.g. "CHF 1,234.50") so we never silently mislabel a currency.
inline QString currency_symbol(const QString& code) {
    const QString c = code.toUpper();
    if (c == QLatin1String("USD") || c == QLatin1String("AUD") ||
        c == QLatin1String("CAD") || c == QLatin1String("NZD") ||
        c == QLatin1String("HKD") || c == QLatin1String("SGD"))
        return QStringLiteral("$");
    if (c == QLatin1String("EUR"))
        return QStringLiteral("€");
    if (c == QLatin1String("GBP"))
        return QStringLiteral("£");
    if (c == QLatin1String("INR"))
        return QStringLiteral("₹");
    if (c == QLatin1String("JPY") || c == QLatin1String("CNY") ||
        c == QLatin1String("RMB"))
        return QStringLiteral("¥");
    if (c == QLatin1String("KRW"))
        return QStringLiteral("₩");
    if (c == QLatin1String("RUB"))
        return QStringLiteral("₽");
    return code + QLatin1Char(' ');
}
} // namespace detail

/// Money formatting with a leading currency symbol.
///
/// Non-compact (default): symbol + 2dp with thousands grouping.
///   format_money(1234.5)              -> "$1,234.50"
///   format_money(-50, "EUR")          -> "-€50.00"
/// Compact: symbol + compact magnitude at 1dp (no grouping; the suffix is the
/// scale). Use for hero numbers / cramped cells where grouping would be noise.
///   format_money(1.2e9, "USD", true)  -> "$1.2B"
///   format_money(1234.5, "GBP", true) -> "£1.2K"
///
/// `currency` is an ISO-ish code resolved via the symbol map; unknown codes fall
/// back to "<CODE> ". NaN/inf yield placeholder(). Zero renders as "$0.00".
inline QString format_money(double v, const QString& currency = QStringLiteral("USD"),
                            bool compact = false) {
    if (!std::isfinite(v))
        return placeholder();
    const QString sym = detail::currency_symbol(currency);
    const QString sign = v < 0 ? QStringLiteral("-") : QString();
    const double a = std::abs(v);
    if (compact)
        return sign + sym + format_compact(a, 1);
    // Thousands grouping at 2dp. QLocale::c() keeps "1,234.50" regardless of the
    // user's system locale so money reads the same across every screen.
    static const QLocale kC = QLocale::c();
    return sign + sym + kC.toString(a, 'f', 2);
}

/// Percent formatting. Input is already a percentage value (12.5 means 12.5%),
/// NOT a fraction — callers holding a fraction must multiply by 100 first.
///   format_percent(1.234)              -> "1.23%"
///   format_percent(1.2, 1)             -> "1.2%"
///   format_percent(1.23, 2, true)      -> "+1.23%"
///   format_percent(-0.5, 2, true)      -> "-0.50%"
///
/// `dp` decimals (default 2). `with_sign` forces a leading "+" on non-negatives
/// (negatives always show "-"). NaN/inf yield placeholder(). Zero renders as
/// "0.00%" (or "+0.00%" with sign) — a real zero is not missing.
inline QString format_percent(double v, int dp = 2, bool with_sign = false) {
    if (!std::isfinite(v))
        return placeholder();
    const QString sign = (with_sign && v >= 0.0) ? QStringLiteral("+") : QString();
    return sign + QString::number(v, 'f', dp) + QLatin1Char('%');
}

/// Standard date / datetime renderings so screens stop each picking their own.
enum class DateStyle {
    Date,         ///< "2026-06-16"            (ISO date)
    DateTime,     ///< "2026-06-16 14:05"      (ISO date + 24h HH:mm)
    DateTimeSec,  ///< "2026-06-16 14:05:09"
    Time,         ///< "14:05"
    Compact,      ///< "16 Jun 26"             (compact human date)
};

/// Render a QDateTime in one of the standard styles. Invalid/null datetimes
/// yield placeholder(). All renderings are locale-independent (ISO-ish) so the
/// same instant reads identically on every screen.
inline QString format_datetime(const QDateTime& dt, DateStyle style = DateStyle::DateTime) {
    if (!dt.isValid())
        return placeholder();
    switch (style) {
    case DateStyle::Date:
        return dt.toString(QStringLiteral("yyyy-MM-dd"));
    case DateStyle::DateTime:
        return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case DateStyle::DateTimeSec:
        return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    case DateStyle::Time:
        return dt.toString(QStringLiteral("HH:mm"));
    case DateStyle::Compact:
        return dt.toString(QStringLiteral("dd MMM yy"));
    }
    return placeholder();
}

/// Convenience: render a QDate as the canonical ISO "yyyy-MM-dd". Invalid dates
/// yield placeholder().
inline QString format_date(const QDate& d) {
    if (!d.isValid())
        return placeholder();
    return d.toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace fincept::ui::formatting
