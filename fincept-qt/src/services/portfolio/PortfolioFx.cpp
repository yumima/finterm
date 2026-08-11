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

} // namespace fincept::portfolio
