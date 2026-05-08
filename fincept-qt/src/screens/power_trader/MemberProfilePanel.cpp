// src/screens/power_trader/MemberProfilePanel.cpp
#include "screens/power_trader/MemberProfilePanel.h"

#include "screens/power_trader/PowerTraderService.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fincept::screens {

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kPartyD = "#3b82f6";
static constexpr const char* kPartyR = "#ef4444";
static constexpr const char* kPartyI = "#eab308";

static const char* party_color(const QString& p) {
    if (p == QLatin1String("D")) return kPartyD;
    if (p == QLatin1String("R")) return kPartyR;
    return kPartyI;
}

static QString section_header_style() {
    return QString(
        "QLabel { background:%1; color:%2; font-size:12px; font-weight:700;"
        " letter-spacing:0.5px; padding:0px 10px; border-bottom:1px solid %3; }")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_MED());
}

// Creates a correctly-sized section header label that works across DPI scales.
// Height uses the explicit stylesheet font (12px) via a constructed QFont so
// it's not affected by the lazy stylesheet application order.
static QLabel* make_section_hdr(const QString& title, QWidget* parent) {
    auto* lbl = new QLabel(title, parent);
    lbl->setStyleSheet(section_header_style());
    // Use the same font parameters as section_header_style() to compute height.
    // Cannot call lbl->font() here — stylesheet hasn't been applied yet.
    QFont f(QStringLiteral("sans-serif"), -1);
    f.setPixelSize(12);
    const int h = QFontMetrics(f).height() + 8;  // content + 4px each side
    lbl->setFixedHeight(qMax(h, 22));
    return lbl;
}

// Format dollar amount with K/M/B suffix — free function used by NavChart + panel
static QString fmt_dollar_free(double v) {
    const bool neg = v < 0;
    double a = std::abs(v);
    QString s;
    if (a >= 1e9)
        s = QString("$%1B").arg(a / 1e9, 0, 'f', 1);
    else if (a >= 1e6)
        s = QString("$%1M").arg(a / 1e6, 0, 'f', 1);
    else if (a >= 1e3)
        s = QString("$%1K").arg(a / 1e3, 0, 'f', 0);
    else
        s = QString("$%1").arg(a, 0, 'f', 0);
    return neg ? ("-" + s) : s;
}

QString MemberProfilePanel::fmt_dollar(double v) { return fmt_dollar_free(v); }

