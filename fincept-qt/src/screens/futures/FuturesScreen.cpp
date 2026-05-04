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

    connect(&ThemeManager::instance(), &ThemeManager::theme_changed, this,
            [this](const ThemeTokens&) { apply_theme(); });
    apply_theme();

    set_active_class("INDEX");
}

void FuturesScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Wake the shared cache (ref-counted; a no-op if other consumers are
    // already visible) and start our local refresh tick.
    FuturesQuoteCache::instance().retain();
    if (refresh_timer_ && !refresh_timer_->isActive())
        refresh_timer_->start();
    // Force-fresh on tab activation so the user always sees the latest data.
    services::MarketDataService::instance().invalidate_quotes({});
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
        connect(btn, &QPushButton::clicked, this, [this, cls]() { set_active_class(cls); });
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
    grid_host_ = new QWidget(body_);
    auto* grid = new QGridLayout(grid_host_);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    heatmap_     = new FuturesHeatmapPanel(grid_host_);
    watchlist_   = new FuturesWatchlistPanel(grid_host_);
    term_        = new FuturesTermStructurePanel(grid_host_);
    chart_       = new FuturesChartPanel(grid_host_);
    settlements_ = new FuturesSettlementsPanel(grid_host_);
    spread_      = new FuturesSpreadPanel(grid_host_);

    // Row 0: heatmap full width (3 columns)
    grid->addWidget(heatmap_,     0, 0, 1, 3);
    // Row 1: watchlist | term structure | continuous chart
    grid->addWidget(watchlist_,   1, 0);
    grid->addWidget(term_,        1, 1);
    grid->addWidget(chart_,       1, 2);
    // Row 2: settlements (2 cols) | spread (1 col)
    grid->addWidget(settlements_, 2, 0, 1, 2);
    grid->addWidget(spread_,      2, 2);

    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 2);
    grid->setRowStretch(2, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    stack->addWidget(grid_host_);

    // ── China host ──
    auto* china_host = new QWidget(body_);
    auto* china_layout = new QVBoxLayout(china_host);
    china_layout->setContentsMargins(6, 6, 6, 6);
    china_ = new FuturesChinaPanel(china_host);
    china_layout->addWidget(china_);
    stack->addWidget(china_host);

    // Cross-panel wiring: clicking a watchlist row routes the symbol to the
    // term structure / chart / settlements panels for drill-down.
    connect(watchlist_, &FuturesWatchlistPanel::symbol_clicked, this, [this](const QString& sym) {
        if (term_) term_->set_symbol(sym);
        if (chart_) chart_->set_symbol(sym);
        if (settlements_) settlements_->set_symbol(sym);
    });
}

void FuturesScreen::set_active_class(const QString& cls) {
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
        for (auto* p : QVector<FuturesPanelBase*>{watchlist_, term_, chart_, settlements_, spread_}) {
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
    // Re-apply active class styling
    if (!active_class_.isEmpty())
        set_active_class(active_class_);
}

} // namespace fincept::screens::futures
