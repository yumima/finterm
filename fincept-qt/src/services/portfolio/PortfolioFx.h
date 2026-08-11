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

#include <QHash>
#include <QMap>
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

/// Instrument→portfolio-currency rates, by symbol and by DATE.
///
/// A cash flow must be converted at the rate that applied when the trade
/// happened, not today's. Reconstructed NAV history already converts each
/// date at that date's FX close, so pairing it with a present-day flow rate
/// leaves the difference — the currency drift between the trade and now —
/// showing up as fabricated performance. It is second-order next to the
/// unit error that preceded it, and it is still wrong.
///
/// Falls back gracefully: a symbol with no series uses its current rate, and
/// a symbol with neither converts 1:1 (the caller should already have
/// flagged that as an incomplete conversion).
class FxRates {
  public:
    FxRates() = default;
    /// Implicit so existing call sites that only know today's rates keep
    /// working unchanged — they simply get the current-rate fallback.
    FxRates(const QHash<QString, double>& current) {
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            current_.insert(it.key().toUpper(), it.value());
    }

    void set_current(const QString& symbol, double rate) { current_.insert(symbol.toUpper(), rate); }
    /// `by_date` maps YYYY-MM-DD → the full instrument→portfolio multiplier
    /// for that date (pair close × any sub-unit factor).
    void set_series(const QString& symbol, const QMap<QString, double>& by_date) {
        series_.insert(symbol.toUpper(), by_date);
    }

    /// Rate for `symbol` on `date` (YYYY-MM-DD).
    ///
    /// Inside the series window: the latest point at or before `date`.
    /// BEFORE it: the earliest known point (a position opened before the
    /// reconstructed window still needs a rate, and that is the closest
    /// honest one). AFTER it: the CURRENT rate, not the last close —
    /// daily history ends at the prior close while the live NAV segment is
    /// valued at today's quote, so carrying the stale close forward would
    /// convert a same-day trade at yesterday's rate and re-create the exact
    /// mismatch this class exists to remove.
    double rate_for(const QString& symbol, const QString& date) const;

  private:
    QHash<QString, double> current_;
    QHash<QString, QMap<QString, double>> series_;
};

} // namespace fincept::portfolio
