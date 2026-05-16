#pragma once
#include <QString>

#include <cmath>

namespace fincept::ui::formatting {

/// Compact volume / size formatting: K / M / B suffixes, one decimal place.
///
/// Matches the convention finterm already uses in StockQuoteWidget and
/// MarketPulsePanel — 1 decimal (Bloomberg-style) rather than 2.
///
/// Returns "--" for NaN, infinity, or negative values. Zero renders as "0"
/// since zero volume is a valid market state (pre-open, halt, no trades yet)
/// and shouldn't be hidden behind a placeholder.
inline QString format_compact_volume(double volume) {
    if (!std::isfinite(volume) || volume < 0.0)
        return QStringLiteral("--");
    if (volume >= 1e9)
        return QString::number(volume / 1e9, 'f', 1) + QLatin1Char('B');
    if (volume >= 1e6)
        return QString::number(volume / 1e6, 'f', 1) + QLatin1Char('M');
    if (volume >= 1e3)
        return QString::number(volume / 1e3, 'f', 1) + QLatin1Char('K');
    return QString::number(static_cast<qint64>(volume));
}

} // namespace fincept::ui::formatting