QString MemberProfilePanel::fmt_pct(double v, bool show_sign) {
    if (show_sign)
        return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 1);
    return QString("%1%").arg(v, 0, 'f', 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Committee → sector mapping (file-local helper)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the sector keywords associated with a given committee name.
/// A committee "overlaps" a holding sector if any sector keyword appears
/// (case-insensitive) in the holding's sector string.
static QStringList committee_sectors(const QString& committee) {
    const QString c = committee.toLower();
    if (c.contains(QLatin1String("armed services")))
        return { QStringLiteral("Defense"), QStringLiteral("Aerospace") };
    if (c.contains(QLatin1String("intelligence")))
        return { QStringLiteral("Technology"), QStringLiteral("Cybersecurity") };
    if (c.contains(QLatin1String("finance")) || c.contains(QLatin1String("banking")))
        return { QStringLiteral("Financials"), QStringLiteral("Banking") };
    if (c.contains(QLatin1String("energy")))
        return { QStringLiteral("Energy"), QStringLiteral("Utilities") };
    if (c.contains(QLatin1String("commerce")))
        return { QStringLiteral("Technology"), QStringLiteral("Consumer") };
    if (c.contains(QLatin1String("health")))
        return { QStringLiteral("Healthcare"), QStringLiteral("Biotech") };
    if (c.contains(QLatin1String("appropriations")))
        return { QStringLiteral("cross-sector") };
    if (c.contains(QLatin1String("foreign relations")))
        return { QStringLiteral("Defense"), QStringLiteral("International") };
    if (c.contains(QLatin1String("homeland security")))
        return { QStringLiteral("Defense"), QStringLiteral("Cybersecurity") };
    return {};
}

/// Returns true if the given sector string matches any of the committee's sectors.
static bool sector_matches_committee(const QString& sector, const QString& committee) {
    const QStringList cmte_sectors = committee_sectors(committee);
    for (const QString& cs : cmte_sectors) {
        if (sector.contains(cs, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// NavChart implementation
// ─────────────────────────────────────────────────────────────────────────────

NavChart::NavChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void NavChart::set_series(const QVector<power_trader::NavPoint>& series) {
    full_series_ = series;
    rebuild_visible();
    update();
}

void NavChart::set_period(const QString& period) {
    period_ = period;
    rebuild_visible();
    update();
}

QVector<power_trader::NavPoint> NavChart::filtered_series(const QString& period) const {
    if (period == QLatin1String("ALL") || full_series_.isEmpty())
        return full_series_;

    const QDate cutoff = [&]() -> QDate {
        const QDate today = QDate::currentDate();
        if (period == QLatin1String("3M"))  return today.addMonths(-3);
        if (period == QLatin1String("6M"))  return today.addMonths(-6);
        if (period == QLatin1String("1Y"))  return today.addYears(-1);
        return today.addYears(-100); // ALL fallback
    }();

    QVector<power_trader::NavPoint> result;
    result.reserve(full_series_.size());
    for (const auto& pt : full_series_) {
        if (pt.date >= cutoff)
            result.append(pt);
    }
    return result;
}

void NavChart::rebuild_visible() {
    visible_series_ = filtered_series(period_);
}

void NavChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(48, 12, -12, -28);

    // Background
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));

    if (visible_series_.isEmpty()) {
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.setFont(QFont("Consolas", 11));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No portfolio history available"));
        return;
    }

    // Determine value range
    double y_min = visible_series_.first().est_nav;
    double y_max = visible_series_.first().est_nav;
    for (const auto& pt : visible_series_) {
        y_min = std::min(y_min, pt.est_nav);
        y_max = std::max(y_max, pt.est_nav);
    }
    const double y_range = (y_max - y_min) < 1.0 ? 1.0 : (y_max - y_min);
    // Add a little headroom
    const double y_pad   = y_range * 0.08;
    const double y_lo    = y_min - y_pad;
    const double y_hi    = y_max + y_pad;
    const double y_span  = y_hi - y_lo;

    const int n = visible_series_.size();

    auto to_pt = [&](int idx) -> QPointF {
        const double x = r.left() + (n > 1 ? (double(idx) / (n - 1)) * r.width() : r.width() / 2.0);
        const double y = r.bottom() - ((visible_series_[idx].est_nav - y_lo) / y_span) * r.height();
        return {x, y};
    };

    // ── Y-axis grid lines + labels ────────────────────────────────────────────
    p.setFont(QFont("Consolas", 10));
    const int kGridLines = 4;
    for (int i = 0; i <= kGridLines; ++i) {
        const double frac = double(i) / kGridLines;
        const double yv   = y_lo + frac * y_span;
        const double py   = r.bottom() - frac * r.height();

        // Grid line
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::SolidLine));
        p.drawLine(QPointF(r.left(), py), QPointF(r.right(), py));

        // Label
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        QString label;
        if (std::abs(yv) >= 1e9)
            label = QString("$%1B").arg(yv / 1e9, 0, 'f', 0);
        else if (std::abs(yv) >= 1e6)
            label = QString("$%1M").arg(yv / 1e6, 0, 'f', 0);
        else if (std::abs(yv) >= 1e3)
            label = QString("$%1K").arg(yv / 1e3, 0, 'f', 0);
        else
            label = QString("$%1").arg(yv, 0, 'f', 0);
        p.drawText(QRectF(0, py - 8, r.left() - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // ── X-axis date labels (first + last + maybe mid) ─────────────────────────
    p.setFont(QFont("Consolas", 10));
    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    const QString fmt = (period_ == QLatin1String("3M") || period_ == QLatin1String("6M"))
                            ? QStringLiteral("MMM yy")
                            : QStringLiteral("MMM yy");
    // First
    const QPointF p0 = to_pt(0);
    p.drawText(QRectF(p0.x() - 24, r.bottom() + 4, 48, 16),
               Qt::AlignCenter, visible_series_.first().date.toString(fmt));
    // Last
    const QPointF pN = to_pt(n - 1);
    p.drawText(QRectF(pN.x() - 24, r.bottom() + 4, 48, 16),
               Qt::AlignCenter, visible_series_.last().date.toString(fmt));
    // Mid
    if (n > 2) {
        const int mid = n / 2;
        const QPointF pm = to_pt(mid);
        p.drawText(QRectF(pm.x() - 24, r.bottom() + 4, 48, 16),
                   Qt::AlignCenter, visible_series_[mid].date.toString(fmt));
    }

    // ── Build line + fill paths ───────────────────────────────────────────────
    QPainterPath line_path;
    QPainterPath fill_path;
    fill_path.moveTo(to_pt(0).x(), r.bottom());

    for (int i = 0; i < n; ++i) {
        const QPointF pt = to_pt(i);
        if (i == 0)
            line_path.moveTo(pt);
        else
            line_path.lineTo(pt);
        fill_path.lineTo(pt);
    }
    fill_path.lineTo(to_pt(n - 1).x(), r.bottom());
    fill_path.closeSubpath();

    // Fill under the line — amber at ~10% opacity
    QColor fill_color(ui::colors::AMBER());
    fill_color.setAlphaF(0.10);
    p.fillPath(fill_path, fill_color);

    // Draw the line
    QPen line_pen(QColor(ui::colors::AMBER()));
    line_pen.setWidthF(1.8);
    line_pen.setCapStyle(Qt::RoundCap);
    line_pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(line_pen);
    p.drawPath(line_path);

    // ── "Estimated" watermark ─────────────────────────────────────────────────
    {
        p.save();
        p.setFont(QFont("Consolas", 9, QFont::Normal, true));
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.drawText(QRectF(r.left() + 4, r.top() + 4, 200, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Estimated"));
        p.restore();
    }

    // ── Hover crosshair ───────────────────────────────────────────────────────
    if (hover_active_ && n > 1) {
        // Find nearest point by X
        const double mx = hover_pos_.x();
        int nearest = 0;
        double best = std::abs(to_pt(0).x() - mx);
        for (int i = 1; i < n; ++i) {
            const double d = std::abs(to_pt(i).x() - mx);
            if (d < best) { best = d; nearest = i; }
        }
        const QPointF np = to_pt(nearest);

        // Vertical crosshair line
        QPen cross_pen(QColor(ui::colors::BORDER_MED()));
        cross_pen.setWidthF(1.0);
        cross_pen.setStyle(Qt::DashLine);
        p.setPen(cross_pen);
        p.drawLine(QPointF(np.x(), r.top()), QPointF(np.x(), r.bottom()));

        // Dot on the line
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(ui::colors::AMBER()));
        p.drawEllipse(np, 4, 4);

        // Tooltip box
        const auto& pt_data = visible_series_[nearest];
        const QString tip = QString("%1  %2")
                                .arg(pt_data.date.toString(QStringLiteral("yyyy-MM-dd")))
                                .arg(fmt_dollar_free(pt_data.est_nav));

        p.setFont(QFont("Consolas", 10));
        const QFontMetrics fm(p.font());
        const QRect tip_rect = fm.boundingRect(tip).adjusted(-6, -3, 6, 3);
        double tx = np.x() + 8;
        double ty = np.y() - tip_rect.height() - 4;
        if (tx + tip_rect.width() > r.right())
            tx = np.x() - tip_rect.width() - 8;
        if (ty < r.top())
            ty = np.y() + 6;

        const QRectF tip_bg(tx, ty, tip_rect.width(), tip_rect.height());
        p.setBrush(QColor(ui::colors::BG_RAISED()));
        p.setPen(QPen(QColor(ui::colors::BORDER_MED())));
        p.drawRoundedRect(tip_bg, 3, 3);

        p.setPen(QColor(ui::colors::TEXT_PRIMARY()));
        p.drawText(tip_bg, Qt::AlignCenter, tip);
    }
}

void NavChart::mouseMoveEvent(QMouseEvent* event) {
    hover_pos_    = event->pos();
    hover_active_ = true;
    update();
    QWidget::mouseMoveEvent(event);
}

void NavChart::leaveEvent(QEvent* event) {
    hover_active_ = false;
    update();
    QWidget::leaveEvent(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// SectorPieChart implementation
// ─────────────────────────────────────────────────────────────────────────────

SectorPieChart::SectorPieChart(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void SectorPieChart::set_slices(const QVector<Slice>& slices) {
    slices_ = slices;
    update();
}

void SectorPieChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));

    const int side = qMin(width(), height());
    const double outer_r = side * 0.90 / 2.0;
    const double inner_r = outer_r * 0.55;
    const QPointF center(width() / 2.0, height() / 2.0);

    if (slices_.isEmpty()) {
        // Draw empty ring placeholder
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, outer_r, outer_r);
        p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
        p.setFont(QFont("Consolas", 10));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("NO DATA"));
        return;
    }

    // Draw slices
    // We track cumulative angle in 1/16th-degree units (Qt's arc unit)
    const double gap_deg = 0.5; // 0.5-degree gap between slices
    const double gap16 = gap_deg * 16.0;

    // Compute total pct so we can normalize
    double total_pct = 0.0;
    for (const auto& sl : slices_) total_pct += sl.pct;
    if (total_pct < 1e-6) total_pct = 1.0;

    const QRectF donut_rect(center.x() - outer_r, center.y() - outer_r,
                             outer_r * 2.0, outer_r * 2.0);

    double angle_deg = 90.0; // Start at 12 o'clock

    for (const auto& sl : slices_) {
        const double span_deg = (sl.pct / total_pct) * 360.0;
        if (span_deg < 0.1) { angle_deg += span_deg; continue; }

        // Shorten by gap on each side
        const double half_gap = (slices_.size() > 1) ? gap_deg * 0.5 : 0.0;
        const double start16  = qRound((angle_deg + half_gap) * 16.0);
        const double span16   = qRound((span_deg - gap_deg) * 16.0);

        if (span16 < 1) { angle_deg += span_deg; continue; }

        // Build donut arc path using QPainterPath
        QPainterPath path;
        path.arcMoveTo(donut_rect, angle_deg + half_gap);
        path.arcTo(donut_rect, angle_deg + half_gap, span_deg - gap_deg);

        // Arc back along inner circle
        const QRectF inner_rect(center.x() - inner_r, center.y() - inner_r,
                                 inner_r * 2.0, inner_r * 2.0);
        path.arcTo(inner_rect, angle_deg + half_gap + span_deg - gap_deg, -(span_deg - gap_deg));
        path.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(sl.color);
        p.drawPath(path);

        angle_deg += span_deg;
    }

    // Punch out center hole with background color
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(ui::colors::BG_BASE()));
    p.drawEllipse(center, inner_r - 1.0, inner_r - 1.0);

    // Center text: "SECTOR\nALLOCATION"
    p.setPen(QColor(ui::colors::TEXT_SECONDARY()));
    QFont center_font("Consolas", 11);
    center_font.setBold(false);
    p.setFont(center_font);
    const QRectF text_rect(center.x() - inner_r * 0.85, center.y() - inner_r * 0.5,
                            inner_r * 1.7, inner_r);
    p.drawText(text_rect, Qt::AlignCenter | Qt::TextWordWrap,
               QStringLiteral("SECTOR\nALLOCATION"));
}

// ─────────────────────────────────────────────────────────────────────────────
// MemberProfilePanel implementation
// ─────────────────────────────────────────────────────────────────────────────

MemberProfilePanel::MemberProfilePanel(QWidget* parent) : QWidget(parent) {
    build_ui();
    show_placeholder();
}

// ── build_ui ──────────────────────────────────────────────────────────────────

void MemberProfilePanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    setStyleSheet(QString("QWidget { background:%1; color:%2; }")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    // ── Placeholder ───────────────────────────────────────────────────────────
    placeholder_ = new QWidget(this);
    {
        auto* pl = new QVBoxLayout(placeholder_);
        pl->setAlignment(Qt::AlignCenter);
        auto* msg = new QLabel(QStringLiteral("No member selected"), placeholder_);
        msg->setAlignment(Qt::AlignCenter);
        msg->setStyleSheet(
            QString("QLabel { color:%1; font-size:13px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        pl->addWidget(msg);
    }
    root->addWidget(placeholder_);

    // ── Scroll area wrapping all detail content ───────────────────────────────
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        QString("QScrollArea { background:%1; border:none; }"
                "QScrollBar:vertical { width:8px; background:%1; border-radius:4px; }"
                "QScrollBar::handle:vertical { background:%2; min-height:30px; border-radius:4px; }"
                "QScrollBar:horizontal { height:8px; background:%1; border-radius:4px; }"
                "QScrollBar::handle:horizontal { background:%2; min-width:30px; border-radius:4px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_BRIGHT()));

    content_ = new QWidget;
    content_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    scroll_area_->setWidget(content_);

    auto* vl = new QVBoxLayout(content_);
    vl->setContentsMargins(0, 0, 0, 16);
    vl->setSpacing(0);

    // ── Wide-screen top row: info LEFT | chart RIGHT ──────────────────────────
    // Left pane: compact member info (name, stats) — not stretched horizontally
    // Right pane: NAV chart fills available width naturally
    auto* top_split = new QSplitter(Qt::Horizontal, content_);
    top_split->setHandleWidth(1);
    top_split->setChildrenCollapsible(false);
    top_split->setStyleSheet(
        QString("QSplitter::handle { background:%1; }").arg(ui::colors::BORDER_DIM()));

    auto* info_pane = new QWidget(top_split);
    info_pane->setMinimumWidth(230);
    info_pane->setMaximumWidth(320);
    info_pane->setStyleSheet(
        QString("QWidget { background:%1; border-right:1px solid %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    // ── ROW 1: info pane (narrow) | chart (wide, full width) ─────────────────
    {
        auto* info_vl = new QVBoxLayout(info_pane);
        info_vl->setContentsMargins(0, 0, 0, 0);
        info_vl->setSpacing(0);
        build_header_bar(info_pane, info_vl);
        build_stat_tiles_compact(info_pane, info_vl);
        info_vl->addStretch();

        auto* chart_pane = new QWidget(top_split);
        chart_pane->setStyleSheet(
            QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
        auto* chart_vl = new QVBoxLayout(chart_pane);
        chart_vl->setContentsMargins(0, 0, 0, 0);
        chart_vl->setSpacing(0);
        build_chart_section(chart_pane, chart_vl);  // chart fills full width — no committee panel

        top_split->setStretchFactor(0, 0);
        top_split->setStretchFactor(1, 1);
        vl->addWidget(top_split);
    }

    const QString splitter_ss =
        QString("QSplitter::handle{background:%1;}").arg(ui::colors::BORDER_DIM());
    auto make_row = [&](int minH) -> QSplitter* {
        auto* sp = new QSplitter(Qt::Horizontal, content_);
        sp->setHandleWidth(1);
        sp->setChildrenCollapsible(false);
        sp->setStyleSheet(splitter_ss);
        sp->setMinimumHeight(minH);
        return sp;
    };

    // ── ROW 2: holdings (LEFT) | trades (RIGHT) — equal width ────────────────
    {
        auto* row2 = make_row(180);

        auto* left2 = new QWidget(row2);
        left2->setStyleSheet(
            QString("QWidget{background:%1;border-right:1px solid %2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* l2 = new QVBoxLayout(left2);
        l2->setContentsMargins(0, 0, 0, 0); l2->setSpacing(0);
        build_holdings_table(left2, l2);

        auto* right2 = new QWidget(row2);
        right2->setStyleSheet(
            QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
        auto* r2 = new QVBoxLayout(right2);
        r2->setContentsMargins(0, 0, 0, 0); r2->setSpacing(0);
        build_trades_section(right2, r2);

        row2->setStretchFactor(0, 1);
        row2->setStretchFactor(1, 1);
        vl->addWidget(row2);
    }

    // ── ROW 3: trader profile (LEFT) | how they compare (RIGHT) — equal width ─
    {
        auto* row3 = make_row(160);

        auto* left3 = new QWidget(row3);
        left3->setStyleSheet(
            QString("QWidget{background:%1;border-right:1px solid %2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* l3 = new QVBoxLayout(left3);
        l3->setContentsMargins(0, 0, 0, 0); l3->setSpacing(0);
        build_insights_section(left3, l3);   // "TRADER PROFILE"

        auto* right3 = new QWidget(row3);
        right3->setStyleSheet(
            QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
        auto* r3 = new QVBoxLayout(right3);
        r3->setContentsMargins(0, 0, 0, 0); r3->setSpacing(0);
        build_ranking_section(right3, r3);   // "HOW THEY COMPARE"

        row3->setStretchFactor(0, 1);
        row3->setStretchFactor(1, 1);
        vl->addWidget(row3);
    }

    // ── ROW 4: sector allocation (LEFT) | combined signal analysis (RIGHT) ────
    {
        auto* row4 = make_row(180);

        auto* left4 = new QWidget(row4);
        left4->setStyleSheet(
            QString("QWidget{background:%1;border-right:1px solid %2;}")
                .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));
        auto* l4 = new QVBoxLayout(left4);
        l4->setContentsMargins(0, 0, 0, 0); l4->setSpacing(0);
        build_sector_section(left4, l4);     // sector pie + breakdown

        auto* right4 = new QWidget(row4);
        right4->setStyleSheet(
            QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
        auto* r4 = new QVBoxLayout(right4);
        r4->setContentsMargins(0, 0, 0, 0); r4->setSpacing(0);
        build_combined_analysis_section(right4, r4);  // committee + signal + insider

        row4->setStretchFactor(0, 1);
        row4->setStretchFactor(1, 1);
        vl->addWidget(row4);
    }

    scroll_area_->setVisible(false);
    root->addWidget(scroll_area_, 1);
}

// ── Section builders ──────────────────────────────────────────────────────────

void MemberProfilePanel::build_header_bar(QWidget* parent, QVBoxLayout* vl) {
    auto* hdr = new QWidget(parent);
    hdr->setStyleSheet(
        QString("QWidget { background:%1; border-bottom:1px solid %2; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED()));
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(12, 6, 12, 6);
    hl->setSpacing(10);

    // Name
    name_label_ = new QLabel(QStringLiteral("—"), hdr);
    name_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:20px; font-weight:700; background:transparent; }")
            .arg(ui::colors::TEXT_PRIMARY()));
    hl->addWidget(name_label_);

    // Party badge pill
    party_badge_ = new QLabel(QStringLiteral("—"), hdr);
    party_badge_->setFixedSize(28, 20);
    party_badge_->setAlignment(Qt::AlignCenter);
    party_badge_->setStyleSheet(
        QString("QLabel { background:%1; color:white; font-size:12px; font-weight:700;"
                " border-radius:10px; }").arg(ui::colors::BG_SURFACE()));
    hl->addWidget(party_badge_);

    // Chamber · State
    meta_label_ = new QLabel(QStringLiteral("—"), hdr);
    meta_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:12px; background:transparent; }")
            .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(meta_label_);

    hl->addStretch();

    // Est. net worth
    net_worth_label_ = new QLabel(QStringLiteral("—"), hdr);
    net_worth_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:13px; font-weight:600; background:transparent; }")
            .arg(ui::colors::AMBER()));
    hl->addWidget(net_worth_label_);

    // Rank chips container (filled later by populate_header)
    rank_chips_ = new QWidget(hdr);
    rank_chips_->setStyleSheet("QWidget { background:transparent; }");
    auto* rc_layout = new QHBoxLayout(rank_chips_);
    rc_layout->setContentsMargins(0, 0, 0, 0);
    rc_layout->setSpacing(4);
    hl->addWidget(rank_chips_);

    vl->addWidget(hdr);
}

void MemberProfilePanel::build_stat_tiles(QWidget* parent, QVBoxLayout* vl) {
    auto* row = new QWidget(parent);
    row->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(10, 10, 10, 0);
    hl->setSpacing(8);

    const QString tile_style =
        QString("QWidget { background:%1; border:1px solid %2; border-radius:4px; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED());
    const QString cap_style =
        QString("QLabel { color:%1; font-size:12px; font-weight:700; letter-spacing:0.5px;"
                " background:transparent; text-transform:uppercase; }")
            .arg(ui::colors::TEXT_SECONDARY());
    const QString val_style =
        QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                " font-family:Consolas,monospace; background:transparent; }")
            .arg(ui::colors::TEXT_PRIMARY());

    auto make_tile = [&](const QString& caption, QLabel*& val_out) {
        auto* tile = new QWidget(row);
        tile->setStyleSheet(tile_style);
        // Cap at 220px — prevents excessive horizontal stretching on wide screens
        tile->setMaximumWidth(220);
        auto* tvl = new QVBoxLayout(tile);
        tvl->setContentsMargins(10, 7, 10, 7);
        tvl->setSpacing(2);
        auto* cap = new QLabel(caption, tile);
        cap->setStyleSheet(cap_style);
        tvl->addWidget(cap);
        val_out = new QLabel(QStringLiteral("—"), tile);
        val_out->setStyleSheet(val_style);
        tvl->addWidget(val_out);
        hl->addWidget(tile);  // no stretch factor — tiles keep compact width
    };

    make_tile(QStringLiteral("EST. PORTFOLIO *"), tile_portfolio_val_);
    make_tile(QStringLiteral("YTD RETURN"),       tile_ytd_return_);
    make_tile(QStringLiteral("ALPHA vs SPY"),     tile_alpha_);
    make_tile(QStringLiteral("CMTE TRADES"),      tile_cmte_trades_);
    hl->addStretch();  // push tiles left; remaining space stays empty

    vl->addWidget(row);
}

void MemberProfilePanel::build_stat_tiles_compact(QWidget* parent, QVBoxLayout* vl) {
    // Compact vertical stat list for the left info pane.
    // Each row: label (left) + value (right). No horizontal stretch — values stay close.
    auto* box = new QWidget(parent);
    box->setStyleSheet(
        QString("QWidget { background:%1; border-top:1px solid %2; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* bvl = new QVBoxLayout(box);
    bvl->setContentsMargins(12, 8, 12, 8);
    bvl->setSpacing(4);

    const QString lbl_ss =
        QString("color:%1;font-size:12px;font-weight:600;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY());
    const QString val_ss =
        QString("color:%1;font-size:13px;font-weight:700;font-family:Consolas,monospace;background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY());

    struct StatRow { const char* caption; QLabel** ptr; };
    const StatRow rows[] = {
        {"Portfolio *",  &tile_portfolio_val_},
        {"YTD Return",   &tile_ytd_return_},
        {"Alpha vs SPY", &tile_alpha_},
        {"Cmte Trades",  &tile_cmte_trades_},
    };

    for (const auto& sr : rows) {
        auto* row = new QWidget(box);
        row->setStyleSheet("background:transparent;");
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);

        auto* lbl = new QLabel(sr.caption, row);
        lbl->setStyleSheet(lbl_ss);
        hl->addWidget(lbl);
        hl->addStretch();

        *sr.ptr = new QLabel(QStringLiteral("—"), row);
        (*sr.ptr)->setStyleSheet(val_ss);
        (*sr.ptr)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(*sr.ptr);

        bvl->addWidget(row);
    }

    vl->addWidget(box);
}

void MemberProfilePanel::build_chart_section(QWidget* parent, QVBoxLayout* vl) {
    // Chart fills the full width of its pane — committee exposure moved to Row 4.
    vl->addWidget(make_section_hdr(QStringLiteral("PORTFOLIO GROWTH CURVE"), parent));

    auto* left_vl = vl;   // write directly into the parent layout
    left_vl->setContentsMargins(0, 0, 0, 0);
    left_vl->setSpacing(0);

    // ── Period bar ────────────────────────────────────────────────────────────
    period_bar_ = new QWidget(parent);
    period_bar_->setStyleSheet(
        QString("QWidget{background:%1;border-bottom:1px solid %2;padding:4px 10px;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* pb_hl = new QHBoxLayout(period_bar_);
    pb_hl->setContentsMargins(0, 4, 0, 4);
    pb_hl->setSpacing(4);

    const QString btn_base =
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                "border-radius:3px;padding:2px 10px;font-size:12px;font-weight:600;}"
                "QPushButton:hover{background:%4;}"
                "QPushButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_RAISED(), ui::colors::AMBER(), ui::colors::BG_BASE());

    for (const QString& lbl : QStringList{"3M", "6M", "1Y", "ALL"}) {
        auto* btn = new QPushButton(lbl, period_bar_);
        btn->setCheckable(true);
        btn->setStyleSheet(btn_base);
        btn->setCursor(Qt::PointingHandCursor);
        if (lbl == QLatin1String("1Y")) btn->setChecked(true);
        pb_hl->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, lbl, btn]() {
            if (period_bar_)
                for (auto* b : period_bar_->findChildren<QPushButton*>())
                    b->setChecked(false);
            btn->setChecked(true);
            if (nav_chart_) nav_chart_->set_period(lbl);
        });
    }
    pb_hl->addStretch();
    vl->addWidget(period_bar_);

    // NavChart — fills full width (no committee panel to the right)
    nav_chart_ = new NavChart(parent);
    nav_chart_->setMinimumHeight(260);
    vl->addWidget(nav_chart_, 1);

    auto* note = new QLabel(
        QStringLiteral("★ Midpoint estimates of disclosed STOCK Act ranges"), parent);
    note->setStyleSheet(
        QString("QLabel{color:%1;font-size:12px;background:transparent;padding:2px 10px;}")
            .arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(note);

    // cmte_exposure_ kept as null — committee info now lives in Row 4 RIGHT
    cmte_exposure_ = nullptr;
    chart_splitter_ = nullptr;
}

void MemberProfilePanel::build_holdings_table(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("ESTIMATED HOLDINGS *"), parent));

    // CMTE OVERLAP removed — long committee names cluttered the table.
    // Committee-ticker correlation lives in the SIGNAL ANALYSIS section (right pane).
    // SIG column added: per-holding insider signal score (0–100).
    static const QStringList kCols = {
        "TICKER", "COMPANY", "SECTOR", "SIG", "EST. COST *", "EST. VALUE *",
        "P&L *", "P&L% *", "WT%"
    };

    holdings_table_ = new QTableWidget(parent);
    holdings_table_->setColumnCount(kCols.size());
    holdings_table_->setHorizontalHeaderLabels(kCols);
    holdings_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    holdings_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    holdings_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    holdings_table_->setShowGrid(false);
    holdings_table_->setAlternatingRowColors(false);
    holdings_table_->verticalHeader()->setVisible(false);
    holdings_table_->setFocusPolicy(Qt::NoFocus);

    auto* h = holdings_table_->horizontalHeader();
    h->setMinimumSectionSize(40);
    // Equal distribution — all columns stretch to share available width
    // Exception: TICKER and numeric columns fixed narrower; COMPANY gets extra
    h->setSectionResizeMode(QHeaderView::Stretch);             // all equal by default
    h->setSectionResizeMode(0, QHeaderView::Fixed);  h->resizeSection(0, 62);  // TICKER
    h->setSectionResizeMode(3, QHeaderView::Fixed);  h->resizeSection(3, 48);  // SIG
    h->setSectionResizeMode(7, QHeaderView::Fixed);  h->resizeSection(7, 60);  // P&L%
    h->setSectionResizeMode(8, QHeaderView::Fixed);  h->resizeSection(8, 52);  // WT%

    holdings_table_->setMinimumHeight(200);

    holdings_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:12px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:4px 8px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                "QTableWidget::item:hover { background:%4; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_DIM(), ui::colors::BG_HOVER()));

    h->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; border-right:1px solid %4;"
                "  padding:4px 6px; font-size:12px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    // Clicking the TICKER column emits navigate_to_markets
    connect(holdings_table_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col != 0) return;
        auto* item = holdings_table_->item(row, 0);
        if (item)
            emit navigate_to_markets(item->text());
    });

    vl->addWidget(holdings_table_);
}

void MemberProfilePanel::build_trades_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("RECENT TRADES (LAST 15)"), parent));

    static const QStringList kCols = {
        "DATE", "TICKER", "B/S", "AMOUNT", "LAG", "SIGNAL", "COMMITTEE"
    };

    trades_table_ = new QTableWidget(parent);
    trades_table_->setColumnCount(kCols.size());
    trades_table_->setHorizontalHeaderLabels(kCols);
    trades_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trades_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    trades_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    trades_table_->setShowGrid(false);
    trades_table_->setAlternatingRowColors(false);
    trades_table_->verticalHeader()->setVisible(false);
    trades_table_->setFocusPolicy(Qt::NoFocus);

    auto* h = trades_table_->horizontalHeader();
    h->setMinimumSectionSize(40);
    // Equal distribution — all stretch unless a column has a clear content reason
    h->setSectionResizeMode(QHeaderView::Stretch);             // all equal by default
    h->setSectionResizeMode(0, QHeaderView::Fixed);  h->resizeSection(0, 92);  // DATE (10 chars)
    h->setSectionResizeMode(1, QHeaderView::Fixed);  h->resizeSection(1, 56);  // TICKER (4 chars)
    h->setSectionResizeMode(2, QHeaderView::Fixed);  h->resizeSection(2, 54);  // B/S (4 chars)
    h->setSectionResizeMode(4, QHeaderView::Fixed);  h->resizeSection(4, 44);  // LAG (2 chars)
    h->setSectionResizeMode(5, QHeaderView::Fixed);  h->resizeSection(5, 56);  // SIGNAL (4 chars)
    // AMOUNT (col 3) and COMMITTEE (col 6) stretch equally with remaining width

    trades_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:12px; font-family:Consolas,monospace;"
                "  gridline-color:transparent; }"
                "QTableWidget::item { padding:3px 6px; border-bottom:1px solid %3; }"
                "QTableWidget::item:selected { background:rgba(217,119,6,0.18); color:%2; }"
                "QTableWidget::item:hover { background:%4; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%3; min-height:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_DIM(), ui::colors::BG_HOVER()));

    h->setStyleSheet(
        QString("QHeaderView::section { background:%1; color:%2; border:none;"
                "  border-bottom:2px solid %3; border-right:1px solid %4;"
                "  padding:4px 6px; font-size:12px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    vl->addWidget(trades_table_);
}

void MemberProfilePanel::build_combined_analysis_section(QWidget* parent, QVBoxLayout* vl) {
    // Row 4 RIGHT: unified panel combining:
    //   • Committee membership + sector mapping
    //   • Stock-committee matching (which holdings overlap which committees)
    //   • Insider signal score + evidence
    vl->addWidget(make_section_hdr(QStringLiteral("SIGNAL & INSIDER ANALYSIS"), parent));

    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        QString("QScrollArea{background:%1;border:none;}"
                "QScrollBar:vertical{width:8px;background:%1;border-radius:4px;}"
                "QScrollBar::handle:vertical{background:%2;min-height:24px;border-radius:4px;}")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_BRIGHT()));

    combined_analysis_ = new QWidget;
    combined_analysis_->setStyleSheet(
        QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
    auto* cal = new QVBoxLayout(combined_analysis_);
    cal->setContentsMargins(12, 8, 12, 10);
    cal->setSpacing(6);

    auto* placeholder = new QLabel(
        QStringLiteral("Select a member to view signal and insider analysis."),
        combined_analysis_);
    placeholder->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    placeholder->setWordWrap(true);
    cal->addWidget(placeholder);
    cal->addStretch();

    scroll->setWidget(combined_analysis_);
    vl->addWidget(scroll, 1);
}

void MemberProfilePanel::build_signal_analysis_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("COMMITTEE SIGNAL ANALYSIS"), parent));

    signal_analysis_ = new QWidget(parent);
    signal_analysis_->setStyleSheet(
        QString("QWidget{background:%1;}").arg(ui::colors::BG_BASE()));
    auto* sal = new QVBoxLayout(signal_analysis_);
    sal->setContentsMargins(12, 8, 12, 8);
    sal->setSpacing(6);

    auto* placeholder = new QLabel(
        QStringLiteral("Select a member to view committee-ticker overlap analysis."),
        signal_analysis_);
    placeholder->setStyleSheet(
        QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    placeholder->setWordWrap(true);
    sal->addWidget(placeholder);

    vl->addWidget(signal_analysis_);
}

void MemberProfilePanel::build_ranking_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("HOW THEY COMPARE"), parent));

    auto* row = new QWidget(parent);
    row->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(10, 8, 10, 8);
    hl->setSpacing(8);

    rank_cards_row_ = row;
    hl->addStretch();
    vl->addWidget(row);
    vl->addStretch();  // pushes header+cards to top, prevents vertical expansion
}

void MemberProfilePanel::build_committees_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("COMMITTEE MEMBERSHIPS & SECTOR OVERLAP"), parent));

    committees_container_ = new QWidget(parent);
    committees_container_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* cvl = new QVBoxLayout(committees_container_);
    cvl->setContentsMargins(10, 8, 10, 8);
    cvl->setSpacing(6);
    // Content filled by populate_committees()

    vl->addWidget(committees_container_);
}

