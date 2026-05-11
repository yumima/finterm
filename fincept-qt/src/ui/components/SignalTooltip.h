// src/ui/components/SignalTooltip.h
#pragma once

#include <QString>

namespace fincept::power_trader { struct PoliticalTrade; }

namespace fincept::ui {

/// Build a rich-text tooltip explaining how a trade's per-trade signal score
/// was composed. Mirrors the weights in
/// `scripts/senate_disclosures_data.py::compute_signal_score`:
///
///   * Committee overlap — up to 30 (20 if only mapped via ticker)
///   * Disclosure timing — up to 15 (>40d=15, >30d=8, >20d=3)
///   * Trade size        — up to 10 (≥$1M=10, ≥$250K=5, ≥$50K=2)
///
/// The numbers are derived from the trade's existing fields
/// (`committee_relevance`, `disclosure_lag_days`, `amount_high`) so the
/// tooltip stays in sync as long as the Python weights and this helper move
/// together. The cite-source comment above is a deliberate reminder for any
/// future weight change.
///
/// Returns HTML safe to pass to QWidget::setToolTip / QTableWidgetItem::
/// setToolTip — bring in `Qt::RichText` if the host widget needs it.
QString tooltip_for_trade_signal(const power_trader::PoliticalTrade& t);

/// Methodology blurb for an *aggregate* signal score (member average, group
/// average, party average). The aggregate has no single per-trade breakdown,
/// so this just explains what the number means and points at the same
/// formula. Optional `n_trades` adds a sample-size line.
QString tooltip_for_aggregate_signal(int n_trades = -1);

/// Methodology blurb for a *peak* signal score — the maximum across a set of
/// trades, used by the committee-insider correlation table in OverviewPanel.
/// `n_trades` is the size of the set being maximised over (e.g. the number
/// of committee-relevant trades).
QString tooltip_for_peak_signal(int n_trades);

} // namespace fincept::ui
