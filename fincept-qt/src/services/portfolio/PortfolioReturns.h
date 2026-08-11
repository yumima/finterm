// src/services/portfolio/PortfolioReturns.h
//
// Time-weighted return over a NAV snapshot series with external cash flows.
//
// Why this exists: the period return was (NAV_now − NAV_start) / NAV_start
// with no cash-flow adjustment, so buying $10k of stock mid-window inflated
// NAV and was reported as performance. An advisor cannot show a client a
// return figure that a deposit improves; TWR is the industry answer — chain
// the sub-period returns between flows so money moving in or out changes the
// portfolio's size but not its measured growth rate.
//
// Conventions:
//   - Daily chaining with END-of-day flows: for consecutive snapshots
//     (V_prev, V_t) and F_t = net external flow dated inside (prev, t],
//     r_t = (V_t − F_t − V_prev) / V_prev, TWR = Π(1+r_t) − 1.
//   - Flows: BUY cost is a contribution (+), SELL proceeds a withdrawal (−).
//     The portfolio models holdings only, no cash balance, so trade cash
//     enters and leaves the NAV at the trade — exactly what TWR must strip.
//   - DIVIDEND rows are NOT flows: cash dividends never enter this NAV, so
//     treating them as withdrawals would fabricate a drag. They are income,
//     reported separately by the ledger. SPLIT rows move no cash.
//   - gain_value = V_end − V_start − Σ flows: the currency gain the market
//     produced, as opposed to the NAV delta the user's deposits produced.
//   - A segment with V_prev ≤ 0 cannot state a return; it is skipped and
//     flagged, not silently invented.
//
// Pure functions — no Qt widgets, no DB, no network.

#pragma once

#include "screens/portfolio/PortfolioTypes.h"
#include "services/portfolio/PortfolioFx.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace fincept::portfolio {

struct PeriodReturn {
    double twr_pct = 0;         // chained time-weighted return over the window, %
    double gain_value = 0;      // currency gain net of external flows
    double net_external_flow = 0; // Σ contributions − withdrawals inside the window
    bool valid = false;         // false when no return could be computed at all
    // True when ≥1 segment had to be skipped (zero/dust base). twr_pct then
    // covers only the computable segments, and gain_value — which spans the
    // whole window — can misattribute day-one funding as gain when the very
    // first snapshot was zero-valued with a same-day purchase. Consumers
    // should treat a degraded window as approximate.
    bool degraded = false;
};

/// Compute the period TWR over `snapshots` (any order; sorted internally by
/// date) extended with a synthetic final point (`live_nav`, `live_date`).
/// `txns` is the portfolio's full transaction log; only BUY/SELL rows dated
/// after the first snapshot and up to `live_date` become flows.
/// `fx_by_symbol` converts each transaction's cash into the currency the NAV
/// series is denominated in. A missing symbol means 1.0 — correct for a
/// single-currency book, and why callers of a multi-currency portfolio must
/// pass the full map: subtracting a CAD flow from a USD NAV delta fabricates
/// a gain or loss on a day nothing moved.
///
/// Each flow converts at the rate for ITS OWN trade date when a historical
/// series is available, matching the per-date conversion the reconstructed
/// NAV already uses; it falls back to the current rate otherwise. A plain
/// QHash of current rates converts implicitly, so single-currency callers
/// need no map at all.
PeriodReturn compute_period_return(QVector<PortfolioSnapshot> snapshots, double live_nav, const QString& live_date,
                                   const QVector<Transaction>& txns, const FxRates& fx = {});

/// Flow-adjusted simple returns (%) between consecutive snapshots:
/// r_i = (V_i − F_i − V_{i−1}) / V_{i−1}, with F_i the net BUY−SELL cash
/// dated inside (date_{i−1}, date_i]. Element i of the result pairs
/// snapshots i and i+1 after sorting; an uncomputable segment (V ≤ 0) is
/// NaN so callers can keep date alignment and skip it explicitly.
///
/// This is the series every risk metric must consume: raw NAV differences
/// contain the user's deposits, and one funding day read as a +100% "return"
/// is enough to dominate volatility, Sharpe, VaR and the beta regression.
QVector<double> flow_adjusted_returns(QVector<PortfolioSnapshot> snapshots, const QVector<Transaction>& txns,
                                      const FxRates& fx = {});

} // namespace fincept::portfolio