void MemberProfilePanel::build_sector_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("SECTOR ALLOCATION & INSIDER CORRELATION"), parent));

    auto* outer = new QWidget(parent);
    outer->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* outer_vl = new QVBoxLayout(outer);
    outer_vl->setContentsMargins(10, 8, 10, 8);
    outer_vl->setSpacing(8);

    // Top row: pie chart + legend
    auto* chart_row = new QWidget(outer);
    chart_row->setStyleSheet("QWidget { background:transparent; }");
    auto* chart_hl = new QHBoxLayout(chart_row);
    chart_hl->setContentsMargins(0, 0, 0, 0);
    chart_hl->setSpacing(16);

    sector_pie_ = new SectorPieChart(chart_row);
    chart_hl->addWidget(sector_pie_);

    sector_legend_ = new QWidget(chart_row);
    sector_legend_->setStyleSheet("QWidget { background:transparent; }");
    auto* legend_vl = new QVBoxLayout(sector_legend_);
    legend_vl->setContentsMargins(0, 0, 0, 0);
    legend_vl->setSpacing(4);
    legend_vl->addStretch();
    // Content filled by populate_sector()
    chart_hl->addWidget(sector_legend_, 1);

    outer_vl->addWidget(chart_row);

    // Insider score label (centered, below chart+legend)
    insider_score_ = new QLabel(QStringLiteral("Insider Index: — / 100"), outer);
    insider_score_->setAlignment(Qt::AlignCenter);
    insider_score_->setStyleSheet(
        QString("QLabel { color:%1; font-size:13px; font-weight:700;"
                " font-family:Consolas,monospace; background:transparent; padding:4px 0; }")
            .arg(ui::colors::AMBER()));
    outer_vl->addWidget(insider_score_);

    vl->addWidget(outer);
}

