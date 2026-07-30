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

constexpr int kMarginLeft   = 46;   // room for the metric axis labels
constexpr int kMarginRight  = 46;   // room for the reaction axis labels
constexpr int kMarginTop    = 18;
constexpr int kMarginBottom = 26;   // quarter labels
constexpr int kMinColumnPx  = 26;

QString pct_label(double v, int dp = 0) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', dp));
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
        // A quarter with neither number is a blank slot in the series, not a
        // column worth the horizontal space.
        const auto m = services::equity::metric_value(p, metric_);
        if (!m.has_value() && !p.reaction_pct.has_value())
            continue;
        cols.append({p.timestamp, m, p.reaction_pct});
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

    const QRect plot(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
                     height() - kMarginTop - kMarginBottom);
    if (plot.width() < kMinColumnPx || plot.height() < 40)
        return;

    QVector<double> metric_vals, reaction_vals;
    for (const auto& c : cols) {
        if (c.metric) metric_vals.append(*c.metric);
        if (c.reaction) reaction_vals.append(*c.reaction);
    }
    const double m_ext = axis_extent(metric_vals);
    const double r_ext = axis_extent(reaction_vals);

    const int zero_y = plot.center().y();
    const double half = plot.height() / 2.0;

    // ── Frame: zero line + quarter gridlines ─────────────────────────────────
    p.setPen(QPen(grid, 1));
    p.drawLine(plot.left(), zero_y, plot.right(), zero_y);

    const double col_w = static_cast<double>(plot.width()) / cols.size();
    const double bar_w = std::min(18.0, col_w * 0.42);

    // ── Axis labels: each series carries its own scale ───────────────────────
    p.setPen(text_dim);
    p.drawText(QRect(0, plot.top() - 6, kMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter,
               pct_label(m_ext));
    p.drawText(QRect(0, zero_y - 6, kMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("0"));
    p.drawText(QRect(0, plot.bottom() - 6, kMarginLeft - 6, 12), Qt::AlignRight | Qt::AlignVCenter,
               pct_label(-m_ext));
    p.setPen(line_col);
    p.drawText(QRect(plot.right() + 6, plot.top() - 6, kMarginRight - 6, 12),
               Qt::AlignLeft | Qt::AlignVCenter, pct_label(r_ext, 1));
    p.drawText(QRect(plot.right() + 6, plot.bottom() - 6, kMarginRight - 6, 12),
               Qt::AlignLeft | Qt::AlignVCenter, pct_label(-r_ext, 1));

    // ── Bars: the selected earnings metric ───────────────────────────────────
    for (int i = 0; i < cols.size(); ++i) {
        if (!cols[i].metric) continue;
        const double v = *cols[i].metric;
        const bool clipped = std::abs(v) > m_ext;
        const double scaled = std::clamp(v / m_ext, -1.0, 1.0);
        const int h = static_cast<int>(std::round(std::abs(scaled) * half));
        const double cx = plot.left() + col_w * (i + 0.5);
        const QRectF bar(cx - bar_w / 2.0, v >= 0 ? zero_y - h : zero_y, bar_w, std::max(1, h));

        QColor c = v >= 0 ? pos : neg;
        c.setAlpha(150);
        p.fillRect(bar, c);

        if (clipped) {
            // Chevron at the clipped end so a compressed axis never reads as
            // "this quarter was the same size as its neighbour".
            const double tip_y = v >= 0 ? bar.top() - 4 : bar.bottom() + 4;
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
        const QPointF pt(plot.left() + col_w * (i + 0.5), zero_y - scaled * half);
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
    p.drawPath(path);
    for (int i = 0; i < dots.size(); ++i) {
        p.setBrush(dot_vals[i] >= 0 ? pos : neg);
        p.setPen(QPen(QColor(ui::colors::BG_BASE()), 1));
        p.drawEllipse(dots[i], 3.2, 3.2);
    }

    // ── Value labels + quarter ticks ─────────────────────────────────────────
    // Only when the columns are wide enough to hold them without colliding.
    const QFontMetrics fm(f);
    const bool room_for_values = col_w >= 42;
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < cols.size(); ++i) {
        const double cx = plot.left() + col_w * (i + 0.5);
        const auto when = QDateTime::fromSecsSinceEpoch(cols[i].timestamp);
        p.setPen(text_dim);
        p.drawText(QRectF(cx - col_w / 2.0, plot.bottom() + 6, col_w, 14),
                   Qt::AlignCenter, when.toString("MMM yy"));

        if (!room_for_values || !cols[i].reaction) continue;
        const double v = *cols[i].reaction;
        const double scaled = std::clamp(v / r_ext, -1.0, 1.0);
        const double y = zero_y - scaled * half;
        const QString lbl = pct_label(v, 1);
        // Park the label on the far side of the dot from the zero line so it
        // never lands on the bar it belongs to.
        const double ly = v >= 0 ? y - 14 : y + 2;
        // Keep the label inside the plot: a dot pinned to the bottom of the
        // reaction axis would otherwise print its value over the quarter
        // labels in the margin below.
        const QRectF lbl_rect(cx - fm.horizontalAdvance(lbl) / 2.0 - 2,
                              std::clamp(ly, static_cast<double>(plot.top() - 14),
                                         static_cast<double>(plot.bottom() - 12)),
                              fm.horizontalAdvance(lbl) + 4, 12);
        // A clamped bar can reach right through where this label sits — back
        // it with the panel colour so the two never overprint into mush.
        QColor plate(ui::colors::BG_SURFACE());
        plate.setAlpha(220);
        p.fillRect(lbl_rect, plate);
        p.setPen(v >= 0 ? pos : neg);
        p.drawText(lbl_rect, Qt::AlignCenter, lbl);
    }

    // ── Legend ───────────────────────────────────────────────────────────────
    const QString metric_name = metric_ == ReactionMetric::Surprise ? QStringLiteral("surprise vs consensus")
                                : metric_ == ReactionMetric::QoQ    ? QStringLiteral("EPS vs prior quarter")
                                                                    : QStringLiteral("EPS vs year-ago quarter");
    p.setPen(text_sec);
    p.drawText(QRect(kMarginLeft, 2, plot.width(), 12), Qt::AlignLeft | Qt::AlignVCenter,
               QString("bars: %1").arg(metric_name));
    p.setPen(line_col);
    p.drawText(QRect(kMarginLeft, 2, plot.width(), 12), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("line: next-session price move"));
}

} // namespace fincept::screens
