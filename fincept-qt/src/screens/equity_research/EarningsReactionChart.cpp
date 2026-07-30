// src/screens/equity_research/EarningsReactionChart.cpp
#include "screens/equity_research/EarningsReactionChart.h"

#include "ui/theme/Theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using services::equity::EarningsPoint;
using services::equity::ReactionMetric;

namespace {

// The two series carry their scale in the header line rather than in axis
// gutters, which keeps every number inside a band of its own: header, plot,
// quarter labels. Nothing floats in a margin.
constexpr int kMarginSide   = 10;
constexpr int kHeaderH      = 15;   // "bars: … (±x%)" / "line: … (±y%)"
constexpr int kFooterH      = 18;   // quarter ticks
constexpr int kMinColumnPx  = 26;
constexpr int kLabelH       = 12;
constexpr double kDotR      = 3.2;
constexpr double kLabelGap  = kDotR + 5.0;   // clears the marker, not just its centre
// Fraction of the half-height the data may use. The remainder is headroom so
// an extreme point is never welded to the frame — and it is capped further
// below so a label always fits above the tallest point without flipping.
constexpr double kDataFill  = 0.84;

QString pct_label(double v, int dp = 0) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', dp));
}

/// Centre x of column `i`.
double cx_of(const QRect& plot, double col_w, int i) {
    return plot.left() + col_w * (i + 0.5);
}

} // namespace

EarningsReactionChart::EarningsReactionChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(230);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_StyledBackground, false);
}

void EarningsReactionChart::set_history(const QVector<EarningsPoint>& history) {
    history_ = history;
    // Service order is newest-first; the chart reads left-to-right in time.
    std::reverse(history_.begin(), history_.end());
    update();
}

void EarningsReactionChart::set_metric(ReactionMetric m) {
    if (metric_ == m)
        return;
    metric_ = m;
    update();
}

QVector<EarningsReactionChart::Column> EarningsReactionChart::columns() const {
    QVector<Column> cols;
    cols.reserve(history_.size());
    for (const auto& p : history_) {
        const auto m = services::equity::metric_value(p, metric_);
        if (p.is_estimate) {
            // Kept even with no consensus bar: its point is the current
            // price, which is what carries the curve up to today.
            if (!m.has_value() && !p.move_since_last_pct.has_value())
                continue;
            cols.append({p.timestamp, m, std::nullopt, true, p.move_since_last_pct});
            continue;
        }
        // A quarter with neither number is a blank slot in the series, not a
        // column worth the horizontal space.
        if (!m.has_value() && !p.reaction_pct.has_value())
            continue;
        cols.append({p.timestamp, m, p.reaction_pct, false, std::nullopt});
    }
    return cols;
}

double EarningsReactionChart::axis_extent(const QVector<double>& values) {
    if (values.isEmpty())
        return 1.0;
    QVector<double> mags;
    mags.reserve(values.size());
    for (double v : values) mags.append(std::abs(v));
    std::sort(mags.begin(), mags.end());
    // 80th percentile, so a single blow-out quarter is clipped rather than
    // compressing every other bar into the zero line.
    const int idx = std::min(mags.size() - 1,
                             static_cast<qsizetype>(std::ceil(mags.size() * 0.8)) - 1);
    const double p80 = mags[std::max<qsizetype>(0, idx)];
    return p80 > 1e-6 ? p80 : (mags.last() > 1e-6 ? mags.last() : 1.0);
}

void EarningsReactionChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));

    const QColor grid(ui::colors::BORDER_DIM());
    const QColor text_dim(ui::colors::TEXT_TERTIARY());
    const QColor text_sec(ui::colors::TEXT_SECONDARY());
    const QColor pos(ui::colors::POSITIVE());
    const QColor neg(ui::colors::NEGATIVE());
    const QColor line_col(QStringLiteral("#22d3ee"));

    const auto cols = columns();
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);

    if (cols.isEmpty()) {
        p.setPen(text_dim);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No reported quarters to plot"));
        return;
    }

    const QRect plot(kMarginSide, kHeaderH, width() - 2 * kMarginSide,
                     height() - kHeaderH - kFooterH);
    if (plot.width() < kMinColumnPx || plot.height() < 40)
        return;

    QVector<double> metric_vals, reaction_vals;
    for (const auto& c : cols) {
        if (c.metric) metric_vals.append(*c.metric);
        if (c.reaction) reaction_vals.append(*c.reaction);
        // The live move shares the price axis, so it has to size it too —
        // otherwise a big inter-print drift would be drawn off the top.
        if (c.live_move) reaction_vals.append(*c.live_move);
    }
    const double m_ext = axis_extent(metric_vals);
    const double r_ext = axis_extent(reaction_vals);

    const double zero_y = plot.center().y();
    // Data span leaves headroom at both ends. The cap guarantees a full label
    // fits above the tallest point, so the outside-the-dot placement below
    // never has to flip and land back on its own marker.
    const double half = plot.height() / 2.0;
    const double span = std::max(12.0, std::min(half * kDataFill, half - (kLabelH + kLabelGap + 2)));

    // ── Header: each series names its own scale, so no axis gutters ──────────
    const QString metric_name = metric_ == ReactionMetric::Surprise ? QStringLiteral("surprise vs consensus")
                                : metric_ == ReactionMetric::QoQ    ? QStringLiteral("EPS vs prior quarter")
                                                                    : QStringLiteral("EPS vs year-ago quarter");
    const QRect header(kMarginSide, 0, plot.width(), kHeaderH);
    p.setPen(text_sec);
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter,
               QString("bars: %1  (±%2%)").arg(metric_name, QString::number(m_ext, 'f', 0)));
    p.setPen(line_col);
    const bool has_live = std::any_of(cols.begin(), cols.end(),
                                      [](const Column& c) { return c.projected && c.live_move; });
    p.drawText(header, Qt::AlignRight | Qt::AlignVCenter,
               QString("line: next-session move%1  (±%2%)")
                   .arg(has_live ? QStringLiteral(", ending at price now") : QString(),
                        QString::number(r_ext, 'f', 1)));

    // ── Zero line ────────────────────────────────────────────────────────────
    p.setPen(QPen(grid, 1));
    p.drawLine(QPointF(plot.left(), zero_y), QPointF(plot.right(), zero_y));

    const double col_w = static_cast<double>(plot.width()) / cols.size();
    const double bar_w = std::min(18.0, col_w * 0.42);

    // ── Bars: the selected earnings metric ───────────────────────────────────
    for (int i = 0; i < cols.size(); ++i) {
        if (!cols[i].metric) continue;
        const double v = *cols[i].metric;
        const bool clipped = std::abs(v) > m_ext;
        const double scaled = std::clamp(v / m_ext, -1.0, 1.0);
        const double h = std::max(1.0, std::abs(scaled) * span);
        const double cx = cx_of(plot, col_w, i);
        const QRectF bar(cx - bar_w / 2.0, v >= 0 ? zero_y - h : zero_y, bar_w, h);

        QColor c = v >= 0 ? pos : neg;
        c.setAlpha(140);
        if (cols[i].projected) {
            // Consensus, not a result: hollow with a dashed outline so it
            // never reads as a quarter the company has actually delivered.
            QColor fill = c;
            fill.setAlpha(45);
            p.fillRect(bar, fill);
            QPen outline(c.lighter(130), 1.0, Qt::DashLine);
            p.setPen(outline);
            p.setBrush(Qt::NoBrush);
            p.drawRect(bar);
        } else {
            p.fillRect(bar, c);
        }

        if (clipped && !cols[i].projected) {
            // Chevron at the clipped end so a compressed axis never reads as
            // "this quarter was the same size as its neighbour". It sits in
            // the headroom, never outside the plot.
            const double tip_y = v >= 0 ? bar.top() - 5 : bar.bottom() + 5;
            const double base_y = v >= 0 ? bar.top() : bar.bottom();
            QPainterPath chev;
            chev.moveTo(cx - bar_w / 2.0, base_y);
            chev.lineTo(cx, tip_y);
            chev.lineTo(cx + bar_w / 2.0, base_y);
            p.fillPath(chev, c);
        }
    }

    // ── Line: realised next-session move ─────────────────────────────────────
    QPainterPath path;
    bool started = false;
    QVector<QPointF> dots;
    QVector<double> dot_vals;
    for (int i = 0; i < cols.size(); ++i) {
        if (!cols[i].reaction) {
            started = false;      // a gap breaks the line rather than bridging it
            continue;
        }
        const double v = *cols[i].reaction;
        const double scaled = std::clamp(v / r_ext, -1.0, 1.0);
        const QPointF pt(cx_of(plot, col_w, i), zero_y - scaled * span);
        if (!started) {
            path.moveTo(pt);
            started = true;
        } else {
            path.lineTo(pt);
        }
        dots.append(pt);
        dot_vals.append(v);
    }
    p.setPen(QPen(line_col, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // ── The present: current price against the last completed print ─────────
    // Dashed from the last settled reaction, hollow marker — this point is
    // still moving, and must not look like one of the finished ones.
    QPointF live_pt;
    bool have_live = false;
    for (int i = 0; i < cols.size(); ++i) {
        if (!cols[i].projected || !cols[i].live_move) continue;
        const double scaled = std::clamp(*cols[i].live_move / r_ext, -1.0, 1.0);
        live_pt = QPointF(cx_of(plot, col_w, i), zero_y - scaled * span);
        have_live = true;
    }
    if (have_live && !dots.isEmpty()) {
        p.setPen(QPen(line_col, 1.4, Qt::DashLine));
        p.drawLine(dots.last(), live_pt);
    }

    for (int i = 0; i < dots.size(); ++i) {
        p.setBrush(dot_vals[i] >= 0 ? pos : neg);
        p.setPen(QPen(QColor(ui::colors::BG_SURFACE()), 1.2));
        p.drawEllipse(dots[i], kDotR, kDotR);
    }
    if (have_live) {
        p.setPen(QPen(line_col, 1.6));
        p.setBrush(QColor(ui::colors::BG_SURFACE()));
        p.drawEllipse(live_pt, kDotR + 1.0, kDotR + 1.0);
    }

    // ── Quarter ticks ────────────────────────────────────────────────────────
    const QFontMetrics fm(f);
    p.setBrush(Qt::NoBrush);
    p.setPen(text_dim);
    // Thin the ticks rather than let them collide: every other label, then
    // every third, until they fit.
    const int tick_step = std::max(1, static_cast<int>(std::ceil(34.0 / std::max(1.0, col_w))));
    for (int i = 0; i < cols.size(); ++i) {
        // The trailing column is always labelled — it is the one the reader
        // is standing in, and thinning it away would be the worst omission.
        if (i % tick_step != 0 && !cols[i].projected) continue;
        const double cx = cx_of(plot, col_w, i);
        const auto when = QDateTime::fromSecsSinceEpoch(cols[i].timestamp);
        const QString tick = cols[i].projected
                                 ? (cols[i].metric ? when.toString("MMM yy") + QStringLiteral(" est")
                                                   : QStringLiteral("now"))
                                 : when.toString("MMM yy");
        p.setPen(cols[i].projected ? QColor(ui::colors::AMBER()) : text_dim);
        p.drawText(QRectF(cx - col_w / 2.0, plot.bottom() + 2, col_w, kFooterH - 2),
                   Qt::AlignCenter, tick);
    }

    // ── Value labels on the line ─────────────────────────────────────────────
    // Drawn last so nothing overprints them, and only when the columns are
    // wide enough to hold them side by side.
    if (col_w < 42)
        return;
    for (int i = 0; i < cols.size(); ++i) {
        // The live point is labelled like the rest, so the reader can read
        // "where are we now" off the same axis as the finished prints.
        if (!cols[i].reaction && !(cols[i].projected && cols[i].live_move)) continue;
        const double v = cols[i].reaction ? *cols[i].reaction : *cols[i].live_move;
        const double scaled = std::clamp(v / r_ext, -1.0, 1.0);
        const double y = zero_y - scaled * span;
        const QString lbl = pct_label(v, 1);
        const double w = fm.horizontalAdvance(lbl) + 8;

        // Outside the dot: above a rise, below a fall. The span cap above
        // reserves the room, so the fallback flip is a belt-and-braces case
        // (a very short widget) rather than the normal path.
        double top = v >= 0 ? y - kLabelH - kLabelGap : y + kLabelGap;
        if (top < plot.top())
            top = y + kLabelGap;
        else if (top + kLabelH > plot.bottom())
            top = y - kLabelH - kLabelGap;
        const double left = std::clamp(cx_of(plot, col_w, i) - w / 2.0,
                                       static_cast<double>(plot.left()),
                                       static_cast<double>(plot.right()) - w);
        const QRectF lbl_rect(left, top, w, kLabelH);

        // A clamped bar can reach through where the label sits — seat it on
        // the panel colour, rounded, so the two never overprint into mush.
        QColor plate(ui::colors::BG_SURFACE());
        plate.setAlpha(230);
        p.setPen(Qt::NoPen);
        p.setBrush(plate);
        p.drawRoundedRect(lbl_rect, 2, 2);
        p.setBrush(Qt::NoBrush);
        p.setPen(v >= 0 ? pos : neg);
        p.drawText(lbl_rect, Qt::AlignCenter, lbl);
    }
}

} // namespace fincept::screens