void MemberProfilePanel::build_insights_section(QWidget* parent, QVBoxLayout* vl) {
    vl->addWidget(make_section_hdr(QStringLiteral("TRADER PROFILE"), parent));

    auto* outer = new QWidget(parent);
    outer->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* outer_vl = new QVBoxLayout(outer);
    outer_vl->setContentsMargins(10, 8, 10, 8);
    outer_vl->setSpacing(0);

    insights_container_ = new QWidget(outer);
    insights_container_->setStyleSheet("QWidget { background:transparent; }");
    // We use a flow-like wrap — implemented as a QWidget with a fixed FlowLayout
    // For simplicity use a QGridLayout that wraps chips at 4 per row
    auto* flow = new QGridLayout(insights_container_);
    flow->setContentsMargins(0, 0, 0, 0);
    flow->setSpacing(6);
    // Content filled by populate_insights()

    outer_vl->addWidget(insights_container_);
    vl->addWidget(outer);
    vl->addStretch();  // pins content to top, prevents header expanding
}

// ── Public API ────────────────────────────────────────────────────────────────

void MemberProfilePanel::set_member(const power_trader::CongressMember& member) {
    current_member_ = member;

    const auto portfolio = power_trader::PowerTraderService::instance()
                               .compute_portfolio(member.id);
    const auto all_signals = power_trader::PowerTraderService::instance()
                                 .committee_insider_signals();
    const auto trades = power_trader::PowerTraderService::instance()
                            .trades_for_member(member.id);

    // Filter signals to this member
    QVector<power_trader::CommitteeInsiderSignal> member_signals;
    for (const auto& sig : all_signals) {
        if (sig.member_id == member.id)
            member_signals.append(sig);
    }

    placeholder_->setVisible(false);
    scroll_area_->setVisible(true);

    populate(member, portfolio, member_signals, trades);
}

void MemberProfilePanel::clear() {
    show_placeholder();
}

void MemberProfilePanel::refresh_theme() {
    // Re-apply stylesheet; sub-widgets with inline styles remain styled;
    // full rebuild would require set_member() but theme changes are rare.
    setStyleSheet(QString("QWidget { background:%1; color:%2; }")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));
    update();
}

// ── Populate helpers ──────────────────────────────────────────────────────────

void MemberProfilePanel::show_placeholder() {
    placeholder_->setVisible(true);
    scroll_area_->setVisible(false);
}

void MemberProfilePanel::populate(const power_trader::CongressMember& member,
                                   const power_trader::MemberPortfolio& portfolio,
                                   const QVector<power_trader::CommitteeInsiderSignal>& insider_signals,
                                   const QVector<power_trader::PoliticalTrade>& trades) {
    populate_header(member, portfolio);
    populate_stat_tiles(member, portfolio);
    populate_chart(portfolio, insider_signals, member.committees);
    populate_holdings(portfolio);
    populate_trades(trades);
    populate_rankings(member.id);
    populate_insights(member, portfolio, trades);
    populate_sector(portfolio, insider_signals);
    populate_combined_analysis(member, portfolio, trades, insider_signals);
}

