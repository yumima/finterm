#include "screens/futures/FuturesScreen.h"

#include "screens/futures/FuturesContracts.h"
#include "screens/futures/FuturesPanels.h"
#include "screens/futures/FuturesQuoteCache.h"
#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace fincept::screens::futures {

using namespace fincept::ui;

FuturesScreen::FuturesScreen(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    build_header();
    root->addWidget(header_bar_);

    build_body();
    root->addWidget(body_, 1);

    // Local refresh tick for the symbol-driven panels (term structure,
    // settlements, chart) — runs in lockstep with the cache cycle. Started
    // in showEvent, stopped in hideEvent so it doesn't poll while the
    // FUTURES tab is hidden.
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kFuturesRefreshIntervalMs);
    connect(refresh_timer_, &QTimer::timeout, this, &FuturesScreen::refresh_all);

    // Click-debounce for the asset class tab strip. Without it, rapid clicks
    // INDEX→RATES→ENERGY→… each fire 4 async Python fetches per panel (term
    // structure, chart, settlements, COT) and bury the 3-worker pool. Single-
    // shot timer that we restart on every click — only the LAST click's
    // class survives, propagated to the panels when the user stops clicking.
    class_change_timer_ = new QTimer(this);
    class_change_timer_->setSingleShot(true);
    class_change_timer_->setInterval(250); // 250 ms = "user landed on a tab"
    connect(class_change_timer_, &QTimer::timeout, this, [this]() {
        if (!pending_class_.isEmpty())
            apply_active_class(pending_class_);
    });

    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { apply_theme(); });
    apply_theme();

    // First class loads immediately — no debounce for the initial paint.
    apply_active_class("INDEX");
}

void FuturesScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Wake the shared cache (ref-counted; a no-op if other consumers are
    // already visible) and start our local refresh tick.
    FuturesQuoteCache::instance().retain();
    if (refresh_timer_ && !refresh_timer_->isActive())
        refresh_timer_->start();
    // Force-fresh refresh — but DON'T invalidate MarketDataService's quote
    // cache. Futures use FuturesDataService, a separate service with no
    // connection to the "market:" cache prefix. The old code called
    // invalidate_quotes({}) here, which wiped every other screen's quote
    // cache for no benefit to Futures. refresh_all() below already triggers
    // the futures fetch through FuturesQuoteCache.
    refresh_all();
}

void FuturesScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refresh_timer_) refresh_timer_->stop();
    FuturesQuoteCache::instance().release();
}

void FuturesScreen::build_header() {
    header_bar_ = new QWidget(this);
    header_bar_->setObjectName("futuresHeader");
    header_bar_->setFixedHeight(36);

    auto* h = new QHBoxLayout(header_bar_);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(8);

    auto* brand = new QLabel("FINCEPT FUTURES");
    brand->setStyleSheet(QString("color:%1;background:transparent;font-weight:700;"
                                 "letter-spacing:0.6px;font-family:'%2';font-size:%3px;")
                             .arg(colors::TEXT_PRIMARY())
                             .arg(fonts::DATA_FAMILY())
                             .arg(fonts::font_px(0)));
    h->addWidget(brand);

    auto* sep = new QLabel("|");
    sep->setStyleSheet(QString("color:%1;background:transparent;").arg(colors::BORDER_DIM()));
    sep->setContentsMargins(8, 0, 8, 0);
    h->addWidget(sep);

    // Asset class tabs
    for (const auto& cls : asset_classes()) {
        auto* btn = new QPushButton(cls);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("asset_class", cls);
        btn->setFixedHeight(26);
        btn->setMinimumWidth(60);
        connect(btn, &QPushButton::clicked, this, [this, cls]() { request_active_class(cls); });
        class_btns_.push_back(btn);
        h->addWidget(btn);
    }

    h->addStretch();
}

