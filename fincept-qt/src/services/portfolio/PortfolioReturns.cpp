// src/services/portfolio/PortfolioReturns.cpp
#include "services/portfolio/PortfolioReturns.h"

#include <QMap>

#include <algorithm>

namespace fincept::portfolio {

PeriodReturn compute_period_return(QVector<PortfolioSnapshot> snapshots, double live_nav, const QString& live_date,
                                   const QVector<Transaction>& txns) {
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

    // Net external flow per date. Only trade cash counts (see header).
    QMap<QString, double> flow_by_date;
    const QString window_start = path.first().first;
    const QString window_end = path.last().first;
    for (const auto& t : txns) {
        const QString d = t.transaction_date.left(10);
        if (d <= window_start || d > window_end)
            continue; // embedded in the baseline, or beyond the live point
        if (t.transaction_type == QLatin1String("BUY"))
            flow_by_date[d] += t.quantity * t.price;
        else if (t.transaction_type == QLatin1String("SELL"))
            flow_by_date[d] -= t.quantity * t.price;
    }

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

        if (v_prev <= 0) {
            // A zero/negative base states no growth rate. Skip and say so.
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

} // namespace fincept::portfolio
