// src/services/portfolio/PortfolioFx.cpp
#include "services/portfolio/PortfolioFx.h"

namespace fincept::portfolio {

QPair<QString, double> fx_price_factor(const QString& currency) {
    const QString trimmed = currency.trimmed();
    // "GBp" is the exact code Yahoo returns for London pence; "GBX" is the
    // ISO-ish alias other feeds use. Match GBp case-sensitively (GBP is the
    // pound and must NOT be scaled) and GBX case-insensitively.
    if (trimmed == QLatin1String("GBp") || trimmed.compare(QLatin1String("GBX"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("GBP"), 0.01};
    return {trimmed.toUpper(), 1.0};
}

QPair<QString, double> fx_pair_for(const QString& instrument_currency, const QString& portfolio_currency) {
    const auto [ccy, factor] = fx_price_factor(instrument_currency);
    const QString port = fx_price_factor(portfolio_currency).first;
    if (ccy.isEmpty() || port.isEmpty() || ccy == port)
        return {QString(), factor};
    return {ccy + port + QStringLiteral("=X"), factor};
}

double FxRates::rate_for(const QString& symbol, const QString& date) const {
    const QString key = symbol.toUpper();
    const double current = current_.value(key, 0.0);
    const auto s = series_.constFind(key);
    if (s != series_.constEnd() && !s->isEmpty()) {
        // Past the end of the series, prefer the live rate: daily closes stop
        // at the prior session while the live NAV segment is valued at
        // today's quote, so the stale close would misconvert same-day trades.
        if (date > s->lastKey() && current > 0.0)
            return current;
        // Latest point at or before `date`. upperBound gives the first entry
        // strictly after it, so step back one.
        auto it = s->upperBound(date);
        if (it != s->constBegin()) {
            --it;
            if (it.value() > 0.0)
                return it.value();
        }
        // The trade predates the series (a position opened before the
        // reconstructed window): the earliest known rate is the closest
        // honest answer available.
        const double earliest = s->constBegin().value();
        if (earliest > 0.0)
            return earliest;
    }
    return current > 0.0 ? current : 1.0;
}

} // namespace fincept::portfolio
