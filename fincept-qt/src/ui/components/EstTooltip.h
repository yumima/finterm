// src/ui/components/EstTooltip.h
#pragma once

#include <QString>

namespace fincept::ui {

/// Methodology tooltips for the most common "modeled / estimated" values
/// shown in Power Trader. Use these on column-header items (via
/// `QTableWidgetItem::setToolTip`) and on prominent QLabel widgets so the
/// user can hover any "(est.)" value to see how it was derived.
///
/// All Power Trader money values are estimates because STOCK Act PTRs and
/// OGE Form 278 disclose AMOUNT RANGES, not exact share counts or prices.
/// The midpoint of each range is used and propagated through every
/// derivative calculation.
namespace est {

/// "$X–$Y midpoint" — trade amounts on PTR rows.
QString amount_tooltip();

/// Estimated YTD alpha vs SPY using disclosed trade dates and range
/// midpoints to reconstruct a notional portfolio.
QString alpha_tooltip();

/// Estimated portfolio return YTD — same reconstruction as alpha but
/// before subtracting SPY.
QString return_tooltip();

/// Estimated net worth from Senate Annual Financial Disclosure asset
/// categories (Senate only — House annual reports aren't machine-readable).
QString net_worth_tooltip();

/// Reconstructed portfolio value at a point in time.
QString portfolio_tooltip();

/// Cabinet conflict score: weighted by holdings overlap with the official's
/// regulatory/budget domain (OGE Form 278 data).
QString conflict_score_tooltip();

/// Disclosure lag — calendar days between transaction and the filed PTR's
/// receipt date. Source data.
QString disclosure_lag_tooltip();

/// Generic helper that prepends an "(est.)" inline marker (a small grey
/// suffix) onto a label string, suitable for `QLabel::setText`. Use to
/// flag e.g. "Net Worth (est.)" without having to change every label
/// constructor site individually.
QString with_marker(const QString& label);

} // namespace est
} // namespace fincept::ui
