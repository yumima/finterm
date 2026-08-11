// src/services/portfolio/PortfolioFx.h
//
// Currency conventions for folding a multi-currency book into one number.
//
// A portfolio that holds AAPL (USD) and RY.TO (CAD) used to sum them as bare
// numbers and label the result with the portfolio's currency. Converting
// needs two decisions, and both are easy to get subtly wrong:
//
//   1. Sub-unit quoting. London prints in pence: "GBp" (sometimes "GBX") is
//      1/100 of a pound. Treating a 4,500 GBp quote as 4,500 GBP is a 100×
//      error — larger than any market move the number is meant to show.
//   2. The FX pair symbol. The naming convention is load-bearing TWICE: the
//      summary FETCHES pair quotes under keys built from it and LOOKS THEM UP
//      under keys built from it. Two spellings that drift make the fetch and
//      the lookup silently disagree — the rate is downloaded and never found,
//      and every cross-currency total quietly falls back to face value.
//
// Both live here, as pure functions, so there is one spelling and it can be
// tested without a database, a network, or a Qt widget.

#pragma once

#include <QPair>
#include <QString>

namespace fincept::portfolio {

/// Normalise a quoted currency code: returns {UPPERCASE code, price factor}
/// where the factor converts a quoted price into the major unit (GBp → GBP
/// is 0.01). Case matters on the way in — "GBp" is pence, "GBP" is pounds —
/// and never on the way out.
QPair<QString, double> fx_price_factor(const QString& currency);

/// Resolve an instrument currency against the portfolio currency into
/// {FX pair symbol, price factor}. The pair is empty when no conversion is
/// needed: the currencies match, or the instrument's currency is unknown
/// (callers must treat unknown as "not converted", not as "1:1").
QPair<QString, double> fx_pair_for(const QString& instrument_currency, const QString& portfolio_currency);

} // namespace fincept::portfolio
