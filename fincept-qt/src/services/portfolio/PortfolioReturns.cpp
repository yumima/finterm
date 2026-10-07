// src/services/portfolio/PortfolioReturns.cpp
#include "services/portfolio/PortfolioReturns.h"

#include "services/portfolio/PortfolioDates.h"

#include <QDate>
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
// 50k "outflow-sized" BUY against an unchanged NAV). See
// is_fabricated_opening() (PortfolioDates.h). The NAV backfill agrees: it
// never reconstructs a date before such a row, so no backfilled NAV lacks
// the position the return math assumes was already held.
//
// Flows are keyed by the LOCAL trade date — the calendar the snapshots are
// written on (PortfolioDates.h).
QMap<QString, double> external_flows_by_date(const QVector<Transaction>& txns, const QString& window_start,
                                             const QString& window_end,
                                             const FxRates& fx) {
    QMap<QString, double> flows;
    for (const auto& t : txns) {
        const QString d = transaction_local_date(t);
        if (d <= window_start || d > window_end)
            continue;
        if (is_fabricated_opening(t))
            continue;
        // Trade cash is in the INSTRUMENT currency; the NAV it is subtracted
        // from is in the portfolio currency. Convert, or a CAD purchase
        // strips more than it added and fabricates a loss — at the rate that
        // applied on the TRADE DATE, matching how the NAV series itself was
        // converted, so currency drift since the trade isn't read as return.
        const double rate = fx.rate_for(t.symbol, d);
        if (t.transaction_type == QLatin1String("BUY"))
            flows[d] += t.quantity * t.price * rate;
        else if (t.transaction_type == QLatin1String("SELL"))
            flows[d] -= t.quantity * t.price * rate;
    }
    return flows;
}

// Trading sessions inside (from, to]. See trading_day_returns() for the
// calendar rule.
class SessionCalendar {
  public:
    explicit SessionCalendar(const QSet<QString>& cal) : cal_(cal) {
        for (const auto& d : cal) {
            if (first_.isEmpty() || d < first_)
                first_ = d;
            if (last_.isEmpty() || d > last_)
                last_ = d;
        }
    }

    bool is_session(const QDate& d) const {
        const QString key = d.toString(Qt::ISODate);
        if (!cal_.isEmpty() && key >= first_ && key <= last_)
            return cal_.contains(key);
        return d.dayOfWeek() <= 5;
    }

    int sessions_between(const QString& from, const QString& to) const {
        const QDate a = QDate::fromString(from.left(10), Qt::ISODate);
        const QDate b = QDate::fromString(to.left(10), Qt::ISODate);
        if (!a.isValid() || !b.isValid() || b <= a)
            return 0;
        int n = 0;
        for (QDate d = a.addDays(1); d <= b; d = d.addDays(1))
            n += is_session(d) ? 1 : 0;
        return n;
    }

  private:
    const QSet<QString>& cal_;
    QString first_, last_;
};

} // namespace

