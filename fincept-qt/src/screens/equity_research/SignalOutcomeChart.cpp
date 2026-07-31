// src/screens/equity_research/SignalOutcomeChart.cpp
#include "screens/equity_research/SignalOutcomeChart.h"

#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using services::equity::EarningsAnalysis;
using services::equity::EarningsVerdict;

namespace {

// Deliberately identical to EarningsReactionChart's. The two charts are
// stacked so a quarter reads straight down through both, which only holds
// while their plot rectangles and column widths agree exactly.
constexpr int kMarginSide  = 10;
constexpr int kHeaderH     = 15;
constexpr int kFooterH     = 18;
constexpr int kMinColumnPx = 26;
constexpr double kDotR     = 3.2;
constexpr double kDataFill = 0.84;

double cx_of(const QRect& plot, double col_w, int i) {
    return plot.left() + col_w * (i + 0.5);
}

/// Axis half-range: the 80th percentile of |value|, so a single outlier can't
/// flatten every other quarter. Same rule as the chart below.
double axis_extent(const QVector<double>& values) {
    if (values.isEmpty())
        return 1.0;
    QVector<double> mags;
    mags.reserve(values.size());
    for (const double v : values)
        mags.append(std::abs(v));
    std::sort(mags.begin(), mags.end());
    const auto idx = std::min<qsizetype>(mags.size() - 1,
                                        static_cast<qsizetype>(mags.size() * 0.8));
    return std::max(1.0, mags.at(idx));
}

} // namespace

SignalOutcomeChart::SignalOutcomeChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(150);
}

void SignalOutcomeChart::set_data(const EarningsAnalysis& analysis,
                                  const QVector<EarningsSignalRecord>& recorded,
                                  const EarningsVerdict& live) {
    cols_.clear();
    summary_.clear();

    for (const auto& q : services::equity::reconstruct_predictions(analysis)) {
        Column c;
        c.timestamp = q.timestamp;
        c.predicted = q.predicted_move_pct;
        c.actual = q.actual_move_pct;
        c.reconstructed = true;
        cols_.append(c);
    }

    // A reading genuinely written down before the print beats the
    // reconstruction for that quarter. Matched on the calendar date in market
    // time: Yahoo shifts a scheduled placeholder to the announcement time once
    // a company reports, so the raw seconds never agree.
    const QTimeZone et("America/New_York");
    auto et_date = [&et](qint64 ts) {
        return QDateTime::fromSecsSinceEpoch(ts).toTimeZone(et).date();
    };
    for (const auto& r : recorded) {
        if (!r.predicted_move_pct)
            continue;
        for (auto& c : cols_) {
            if (et_date(c.timestamp) != et_date(r.report_ts))
                continue;
            c.predicted = r.predicted_move_pct;
            c.reconstructed = false;
            break;
        }
    }

    // The coming print: a prediction with no outcome yet, in its own column so
    // it lines up with the projected column of the chart below.
    if (analysis.next.timestamp.has_value()) {
        Column c;
        c.timestamp = *analysis.next.timestamp;
        c.predicted = live.predicted_move_pct;
        c.projected = true;
        c.reconstructed = false;
        cols_.append(c);
    }

    // ── Subtitle: only over points that have both halves ─────────────────────
    int pairs = 0, hits = 0, live_pairs = 0;
    double abs_err = 0;
    for (const auto& c : cols_) {
        if (c.projected || !c.predicted || !c.actual)
            continue;
        ++pairs;
        if (!c.reconstructed)
            ++live_pairs;
        abs_err += std::abs(*c.predicted - *c.actual);
        // Direction only counts when the estimate committed to one. A near-zero
        // prediction is not a call, and grading it as one would manufacture a
        // hit rate out of noise.
        if (std::abs(*c.predicted) >= 0.25 && (*c.predicted > 0) == (*c.actual > 0))
            ++hits;
    }
    if (pairs > 0) {
        summary_ = QString("direction right on %1 of %2 · mean miss %3 pp")
                       .arg(hits).arg(pairs)
                       .arg(QString::number(abs_err / pairs, 'f', 1));
        if (live_pairs == 0)
            summary_ += QStringLiteral(" · all reconstructed");
        else
            summary_ += QString(" · %1 recorded live").arg(live_pairs);
    }
    update();
}

void SignalOutcomeChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));

    const QColor grid(ui::colors::BORDER_DIM());
    const QColor text_sec(ui::colors::TEXT_SECONDARY());
    const QColor actual_col(ui::colors::TEXT_PRIMARY());
    const QColor pred_col("#a855f7");   // the scorecard's accent — this is its line

    if (cols_.isEmpty()) {
        p.setPen(text_sec);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No reported quarters to compare"));
        return;
    }

    const QRect plot(kMarginSide, kHeaderH, width() - 2 * kMarginSide,
                     height() - kHeaderH - kFooterH);
    if (plot.width() < kMinColumnPx || plot.height() < 40)
        return;

    // One shared scale. Unlike the chart below — where bars and line measure
    // different quantities — both series here are a percentage move on the
    // same print, so plotting them against different axes would make the gap
    // between them meaningless, and the gap is the entire subject.
    QVector<double> all;
    for (const auto& c : cols_) {
        if (c.predicted) all.append(*c.predicted);
        if (c.actual) all.append(*c.actual);
    }
    const double ext = axis_extent(all);
    const double zero_y = plot.center().y();
    const double span = std::max(12.0, plot.height() / 2.0 * kDataFill);

    auto y_of = [&](double v) {
        return zero_y - std::clamp(v / ext, -1.0, 1.0) * span;
    };

    const QRect header(kMarginSide, 0, plot.width(), kHeaderH);
    p.setPen(pred_col);
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("predicted — dotted where rebuilt after the fact"));
    p.setPen(text_sec);
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("actual next-session move  (±%1%)").arg(QString::number(ext, 'f', 1)));

    p.setPen(QPen(grid, 1));
    p.drawLine(QPointF(plot.left(), zero_y), QPointF(plot.right(), zero_y));

    const double col_w = static_cast<double>(plot.width()) / cols_.size();

    // ── The gap, shaded ──────────────────────────────────────────────────────
    // Drawn first so both lines sit on top of it. This band IS the error, and
    // it is the reason the two series share one chart rather than two.
    for (int i = 0; i < cols_.size(); ++i) {
        if (!cols_[i].predicted || !cols_[i].actual)
            continue;
        const double cx = cx_of(plot, col_w, i);
        const double w = std::min(10.0, col_w * 0.30);
        const double y1 = y_of(*cols_[i].predicted);
        const double y2 = y_of(*cols_[i].actual);
        QColor fill(pred_col);
        fill.setAlpha(46);
        p.fillRect(QRectF(cx - w / 2.0, std::min(y1, y2), w, std::abs(y2 - y1)), fill);
    }

    // ── Actual ───────────────────────────────────────────────────────────────
    QPainterPath actual_path;
    QVector<QPointF> actual_dots;
    bool started = false;
    for (int i = 0; i < cols_.size(); ++i) {
        if (!cols_[i].actual) continue;
        const QPointF pt(cx_of(plot, col_w, i), y_of(*cols_[i].actual));
        actual_dots.append(pt);
        if (!started) { actual_path.moveTo(pt); started = true; }
        else            actual_path.lineTo(pt);
    }
    p.setPen(QPen(actual_col, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(actual_path);

    // ── Predicted, split at the reconstruction boundary ──────────────────────
    // Segment by segment rather than as one path: the dash pattern has to
    // change mid-series, and a reader must be able to see exactly where the
    // rebuilt estimates stop and the recorded ones start.
    QPointF prev;
    bool have_prev = false;
    bool prev_recon = true;
    for (int i = 0; i < cols_.size(); ++i) {
        if (!cols_[i].predicted) { have_prev = false; continue; }
        const QPointF pt(cx_of(plot, col_w, i), y_of(*cols_[i].predicted));
        if (have_prev) {
            // A segment is only solid when BOTH ends were recorded live.
            const bool solid = !prev_recon && !cols_[i].reconstructed;
            p.setPen(QPen(pred_col, 1.6, solid ? Qt::SolidLine : Qt::DotLine));
            p.drawLine(prev, pt);
        }
        prev = pt;
        prev_recon = cols_[i].reconstructed;
        have_prev = true;
    }

    for (const auto& pt : actual_dots) {
        p.setBrush(actual_col);
        p.setPen(QPen(QColor(ui::colors::BG_SURFACE()), 1.2));
        p.drawEllipse(pt, kDotR, kDotR);
    }
    for (int i = 0; i < cols_.size(); ++i) {
        if (!cols_[i].predicted) continue;
        const QPointF pt(cx_of(plot, col_w, i), y_of(*cols_[i].predicted));
        // Hollow while the estimate is a reconstruction or still unsettled;
        // filled only once it was both recorded beforehand and resolved.
        const bool settled = !cols_[i].reconstructed && !cols_[i].projected;
        p.setBrush(settled ? QBrush(pred_col) : QBrush(QColor(ui::colors::BG_SURFACE())));
        p.setPen(QPen(pred_col, 1.4));
        p.drawEllipse(pt, kDotR, kDotR);
    }

    // ── Quarter ticks, on the same cadence as the chart below ────────────────
    p.setPen(text_sec);
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 2.0));
    p.setFont(f);
    const int step = std::max(1, static_cast<int>(std::ceil(34.0 / std::max(1.0, col_w))));
    for (int i = 0; i < cols_.size(); i += step) {
        const auto when = QDateTime::fromSecsSinceEpoch(cols_[i].timestamp);
        p.drawText(QRectF(cx_of(plot, col_w, i) - col_w / 2.0, plot.bottom() + 2, col_w,
                          kFooterH - 2),
                   Qt::AlignCenter, when.toString("MMM yy"));
    }
}

} // namespace fincept::screens
