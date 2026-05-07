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
        "QLabel { background:%1; color:%2; font-size:9px; font-weight:700;"
        " letter-spacing:1.5px; padding:6px 10px; border-bottom:1px solid %3; }")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM());
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
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
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
    p.setFont(QFont("Consolas", 8));
    const int kGridLines = 4;
    for (int i = 0; i <= kGridLines; ++i) {
        const double frac = double(i) / kGridLines;
        const double yv   = y_lo + frac * y_span;
        const double py   = r.bottom() - frac * r.height();

        // Grid line
        p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1, Qt::SolidLine));
        p.drawLine(QPointF(r.left(), py), QPointF(r.right(), py));

        // Label
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
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
    p.setFont(QFont("Consolas", 8));
    p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
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
        p.setPen(QColor(ui::colors::TEXT_TERTIARY()));
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

        p.setFont(QFont("Consolas", 9));
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
            QString("QLabel { color:%1; font-size:14px; background:transparent; }")
                .arg(ui::colors::TEXT_TERTIARY()));
        pl->addWidget(msg);
    }
    root->addWidget(placeholder_);

    // ── Scroll area wrapping all detail content ───────────────────────────────
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet(
        QString("QScrollArea { background:%1; border:none; }"
                "QScrollBar:vertical { width:4px; background:%1; }"
                "QScrollBar::handle:vertical { background:%2; min-height:16px; }"
                "QScrollBar:horizontal { height:4px; background:%1; }"
                "QScrollBar::handle:horizontal { background:%2; min-width:16px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    content_ = new QWidget;
    content_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    scroll_area_->setWidget(content_);

    auto* vl = new QVBoxLayout(content_);
    vl->setContentsMargins(0, 0, 0, 16);
    vl->setSpacing(0);

    build_header_bar(content_, vl);
    build_stat_tiles(content_, vl);
    build_chart_section(content_, vl);
    build_holdings_table(content_, vl);
    build_trades_section(content_, vl);
    build_ranking_section(content_, vl);
    vl->addStretch();

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
    hl->setContentsMargins(12, 10, 12, 10);
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
        QString("QLabel { background:%1; color:white; font-size:11px; font-weight:700;"
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
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM());
    const QString cap_style =
        QString("QLabel { color:%1; font-size:9px; font-weight:700; letter-spacing:1px;"
                " background:transparent; text-transform:uppercase; }")
            .arg(ui::colors::TEXT_TERTIARY());
    const QString val_style =
        QString("QLabel { color:%1; font-size:18px; font-weight:700;"
                " font-family:Consolas,monospace; background:transparent; }")
            .arg(ui::colors::TEXT_PRIMARY());

    auto make_tile = [&](const QString& caption, QLabel*& val_out) {
        auto* tile = new QWidget(row);
        tile->setStyleSheet(tile_style);
        auto* tvl = new QVBoxLayout(tile);
        tvl->setContentsMargins(10, 8, 10, 8);
        tvl->setSpacing(2);
        auto* cap = new QLabel(caption, tile);
        cap->setStyleSheet(cap_style);
        tvl->addWidget(cap);
        val_out = new QLabel(QStringLiteral("—"), tile);
        val_out->setStyleSheet(val_style);
        tvl->addWidget(val_out);
        hl->addWidget(tile, 1);
    };

    // 4 tiles — add asterisk to portfolio value caption
    make_tile(QStringLiteral("EST. PORTFOLIO VALUE *"), tile_portfolio_val_);
    make_tile(QStringLiteral("YTD RETURN"),             tile_ytd_return_);
    make_tile(QStringLiteral("ALPHA vs SPY"),            tile_alpha_);
    make_tile(QStringLiteral("COMMITTEE TRADES"),        tile_cmte_trades_);

    vl->addWidget(row);
}

void MemberProfilePanel::build_chart_section(QWidget* parent, QVBoxLayout* vl) {
    // Section label
    auto* hdr_label = new QLabel(QStringLiteral("ESTIMATED PORTFOLIO GROWTH (from disclosed trades)"), parent);
    hdr_label->setStyleSheet(section_header_style());
    vl->addWidget(hdr_label);

    // Splitter: left = chart + controls, right = committee exposure
    chart_splitter_ = new QSplitter(Qt::Horizontal, parent);
    chart_splitter_->setHandleWidth(1);
    chart_splitter_->setStyleSheet(
        QString("QSplitter::handle { background:%1; }").arg(ui::colors::BORDER_DIM()));
    chart_splitter_->setMinimumHeight(280);

    // ── Left side: period buttons + nav chart ─────────────────────────────────
    auto* left = new QWidget;
    left->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* left_vl = new QVBoxLayout(left);
    left_vl->setContentsMargins(10, 8, 8, 8);
    left_vl->setSpacing(6);

    // Period selector bar
    period_bar_ = new QWidget(left);
    period_bar_->setStyleSheet("QWidget { background:transparent; }");
    auto* pb_hl = new QHBoxLayout(period_bar_);
    pb_hl->setContentsMargins(0, 0, 0, 0);
    pb_hl->setSpacing(4);

    const QString btn_base =
        QString("QPushButton { background:%1; color:%2; border:1px solid %3;"
                " border-radius:3px; padding:2px 10px; font-size:10px; font-weight:600; }"
                "QPushButton:hover { background:%4; }"
                "QPushButton:checked { background:%5; color:%6; border-color:%5; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_RAISED(), ui::colors::AMBER(), ui::colors::BG_BASE());

    const QStringList periods = {"3M", "6M", "1Y", "ALL"};
    for (const QString& lbl : periods) {
        auto* btn = new QPushButton(lbl, period_bar_);
        btn->setCheckable(true);
        btn->setStyleSheet(btn_base);
        btn->setCursor(Qt::PointingHandCursor);
        if (lbl == QLatin1String("1Y"))
            btn->setChecked(true);
        pb_hl->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, lbl, btn]() {
            // Uncheck siblings
            if (period_bar_) {
                const auto btns = period_bar_->findChildren<QPushButton*>();
                for (auto* b : btns)
                    b->setChecked(false);
            }
            btn->setChecked(true);
            if (nav_chart_)
                nav_chart_->set_period(lbl);
        });
    }
    pb_hl->addStretch();
    left_vl->addWidget(period_bar_);

    // NavChart
    nav_chart_ = new NavChart(left);
    left_vl->addWidget(nav_chart_, 1);

    // Footnote
    auto* note = new QLabel(
        QStringLiteral("★ All values are midpoint estimates of disclosed ranges"), left);
    note->setStyleSheet(
        QString("QLabel { color:%1; font-size:9px; background:transparent; padding:2px 0; }")
            .arg(ui::colors::TEXT_TERTIARY()));
    left_vl->addWidget(note);

    // ── Right side: committee exposure ────────────────────────────────────────
    cmte_exposure_ = new QWidget;
    cmte_exposure_->setStyleSheet(
        QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    // Content filled by populate_chart()

    chart_splitter_->addWidget(left);
    chart_splitter_->addWidget(cmte_exposure_);
    chart_splitter_->setStretchFactor(0, 55);
    chart_splitter_->setStretchFactor(1, 45);

    vl->addWidget(chart_splitter_);
}

void MemberProfilePanel::build_holdings_table(QWidget* parent, QVBoxLayout* vl) {
    auto* hdr_label = new QLabel(
        QStringLiteral("ESTIMATED HOLDINGS *"), parent);
    hdr_label->setStyleSheet(section_header_style());
    vl->addWidget(hdr_label);

    static const QStringList kCols = {
        "TICKER", "COMPANY", "SECTOR", "EST. COST *", "EST. VALUE *",
        "P&L *", "P&L% *", "WT%", "CMTE OVERLAP"
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
    h->setMinimumSectionSize(20);
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Fixed);   // TICKER
    h->resizeSection(0, 70);
    h->setSectionResizeMode(1, QHeaderView::Stretch); // COMPANY
    h->setSectionResizeMode(2, QHeaderView::Fixed);   // SECTOR
    h->resizeSection(2, 90);
    h->setSectionResizeMode(3, QHeaderView::Fixed);   // EST. COST
    h->resizeSection(3, 80);
    h->setSectionResizeMode(4, QHeaderView::Fixed);   // EST. VALUE
    h->resizeSection(4, 80);
    h->setSectionResizeMode(5, QHeaderView::Fixed);   // P&L
    h->resizeSection(5, 80);
    h->setSectionResizeMode(6, QHeaderView::Fixed);   // P&L%
    h->resizeSection(6, 68);
    h->setSectionResizeMode(7, QHeaderView::Fixed);   // WT%
    h->resizeSection(7, 56);
    h->setSectionResizeMode(8, QHeaderView::Fixed);   // CMTE OVERLAP
    h->resizeSection(8, 140);

    holdings_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:11px; font-family:Consolas,monospace;"
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
                "  padding:4px 6px; font-size:9px; font-weight:700; letter-spacing:0.5px; }")
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
    auto* hdr_label = new QLabel(QStringLiteral("RECENT TRADES (LAST 15)"), parent);
    hdr_label->setStyleSheet(section_header_style());
    vl->addWidget(hdr_label);

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
    h->setMinimumSectionSize(20);
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::Fixed);    // DATE
    h->resizeSection(0, 84);
    h->setSectionResizeMode(1, QHeaderView::Fixed);    // TICKER
    h->resizeSection(1, 64);
    h->setSectionResizeMode(2, QHeaderView::Fixed);    // B/S
    h->resizeSection(2, 44);
    h->setSectionResizeMode(3, QHeaderView::Stretch);  // AMOUNT
    h->setSectionResizeMode(4, QHeaderView::Fixed);    // LAG
    h->resizeSection(4, 44);
    h->setSectionResizeMode(5, QHeaderView::Fixed);    // SIGNAL
    h->resizeSection(5, 54);
    h->setSectionResizeMode(6, QHeaderView::Fixed);    // COMMITTEE
    h->resizeSection(6, 180);

    trades_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; border:none;"
                "  font-size:11px; font-family:Consolas,monospace;"
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
                "  padding:4px 6px; font-size:9px; font-weight:700; letter-spacing:0.5px; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::AMBER(), ui::colors::BORDER_DIM()));

    vl->addWidget(trades_table_);
}

