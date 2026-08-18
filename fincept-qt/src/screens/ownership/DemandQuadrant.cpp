#include "screens/ownership/DemandQuadrant.h"

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace fincept::screens {

using namespace fincept::ownership;
namespace fmt = fincept::ui::formatting;

namespace {

double min_med(const QVector<QPointF>& v) {
    double m = v.first().y();
    for (const auto& p : v) m = std::min(m, p.y());
    return m;
}
double max_med(const QVector<QPointF>& v) {
    double m = v.first().y();
    for (const auto& p : v) m = std::max(m, p.y());
    return m;
}
constexpr int kPad = 8;
constexpr int kAxisW = 74;   // room for the y labels without clipping
constexpr int kAxisH = 38;   // two footer lines, clear of the edge

/// Share change compressed for display. A fund that doubled and one that added
/// 2% are both "buying", and a linear axis would put every ordinary move on the
/// zero line to make room for one outlier.
double squash(double pct) {
    const double capped = std::clamp(pct, -1.0, 4.0);
    return capped >= 0 ? std::log10(1.0 + capped * 9.0)          //  0..1
                       : -std::log10(1.0 - capped * 9.0) / 1.0;  // -1..0
}
} // namespace

DemandQuadrant::DemandQuadrant(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

void DemandQuadrant::set_demand(const InstitutionalDemand& d) {
    d_ = d;
    update();
}

void DemandQuadrant::clear() {
    d_ = {};
    hit_.clear();
    update();
}

void DemandQuadrant::set_empty_text(const QString& t) {
    empty_text_ = t;
    update();
}

QSize DemandQuadrant::minimumSizeHint() const { return {320, 240}; }

QVector<DemandQuadrant::Plotted> DemandQuadrant::layout_points(const QRect& plot) const {
    QVector<Plotted> out;
    if (d_.points.isEmpty())
        return out;

    // x is weight on a log scale — holdings run from a tenth of a percent to
    // thirty, and a linear axis collapses everything below the largest.
    double wmax = 0.0;
    for (const auto& p : d_.points)
        wmax = std::max(wmax, p.weight);
    if (wmax <= 0.0)
        return out;
    const double lo = std::log10(0.0005), hi = std::log10(std::max(wmax, 0.01));

    out.reserve(d_.points.size());
    for (const auto& p : d_.points) {
        const double lw = std::log10(std::max(p.weight, 0.0005));
        const double fx = std::clamp((lw - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);
        // A holder with no prior quarter has no percentage; park it on the
        // zero line rather than inventing a change for it.
        const double fy = p.pct ? std::clamp(squash(*p.pct), -1.0, 1.0) : 0.0;
        out.push_back({QPointF(plot.left() + fx * plot.width(),
                               plot.center().y() - fy * (plot.height() / 2.0 - 6)),
                       &p});
    }
    return out;
}

void DemandQuadrant::paintEvent(QPaintEvent*) {
    QPainter g(this);
    // Paint the ground explicitly. Inheriting it means the labels are drawn in
    // the terminal's near-white on whatever the parent happens to be, which is
    // white-on-white the moment this widget is used anywhere else.
    g.fillRect(rect(), QColor(ui::colors::BG_SURFACE()));
    g.setRenderHint(QPainter::Antialiasing, true);
    QFont f = font();
    f.setPixelSize(11);
    g.setFont(f);

    if (!d_.has_data()) {
        g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        g.drawText(rect().adjusted(kPad, kPad, -kPad, -kPad),
                   Qt::AlignCenter | Qt::TextWordWrap,
                   empty_text_.isEmpty() ? QStringLiteral("No demand data.") : empty_text_);
        return;
    }

    plot_ = rect().adjusted(kAxisW, kPad + 14, -kPad, -(kAxisH));
    const int midY = plot_.center().y();
    const double lw = std::log10(std::max(d_.median_weight, 0.0005));
    double wmax = 0.0;
    for (const auto& p : d_.points) wmax = std::max(wmax, p.weight);
    const double lo = std::log10(0.0005), hi = std::log10(std::max(wmax, 0.01));
    const int midX = plot_.left() +
                     static_cast<int>(std::clamp((lw - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0) *
                                      plot_.width());

    // ── quadrant grounds ────────────────────────────────────────────────────
    // Only the two that carry a reading are tinted: high-conviction buying and
    // high-conviction selling. Tinting all four would be decoration.
    auto tint = [&](const QRect& r, const QString& c, int a) {
        QColor q(c);
        q.setAlpha(a);
        g.fillRect(r, q);
    };
    tint(QRect(midX, plot_.top(), plot_.right() - midX, midY - plot_.top()),
         ui::colors::GREEN(), 22);
    tint(QRect(midX, midY, plot_.right() - midX, plot_.bottom() - midY),
         ui::colors::RED(), 22);

    g.setPen(QPen(QColor(ui::colors::BORDER_MED()), 1));
    g.drawLine(plot_.left(), midY, plot_.right(), midY);
    g.drawLine(midX, plot_.top(), midX, plot_.bottom());

    // ── the cloud ───────────────────────────────────────────────────────────
    hit_ = layout_points(plot_);
    for (const auto& pt : hit_) {
        if (pt.p->top)
            continue;   // named points are drawn on top, after these
        const double d = pt.p->delta.value_or(0.0);
        QColor c(d > 0 ? ui::colors::GREEN() : (d < 0 ? ui::colors::RED()
                                                      : ui::colors::TEXT_SECONDARY()));
        // Low alpha so density reads as density — five thousand opaque dots is
        // a solid block, which says nothing.
        c.setAlpha(70);
        g.setPen(Qt::NoPen);
        g.setBrush(c);
        g.drawEllipse(pt.at, 2.4, 2.4);
    }

    // ── the trend through the cloud ─────────────────────────────────────────
    //
    // A running MEDIAN, not a regression line. Least squares over these points
    // returns r values of 0.00 to 0.05 on every large cap measured: conviction
    // does not predict direction, and a straight line drawn through that
    // asserts a relationship the data does not contain. The median of each
    // vertical slice asserts nothing — it says where the middle of the cloud
    // sits as conviction rises.
    //
    // The medians are tiny (a few tenths of a percent), so plotted on the same
    // axis as the cloud the curve is a flat line on the zero axis — true, and
    // useless. It is drawn on its OWN scale, amplified to fill a band, so the
    // SHAPE and DIRECTION are readable. The amplification and the real range
    // are printed next to it: the curve shows which way, the label says how
    // much, and neither is left to be inferred from the other.
    if (hit_.size() >= 60) {
        constexpr int kBins = 14;
        QVector<QVector<double>> bins(kBins);
        QVector<double> bin_x(kBins, 0.0);
        QVector<int>    bin_n(kBins, 0);
        const double w = std::max(1.0, static_cast<double>(plot_.width()));
        for (const auto& pt : hit_) {
            if (!pt.p->pct)
                continue;   // a new position has no prior to measure against
            const int b = std::clamp(
                static_cast<int>((pt.at.x() - plot_.left()) / w * kBins), 0, kBins - 1);
            bins[b].push_back(*pt.p->pct);
            bin_x[b] += pt.at.x();
            bin_n[b] += 1;
        }
        QVector<QPointF> med;   // x in pixels, y still in DATA units
        for (int b = 0; b < kBins; ++b) {
            if (bin_n[b] < 8)   // a bin holding three holders is noise, not a trend
                continue;
            std::sort(bins[b].begin(), bins[b].end());
            med.push_back(QPointF(bin_x[b] / bin_n[b], bins[b][bins[b].size() / 2]));
        }
        if (med.size() >= 3) {
            double peak = 0.0;
            for (const auto& m : med)
                peak = std::max(peak, std::abs(m.y()));
            if (peak > 1e-9) {
                // Fill roughly a third of each half, so the curve is legible
                // without colliding with the quadrant counts.
                const double band = (plot_.height() / 2.0) * 0.34;
                const double amp = band / peak;
                QVector<QPointF> pts;
                for (const auto& m : med)
                    pts.push_back(QPointF(m.x(), midY - m.y() * amp));

                // Segment by sign so direction is carried by colour as well as
                // position — accumulating stretches green, distributing red.
                g.setBrush(Qt::NoBrush);
                for (int i = 1; i < pts.size(); ++i) {
                    const double mid_val = (med[i - 1].y() + med[i].y()) / 2.0;
                    QColor c(mid_val > 0 ? ui::colors::GREEN()
                                         : (mid_val < 0 ? ui::colors::RED()
                                                        : ui::colors::TEXT_SECONDARY()));
                    QPainterPath seg(pts[i - 1]);
                    const double mx = (pts[i - 1].x() + pts[i].x()) / 2.0;
                    seg.cubicTo(QPointF(mx, pts[i - 1].y()), QPointF(mx, pts[i].y()), pts[i]);
                    g.setPen(QPen(c, 2.2));
                    g.drawPath(seg);
                }
                // Endpoint marker: where the highest-conviction holders sit.
                const QColor endc(med.last().y() >= 0 ? ui::colors::GREEN()
                                                      : ui::colors::RED());
                g.setPen(QPen(QColor(ui::colors::BG_BASE()), 1.5));
                g.setBrush(endc);
                g.drawEllipse(pts.last(), 3.6, 3.6);

                QFont cf = f;
                cf.setPixelSize(10);
                g.setFont(cf);
                g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
                // The multiplier itself tells a reader nothing — what they
                // need is that the curve is not to scale and what the real
                // numbers are. Shape from the line, magnitude from the label.
                const QString caption =
                    QStringLiteral("median holder %1 to %2 — shape amplified, not to scale")
                        .arg(fmt::format_percent(min_med(med) * 100.0, 2, true),
                             fmt::format_percent(max_med(med) * 100.0, 2, true));
                // Top centre: the corners carry the four quadrant counts and
                // the bottom band carries the axis labels and the named
                // managers, so the middle of the top edge is the one strip of
                // this chart nothing else claims.
                const QRectF cap(plot_.left() + plot_.width() / 4, plot_.top() + 1,
                                 plot_.width() / 2, 14);
                g.drawText(cap, Qt::AlignHCenter | Qt::AlignVCenter, caption);
            }
        }
    }

    // ── the ten largest, named ──────────────────────────────────────────────
    g.setPen(Qt::NoPen);
    for (const auto& pt : hit_) {
        if (!pt.p->top)
            continue;
        const double d = pt.p->delta.value_or(0.0);
        QColor c(d > 0 ? ui::colors::GREEN() : (d < 0 ? ui::colors::RED()
                                                      : ui::colors::TEXT_PRIMARY()));
        g.setBrush(c);
        g.setPen(QPen(QColor(ui::colors::BG_BASE()), 1.5));
        g.drawEllipse(pt.at, 4.6, 4.6);
    }
    QFont lf = f;
    lf.setPixelSize(10);
    g.setFont(lf);
    const QFontMetrics lfm(lf);

    // The ten largest cluster near the median, so their labels land on top of
    // one another and none of them can be read. Lay them out top-down and push
    // each one clear of the last, drawing a leader back to its dot — a name
    // that has moved and says so beats a legible pile of overlapping text.
    QVector<const Plotted*> named;
    for (const auto& pt : hit_)
        if (pt.p->top)
            named.push_back(&pt);
    std::sort(named.begin(), named.end(),
              [](const Plotted* a, const Plotted* b) { return a->at.y() < b->at.y(); });

    const int lh = lfm.height() + 3;
    double lastY = -1e9;
    for (const auto* pt : named) {
        const QString name = lfm.elidedText(pt->p->manager, Qt::ElideRight, 150);
        const int w = lfm.horizontalAdvance(name);
        const bool left = pt->at.x() > plot_.center().x();
        double ty = std::max(pt->at.y(), lastY + lh);
        ty = std::clamp(ty, static_cast<double>(plot_.top() + lh),
                        static_cast<double>(plot_.bottom()));
        lastY = ty;

        const double tx = left ? pt->at.x() - 10 - w : pt->at.x() + 10;
        if (std::abs(ty - pt->at.y()) > 2.0) {
            g.setPen(QPen(QColor(ui::colors::BORDER_BRIGHT()), 1));
            g.drawLine(QPointF(pt->at.x() + (left ? -6 : 6), pt->at.y()),
                       QPointF(left ? tx + w + 2 : tx - 2, ty - 3.5));
        }
        // A dark plate under the text so a name over the dot cloud stays
        // readable rather than competing with a thousand dots.
        QColor plate(ui::colors::BG_SURFACE());
        plate.setAlpha(215);
        g.fillRect(QRectF(tx - 3, ty - lfm.ascent() - 1, w + 6, lh - 1), plate);
        g.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        g.drawText(QPointF(tx, ty), name);
    }
    g.setFont(f);

    // ── quadrant counts ─────────────────────────────────────────────────────
    auto badge = [&](const QRect& r, int n, const QString& label, const QString& colour,
                     Qt::Alignment align) {
        g.setPen(QColor(colour));
        QFont bf = f;
        bf.setPixelSize(15);
        bf.setBold(true);
        g.setFont(bf);
        g.drawText(r.adjusted(7, 5, -7, 0), align | Qt::AlignTop, QString::number(n));
        QFont sf = f;
        sf.setPixelSize(10);
        g.setFont(sf);
        g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        g.drawText(r.adjusted(7, 22, -7, 0), align | Qt::AlignTop, label);
        g.setFont(f);
    };
    badge(QRect(midX, plot_.top(), plot_.right() - midX, 44), d_.high_add,
          QStringLiteral("conviction buying"), ui::colors::GREEN(), Qt::AlignRight);
    badge(QRect(plot_.left(), plot_.top(), midX - plot_.left(), 44), d_.low_add,
          QStringLiteral("small buying"), ui::colors::TEXT_SECONDARY(), Qt::AlignLeft);
    badge(QRect(midX, plot_.bottom() - 44, plot_.right() - midX, 44), d_.high_cut,
          QStringLiteral("conviction selling"), ui::colors::RED(), Qt::AlignRight);
    badge(QRect(plot_.left(), plot_.bottom() - 44, midX - plot_.left(), 44), d_.low_cut,
          QStringLiteral("small selling"), ui::colors::TEXT_SECONDARY(), Qt::AlignLeft);

    // ── axes ────────────────────────────────────────────────────────────────
    g.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    g.drawText(QRect(0, midY - 8, kAxisW - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("no change"));
    g.drawText(QRect(0, plot_.top() - 2, kAxisW - 6, 16), Qt::AlignRight | Qt::AlignTop,
               QStringLiteral("bought"));
    g.drawText(QRect(0, plot_.bottom() - 14, kAxisW - 6, 16), Qt::AlignRight | Qt::AlignBottom,
               QStringLiteral("sold"));
    g.drawText(QRect(plot_.left(), plot_.bottom() + 4, plot_.width(), 14),
               Qt::AlignLeft, QStringLiteral("smaller position"));
    g.drawText(QRect(plot_.left(), plot_.bottom() + 4, plot_.width(), 14),
               Qt::AlignRight, QStringLiteral("larger position  →  % of their book"));
    g.setPen(QColor(ui::colors::TEXT_DIM()));
    g.drawText(QRect(plot_.left(), plot_.bottom() + 17, plot_.width(), 14), Qt::AlignLeft,
               QStringLiteral("median holder %1")
                   .arg(fmt::format_percent(d_.median_weight * 100.0, 2)));
    QString foot = QStringLiteral("%1 discretionary holders").arg(d_.holders);
    if (d_.points_truncated > 0)
        foot += QStringLiteral(" · %1 smallest not plotted").arg(d_.points_truncated);
    if (d_.unchanged > 0)
        foot += QStringLiteral(" · %1 unchanged, on the line").arg(d_.unchanged);
    g.drawText(QRect(plot_.left(), plot_.bottom() + 17, plot_.width(), 14), Qt::AlignRight, foot);
}

bool DemandQuadrant::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const DemandPoint* best = nullptr;
        double bestd = 1e9;
        for (const auto& pt : hit_) {
            const double dx = pt.at.x() - he->pos().x(), dy = pt.at.y() - he->pos().y();
            const double d = std::sqrt(dx * dx + dy * dy);
            // Named points win ties so a big holder stays reachable inside a
            // dense cloud.
            const double bias = pt.p->top ? 3.0 : 0.0;
            if (d - bias < bestd) { bestd = d - bias; best = pt.p; }
        }
        if (best && bestd < 9.0) {
            QToolTip::showText(
                he->globalPos(),
                QStringLiteral("%1\n%2 of their book · %3 shares\n%4%5")
                    .arg(best->manager,
                         fmt::format_percent(best->weight * 100.0, 2),
                         best->shares ? fmt::format_compact(*best->shares) : fmt::placeholder(),
                         best->action.isEmpty() ? QStringLiteral("—") : best->action)
                    .arg(best->pct ? QStringLiteral("  %1")
                                         .arg(fmt::format_percent(*best->pct * 100.0, 1, true))
                                   : QString()),
                this);
            return true;
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void DemandQuadrant::mousePressEvent(QMouseEvent* e) {
    for (const auto& pt : hit_) {
        if (!pt.p->top)
            continue;
        const double dx = pt.at.x() - e->pos().x(), dy = pt.at.y() - e->pos().y();
        if (std::sqrt(dx * dx + dy * dy) < 8.0) {
            emit holder_activated(pt.p->manager);
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

} // namespace fincept::screens