void MemberProfilePanel::populate_header(const power_trader::CongressMember& m,
                                          const power_trader::MemberPortfolio& p) {
    name_label_->setText(m.full_name);

    // Party badge
    party_badge_->setText(m.party);
    party_badge_->setStyleSheet(
        QString("QLabel { background:%1; color:white; font-size:12px; font-weight:700;"
                " border-radius:10px; }").arg(party_color(m.party)));

    // Meta: Chamber · State [· District]
    QString meta = power_trader::chamber_label(m.chamber) + QStringLiteral(" · ") + m.state;
    if (!m.district.isEmpty())
        meta += QStringLiteral(" · ") + m.district;
    meta_label_->setText(meta);

    // Net worth
    net_worth_label_->setText(QStringLiteral("Est. ") + fmt_dollar(m.estimated_net_worth));

    // ── Rank chips (top 3 ranking dimensions) ────────────────────────────────
    // Remove old chips
    {
        auto* layout = qobject_cast<QHBoxLayout*>(rank_chips_->layout());
        while (layout && layout->count() > 0) {
            auto* item = layout->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }

    // Gather ranks from portfolio
    struct RankEntry { int rank; QString label; };
    const QVector<RankEntry> all_ranks = {
        { p.rank_alpha,     QStringLiteral("Alpha") },
        { p.rank_return,    QStringLiteral("Return") },
        { p.rank_net_worth, QStringLiteral("Net Worth") },
        { p.rank_frequency, QStringLiteral("Frequency") },
        { p.rank_signal,    QStringLiteral("Avg Signal") },
    };

    // Sort ascending (rank 1 = best) and take top 3 non-zero
    QVector<RankEntry> sorted_ranks;
    for (const auto& re : all_ranks)
        if (re.rank > 0) sorted_ranks.append(re);
    std::sort(sorted_ranks.begin(), sorted_ranks.end(),
              [](const RankEntry& a, const RankEntry& b) { return a.rank < b.rank; });
    if (sorted_ranks.size() > 3)
        sorted_ranks = sorted_ranks.mid(0, 3);

    auto* hl = qobject_cast<QHBoxLayout*>(rank_chips_->layout());
    for (const auto& re : sorted_ranks) {
        auto* chip = new QLabel(
            QString("#%1 %2").arg(re.rank).arg(re.label), rank_chips_);
        chip->setStyleSheet(
            QString("QLabel { background:rgba(217,119,6,0.18); color:%1;"
                    " font-size:12px; font-weight:700; border-radius:8px;"
                    " padding:2px 8px; border:1px solid %1; }")
                .arg(ui::colors::AMBER()));
        if (hl) hl->addWidget(chip);
    }
}

void MemberProfilePanel::populate_stat_tiles(const power_trader::CongressMember& m,
                                              const power_trader::MemberPortfolio& p) {
    // Tile 1 — EST. PORTFOLIO VALUE
    tile_portfolio_val_->setText(fmt_dollar(p.est_total_value));
    tile_portfolio_val_->setStyleSheet(
        QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                " font-family:Consolas,monospace; background:transparent; }")
            .arg(ui::colors::AMBER()));

    // Tile 2 — YTD RETURN
    {
        const bool pos = m.portfolio_return_ytd >= 0;
        tile_ytd_return_->setText(fmt_pct(m.portfolio_return_ytd));
        tile_ytd_return_->setStyleSheet(
            QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(pos ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
    }

    // Tile 3 — ALPHA vs SPY
    {
        const bool pos = m.alpha_ytd >= 0;
        tile_alpha_->setText(fmt_pct(m.alpha_ytd));
        tile_alpha_->setStyleSheet(
            QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(pos ? ui::colors::POSITIVE() : ui::colors::NEGATIVE()));
    }

    // Tile 4 — COMMITTEE TRADES
    // Count holdings where committee_overlap == true
    {
        int overlap = 0;
        for (const auto& h : p.holdings)
            if (h.committee_overlap) ++overlap;
        const int total = p.holdings.size();
        const int pct = total > 0 ? qRound(100.0 * overlap / total) : 0;
        tile_cmte_trades_->setText(
            QString("%1/%2 (%3%)").arg(overlap).arg(total).arg(pct));
        tile_cmte_trades_->setStyleSheet(
            QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(overlap > 0 ? ui::colors::WARNING() : ui::colors::TEXT_PRIMARY()));
    }
}

void MemberProfilePanel::populate_chart(
    const power_trader::MemberPortfolio& p,
    const QVector<power_trader::CommitteeInsiderSignal>& insider_signals,
    const QStringList& committees)
{
    // Feed data to the NavChart
    nav_chart_->set_series(p.nav_series);
    nav_chart_->set_period(QStringLiteral("1Y"));

    // Re-check the 1Y button
    if (period_bar_) {
        const auto btns = period_bar_->findChildren<QPushButton*>();
        for (auto* btn : btns) {
            btn->setChecked(btn->text() == QLatin1String("1Y"));
        }
    }

    // Committee exposure panel removed — info lives in Row 4 combined analysis.
    if (!cmte_exposure_) return;
    {
        auto* old_layout = cmte_exposure_->layout();
        if (old_layout) {
            while (old_layout->count() > 0) {
                auto* item = old_layout->takeAt(0);
                if (auto* w = item->widget()) w->deleteLater();
                delete item;
            }
            delete old_layout;
        }
    }

    auto* evl = new QVBoxLayout(cmte_exposure_);
    evl->setContentsMargins(10, 8, 10, 8);
    evl->setSpacing(8);

    auto* cap = new QLabel(QStringLiteral("COMMITTEE EXPOSURE"), cmte_exposure_);
    cap->setStyleSheet(
        QString("QLabel { color:%1; font-size:12px; font-weight:700;"
                " letter-spacing:1.2px; background:transparent; }")
            .arg(ui::colors::TEXT_SECONDARY()));
    evl->addWidget(cap);

    if (committees.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No committee data available"), cmte_exposure_);
        none->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        evl->addWidget(none);
    } else {
        // Build per-committee overlap_pct from insider signals
        QHash<QString, double> cmte_overlap;
        for (const auto& sig : insider_signals) {
            // Use the highest signal overlap_pct per committee
            const double existing = cmte_overlap.value(sig.committee, -1.0);
            if (sig.overlap_pct > existing)
                cmte_overlap[sig.committee] = sig.overlap_pct;
        }

        for (const QString& cmte : committees) {
            const double overlap_pct = cmte_overlap.value(cmte, 0.0);

            auto* row = new QWidget(cmte_exposure_);
            row->setStyleSheet("QWidget { background:transparent; }");
            auto* rvl = new QVBoxLayout(row);
            rvl->setContentsMargins(0, 0, 0, 0);
            rvl->setSpacing(3);

            // Committee name + overlap %
            auto* label_row = new QWidget(row);
            label_row->setStyleSheet("QWidget { background:transparent; }");
            auto* lhl = new QHBoxLayout(label_row);
            lhl->setContentsMargins(0, 0, 0, 0);
            lhl->setSpacing(4);

            auto* name_lbl = new QLabel(cmte, label_row);
            name_lbl->setStyleSheet(
                QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                    .arg(ui::colors::TEXT_PRIMARY()));
            lhl->addWidget(name_lbl, 1);

            const char* pct_color = overlap_pct > 70.0 ? ui::colors::NEGATIVE
                                   : overlap_pct > 40.0 ? ui::colors::WARNING
                                   : ui::colors::TEXT_SECONDARY;
            auto* pct_lbl = new QLabel(
                QString("%1%").arg(overlap_pct, 0, 'f', 0), label_row);
            pct_lbl->setStyleSheet(
                QString("QLabel { color:%1; font-size:12px; font-weight:600;"
                        " background:transparent; }").arg(pct_color));
            lhl->addWidget(pct_lbl);

            rvl->addWidget(label_row);

            // Horizontal progress bar drawn inline with a QWidget + paintEvent
            // We implement a simple bar using a styled frame
            auto* bar_outer = new QFrame(row);
            bar_outer->setFixedHeight(5);
            bar_outer->setStyleSheet(
                QString("QFrame { background:%1; border-radius:2px; border:none; }")
                    .arg(ui::colors::BORDER_DIM()));

            auto* bar_inner = new QWidget(bar_outer);
            const int fill_w = qMax(2, qRound(overlap_pct / 100.0 * 200)); // visual cap at 200px
            bar_inner->setGeometry(0, 0, fill_w, 5);
            const char* bar_color = overlap_pct > 70.0 ? ui::colors::NEGATIVE
                                   : overlap_pct > 40.0 ? ui::colors::WARNING
                                   : ui::colors::TEXT_SECONDARY;
            bar_inner->setStyleSheet(
                QString("QWidget { background:%1; border-radius:2px; border:none; }")
                    .arg(bar_color));

            rvl->addWidget(bar_outer);
            evl->addWidget(row);
        }
    }

    evl->addStretch();
}

void MemberProfilePanel::populate_holdings(const power_trader::MemberPortfolio& p) {
    // Sort by est_cost_basis descending
    QVector<power_trader::MemberHolding> sorted = p.holdings;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const power_trader::MemberHolding& a, const power_trader::MemberHolding& b) {
                         return a.est_cost_basis > b.est_cost_basis;
                     });

    holdings_table_->setRowCount(sorted.size());

    for (int r = 0; r < sorted.size(); ++r) {
        const auto& h = sorted[r];
        holdings_table_->setRowHeight(r, 28);

        auto set_item = [&](int col, const QString& text,
                            const char* color    = nullptr,
                            Qt::Alignment align  = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setFont(QFont("Consolas", 11));
            holdings_table_->setItem(r, col, item);
        };

        // TICKER — cyan, bold, clickable
        {
            auto* item = new QTableWidgetItem(h.ticker);
            item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            item->setForeground(QColor(ui::colors::CYAN()));
            QFont f("Consolas", 13);
            f.setBold(true);
            item->setFont(f);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            holdings_table_->setItem(r, 0, item);
        }

        // COMPANY
        set_item(1, h.asset_name);

        // SECTOR
        set_item(2, h.sector, ui::colors::TEXT_SECONDARY);

        // SIG — per-holding insider signal score; amber if committee overlap
        {
            const double sig = h.committee_overlap ? 80.0 : 30.0;  // proxy signal
            auto* sig_item = new QTableWidgetItem(QString::number(sig, 'f', 0));
            sig_item->setTextAlignment(Qt::AlignCenter);
            sig_item->setForeground(QColor(h.committee_overlap
                                           ? ui::colors::AMBER : ui::colors::TEXT_SECONDARY()));
            sig_item->setFlags(sig_item->flags() & ~Qt::ItemIsEditable);
            holdings_table_->setItem(r, 3, sig_item);
        }

        // EST. COST (col 4)
        set_item(4, fmt_dollar(h.est_cost_basis),
                 ui::colors::TEXT_SECONDARY, Qt::AlignRight | Qt::AlignVCenter);

        // EST. VALUE (col 5)
        set_item(5, fmt_dollar(h.est_market_value),
                 ui::colors::AMBER, Qt::AlignRight | Qt::AlignVCenter);

        // P&L (col 6)
        {
            const bool pos = h.est_pnl >= 0;
            const QString pnl_str = (pos ? "+" : "") + fmt_dollar(h.est_pnl);
            set_item(6, pnl_str,
                     pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE,
                     Qt::AlignRight | Qt::AlignVCenter);
        }

        // P&L% (col 7)
        {
            const bool pos = h.est_pnl_pct >= 0;
            set_item(7, fmt_pct(h.est_pnl_pct),
                     pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE,
                     Qt::AlignRight | Qt::AlignVCenter);
        }

        // WT% (col 8)
        set_item(8, QString("%1%").arg(h.est_weight, 0, 'f', 1),
                 ui::colors::TEXT_PRIMARY, Qt::AlignRight | Qt::AlignVCenter);
    }
}

void MemberProfilePanel::populate_trades(const QVector<power_trader::PoliticalTrade>& all_trades) {
    // Sort by disclosure date desc, take last 15
    QVector<power_trader::PoliticalTrade> sorted = all_trades;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const power_trader::PoliticalTrade& a, const power_trader::PoliticalTrade& b) {
                         return a.disclosure_date > b.disclosure_date;
                     });
    if (sorted.size() > 15)
        sorted = sorted.mid(0, 15);

    trades_table_->setRowCount(sorted.size());

    for (int r = 0; r < sorted.size(); ++r) {
        const auto& t = sorted[r];
        trades_table_->setRowHeight(r, 28);

        auto set_item = [&](int col, const QString& text,
                            const char* color   = nullptr,
                            Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            item->setForeground(QColor(color ? color : ui::colors::TEXT_PRIMARY()));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setFont(QFont("Consolas", 11));
            trades_table_->setItem(r, col, item);
        };

        // DATE
        set_item(0, t.disclosure_date.toString(QStringLiteral("yyyy-MM-dd")),
                 ui::colors::TEXT_SECONDARY);

        // TICKER
        set_item(1, t.ticker, ui::colors::CYAN);

        // B/S
        {
            const bool is_buy  = t.direction == power_trader::TradeDirection::Buy;
            const bool is_sell = t.direction == power_trader::TradeDirection::Sell;
            const char* dir_col = is_buy  ? ui::colors::POSITIVE
                                : is_sell ? ui::colors::NEGATIVE
                                : ui::colors::TEXT_SECONDARY;
            set_item(2, power_trader::direction_label(t.direction), dir_col, Qt::AlignCenter);
        }

        // AMOUNT
        set_item(3, t.amount_range_label, ui::colors::TEXT_SECONDARY);

        // LAG
        {
            const char* lag_col = t.disclosure_lag_days > 30
                                      ? ui::colors::WARNING
                                      : ui::colors::TEXT_SECONDARY;
            set_item(4, QString::number(t.disclosure_lag_days), lag_col, Qt::AlignCenter);
        }

        // SIGNAL
        {
            const char* sig_col = t.signal_score >= 70.0
                                      ? ui::colors::AMBER
                                      : ui::colors::TEXT_TERTIARY;
            set_item(5, QString::number(t.signal_score, 'f', 0), sig_col, Qt::AlignCenter);
        }

        // COMMITTEE
        set_item(6, t.committee_relevance.isEmpty()
                        ? QStringLiteral("–")
                        : t.committee_relevance,
                 t.committee_relevance.isEmpty() ? ui::colors::TEXT_TERTIARY : ui::colors::WARNING);
    }
}