PeriodReturn compute_period_return(QVector<PortfolioSnapshot> snapshots, double live_nav, const QString& live_date,
                                   const QVector<Transaction>& txns, const FxRates& fx) {
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
        external_flows_by_date(txns, path.first().first, path.last().first, fx);

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
        if (!std::isfinite(flow)) {
            // A flow with an unknown FX rate: the window's growth cannot be
            // chained honestly. No return at all (valid stays false) — and
            // flag it so callers don't fall back to a naive NAV ratio.
            out = PeriodReturn{};
            out.fx_unknown = true;
            return out;
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
                                      const FxRates& fx) {
    std::sort(snapshots.begin(), snapshots.end(),
              [](const PortfolioSnapshot& a, const PortfolioSnapshot& b) { return a.snapshot_date < b.snapshot_date; });

    QVector<double> out;
    if (snapshots.size() < 2)
        return out;

    const QMap<QString, double> flow_by_date =
        external_flows_by_date(txns, snapshots.first().snapshot_date.left(10),
                               snapshots.last().snapshot_date.left(10), fx);

    out.reserve(snapshots.size() - 1);
    auto flow_it = flow_by_date.constBegin();
    for (int i = 1; i < snapshots.size(); ++i) {
        double flow = 0;
        while (flow_it != flow_by_date.constEnd() && flow_it.key() <= snapshots[i].snapshot_date.left(10)) {
            flow += flow_it.value();
            ++flow_it;
        }
        const double prev = snapshots[i - 1].total_value;
        // A flow with an unknown FX rate (NaN) leaves the day's flow — and so
        // its return — unknown: can't-compute, never a guessed conversion.
        if (prev < kMinBaseNav || !std::isfinite(flow)) {
            out.append(std::numeric_limits<double>::quiet_NaN());
            continue;
        }
        out.append((snapshots[i].total_value - flow - prev) / prev * 100.0);
    }
    return out;
}

QVector<SegmentReturn> trading_day_returns(QVector<PortfolioSnapshot> snapshots, const QVector<Transaction>& txns,
                                           const FxRates& fx, const QSet<QString>& calendar) {
    std::sort(snapshots.begin(), snapshots.end(),
              [](const PortfolioSnapshot& a, const PortfolioSnapshot& b) { return a.snapshot_date < b.snapshot_date; });
    // The raw consecutive-snapshot returns, flow convention and all, are
    // exactly flow_adjusted_returns(); only the calendar treatment is new.
    const QVector<double> raw = flow_adjusted_returns(snapshots, txns, fx);
    QVector<SegmentReturn> out;
    if (raw.isEmpty())
        return out;
    out.reserve(raw.size());

    const SessionCalendar cal(calendar);
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    // Pending merge state: growth chained since `start`, and the sessions it
    // has crossed so far.
    QString start = snapshots.first().snapshot_date.left(10);
    double growth = 1.0;
    bool unknown = false;
    int sessions = 0;
    for (int i = 0; i < raw.size(); ++i) {
        const QString end = snapshots[i + 1].snapshot_date.left(10);
        if (std::isnan(raw[i]))
            unknown = true;
        else
            growth *= 1.0 + raw[i] / 100.0;
        sessions += cal.sessions_between(snapshots[i].snapshot_date, end);
        if (sessions == 0)
            continue; // no session crossed yet (weekend / holiday row) — keep chaining
        out.append({start, end, unknown ? kNaN : (growth - 1.0) * 100.0, sessions});
        start = end;
        growth = 1.0;
        unknown = false;
        sessions = 0;
    }
    // Trailing pieces that crossed no session (e.g. a Saturday row after
    // Friday's): fold into the last emitted segment so the chained growth is
    // complete. Its session count is unchanged — no session was added.
    if (!out.isEmpty() && (unknown || std::abs(growth - 1.0) > 0.0)) {
        SegmentReturn& last = out.last();
        if (unknown || std::isnan(last.pct))
            last.pct = kNaN;
        else
            last.pct = ((1.0 + last.pct / 100.0) * growth - 1.0) * 100.0;
        last.end_date = snapshots.last().snapshot_date.left(10);
    } else if (!out.isEmpty()) {
        out.last().end_date = snapshots.last().snapshot_date.left(10);
    }
    return out;
}

QVector<double> segment_flows(const QVector<NavPoint>& path, const QVector<Transaction>& txns, const FxRates& fx) {
    QVector<double> out(path.size(), 0.0);
    if (path.size() < 2)
        return out;
    const QMap<QString, double> flow_by_date =
        external_flows_by_date(txns, path.first().date.left(10), path.last().date.left(10), fx);
    auto it = flow_by_date.constBegin();
    for (int i = 1; i < path.size(); ++i) {
        double flow = 0;
        while (it != flow_by_date.constEnd() && it.key() <= path[i].date.left(10)) {
            flow += it.value();
            ++it;
        }
        out[i] = flow;
    }
    return out;
}

QVector<double> twr_index(const QVector<NavPoint>& path, const QVector<Transaction>& txns, const FxRates& fx) {
    QVector<double> out(path.size(), std::numeric_limits<double>::quiet_NaN());
    if (path.isEmpty())
        return out;
    const QVector<double> flows = segment_flows(path, txns, fx);
    double level = 1.0;
    out[0] = level;
    for (int i = 1; i < path.size(); ++i) {
        if (!std::isfinite(flows[i]) || !std::isfinite(level)) {
            level = std::numeric_limits<double>::quiet_NaN();
        } else if (path[i - 1].value >= kMinBaseNav) {
            level *= 1.0 + (path[i].value - flows[i] - path[i - 1].value) / path[i - 1].value;
        }
        out[i] = level;
    }
    return out;
}

} // namespace fincept::portfolio
