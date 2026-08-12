// src/screens/equity_research/EarningsReactionChart.cpp
#include "screens/equity_research/EarningsReactionChart.h"

#include "ui/theme/Theme.h"

#include <QMouseEvent>
#include <QTimeZone>
#include <QToolTip>

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

// The two series' colours, named once. The hover text describes the lines by
// colour, so a literal drifting between the painter and the tooltip would have
// the tooltip confidently pointing at the wrong line.
const QString kLineColor = QStringLiteral("#22d3ee");   // realised move
const QString kPredColor = QStringLiteral("#a855f7");   // the signal's estimate

QString pct_label(double v, int dp = 0) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', dp));
}

/// Centre x of column `i`.
double cx_of(const QRect& plot, double col_w, int i) {
    return plot.left() + col_w * (i + 0.5);
}

} // namespace

EarningsReactionChart::EarningsReactionChart(QWidget* parent) : QWidget(parent) {
    // Hover explains a quarter without needing a click or a selection.
    setMouseTracking(true);
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

void EarningsReactionChart::set_prediction_series(const QVector<PredictionSeries>& series) {
    series_ = series;
    update();
}

namespace {
/// A series' estimate for one column, matched on exact timestamp — every
/// series is built from the same history rows, so a miss means that predictor
/// had nothing to say about that quarter rather than a lookup needing slack.
const services::equity::QuarterPrediction* point_at(
    const EarningsReactionChart::PredictionSeries& s, qint64 ts) {
    for (const auto& q : s.points)
        if (q.timestamp == ts && q.predicted_move_pct.has_value())
            return &q;
    return nullptr;
}
} // namespace

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

QRect EarningsReactionChart::plot_rect() const {
    return QRect(kMarginSide, kHeaderH, width() - 2 * kMarginSide,
                 height() - kHeaderH - kFooterH);
}

int EarningsReactionChart::column_at(double x, const QVector<Column>& cols) const {
    const QRect plot = plot_rect();
    if (cols.isEmpty() || plot.width() <= 0 || x < plot.left() || x > plot.right())
        return -1;
    const double col_w = static_cast<double>(plot.width()) / cols.size();
    const int i = static_cast<int>((x - plot.left()) / col_w);
    return std::clamp(i, 0, static_cast<int>(cols.size()) - 1);
}

QString EarningsReactionChart::tooltip_for(const Column& c) const {
    const QString metric_name = metric_ == ReactionMetric::Surprise
                                    ? QStringLiteral("Surprise vs consensus")
                                : metric_ == ReactionMetric::QoQ
                                    ? QStringLiteral("EPS vs prior quarter")
                                    : QStringLiteral("EPS vs year-ago quarter");
    const QColor pred_col(kPredColor);

    QStringList rows;
    // These are ANNOUNCEMENT timestamps (they carry a real time of day), not
    // exchange-midnight bar stamps — the date belongs to the exchange's
    // calendar. Rendered in ET like every other announcement date on the tab;
    // UTC put a 20:00+ ET print on the next day, viewer-local shifted with
    // the reader.
    const auto when =
        QDateTime::fromSecsSinceEpoch(c.timestamp).toTimeZone(QTimeZone("America/New_York"));
    rows << QString("<b>%1</b>").arg(c.projected ? QString("Next report · %1").arg(when.toString("d MMM yyyy"))
                                                 : when.toString("d MMM yyyy"));

    if (c.metric)
        rows << QString("Bar — %1: <b>%2</b>").arg(metric_name, pct_label(*c.metric, 1));

    // The solid line.
    if (c.reaction) {
        rows << QString("<span style='color:%1'>Solid line</span> — actual next-session move: "
                        "<b>%2</b>")
                    .arg(kLineColor, pct_label(*c.reaction, 2));
    } else if (c.projected && c.live_move) {
        rows << QString("<span style='color:%1'>Solid line</span> — this print hasn't happened. "
                        "The dashed leg carries the price to today: <b>%2</b> since the last "
                        "print's close.")
                    .arg(kLineColor, pct_label(*c.live_move, 2));
    }

    // Every predictor on screen, with what it said here and how far it missed.
    bool any = false;
    for (const auto& sr : series_) {
        const auto* q = point_at(sr, c.timestamp);
        if (!q) continue;
        any = true;
        QString line = QString("<span style='color:%1'>%2</span> — predicted <b>%3</b>")
                           .arg(sr.color, sr.label, pct_label(*q->predicted_move_pct, 2));
        if (c.reaction) {
            line += QString(", missed by <b>%1 pp</b>")
                        .arg(QString::number(std::abs(*q->predicted_move_pct - *c.reaction), 'f', 2));
        }
        rows << line;
        if (series_.size() == 1 && q->bound_pct) {
            rows << QString("Shaded band — the most it could have said either way: <b>±%1%</b>")
                        .arg(QString::number(*q->bound_pct, 'f', 2));
        }
        rows << (q->reconstructed
                     ? QStringLiteral("<i>Dotted: rebuilt after the print. Consensus and "
                                      "revisions aren't published with history, so a past "
                                      "quarter can only be answered from what survives.</i>")
                     : QStringLiteral("<i>Solid: recorded before the print, whole model "
                                      "available.</i>"));
    }
    if (!any && !c.projected)
        rows << QStringLiteral("<i>No predictor had anything to say about this quarter.</i>");

    // Fixed width so the paragraph wraps into a box rather than one long line;
    // Qt only word-wraps a tooltip when it is rich text, and only bounds the
    // width when a table sets one.
    return QStringLiteral("<qt><table width=\"330\" cellpadding=\"0\" cellspacing=\"0\"><tr><td>%1"
                          "</td></tr></table></qt>")
        .arg(rows.join(QStringLiteral("<br>")));
}

void EarningsReactionChart::mouseMoveEvent(QMouseEvent* e) {
    const auto cols = columns();
    const int i = column_at(e->position().x(), cols);
    if (i < 0)
        QToolTip::hideText();
    else
        QToolTip::showText(e->globalPosition().toPoint(), tooltip_for(cols[i]), this);
    QWidget::mouseMoveEvent(e);
}

void EarningsReactionChart::leaveEvent(QEvent* e) {
    QToolTip::hideText();
    QWidget::leaveEvent(e);
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
    const QColor line_col(kLineColor);

    const auto cols = columns();
    QFont f = p.font();
    f.setPixelSize(9);
    p.setFont(f);

    if (cols.isEmpty()) {
        p.setPen(text_dim);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No reported quarters to plot"));
        return;
    }

    const QRect plot = plot_rect();
    if (plot.width() < kMinColumnPx || plot.height() < 40)
        return;

    QVector<double> metric_vals, reaction_vals;
    for (const auto& c : cols) {
        if (c.metric) metric_vals.append(*c.metric);
        if (c.reaction) reaction_vals.append(*c.reaction);
        // The live move shares the price axis, so it has to size it too —
        // otherwise a big inter-print drift would be drawn off the top.
        if (c.live_move) reaction_vals.append(*c.live_move);
        // The prediction is a move on the same session as the reaction, so it
        // belongs on that axis — and has to be allowed to stretch it, or a
        // bold estimate would be drawn clipped against the frame.
        // Every predictor shares this axis, and the band is drawn, so both
        // have to fit — sizing to the reaction line alone would clip them.
        for (const auto& s : series_) {
            if (const auto* q = point_at(s, c.timestamp)) {
                reaction_vals.append(*q->predicted_move_pct);
                if (q->bound_pct) reaction_vals.append(*q->bound_pct);
            }
        }
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
        // Same off-scale honesty as the bars' chevron: a point pinned at the
        // frame must not read as "this move was exactly axis-sized". A small
        // chevron in the headroom marks the clip; the value label (when the
        // columns are wide enough) carries the real number.
        if (std::abs(v) > r_ext) {
            const double dir = v >= 0 ? -1.0 : 1.0;   // screen-y: up is negative
            QPainterPath chev;
            chev.moveTo(pt.x() - 4.0, pt.y() + dir * 4.0);
            chev.lineTo(pt.x(), pt.y() + dir * 9.0);
            chev.lineTo(pt.x() + 4.0, pt.y() + dir * 4.0);
            p.fillPath(chev, QColor(line_col));
        }
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

    // ── What the signal predicted ────────────────────────────────────────────
    // Same axis as the reaction line above, so the vertical distance between
    // the two IS the error. Drawn segment by segment because the dash pattern
    // changes mid-series: dotted while the estimates were rebuilt after the
    // fact, solid only between two that were recorded before their prints.
    // ── Each selected predictor ──────────────────────────────────────────────
    // Same axis as the reaction line, so the vertical distance from a line to
    // that one IS its error. Segment by segment because the dash pattern
    // changes mid-series: dotted while estimates were rebuilt after the fact,
    // solid between two recorded before their prints.
    //
    // The band is drawn only for a lone series. With several on screen the
    // question is which line tracks best, and overlapping envelopes bury it.
    for (const auto& sr : series_) {
        const QColor col(sr.color);
        if (series_.size() == 1) {
            QPolygonF top, bottom;
            auto flush = [&]() {
                if (top.size() < 2) { top.clear(); bottom.clear(); return; }
                QPolygonF poly = top;
                for (int k = bottom.size() - 1; k >= 0; --k) poly << bottom[k];
                QColor fill(col);
                fill.setAlpha(28);
                p.setPen(Qt::NoPen);
                p.setBrush(fill);
                p.drawPolygon(poly);
                top.clear();
                bottom.clear();
            };
            for (int i = 0; i < cols.size(); ++i) {
                const auto* q = point_at(sr, cols[i].timestamp);
                if (!q || !q->bound_pct) { flush(); continue; }
                const double b = std::clamp(*q->bound_pct / r_ext, 0.0, 1.0);
                const double cx = cx_of(plot, col_w, i);
                top    << QPointF(cx, zero_y - b * span);
                bottom << QPointF(cx, zero_y + b * span);
            }
            flush();
        }

        QPointF prev;
        bool have_prev = false;
        bool prev_recon = true;
        for (int i = 0; i < cols.size(); ++i) {
            const auto* q = point_at(sr, cols[i].timestamp);
            if (!q) { have_prev = false; continue; }
            const double scaled = std::clamp(*q->predicted_move_pct / r_ext, -1.0, 1.0);
            const QPointF pt(cx_of(plot, col_w, i), zero_y - scaled * span);
            if (have_prev) {
                const bool solid = !prev_recon && !q->reconstructed;
                p.setPen(QPen(col, 1.4, solid ? Qt::SolidLine : Qt::DotLine));
                p.drawLine(prev, pt);
            }
            prev = pt;
            prev_recon = q->reconstructed;
            have_prev = true;
        }
        for (int i = 0; i < cols.size(); ++i) {
            const auto* q = point_at(sr, cols[i].timestamp);
            if (!q) continue;
            const double scaled = std::clamp(*q->predicted_move_pct / r_ext, -1.0, 1.0);
            const QPointF pt(cx_of(plot, col_w, i), zero_y - scaled * span);
            p.setBrush(q->reconstructed ? QBrush(QColor(ui::colors::BG_SURFACE())) : QBrush(col));
            p.setPen(QPen(col, 1.2));
            p.drawEllipse(pt, kDotR - 0.6, kDotR - 0.6);
            // Off-scale marker, same convention as the bars and the reaction
            // line: a clipped point must not read as an axis-sized estimate.
            if (std::abs(*q->predicted_move_pct) > r_ext) {
                const double dir = *q->predicted_move_pct >= 0 ? -1.0 : 1.0;
                QPainterPath chev;
                chev.moveTo(pt.x() - 3.0, pt.y() + dir * 4.0);
                chev.lineTo(pt.x(), pt.y() + dir * 8.0);
                chev.lineTo(pt.x() + 3.0, pt.y() + dir * 4.0);
                p.fillPath(chev, col);
            }
        }
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
        // Announcement timestamps → ET, same rationale as the tooltip above.
        const auto when =
            QDateTime::fromSecsSinceEpoch(cols[i].timestamp).toTimeZone(QTimeZone("America/New_York"));
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
