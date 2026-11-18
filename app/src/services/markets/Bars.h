// src/services/markets/Bars.h
#pragma once
// OHLCV bars: the one type and the one set of rules for every price-history
// read in the app (MarketDataService::fetch_bars, the market:history topic).
// Equity Research's Candle is this type (an alias), so a chart, a technical
// rating and a portfolio return can never read the same series two ways.

#include "core/util/BarTime.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QVector>

#include <cmath>
#include <limits>

namespace finterm::services {

struct HistoryPoint {
    /// Daily-and-longer bars: midnight in the EXCHANGE's own zone — a
    /// session, not an instant; decode with date(). Intraday bars (1m…1h) are
    /// real instants: decode those in a real timezone, never with date(),
    /// whose +14h would roll a 15:30 ET bar into the next day. See
    /// core/util/BarTime.h for what a bare fromSecsSinceEpoch() costs.
    qint64 timestamp = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    qint64 volume = 0;

    /// The session this bar covers, on the exchange's calendar (daily and
    /// longer intervals only; a weekly or monthly bar is stamped at the
    /// start of its period).
    QDate date() const { return core::bartime::bar_date(timestamp); }
};

namespace bars {

/// A row with all four prices, present and finite. Deliberately NOT a
/// positivity test: zero and negative prints are real (^IRX closed at 0.000
/// through 2020-21, CL=F settled at -37.63 on 2020-04-20). yfinance emits an all-NaN row for a session
/// it has no prices for yet (typically today's bar); the daemon must send
/// NaN as JSON null, and a bare toDouble() reads null as 0.0 — "no price"
/// silently becoming "priced at zero". One such bar drags a chart's range to
/// 0 and flattens every real candle on a log axis.
inline bool row_has_prices(const QJsonObject& o) {
    for (const char* f : {"open", "high", "low", "close"}) {
        const double v = o.value(QLatin1String(f)).toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(v))
            return false;
    }
    return true;
}

/// The rows with prices, in order (what the technicals and TALipp read).
inline QJsonArray priced_rows(const QJsonArray& rows) {
    QJsonArray out;
    for (const auto& v : rows)
        if (row_has_prices(v.toObject()))
            out.append(v);
    return out;
}

/// Rows → bars; price-less rows are dropped, never zero-priced.
inline QVector<HistoryPoint> parse(const QJsonArray& rows) {
    QVector<HistoryPoint> out;
    out.reserve(rows.size());
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        if (!row_has_prices(o))
            continue;
        HistoryPoint p;
        p.timestamp = static_cast<qint64>(o.value(QLatin1String("timestamp")).toDouble());
        p.open = o.value(QLatin1String("open")).toDouble();
        p.high = o.value(QLatin1String("high")).toDouble();
        p.low = o.value(QLatin1String("low")).toDouble();
        p.close = o.value(QLatin1String("close")).toDouble();
        p.volume = static_cast<qint64>(o.value(QLatin1String("volume")).toDouble());
        out.append(p);
    }
    return out;
}

/// Intraday intervals (1m … 90m, 1h) vs daily and longer.
inline bool is_intraday(const QString& interval) {
    return (interval.endsWith(QLatin1Char('m')) && !interval.endsWith(QLatin1String("mo"))) ||
           interval.endsWith(QLatin1Char('h'));
}

} // namespace bars
} // namespace finterm::services

// Declared with the type (equity::Candle is this type, so one declaration
// serves both names).
Q_DECLARE_METATYPE(finterm::services::HistoryPoint)
Q_DECLARE_METATYPE(QVector<finterm::services::HistoryPoint>)