void MemberProfilePanel::populate_rankings(const QString& member_id) {
    // Clear old cards from layout
    auto* hl = qobject_cast<QHBoxLayout*>(rank_cards_row_->layout());
    if (hl) {
        while (hl->count() > 0) {
            auto* item = hl->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    if (!hl) return;

    using RD = power_trader::RankingDimension;
    const QVector<QPair<RD, QString>> dims = {
        { RD::Alpha,     QStringLiteral("Alpha") },
        { RD::Return,    QStringLiteral("Return") },
        { RD::NetWorth,  QStringLiteral("Net Worth") },
        { RD::Frequency, QStringLiteral("Frequency") },
        { RD::AvgSignal, QStringLiteral("Avg Signal") },
        { RD::BestPick,  QStringLiteral("Best Pick") },
    };

    for (const auto& [dim, label] : dims) {
        const auto ranked = power_trader::PowerTraderService::instance().ranked_members(dim);
        const int total   = ranked.size();

        // Find this member's rank
        int rank = 0;
        QString rank_val_label = QStringLiteral("—");
        for (const auto& rm : ranked) {
            if (rm.member.id == member_id) {
                rank = rm.rank;
                rank_val_label = rm.rank_label;
                break;
            }
        }

        // Card widget
        auto* card = new QWidget(rank_cards_row_);
        card->setStyleSheet(
            QString("QWidget { background:%1; border:1px solid %2; border-radius:4px; }")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
        card->setMinimumWidth(110);

        auto* cvl = new QVBoxLayout(card);
        cvl->setContentsMargins(10, 8, 10, 8);
        cvl->setSpacing(3);

        // Dimension name (top, small)
        auto* dim_lbl = new QLabel(label, card);
        dim_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; font-weight:700;"
                    " letter-spacing:0.8px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        cvl->addWidget(dim_lbl);

        // Rank value
        auto* val_lbl = new QLabel(rank_val_label, card);
        val_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:16px; font-weight:700;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(ui::colors::TEXT_PRIMARY()));
        cvl->addWidget(val_lbl);

        // Rank badge "#N of M"
        auto* badge_lbl = new QLabel(
            rank > 0 ? QString("#%1 of %2").arg(rank).arg(total) : QStringLiteral("—"),
            card);
        badge_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; font-weight:600;"
                    " background:transparent; }").arg(ui::colors::AMBER()));
        cvl->addWidget(badge_lbl);

        hl->addWidget(card, 1);
    }

    hl->addStretch();
}

// ── Section 7: populate_committees ───────────────────────────────────────────

void MemberProfilePanel::populate_signal_analysis(
    const power_trader::CongressMember& m,
    const power_trader::MemberPortfolio& p,
    const QVector<power_trader::PoliticalTrade>& trades)
{
    if (!signal_analysis_) return;

    auto* sal = qobject_cast<QVBoxLayout*>(signal_analysis_->layout());
    if (sal) {
        while (sal->count() > 0) {
            auto* item = sal->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    } else return;

    if (m.committees.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No committee data available."),
                                signal_analysis_);
        none->setStyleSheet(
            QString("color:%1;font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
        sal->addWidget(none);
        return;
    }

    // Build committee → list of overlapping holdings
    QHash<QString, QStringList> cmte_holdings;
    for (const auto& h : p.holdings) {
        if (h.committee_overlap && !h.committee_name.isEmpty())
            cmte_holdings[h.committee_name].append(h.ticker);
    }

    // Per-committee count of relevant trades
    QHash<QString, int> cmte_trade_count;
    for (const auto& t : trades)
        if (!t.committee_relevance.isEmpty())
            cmte_trade_count[t.committee_relevance]++;

    const int total_trades = trades.size();

    const QString card_ss =
        QString("QWidget{background:%1;border:1px solid %2;border-radius:3px;}")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED());
    const QString cmte_ss =
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY());
    const QString detail_ss =
        QString("color:%1;font-size:12px;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY());
    const QString ticker_ss =
        QString("color:%1;font-size:12px;font-weight:700;"
                "font-family:Consolas,monospace;background:transparent;")
            .arg(ui::colors::CYAN());

    for (const auto& committee : m.committees) {
        const QStringList& overlap_tickers = cmte_holdings.value(committee);
        const int tc = cmte_trade_count.value(committee, 0);
        const double pct = total_trades > 0 ? 100.0 * tc / total_trades : 0.0;

        auto* card = new QWidget(signal_analysis_);
        card->setStyleSheet(card_ss);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 6, 10, 6);
        cl->setSpacing(3);

        // Committee name + trade overlap %
        auto* row1 = new QHBoxLayout;
        auto* cmte_lbl = new QLabel(committee, card);
        cmte_lbl->setStyleSheet(cmte_ss);
        cmte_lbl->setWordWrap(true);
        row1->addWidget(cmte_lbl, 1);

        if (tc > 0) {
            auto* score_lbl = new QLabel(
                QString("%1 trades (%2%)").arg(tc).arg(pct, 0, 'f', 0), card);
            score_lbl->setStyleSheet(
                QString("color:%1;font-size:12px;font-weight:600;background:transparent;")
                    .arg(pct > 30 ? ui::colors::WARNING : ui::colors::AMBER()));
            row1->addWidget(score_lbl);
        }
        cl->addLayout(row1);

        // Overlapping holdings
        if (overlap_tickers.isEmpty()) {
            auto* none_lbl = new QLabel(QStringLiteral("No overlapping holdings"), card);
            none_lbl->setStyleSheet(detail_ss);
            cl->addWidget(none_lbl);
        } else {
            auto* row2 = new QHBoxLayout;
            row2->setSpacing(4);
            auto* holds_lbl = new QLabel(QStringLiteral("Holdings: "), card);
            holds_lbl->setStyleSheet(detail_ss);
            row2->addWidget(holds_lbl);
            for (const auto& tk : overlap_tickers) {
                auto* t_lbl = new QLabel(tk, card);
                t_lbl->setStyleSheet(ticker_ss);
                row2->addWidget(t_lbl);
            }
            row2->addStretch();
            cl->addLayout(row2);
        }

        sal->addWidget(card);
    }

    // Summary line
    const int total_overlap = cmte_trade_count.values().isEmpty() ? 0
        : std::accumulate(cmte_trade_count.begin(), cmte_trade_count.end(), 0);
    const double overlap_pct = total_trades > 0 ? 100.0 * total_overlap / total_trades : 0;
    auto* sum_lbl = new QLabel(
        QString("Total committee-relevant trades: %1 / %2  (%3%)")
            .arg(total_overlap).arg(total_trades).arg(overlap_pct, 0, 'f', 0),
        signal_analysis_);
    sum_lbl->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:600;padding-top:4px;background:transparent;")
            .arg(overlap_pct > 30 ? ui::colors::WARNING : ui::colors::TEXT_SECONDARY()));
    sal->addWidget(sum_lbl);
}