void MemberProfilePanel::build_ranking_section(QWidget* parent, QVBoxLayout* vl) {
    auto* hdr_label = new QLabel(QStringLiteral("HOW THEY COMPARE"), parent);
    hdr_label->setStyleSheet(section_header_style());
    vl->addWidget(hdr_label);

    auto* row = new QWidget(parent);
    row->setStyleSheet(QString("QWidget { background:%1; }").arg(ui::colors::BG_BASE()));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(10, 10, 10, 10);
    hl->setSpacing(8);

    rank_cards_row_ = row;
    // Cards are populated by populate_rankings()

    hl->addStretch();
    vl->addWidget(row);
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
}

void MemberProfilePanel::populate_header(const power_trader::CongressMember& m,
                                          const power_trader::MemberPortfolio& p) {
    name_label_->setText(m.full_name);

    // Party badge
    party_badge_->setText(m.party);
    party_badge_->setStyleSheet(
        QString("QLabel { background:%1; color:white; font-size:11px; font-weight:700;"
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
                    " font-size:9px; font-weight:700; border-radius:8px;"
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

    // ── Build committee exposure panel ────────────────────────────────────────
    // Clear old layout
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
        QString("QLabel { color:%1; font-size:9px; font-weight:700;"
                " letter-spacing:1.2px; background:transparent; }")
            .arg(ui::colors::TEXT_TERTIARY()));
    evl->addWidget(cap);

    if (committees.isEmpty()) {
        auto* none = new QLabel(QStringLiteral("No committee data available"), cmte_exposure_);
        none->setStyleSheet(
            QString("QLabel { color:%1; font-size:11px; background:transparent; }")
                .arg(ui::colors::TEXT_TERTIARY()));
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
                QString("QLabel { color:%1; font-size:10px; background:transparent; }")
                    .arg(ui::colors::TEXT_PRIMARY()));
            lhl->addWidget(name_lbl, 1);

            const char* pct_color = overlap_pct > 70.0 ? ui::colors::NEGATIVE
                                   : overlap_pct > 40.0 ? ui::colors::WARNING
                                   : ui::colors::TEXT_SECONDARY;
            auto* pct_lbl = new QLabel(
                QString("%1%").arg(overlap_pct, 0, 'f', 0), label_row);
            pct_lbl->setStyleSheet(
                QString("QLabel { color:%1; font-size:10px; font-weight:600;"
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
            QFont f("Consolas", 11);
            f.setBold(true);
            item->setFont(f);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            holdings_table_->setItem(r, 0, item);
        }

        // COMPANY
        set_item(1, h.asset_name);

        // SECTOR
        set_item(2, h.sector, ui::colors::TEXT_TERTIARY);

        // EST. COST
        set_item(3, fmt_dollar(h.est_cost_basis),
                 ui::colors::TEXT_SECONDARY, Qt::AlignRight | Qt::AlignVCenter);

        // EST. VALUE
        set_item(4, fmt_dollar(h.est_market_value),
                 ui::colors::AMBER, Qt::AlignRight | Qt::AlignVCenter);

        // P&L
        {
            const bool pos = h.est_pnl >= 0;
            const QString pnl_str = (pos ? "+" : "") + fmt_dollar(h.est_pnl);
            set_item(5, pnl_str,
                     pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE,
                     Qt::AlignRight | Qt::AlignVCenter);
        }

        // P&L%
        {
            const bool pos = h.est_pnl_pct >= 0;
            set_item(6, fmt_pct(h.est_pnl_pct),
                     pos ? ui::colors::POSITIVE : ui::colors::NEGATIVE,
                     Qt::AlignRight | Qt::AlignVCenter);
        }

        // WT%
        set_item(7, QString("%1%").arg(h.est_weight, 0, 'f', 1),
                 ui::colors::TEXT_PRIMARY, Qt::AlignRight | Qt::AlignVCenter);

        // CMTE OVERLAP
        if (h.committee_overlap) {
            const QString cmte_text = QStringLiteral("● ") + h.committee_name;
            set_item(8, cmte_text, ui::colors::WARNING);
        } else {
            set_item(8, QStringLiteral("–"), ui::colors::TEXT_TERTIARY);
        }
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
            QString("QLabel { color:%1; font-size:9px; font-weight:700;"
                    " letter-spacing:0.8px; background:transparent; }")
                .arg(ui::colors::TEXT_TERTIARY()));
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
            QString("QLabel { color:%1; font-size:10px; font-weight:600;"
                    " background:transparent; }").arg(ui::colors::AMBER()));
        cvl->addWidget(badge_lbl);

        hl->addWidget(card, 1);
    }

    hl->addStretch();
}

} // namespace fincept::screens
