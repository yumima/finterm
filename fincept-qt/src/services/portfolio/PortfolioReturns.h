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

#include <QString>
#include <QVector>

namespace fincept::portfolio {

struct PeriodReturn {
    double twr_pct = 0;         // chained time-weighted return over the window, %
    double gain_value = 0;      // currency gain net of external flows
    double net_external_flow = 0; // Σ contributions − withdrawals inside the window
    bool valid = false;         // false when no return could be computed at all
    bool degraded = false;      // true when ≥1 segment had to be skipped
};

/// Compute the period TWR over `snapshots` (any order; sorted internally by
/// date) extended with a synthetic final point (`live_nav`, `live_date`).
/// `txns` is the portfolio's full transaction log; only BUY/SELL rows dated
/// after the first snapshot and up to `live_date` become flows.
PeriodReturn compute_period_return(QVector<PortfolioSnapshot> snapshots, double live_nav, const QString& live_date,
                                   const QVector<Transaction>& txns);

} // namespace fincept::portfolio