void MemberProfilePanel::populate_combined_analysis(
    const power_trader::CongressMember& m,
    const power_trader::MemberPortfolio& p,
    const QVector<power_trader::PoliticalTrade>& trades,
    const QVector<power_trader::CommitteeInsiderSignal>& insider_signals)
{
    if (!combined_analysis_) return;

    // Clear existing content then rebuild
    auto* cal = qobject_cast<QVBoxLayout*>(combined_analysis_->layout());
    if (!cal) {
        cal = new QVBoxLayout(combined_analysis_);
        cal->setContentsMargins(12, 8, 12, 10);
        cal->setSpacing(6);
    } else {
        while (cal->count() > 0) {
            auto* item = cal->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }

    const QString section_ss =
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;"
                "margin-top:6px;border-bottom:1px solid %2;padding-bottom:3px;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM());
    const QString card_ss =
        QString("QWidget{background:%1;border:1px solid %2;border-radius:3px;}")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED());
    const QString lbl_ss =
        QString("color:%1;font-size:12px;background:transparent;")
            .arg(ui::colors::TEXT_SECONDARY());
    const QString val_ss =
        QString("color:%1;font-size:12px;font-weight:700;"
                "font-family:Consolas,monospace;background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY());
    const QString ticker_ss =
        QString("color:%1;font-size:12px;font-weight:700;"
                "font-family:Consolas,monospace;background:transparent;")
            .arg(ui::colors::CYAN());

    // ── SECTION 1: Committee membership + sector mapping + trade overlap ───────
    {
        auto* sec = new QLabel(QStringLiteral("COMMITTEE MEMBERSHIP & OVERLAP"), combined_analysis_);
        sec->setStyleSheet(section_ss);
        cal->addWidget(sec);

        const int total_trades = trades.size();
        QHash<QString, double> cmte_overlap_pct;
        for (const auto& sig : insider_signals)
            if (sig.overlap_pct > cmte_overlap_pct.value(sig.committee, -1.0))
                cmte_overlap_pct[sig.committee] = sig.overlap_pct;

        if (m.committees.isEmpty()) {
            auto* n = new QLabel(QStringLiteral("No committee data."), combined_analysis_);
            n->setStyleSheet(lbl_ss);
            cal->addWidget(n);
        } else {
            for (const auto& cmte : m.committees) {
                int tc = 0;
                for (const auto& t : trades)
                    if (t.committee_relevance == cmte) ++tc;
                const double pct = total_trades > 0 ? 100.0 * tc / total_trades : 0;
                const double ovl = cmte_overlap_pct.value(cmte, 0.0);

                auto* card = new QWidget(combined_analysis_);
                card->setStyleSheet(card_ss);
                auto* cl = new QVBoxLayout(card);
                cl->setContentsMargins(10, 5, 10, 5);
                cl->setSpacing(2);

                auto* row1 = new QHBoxLayout;
                auto* cnl = new QLabel(cmte, card);
                cnl->setStyleSheet(val_ss);
                cnl->setWordWrap(true);
                row1->addWidget(cnl, 1);
                if (tc > 0) {
                    auto* pl = new QLabel(
                        QString("%1 trades · %2% overlap").arg(tc).arg(pct, 0, 'f', 0), card);
                    pl->setStyleSheet(
                        QString("color:%1;font-size:12px;font-weight:600;background:transparent;")
                            .arg(pct > 30 ? ui::colors::WARNING : ui::colors::AMBER()));
                    row1->addWidget(pl);
                }
                cl->addLayout(row1);

                // Sectors regulated by this committee
                const QStringList secs = committee_sectors(cmte);
                if (!secs.isEmpty()) {
                    auto* sl = new QLabel("Sectors: " + secs.join(", "), card);
                    sl->setStyleSheet(lbl_ss);
                    sl->setWordWrap(true);
                    cl->addWidget(sl);
                }
                cal->addWidget(card);
            }
        }
    }

    // ── SECTION 2: Stock-committee matching ───────────────────────────────────
    {
        auto* sec = new QLabel(QStringLiteral("HOLDING → COMMITTEE CORRELATION"), combined_analysis_);
        sec->setStyleSheet(section_ss);
        cal->addWidget(sec);

        QVector<power_trader::MemberHolding> overlap_holdings;
        for (const auto& h2 : p.holdings)
            if (h2.committee_overlap)
                overlap_holdings.append(h2);

        if (overlap_holdings.isEmpty()) {
            auto* n = new QLabel(QStringLiteral("No holdings overlap committee sectors."), combined_analysis_);
            n->setStyleSheet(lbl_ss);
            cal->addWidget(n);
        } else {
            for (const auto& h : overlap_holdings) {
                auto* card = new QWidget(combined_analysis_);
                card->setStyleSheet(card_ss);
                auto* cl = new QHBoxLayout(card);
                cl->setContentsMargins(10, 5, 10, 5);
                cl->setSpacing(8);

                auto* tl = new QLabel(h.ticker, card);
                tl->setStyleSheet(ticker_ss);
                tl->setFixedWidth(56);
                cl->addWidget(tl);

                auto* nl = new QLabel(h.asset_name, card);
                nl->setStyleSheet(lbl_ss);
                cl->addWidget(nl, 1);

                auto* cmte_lbl = new QLabel("● " + h.committee_name, card);
                cmte_lbl->setStyleSheet(
                    QString("color:%1;font-size:12px;background:transparent;")
                        .arg(ui::colors::AMBER()));
                cmte_lbl->setWordWrap(true);
                cl->addWidget(cmte_lbl);

                cal->addWidget(card);
            }
        }
    }

    // ── SECTION 3: Insider signal score ──────────────────────────────────────
    {
        auto* sec = new QLabel(QStringLiteral("INSIDER SIGNAL INDEX"), combined_analysis_);
        sec->setStyleSheet(section_ss);
        cal->addWidget(sec);

        // Average signal across all trades
        double avg_sig = 0; int n = 0;
        for (const auto& t : trades) { avg_sig += t.signal_score; ++n; }
        if (n > 0) avg_sig /= n;

        int overlap_count = 0;
        for (const auto& h : p.holdings)
            if (h.committee_overlap) ++overlap_count;
        const double cmte_pct = p.holdings.isEmpty() ? 0
            : 100.0 * overlap_count / p.holdings.size();

        auto* card = new QWidget(combined_analysis_);
        card->setStyleSheet(card_ss);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(10, 6, 10, 6);
        cl->setSpacing(4);

        auto add_kv = [&](const QString& label, const QString& val) {
            auto* row = new QHBoxLayout;
            auto* ll = new QLabel(label, card); ll->setStyleSheet(lbl_ss);
            auto* vl = new QLabel(val, card);
            vl->setStyleSheet(val_ss);
            vl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(ll); row->addStretch(); row->addWidget(vl);
            cl->addLayout(row);
        };
        add_kv("Avg trade signal:",
               QString::number(avg_sig, 'f', 0) + "/100");
        add_kv("Holdings with committee overlap:",
               QString("%1 / %2  (%3%)")
                   .arg(overlap_count)
                   .arg(p.holdings.size())
                   .arg(cmte_pct, 0, 'f', 0));
        add_kv("Avg disclosure lag:",
               QString::number(
                   power_trader::PowerTraderService::instance()
                       .avg_disclosure_lag(m.id), 'f', 0) + "d");

        cal->addWidget(card);
    }

    cal->addStretch();
}

void MemberProfilePanel::populate_committees(
    const power_trader::CongressMember& m,
    const QVector<power_trader::CommitteeInsiderSignal>& insider_sigs,
    const QVector<power_trader::PoliticalTrade>& trades)
{
    // Clear old layout contents
    auto* cvl = qobject_cast<QVBoxLayout*>(committees_container_->layout());
    if (cvl) {
        while (cvl->count() > 0) {
            auto* item = cvl->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    if (!cvl) return;

    if (m.committees.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No committee data available"),
                                committees_container_);
        none->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        cvl->addWidget(none);
        return;
    }

    const int total_trades = trades.size();

    // Build per-committee overlap_pct from insider signals
    QHash<QString, double> cmte_overlap_pct;
    for (const auto& sig : insider_sigs) {
        const double existing = cmte_overlap_pct.value(sig.committee, -1.0);
        if (sig.overlap_pct > existing)
            cmte_overlap_pct[sig.committee] = sig.overlap_pct;
    }

    for (const QString& committee : m.committees) {
        // Count trades matching this committee
        int cmte_trade_count = 0;
        for (const auto& t : trades) {
            if (t.committee_relevance == committee)
                ++cmte_trade_count;
        }
        const double trade_pct = total_trades > 0
            ? (100.0 * cmte_trade_count / total_trades)
            : 0.0;

        // Determine sector labels for this committee
        const QStringList sectors = committee_sectors(committee);
        const QString sector_str = sectors.isEmpty()
            ? QStringLiteral("General")
            : sectors.join(QStringLiteral(" · "));

        // Card widget
        auto* card = new QWidget(committees_container_);
        card->setStyleSheet(
            QString("QWidget { background:%1; border:1px solid %2;"
                    " border-radius:3px; }")
                .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

        auto* card_vl = new QVBoxLayout(card);
        card_vl->setContentsMargins(10, 8, 10, 8);
        card_vl->setSpacing(4);

        // Committee name row
        auto* name_row = new QWidget(card);
        name_row->setStyleSheet("QWidget { background:transparent; }");
        auto* name_hl = new QHBoxLayout(name_row);
        name_hl->setContentsMargins(0, 0, 0, 0);
        name_hl->setSpacing(8);

        auto* cmte_name_lbl = new QLabel(committee, name_row);
        cmte_name_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; font-weight:700;"
                    " background:transparent; }").arg(ui::colors::AMBER()));
        name_hl->addWidget(cmte_name_lbl, 1);

        // Trade count badge
        auto* count_lbl = new QLabel(
            QString("%1 / %2 trades (%3%)")
                .arg(cmte_trade_count)
                .arg(total_trades)
                .arg(trade_pct, 0, 'f', 0),
            name_row);
        const char* count_color = trade_pct > 60.0 ? ui::colors::NEGATIVE
                                : trade_pct > 30.0 ? ui::colors::WARNING
                                : ui::colors::TEXT_TERTIARY;
        count_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; font-weight:600;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(count_color));
        name_hl->addWidget(count_lbl);
        card_vl->addWidget(name_row);

        // Oversight sectors label
        auto* sectors_lbl = new QLabel(sector_str, card);
        sectors_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        card_vl->addWidget(sectors_lbl);

        // Progress bar for trade %
        auto* bar_outer = new QFrame(card);
        bar_outer->setFixedHeight(4);
        bar_outer->setStyleSheet(
            QString("QFrame { background:%1; border-radius:2px; border:none; }")
                .arg(ui::colors::BORDER_DIM()));

        auto* bar_inner = new QWidget(bar_outer);
        // Cap visual at full width; clamp
        const int bar_w = qMax(2, qMin(qRound(trade_pct / 100.0 * 300), 300));
        bar_inner->setGeometry(0, 0, bar_w, 4);
        bar_inner->setStyleSheet(
            QString("QWidget { background:%1; border-radius:2px; border:none; }")
                .arg(count_color));
        card_vl->addWidget(bar_outer);

        cvl->addWidget(card);
    }
}

// ── Section 8: populate_sector ────────────────────────────────────────────────

void MemberProfilePanel::populate_sector(
    const power_trader::MemberPortfolio& portfolio,
    const QVector<power_trader::CommitteeInsiderSignal>& insider_sigs)
{
    // ── Sector color map ──────────────────────────────────────────────────────
    static const QHash<QString, QColor> kSectorColors = {
        { QStringLiteral("Technology"),  QColor("#22d3ee") },
        { QStringLiteral("Defense"),     QColor("#ef4444") },
        { QStringLiteral("Financials"),  QColor("#3b82f6") },
        { QStringLiteral("Energy"),      QColor("#f97316") },
        { QStringLiteral("Healthcare"),  QColor("#22c55e") },
        { QStringLiteral("Consumer"),    QColor("#a855f7") },
        { QStringLiteral("ETF"),         QColor("#94a3b8") },
        { QStringLiteral("Other"),       QColor("#64748b") },
    };

    auto sector_color = [&](const QString& sector) -> QColor {
        for (auto it = kSectorColors.cbegin(); it != kSectorColors.cend(); ++it) {
            if (sector.contains(it.key(), Qt::CaseInsensitive))
                return it.value();
        }
        return QColor("#64748b");
    };

    // ── Aggregate holdings by sector ─────────────────────────────────────────
    QHash<QString, double> sector_weight;  // sector -> sum of est_weight
    for (const auto& h : portfolio.holdings) {
        const QString sec = h.sector.isEmpty() ? QStringLiteral("Other") : h.sector;
        sector_weight[sec] += h.est_weight;
    }

    // ── Compute correlation score per sector ──────────────────────────────────
    // Find the max overlap_pct from insider signals where committee overlaps sector
    QHash<QString, double> sector_corr;
    for (const auto& sig : insider_sigs) {
        for (auto it = sector_weight.cbegin(); it != sector_weight.cend(); ++it) {
            if (sector_matches_committee(it.key(), sig.committee)) {
                const double existing = sector_corr.value(it.key(), 0.0);
                if (sig.overlap_pct > existing)
                    sector_corr[it.key()] = sig.overlap_pct;
            }
        }
    }

    // ── Build slices ──────────────────────────────────────────────────────────
    QVector<SectorPieChart::Slice> slices;
    slices.reserve(sector_weight.size());
    for (auto it = sector_weight.cbegin(); it != sector_weight.cend(); ++it) {
        SectorPieChart::Slice sl;
        sl.sector     = it.key();
        sl.pct        = it.value();
        sl.color      = sector_color(it.key());
        sl.corr_score = sector_corr.value(it.key(), 0.0);
        slices.append(sl);
    }
    // Sort by pct descending
    std::sort(slices.begin(), slices.end(),
              [](const SectorPieChart::Slice& a, const SectorPieChart::Slice& b) {
                  return a.pct > b.pct;
              });

    sector_pie_->set_slices(slices);

    // ── Rebuild legend ────────────────────────────────────────────────────────
    auto* legend_vl = qobject_cast<QVBoxLayout*>(sector_legend_->layout());
    if (legend_vl) {
        while (legend_vl->count() > 0) {
            auto* item = legend_vl->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    } else {
        legend_vl = new QVBoxLayout(sector_legend_);
        legend_vl->setContentsMargins(0, 0, 0, 0);
        legend_vl->setSpacing(4);
    }

    for (const auto& sl : slices) {
        auto* row = new QWidget(sector_legend_);
        row->setStyleSheet("QWidget { background:transparent; }");
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 2, 0, 2);
        hl->setSpacing(6);

        // Colored dot
        auto* dot = new QLabel(QStringLiteral("●"), row);
        dot->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                .arg(sl.color.name()));
        dot->setFixedWidth(14);
        hl->addWidget(dot);

        // Sector name
        auto* name_lbl = new QLabel(sl.sector, row);
        name_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; background:transparent; }")
                .arg(ui::colors::TEXT_PRIMARY()));
        hl->addWidget(name_lbl, 1);

        // Pct
        auto* pct_lbl = new QLabel(
            QString("%1%").arg(sl.pct, 0, 'f', 1), row);
        pct_lbl->setStyleSheet(
            QString("QLabel { color:%1; font-size:12px; font-weight:600;"
                    " font-family:Consolas,monospace; background:transparent; }")
                .arg(ui::colors::TEXT_SECONDARY()));
        pct_lbl->setFixedWidth(44);
        pct_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(pct_lbl);

        // Narrow allocation bar (40px wide, 4px tall, inline via a small widget)
        auto* mini_bar = new QFrame(row);
        mini_bar->setFixedSize(40, 4);
        mini_bar->setStyleSheet(
            QString("QFrame { background:%1; border-radius:2px; border:none; }")
                .arg(ui::colors::BORDER_DIM()));
        auto* mini_fill = new QWidget(mini_bar);
        const int fw = qMax(1, qRound(sl.pct / 100.0 * 40));
        mini_fill->setGeometry(0, 0, fw, 4);
        mini_fill->setStyleSheet(
            QString("QWidget { background:%1; border-radius:2px; border:none; }")
                .arg(sl.color.name()));
        hl->addWidget(mini_bar);

        // Committee overlap badge if corr > 0
        if (sl.corr_score > 0.0) {
            auto* cmte_badge = new QLabel(QStringLiteral("CMTE ●"), row);
            cmte_badge->setStyleSheet(
                QString("QLabel { color:%1; font-size:12px; font-weight:700;"
                        " background:rgba(217,119,6,0.15); border:1px solid %1;"
                        " border-radius:3px; padding:0px 4px; }")
                    .arg(ui::colors::AMBER()));
            hl->addWidget(cmte_badge);
        }

        legend_vl->addWidget(row);
    }
    legend_vl->addStretch();

    // ── Compute Insider Index ─────────────────────────────────────────────────
    double total_pct = 0.0;
    for (const auto& sl : slices) total_pct += sl.pct;
    double insider_idx = 0.0;
    if (total_pct > 0.0) {
        for (const auto& sl : slices)
            insider_idx += (sl.pct / total_pct) * sl.corr_score;
    }
    // Scale to 0–100 (corr_score is already 0–100, so this is a weighted average)
    const int idx_int = qRound(insider_idx);

    const char* score_color = idx_int > 80 ? ui::colors::NEGATIVE
                            : idx_int > 70 ? ui::colors::WARNING
                            : ui::colors::AMBER;
    insider_score_->setText(
        QString("Insider Index: %1 / 100").arg(idx_int));
    insider_score_->setStyleSheet(
        QString("QLabel { color:%1; font-size:13px; font-weight:700;"
                " font-family:Consolas,monospace; background:transparent; padding:4px 0; }")
            .arg(score_color));
}

