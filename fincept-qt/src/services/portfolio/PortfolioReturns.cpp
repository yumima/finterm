// src/services/portfolio/PortfolioReturns.cpp
#include "services/portfolio/PortfolioReturns.h"

#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fincept::portfolio {

namespace {

// A NAV below one cent is not a meaningful base for a return: a dust
// remnant after liquidation (1e-8) would otherwise mint an astronomical
// segment return that sails past the NaN filter and dominates every
// statistic downstream.
constexpr double kMinBaseNav = 0.01;

// Net external cash per date inside (start, end]: BUY cost enters, SELL
// proceeds leave. Shared by both entry points so the flow convention cannot
// drift between the period TWR and the daily return series.
//
// v049's synthesized opening BUYs participate like any other row when their
// date is real: backfilled NAV is reconstructed from the same ledger, so the
// NAV jump and the flow that strips it agree. The exception is a migrated
// position whose first_purchase_date was EMPTY — v049 dated its opening BUY
// at migration time, and live pre-v049 snapshots already contained the
// position's value, so counting that row as a flow fabricates a crash (a
// 50k "outflow-sized" BUY against an unchanged NAV). Those fabricated dates
// are precisely detectable: the synthesized note marker plus a transaction
// date on the same day the row was created. Real opening dates (far before
// created_at) keep stripping normally.
bool is_fabricated_opening(const Transaction& t) {
    return t.notes.contains(QLatin1String("synthesized from the holdings row")) &&
           t.transaction_date.left(10) == t.created_at.left(10);
}

QMap<QString, double> external_flows_by_date(const QVector<Transaction>& txns, const QString& window_start,
                                             const QString& window_end,
                                             const QHash<QString, double>& fx_by_symbol) {
    QMap<QString, double> flows;
    for (const auto& t : txns) {
        const QString d = t.transaction_date.left(10);
        if (d <= window_start || d > window_end)
            continue;
        if (is_fabricated_opening(t))
            continue;
        // Trade cash is in the INSTRUMENT currency; the NAV it is subtracted
        // from is in the portfolio currency. Convert, or a CAD purchase
        // strips more than it added and fabricates a loss.
        const double rate = fx_by_symbol.value(t.symbol.toUpper(), 1.0);
        if (t.transaction_type == QLatin1String("BUY"))
            flows[d] += t.quantity * t.price * rate;
        else if (t.transaction_type == QLatin1String("SELL"))
            flows[d] -= t.quantity * t.price * rate;
    }
    return flows;
}

} // namespace

PeriodReturn compute_period_return(QVector<PortfolioSnapshot> snapshots, double live_nav, const QString& live_date,
                                   const QVector<Transaction>& txns,
                                   const QHash<QString, double>& fx_by_symbol) {
    PeriodReturn out;

    std::sort(snapshots.begin(), snapshots.end(),
              [](const PortfolioSnapshot& a, const PortfolioSnapshot& b) { return a.snapshot_date < b.snapshot_date; });

    // The value path the return is measured over: the snapshots plus a final
    // live point. A duplicate date on the live point (snapshot already written
    // today) keeps the fresher live value.
    QVector<QPair<QString, double>> path;
    path.reserve(snapshots.size() + 1);
    for (const auto& s : snapshots)
        path.append({s.snapshot_date.left(10), s.total_value});
    if (!live_date.isEmpty()) {
        const QString d = live_date.left(10);
        if (!path.isEmpty() && path.last().first == d)
            path.last().second = live_nav;
        else
            path.append({d, live_nav});
    }
    if (path.size() < 2)
        return out; // nothing to chain

    // Net external flow per date. Only trade cash counts (see header);
    // flows dated at or before the window start are embedded in the baseline.
    const QMap<QString, double> flow_by_date =
        external_flows_by_date(txns, path.first().first, path.last().first, fx_by_symbol);

    double growth = 1.0;
    bool any_segment = false;
    auto flow_it = flow_by_date.constBegin();
    for (int i = 1; i < path.size(); ++i) {
        const double v_prev = path[i - 1].second;
        const double v_curr = path[i].second;

        // Flows dated inside (prev, curr] belong to this segment. flow_by_date
        // is date-ordered, so a single forward cursor covers every segment.
        double flow = 0;
        while (flow_it != flow_by_date.constEnd() && flow_it.key() <= path[i].first) {
            // Keys ≤ window_start were excluded above, so everything the
            // cursor passes belongs to some segment at or before this one;
            // dates between snapshots (weekend trades) land in the segment
            // ending at the next snapshot, which is exactly this fold.
            flow += flow_it.value();
            ++flow_it;
        }
        out.net_external_flow += flow;

        if (v_prev < kMinBaseNav) {
            // A zero/dust/negative base states no growth rate. Skip and say so.
            out.degraded = true;
            continue;
        }
        growth *= 1.0 + (v_curr - flow - v_prev) / v_prev;
        any_segment = true;
    }

    if (!any_segment)
        return out;

    out.twr_pct = (growth - 1.0) * 100.0;
    out.gain_value = path.last().second - path.first().second - out.net_external_flow;
    out.valid = true;
    return out;
}

QVector<double> flow_adjusted_returns(QVector<PortfolioSnapshot> snapshots, const QVector<Transaction>& txns,
                                      const QHash<QString, double>& fx_by_symbol) {
    std::sort(snapshots.begin(), snapshots.end(),
              [](const PortfolioSnapshot& a, const PortfolioSnapshot& b) { return a.snapshot_date < b.snapshot_date; });

    QVector<double> out;
    if (snapshots.size() < 2)
        return out;

    const QMap<QString, double> flow_by_date =
        external_flows_by_date(txns, snapshots.first().snapshot_date.left(10),
                               snapshots.last().snapshot_date.left(10), fx_by_symbol);

    out.reserve(snapshots.size() - 1);
    auto flow_it = flow_by_date.constBegin();
    for (int i = 1; i < snapshots.size(); ++i) {
        double flow = 0;
        while (flow_it != flow_by_date.constEnd() && flow_it.key() <= snapshots[i].snapshot_date.left(10)) {
            flow += flow_it.value();
            ++flow_it;
        }
        const double prev = snapshots[i - 1].total_value;
        if (prev < kMinBaseNav) {
            out.append(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        out.append((snapshots[i].total_value - flow - prev) / prev * 100.0);
    }
    return out;
}

} // namespace fincept::portfolio
