#pragma once
// OvernightSummary.h — the one-line "overnight" read at the top of the WORLD
// board, generated from the numbers by fixed rules (no model, no opinion):
//
//   Asia mixed: Nikkei +0.8%, Hang Seng −1.2% · Europe higher: Euro Stoxx 50
//   +0.6% · US futures +0.3% · 10y 4.12% (+3bp) · Dollar −0.2%
//
// Rules (region_verdict): a move counts as up at >= +0.10%, down at <= −0.10%,
// otherwise flat. All up → "higher", all down → "lower", none either way →
// "flat", ups and flats only → "mostly higher", downs and flats only →
// "mostly lower", ups and downs → "mixed". An unknown (NaN) move is left out,
// never read as 0; a part with no known number is left out of the line.

#include "screens/markets/world/WorldMarketsModel.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>

namespace finterm::screens::world {

inline constexpr double kFlatBand = 0.10; ///< |move| below this (percent) is flat

struct NamedMove {
    QString name;
    double pct = kNaN;
};

inline QString region_verdict(const QVector<NamedMove>& moves) {
    int n = 0, up = 0, down = 0;
    for (const auto& m : moves) {
        if (!std::isfinite(m.pct))
            continue;
        ++n;
        if (m.pct >= kFlatBand)
            ++up;
        else if (m.pct <= -kFlatBand)
            ++down;
    }
    if (n == 0)
        return {};
    if (up == n)
        return QStringLiteral("higher");
    if (down == n)
        return QStringLiteral("lower");
    if (up == 0 && down == 0)
        return QStringLiteral("flat");
    if (down == 0)
        return QStringLiteral("mostly higher");
    if (up == 0)
        return QStringLiteral("mostly lower");
    return QStringLiteral("mixed");
}

struct OvernightInputs {
    QVector<NamedMove> asia;   ///< priority order: the first two known are named
    QVector<NamedMove> europe; ///< the first known is named
    double us_futures_pct = kNaN;
    double us10y_pct = kNaN; ///< the yield level, percent
    double us10y_bp = kNaN;  ///< its change, basis points
    double dollar_pct = kNaN;
};

namespace detail {
inline QString region_part(const QString& region, const QVector<NamedMove>& moves, int named) {
    const QString verdict = region_verdict(moves);
    if (verdict.isEmpty())
        return {};
    QStringList names;
    for (const auto& m : moves) {
        if (names.size() >= named)
            break;
        if (std::isfinite(m.pct))
            names << m.name + QLatin1Char(' ') + signed_pct(m.pct, 1);
    }
    return region + QLatin1Char(' ') + verdict + QStringLiteral(": ") + names.join(QStringLiteral(", "));
}
} // namespace detail

inline QString overnight_line(const OvernightInputs& in) {
    QStringList parts;
    if (const QString a = detail::region_part(QStringLiteral("Asia"), in.asia, 2); !a.isEmpty())
        parts << a;
    if (const QString e = detail::region_part(QStringLiteral("Europe"), in.europe, 1); !e.isEmpty())
        parts << e;
    if (std::isfinite(in.us_futures_pct))
        parts << QStringLiteral("US futures ") + signed_pct(in.us_futures_pct, 1);
    if (std::isfinite(in.us10y_pct)) {
        QString y = QStringLiteral("10y ") + QString::number(in.us10y_pct, 'f', 2) + QLatin1Char('%');
        if (std::isfinite(in.us10y_bp)) {
            const double r = std::round(in.us10y_bp);
            const QString bp = r == 0.0 ? QStringLiteral("0bp")
                                        : (r > 0 ? QStringLiteral("+") : minus()) +
                                              QString::number(std::abs(r), 'f', 0) + QStringLiteral("bp");
            y += QStringLiteral(" (") + bp + QLatin1Char(')');
        }
        parts << y;
    }
    if (std::isfinite(in.dollar_pct))
        parts << (std::abs(in.dollar_pct) < 0.05 ? QStringLiteral("Dollar flat")
                                                 : QStringLiteral("Dollar ") + signed_pct(in.dollar_pct, 1));
    return parts.join(QStringLiteral(" · "));
}

} // namespace finterm::screens::world