void FuturesScreen::build_body() {
    body_ = new QWidget(this);
    auto* stack = new QStackedLayout(body_);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);

    // ── Grid host (shown for all classes except CHINA) ──
    //
    // Resizable Bloomberg-style 3-rail layout built from QSplitters so the
    // user can drag any divider to suit their screen. We deliberately don't
    // pin minimum/maximum widths on panels — Qt's stretch factors set the
    // initial proportions and the user owns the rest. CLAUDE.md note:
    // "Trust Qt defaults — don't lock widths or intercept splitterMoved".
    grid_host_ = new QWidget(body_);
    auto* host_layout = new QVBoxLayout(grid_host_);
    host_layout->setContentsMargins(6, 6, 6, 6);
    host_layout->setSpacing(6);

    heatmap_     = new FuturesHeatmapPanel(grid_host_);
    watchlist_   = new FuturesWatchlistPanel(grid_host_);
    term_        = new FuturesTermStructurePanel(grid_host_);
    chart_       = new FuturesChartPanel(grid_host_);
    settlements_ = new FuturesSettlementsPanel(grid_host_);
    spread_      = new FuturesSpreadPanel(grid_host_);
    cot_         = new FuturesCotPanel(grid_host_);
    expiry_      = new FuturesExpiryPanel(grid_host_);

    // Top vertical splitter: heatmap strip (top) | row1 (middle) | row2 (bottom).
    auto* v_split = new QSplitter(Qt::Vertical, grid_host_);
    v_split->setChildrenCollapsible(false);  // panels can't collapse to 0
    v_split->setHandleWidth(4);

    // Row 1 — primary research:
    //   watchlist (drives symbol selection) | continuous chart | settlements · OI
    auto* row1 = new QSplitter(Qt::Horizontal, v_split);
    row1->setChildrenCollapsible(false);
    row1->setHandleWidth(4);
    row1->addWidget(watchlist_);
    row1->addWidget(chart_);
    row1->addWidget(settlements_);
    row1->setStretchFactor(0, 1);  // watchlist  — narrow rail
    row1->setStretchFactor(1, 3);  // chart      — elastic
    row1->setStretchFactor(2, 1);  // settlements — narrow rail

    // Row 2 — analytics:
    //   [spread monitor / expiry calendar — vertical splitter] | COT | term structure
    auto* row2 = new QSplitter(Qt::Horizontal, v_split);
    row2->setChildrenCollapsible(false);
    row2->setHandleWidth(4);

    auto* left_v = new QSplitter(Qt::Vertical, row2);
    left_v->setChildrenCollapsible(false);
    left_v->setHandleWidth(4);
    left_v->addWidget(spread_);
    left_v->addWidget(expiry_);
    left_v->setStretchFactor(0, 1);  // spread — compact KPI card
    left_v->setStretchFactor(1, 3);  // expiry — taller table

    row2->addWidget(left_v);
    row2->addWidget(cot_);
    row2->addWidget(term_);
    row2->setStretchFactor(0, 1);
    row2->setStretchFactor(1, 3);
    row2->setStretchFactor(2, 1);

    v_split->addWidget(heatmap_);
    v_split->addWidget(row1);
    v_split->addWidget(row2);
    v_split->setStretchFactor(0, 1);  // heatmap — compact strip
    v_split->setStretchFactor(1, 3);  // row1
    v_split->setStretchFactor(2, 3);  // row2

    host_layout->addWidget(v_split, 1);

    stack->addWidget(grid_host_);

    // ── China host ──
    auto* china_host = new QWidget(body_);
    auto* china_layout = new QVBoxLayout(china_host);
    china_layout->setContentsMargins(6, 6, 6, 6);
    china_ = new FuturesChinaPanel(china_host);
    china_layout->addWidget(china_);
    stack->addWidget(china_host);

    // Cross-panel wiring: clicking a watchlist row routes the symbol to the
    // term structure / chart / settlements / COT panels for drill-down.
    connect(watchlist_, &FuturesWatchlistPanel::symbol_clicked, this, [this](const QString& sym) {
        if (term_) term_->set_symbol(sym);
        if (chart_) chart_->set_symbol(sym);
        if (settlements_) settlements_->set_symbol(sym);
        if (cot_) cot_->set_symbol(sym);
    });
}

