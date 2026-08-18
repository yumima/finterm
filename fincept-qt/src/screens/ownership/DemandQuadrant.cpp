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

constexpr int kVerdictH = 48;
constexpr int kCurveH   = 46;   // caption row plus the share-buying band
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

    // kVerdictH is reserved under the axis labels for the named scale that
    // says what the whole cloud adds up to.
    plot_ = rect().adjusted(kAxisW, kPad + 14 + kCurveH, -kPad, -(kAxisH + kVerdictH));
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

    // ── share buying, by conviction ─────────────────────────────────────────
    //
    // One question, one line: as positions get larger, does the share of
    // holders BUYING rise or fall? A count of buyers over buyers-plus-sellers
    // in each conviction slice — bounded 0 to 100%, with a 50% guide, so
    // "more buyers than sellers here" is read off the line's side of the
    // guide rather than decoded.
    //
    // It sits in its own band, sharing only the x axis with the cloud below.
    // The previous attempt drew a median of share-CHANGE straight over the
    // scatter, which put a second unit on the scatter's own y axis and had to
    // be amplified to be visible at all — two reasons a reader could not say
    // what it meant.
    if (hit_.size() >= 60) {
        // Caption gets its own row above the band: drawn inside it, the text
        // and the line land on the same pixels wherever the curve is flat.
        const QRect band(plot_.left(), plot_.top() - kCurveH + 14, plot_.width(),
                         kCurveH - 22);
        constexpr int kBins = 16;
        QVector<int> buyers(kBins, 0), movers(kBins, 0);
        QVector<double> bx(kBins, 0.0);
        const double w = std::max(1.0, static_cast<double>(plot_.width()));
        for (const auto& pt : hit_) {
            const double dl = pt.p->delta.value_or(0.0);
            if (std::fabs(dl) < 1.0)
                continue;   // holders who did nothing carry no direction
            const int b = std::clamp(
                static_cast<int>((pt.at.x() - plot_.left()) / w * kBins), 0, kBins - 1);
            movers[b] += 1;
            buyers[b] += dl > 0 ? 1 : 0;
            bx[b] += pt.at.x();
        }
        QVector<QPointF> line;
        for (int b = 0; b < kBins; ++b) {
            if (movers[b] < 8)   // a slice of three holders is noise
                continue;
            const double share = static_cast<double>(buyers[b]) / movers[b];
            line.push_back(QPointF(bx[b] / movers[b], band.bottom() - share * band.height()));
        }
        // The 50% guide: the line's side of this IS the reading.
        const int mid = band.bottom() - band.height() / 2;
        g.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::DashLine));
        g.drawLine(band.left(), mid, band.right(), mid);
        if (line.size() >= 3) {
            g.setBrush(Qt::NoBrush);
            for (int i = 1; i < line.size(); ++i) {
                const bool buying = (line[i - 1].y() + line[i].y()) / 2.0 < mid;
                g.setPen(QPen(QColor(buying ? ui::colors::GREEN() : ui::colors::RED()), 2.0));
                QPainterPath seg(line[i - 1]);
                const double mx = (line[i - 1].x() + line[i].x()) / 2.0;
                seg.cubicTo(QPointF(mx, line[i - 1].y()), QPointF(mx, line[i].y()), line[i]);
                g.drawPath(seg);
            }
        }
        QFont bf = f;
        bf.setPixelSize(9);
        g.setFont(bf);
        g.setPen(QColor(ui::colors::TEXT_DIM()));
        g.drawText(QRect(band.left(), band.top() - 14, band.width(), 12),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("share of movers buying — above the line, buyers outnumber "
                                  "sellers at that position size"));
        g.drawText(QRect(band.left() - kAxisW, mid - 6, kAxisW - 4, 12),
                   Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("50%"));
        g.setFont(f);
    }

    // The running median that used to sit here is gone. It was a real
    // statistic — the middle of each vertical slice — but on every large cap
    // measured it came out flat along zero, so it had to be amplified to be
    // visible at all, and an amplified flat line tells a reader nothing they
    // can act on while costing them the effort of decoding it. What they want
    // from a cloud of five thousand holders is the direction it adds up to.
    // That is drawn as a named scale below the plot instead.

    // ── what it adds up to ──────────────────────────────────────────────────
    //
    // The one line a reader wants from five thousand holders: which way did
    // the money actually go. Measured as the NET share of the money that
    // MOVED — net flow over gross flow — rather than net flow against the
    // position, because the second is dominated by how big the holding
    // already is and barely moves however violently the quarter traded.
    //
    // So: of every dollar institutions put in or took out of this name this
    // quarter, what share was net out. AAPL's Mar-2026 quarter is 17.3B
    // bought against 28.5B sold, so a quarter of the money that moved, moved
    // out — which is a fact about the quarter, not a forecast about the stock.
    if (d_.value_bought && d_.value_sold) {
        const double gross = *d_.value_bought + *d_.value_sold;
        if (gross > 0.0) {
            const double net = (*d_.value_bought - *d_.value_sold) / gross;

            // Sits BELOW the axis footer, not over it.
            const int top = plot_.bottom() + kAxisH + 2;
            const QRect scale(plot_.left(), top + 14, plot_.width(), 11);

            // Zones are EQUAL WIDTH, not proportional to their numeric range.
            // Mapped linearly over -100%..+100% the two extreme zones take
            // four fifths of the bar and "holding" becomes a sliver too narrow
            // to label — while almost every real quarter lands in the middle.
            // Equal widths give every reading room to be named, and the marker
            // is placed proportionally inside its own zone.
            struct Zone { double from, to; const char* name; };
            const Zone zones[] = {
                {-1.00, -0.20, "DISTRIBUTING"}, {-0.20, -0.05, "TRIMMING"},
                {-0.05,  0.05, "HOLDING"},      { 0.05,  0.20, "ADDING"},
                { 0.20,  1.00, "ACCUMULATING"},
            };
            constexpr int kZones = 5;
            const int zw = scale.width() / kZones;

            QFont zf = f;
            zf.setPixelSize(9);
            const QFontMetrics zfm(zf);
            int marker_x = scale.left();
            for (int i = 0; i < kZones; ++i) {
                const auto& z = zones[i];
                const int x0 = scale.left() + i * zw;
                const bool live = net >= z.from && net < z.to;
                QColor c(z.to <= -0.05 ? ui::colors::RED()
                       : z.from >= 0.05 ? ui::colors::GREEN()
                                        : ui::colors::TEXT_SECONDARY());
                QColor band(c);
                band.setAlpha(live ? 160 : 30);
                g.fillRect(QRect(x0 + 1, scale.top(), zw - 2, scale.height()), band);
                g.setFont(zf);
                g.setPen(QColor(live ? ui::colors::TEXT_PRIMARY() : ui::colors::TEXT_DIM()));
                const QString nm = QString::fromLatin1(z.name);
                g.drawText(QRect(x0, scale.bottom() + 2, zw, 11),
                           Qt::AlignHCenter | Qt::AlignTop,
                           zfm.horizontalAdvance(nm) < zw - 2 ? nm : nm.left(4));
                if (live) {
                    const double t = (net - z.from) / std::max(1e-9, z.to - z.from);
                    marker_x = x0 + static_cast<int>(std::clamp(t, 0.0, 1.0) * zw);
                }
            }
            g.setPen(QPen(QColor(ui::colors::TEXT_PRIMARY()), 2));
            g.drawLine(marker_x, scale.top() - 3, marker_x, scale.bottom() + 1);

            g.setPen(QColor(ui::colors::TEXT_PRIMARY()));
            QFont vf = f;
            vf.setPixelSize(10);
            vf.setBold(true);
            g.setFont(vf);
            g.drawText(QRect(scale.left(), top, scale.width(), 13),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       QStringLiteral("%1 of the money that moved, moved %2")
                           .arg(fmt::format_percent(std::fabs(net) * 100.0, 0),
                                net >= 0 ? QStringLiteral("in") : QStringLiteral("out")));
            g.setFont(f);
            verdict_rect_ = QRect(scale.left(), top, scale.width(), 40);
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
