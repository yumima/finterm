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
#include <QSet>
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
    // True when a flow inside the window could not be converted (unknown FX
    // rate). valid is then false and NO return may be shown — not even a
    // naive NAV ratio, which would count the flow itself as performance.
    bool fx_unknown = false;
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

/// One flow-adjusted return over a run of TRADING sessions.
struct SegmentReturn {
    QString start_date;    // snapshot the segment starts from (YYYY-MM-DD)
    QString end_date;      // snapshot the segment ends at
    double pct = 0;        // flow-adjusted return over the segment, %; NaN = uncomputable
    int trading_days = 0;  // trading sessions inside (start_date, end_date], ≥ 1
};

/// Flow-adjusted returns rebuilt on a TRADING-DAY calendar.
///
/// Snapshots are written on whatever calendar day the app runs, weekends
/// included, and the app is not run every day. Read as consecutive "daily"
/// returns, a Saturday row adds a ~0% day (understating volatility) and a
/// Tuesday→Friday gap adds a three-session move as one "day" (overstating
/// it) — both then annualised with √252. Instead:
///
///   - A segment with NO trading session inside (prev, curr] (Fri→Sat,
///     Sat→Sun) is merged into the next segment by CHAINING the growth, so a
///     weekend snapshot is effectively skipped while any flow dated on it
///     still lands exactly where the NAV reflects it. TWR chaining is exact
///     under merging: Π(1+r) over the pieces is the merged segment's growth.
///     Trailing zero-session pieces fold into the last emitted segment.
///   - Every emitted segment states how many sessions it spans. The caller
///     decides: per-day statistics (volatility, Sharpe/Sortino, VaR, beta)
///     use only 1-session segments — a k-session return is NOT a daily
///     observation, and rescaling it by √k would assume the very i.i.d.
///     random walk the statistics are trying to measure; chained quantities
///     (drawdown, cumulative growth) use every segment, gaps included, so the
///     growth index is never broken.
///
/// `calendar` (YYYY-MM-DD set, e.g. a benchmark's bar dates) names the
/// trading sessions where it reaches; outside its first..last span, and when
/// it is empty, Monday–Friday are sessions (exchange holidays then count as
/// sessions, which can only cause a holiday-spanning return to be treated as
/// a gap and left out of the per-day statistics — never the reverse).
QVector<SegmentReturn> trading_day_returns(QVector<PortfolioSnapshot> snapshots, const QVector<Transaction>& txns,
                                           const FxRates& fx = {}, const QSet<QString>& calendar = {});

/// Point on a NAV path: date (YYYY-MM-DD) and value. Duplicate dates allowed.
struct NavPoint {
    QString date;
    double value = 0;
};

/// Net external flow per path point: out[i] = Σ BUY−SELL cash dated in
/// (date[i−1], date[i]] (local trade date, converted at its own trade-date
/// rate); out[0] = 0. `path` must be sorted by date. NaN when a flow in that
/// segment has no known FX rate.
QVector<double> segment_flows(const QVector<NavPoint>& path, const QVector<Transaction>& txns,
                              const FxRates& fx = {});

/// Time-weighted growth index over `path` (sorted by date): 1.0 at the first
/// point, then chained by each segment's flow-adjusted return — the series a
/// "like-for-like" comparison against a benchmark must use, because raw NAV
/// rises with every deposit. A zero/dust-base segment carries the level
/// flat (it states no growth); from an unknown-FX flow onwards every value is
/// NaN.
QVector<double> twr_index(const QVector<NavPoint>& path, const QVector<Transaction>& txns, const FxRates& fx = {});

} // namespace fincept::portfolio