void FuturesScreen::request_active_class(const QString& cls) {
    // Always reflect the latest button click in the tab visual state so the
    // user gets immediate feedback (active tab highlights) even before the
    // debounce fires. The actual panel propagation is deferred.
    pending_class_ = cls;
    for (auto* b : class_btns_) {
        const bool active = b->property("asset_class").toString() == cls;
        b->setStyleSheet(QString(
            "QPushButton { background:%1; color:%2; border:1px solid %3;"
            "  padding:0 10px; font-weight:600; letter-spacing:0.5px;"
            "  font-family:'%4'; font-size:%5px; }"
            "QPushButton:hover { background:%6; }")
            .arg(active ? colors::AMBER()   : QString("transparent"))
            .arg(active ? colors::BG_BASE() : colors::TEXT_PRIMARY())
            .arg(active ? colors::AMBER_DIM(): colors::BORDER_DIM())
            .arg(fonts::DATA_FAMILY())
            .arg(fonts::font_px(-2))
            .arg(colors::BG_RAISED()));
    }
    // No-op short-circuit: if the user clicks the already-active class there
    // is nothing to refetch. Cancels any pending debounce that targets the
    // same class.
    if (cls == active_class_) {
        if (class_change_timer_) class_change_timer_->stop();
        return;
    }
    if (class_change_timer_) class_change_timer_->start();
}

void FuturesScreen::apply_active_class(const QString& cls) {
    active_class_ = cls;

    // Update tab visual state
    for (auto* b : class_btns_) {
        const bool active = b->property("asset_class").toString() == cls;
        b->setStyleSheet(QString(
            "QPushButton { background:%1; color:%2; border:1px solid %3;"
            "  padding:0 10px; font-weight:600; letter-spacing:0.5px;"
            "  font-family:'%4'; font-size:%5px; }"
            "QPushButton:hover { background:%6; }")
            .arg(active ? colors::AMBER()   : QString("transparent"))
            .arg(active ? colors::BG_BASE() : colors::TEXT_PRIMARY())
            .arg(active ? colors::AMBER_DIM(): colors::BORDER_DIM())
            .arg(fonts::DATA_FAMILY())
            .arg(fonts::font_px(-2))
            .arg(colors::BG_RAISED()));
    }

    auto* stack = qobject_cast<QStackedLayout*>(body_->layout());
    if (cls == "CHINA") {
        if (stack) stack->setCurrentIndex(1);
        if (china_) china_->refresh();
    } else {
        if (stack) stack->setCurrentIndex(0);
        if (heatmap_) heatmap_->refresh();
        for (auto* p : QVector<FuturesPanelBase*>{watchlist_, term_, chart_, settlements_, spread_, cot_, expiry_}) {
            if (p) p->set_asset_class(cls);
        }
    }
}

void FuturesScreen::refresh_all() {
    if (active_class_ == "CHINA") {
        if (china_) china_->refresh();
        return;
    }
    // Cache-driven panels (heatmap/watchlist/spread) re-render automatically
    // when the cache broadcasts; we just kick the cache. Symbol-driven panels
    // refresh themselves to pick up new history/settlements data.
    FuturesQuoteCache::instance().refresh();
    if (term_) term_->refresh();
    if (chart_) chart_->refresh();
    if (settlements_) settlements_->refresh();
}

void FuturesScreen::apply_theme() {
    setStyleSheet(QString("background:%1;").arg(colors::BG_BASE()));
    if (header_bar_)
        header_bar_->setStyleSheet(QString("#futuresHeader { background:%1; border-bottom:1px solid %2; }")
                                       .arg(colors::BG_SURFACE())
                                       .arg(colors::BORDER_DIM()));
    // Re-apply active class styling on theme change. Use apply_active_class()
    // (not request_active_class) so the theme reload doesn't go through the
    // debounce path — theme changes need to repaint immediately.
    if (!active_class_.isEmpty())
        apply_active_class(active_class_);
}

} // namespace fincept::screens::futures