// ── Section 9: populate_insights ─────────────────────────────────────────────

void MemberProfilePanel::populate_insights(
    const power_trader::CongressMember& m,
    const power_trader::MemberPortfolio& portfolio,
    const QVector<power_trader::PoliticalTrade>& trades)
{
    // Clear old chips
    auto* grid = qobject_cast<QGridLayout*>(insights_container_->layout());
    if (grid) {
        while (grid->count() > 0) {
            auto* item = grid->takeAt(0);
            if (auto* w = item->widget()) w->deleteLater();
            delete item;
        }
    }
    if (!grid) return;

    // ── Compute stats from trades ─────────────────────────────────────────────
    const QDate today = QDate::currentDate();
    const QDate cutoff_90d = today.addDays(-90);

    double sum_lag          = 0.0;
    int    late_count       = 0;  // lag > 30
    double net_flow_90d     = 0.0;
    int    total_trade_cnt  = trades.size();
    int    cmte_overlap_cnt = 0;

    QHash<QString, int>    ticker_count;   // for most traded
    QHash<QString, int>    ticker_buy_cnt; // for conviction
    int stock_count = 0, option_count = 0, etf_count = 0;

    // best pick tracking
    double best_pnl_pct    = 0.0;
    QString best_ticker;

    for (const auto& t : trades) {
        sum_lag += t.disclosure_lag_days;
        if (t.disclosure_lag_days > 30) ++late_count;

        if (!t.committee_relevance.isEmpty()) ++cmte_overlap_cnt;

        ticker_count[t.ticker]++;
        if (t.direction == power_trader::TradeDirection::Buy)
            ticker_buy_cnt[t.ticker]++;

        // Asset type
        switch (t.asset_type) {
            case power_trader::AssetType::Option: ++option_count; break;
            case power_trader::AssetType::ETF:    ++etf_count;    break;
            default:                               ++stock_count;  break;
        }

        // Net flow 90d
        if (t.transaction_date >= cutoff_90d) {
            const double mid = (t.amount_low + t.amount_high) / 2.0;
            if (t.direction == power_trader::TradeDirection::Buy)
                net_flow_90d += mid;
            else if (t.direction == power_trader::TradeDirection::Sell)
                net_flow_90d -= mid;
        }
    }

    const double avg_lag = total_trade_cnt > 0
        ? sum_lag / total_trade_cnt : 0.0;
    const double late_pct = total_trade_cnt > 0
        ? (100.0 * late_count / total_trade_cnt) : 0.0;

    // Most traded ticker
    QString most_traded_ticker;
    int most_traded_n = 0;
    for (auto it = ticker_count.cbegin(); it != ticker_count.cend(); ++it) {
        if (it.value() > most_traded_n) {
            most_traded_n = it.value();
            most_traded_ticker = it.key();
        }
    }

    // Conviction picks (buy_count >= 2)
    QVector<QPair<QString, int>> conviction_picks;
    for (auto it = ticker_buy_cnt.cbegin(); it != ticker_buy_cnt.cend(); ++it) {
        if (it.value() >= 2)
            conviction_picks.append({ it.key(), it.value() });
    }
    std::sort(conviction_picks.begin(), conviction_picks.end(),
              [](const QPair<QString,int>& a, const QPair<QString,int>& b) {
                  return a.second > b.second;
              });

    // Asset mix
    const int asset_total = stock_count + option_count + etf_count;
    const int stock_pct   = asset_total > 0 ? qRound(100.0 * stock_count  / asset_total) : 0;
    const int option_pct  = asset_total > 0 ? qRound(100.0 * option_count / asset_total) : 0;
    const int etf_pct     = asset_total > 0 ? qRound(100.0 * etf_count    / asset_total) : 0;

    // Best pick from portfolio
    for (const auto& h : portfolio.holdings) {
        if (h.est_pnl_pct > best_pnl_pct) {
            best_pnl_pct = h.est_pnl_pct;
            best_ticker  = h.ticker;
        }
    }

    // Committee overlap %
    const double cmte_pct = total_trade_cnt > 0
        ? (100.0 * cmte_overlap_cnt / total_trade_cnt) : 0.0;

    // ── Helper: make a chip widget ────────────────────────────────────────────
    struct ChipSpec {
        QString text;
        const char* fg_color;
        const char* bg_color;   // nullptr = BG_RAISED
        const char* bd_color;   // nullptr = BORDER_DIM
    };

    QVector<ChipSpec> chips;

    // 1. Avg lag
    chips.append({
        QString("Avg Lag: %1d").arg(qRound(avg_lag)),
        avg_lag > 35.0 ? ui::colors::NEGATIVE
      : avg_lag > 20.0 ? ui::colors::AMBER
      : ui::colors::TEXT_SECONDARY,
        nullptr, nullptr
    });

    // 2. Late filers %
    chips.append({
        QString("Late (>30d): %1%").arg(qRound(late_pct)),
        late_pct > 40.0 ? ui::colors::NEGATIVE : ui::colors::TEXT_SECONDARY,
        nullptr, nullptr
    });

    // 3. Net flow 90d
    {
        const bool net_buy = net_flow_90d >= 0;
        chips.append({
            net_buy
                ? QString("Net Buyer: +%1").arg(fmt_dollar_free(net_flow_90d))
                : QString("Net Seller: %1").arg(fmt_dollar_free(net_flow_90d)),
            net_buy ? ui::colors::POSITIVE : ui::colors::NEGATIVE,
            nullptr, nullptr
        });
    }

    // 4. Most traded
    if (!most_traded_ticker.isEmpty()) {
        chips.append({
            QString("Top Pick: %1 (%2 trades)").arg(most_traded_ticker).arg(most_traded_n),
            ui::colors::TEXT_PRIMARY,
            nullptr, nullptr
        });
    }

    // 5. Conviction picks
    if (!conviction_picks.isEmpty()) {
        QString conv_str = QStringLiteral("Conviction:");
        const int max_show = qMin(3, (int)conviction_picks.size());
        for (int i = 0; i < max_show; ++i) {
            conv_str += QString(" %1×%2")
                .arg(conviction_picks[i].first)
                .arg(conviction_picks[i].second);
        }
        chips.append({ conv_str, ui::colors::CYAN, nullptr, nullptr });
    }

    // 6. Asset mix
    chips.append({
        QString("Stocks: %1%  Options: %2%  ETFs: %3%")
            .arg(stock_pct).arg(option_pct).arg(etf_pct),
        ui::colors::TEXT_SECONDARY,
        nullptr, nullptr
    });

    // 7. Best pick
    if (!best_ticker.isEmpty()) {
        chips.append({
            QString("Best Pick: %1 +%2%")
                .arg(best_ticker).arg(best_pnl_pct, 0, 'f', 1),
            ui::colors::CYAN,
            nullptr, nullptr
        });
    }

    // 8. Total trades
    chips.append({
        QString("%1 trades on record").arg(total_trade_cnt),
        ui::colors::TEXT_TERTIARY,
        nullptr, nullptr
    });

    // 9. Committee overlap warning (prominent, red bg) if overlap > 60%
    if (cmte_pct > 60.0) {
        chips.append({
            QString("⚠ %1/%2 trades (%3%) match committee sectors")
                .arg(cmte_overlap_cnt).arg(total_trade_cnt).arg(qRound(cmte_pct)),
            "white",
            ui::colors::NEGATIVE,
            ui::colors::NEGATIVE
        });
    }

    // ── Add chip widgets to grid (4 per row) ─────────────────────────────────
    const QString chip_base_style =
        QString("QLabel { background:%1; color:%%fg%%; border:1px solid %2;"
                " border-radius:12px; padding:4px 10px; font-size:12px; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM());

    int col = 0, row_idx = 0;
    for (const auto& chip : chips) {
        auto* lbl = new QLabel(chip.text, insights_container_);
        lbl->setWordWrap(false);

        const QString bg = chip.bg_color
            ? QString(chip.bg_color) : QString(ui::colors::BG_RAISED());
        const QString bd = chip.bd_color
            ? QString(chip.bd_color) : QString(ui::colors::BORDER_DIM());
        lbl->setStyleSheet(
            QString("QLabel { background:%1; color:%2; border:1px solid %3;"
                    " border-radius:12px; padding:4px 10px; font-size:12px; }")
                .arg(bg, QString(chip.fg_color), bd));

        grid->addWidget(lbl, row_idx, col);
        ++col;
        if (col >= 4) { col = 0; ++row_idx; }
    }
}

} // namespace fincept::screens
