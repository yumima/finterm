// src/screens/equity_research/EquityResearchScreen.cpp
#include "screens/equity_research/EquityResearchScreen.h"

#include "core/events/EventBus.h"
#include "core/session/ScreenStateManager.h"
#include "core/symbol/SymbolContext.h"
#include "services/app_context/AppContextService.h"
#include "core/symbol/SymbolDragSource.h"
#include "screens/equity_research/EquityAiTab.h"
#include "screens/equity_research/EquityAnalysisTab.h"
#include "screens/equity_research/EquityEarningsTab.h"
#include "screens/equity_research/EquityFinancialsTab.h"
#include "screens/equity_research/EquityNewsTab.h"
#include "screens/equity_research/EquityOverviewTab.h"
#include "screens/equity_research/EquityPeersTab.h"
#include "screens/equity_research/EquitySentimentTab.h"
#include "screens/equity_research/EquityTalippTab.h"
#include "screens/equity_research/EquityTechnicalsTab.h"
#include "screens/relationship_map/RelationshipMapScreen.h"
#include "services/equity/EquityResearchService.h"
#include "services/finnhub/FinnhubLiveTicks.h"
#include "services/finnhub/FinnhubService.h"
#include "services/markets/MarketDataService.h"
#include "services/query/QueryStore.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaType>
#include <QPainter>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTimeZone>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

// Data roles on QListWidgetItem for the search-result popup. UserRole holds
// the symbol (used by activation handlers); the rest are read by the
// delegate below for richer rendering. Kept anonymous-namespace local to
// this translation unit — only the popup uses them.
constexpr int kRoleSymbol   = Qt::UserRole;
constexpr int kRoleName     = Qt::UserRole + 1;
constexpr int kRoleExchange = Qt::UserRole + 2;
constexpr int kRoleCurrency = Qt::UserRole + 3;
constexpr int kRoleType     = Qt::UserRole + 4;

// Custom row painter for the inline ticker-search popup. Renders each
// match as:
//     SYMBOL (bold amber) · NAME (primary, ellipsized) ········ EX · CCY · [TYPE]
// The right-aligned chip stack disambiguates cross-listings (e.g. BHP on
// NYSE in USD vs ASX in AUD), which a single plain-text line buried in
// the middle of the row couldn't make obvious. Type chip only shown when
// the row isn't a plain equity — adding "EQUITY" to every line would be
// noise.
class SearchResultDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override {
        const QFontMetrics fm(opt.font);
        return {0, fm.height() + 10};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();

        // Selection background — let the default style draw the highlight
        // rectangle so the popup's QSS theming continues to control colors.
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        o.text.clear();   // suppress the default text-paint
        QStyledItemDelegate::paint(p, o, idx);

        const QString sym  = idx.data(kRoleSymbol).toString();
        const QString name = idx.data(kRoleName).toString();
        const QString exch = idx.data(kRoleExchange).toString();
        const QString ccy  = idx.data(kRoleCurrency).toString();
        const QString type = idx.data(kRoleType).toString().toUpper();

        QRect r = opt.rect.adjusted(8, 0, -8, 0);

        // ── Right side: type chip first (drawn rightmost), then ccy, exch.
        QFont chip_font = opt.font;
        chip_font.setPointSizeF(chip_font.pointSizeF() - 1);
        chip_font.setBold(true);
        const QFontMetrics chip_fm(chip_font);
        p->setFont(chip_font);

        auto draw_chip = [&](const QString& text, const QColor& color) {
            if (text.isEmpty()) return;
            const int tw = chip_fm.horizontalAdvance(text);
            const QRect cr(r.right() - tw - 8, r.top() + (r.height() - chip_fm.height()) / 2 - 1,
                           tw + 8, chip_fm.height() + 2);
            // Background fill is omitted on purpose — keeps the popup looking
            // flat and avoids fighting selection highlight. Just coloured text.
            p->setPen(color);
            p->drawText(cr, Qt::AlignCenter, text);
            r.setRight(cr.left() - 6);
        };

        const QColor dim(0x88, 0x88, 0x88);
        if (!type.isEmpty() && type != "EQUITY" && type != "STOCK")
            draw_chip("[" + type + "]", dim);
        draw_chip(ccy, dim);
        draw_chip(exch, QColor(0xC8, 0x9B, 0x3C));   // muted amber

        // ── Left side: symbol bold amber, then name in primary text colour.
        QFont sym_font = opt.font;
        sym_font.setBold(true);
        const QFontMetrics sym_fm(sym_font);
        p->setFont(sym_font);
        p->setPen(QColor(0xFF, 0xB0, 0x00));
        const int sym_w = sym_fm.horizontalAdvance(sym);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, sym);

        QRect name_rect = r.adjusted(sym_w + 12, 0, 0, 0);
        QFont name_font = opt.font;
        p->setFont(name_font);
        p->setPen(opt.palette.color(QPalette::Text));
        const QString elided = QFontMetrics(name_font).elidedText(
            name, Qt::ElideRight, name_rect.width());
        p->drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft, elided);

        p->restore();
    }
};

} // namespace

EquityResearchScreen::EquityResearchScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    // Refresh timer — only started in showEvent
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(20 * 1000); // 20s quote refresh
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        if (current_symbol_.isEmpty()) return;
        // The tab panels read through QueryStore, which nothing was
        // re-kicking — so the Overview tab's OHLC, chart, financials, news
        // and peers sat on their first-load values for as long as the symbol
        // stayed open. revalidate_owner refreshes exactly the keys the
        // visible tab subscribes to, keeping the displayed values in place
        // until the replacements land.
        //
        // Ordering matters: the QueryStore quote fetcher and fetch_quote()
        // dedupe through DIFFERENT in-flight guards, so running both against
        // a lapsed TTL spawned two identical daemon RPCs every minute. The
        // subscribed path covers the Overview tab; the broadcast fetch runs
        // only for tabs that have no quote subscription of their own.
        bool quote_covered = false;
        if (tab_widget_) {
            if (QWidget* visible = tab_widget_->currentWidget()) {
                services::query::QueryStore::instance().revalidate_owner(visible);
                quote_covered = (visible == overview_tab_);
            }
        }
        if (!quote_covered)
            services::equity::EquityResearchService::instance().fetch_quote(current_symbol_);
    });

    // Wire service signals
    auto& svc = services::equity::EquityResearchService::instance();
    connect(&svc, &services::equity::EquityResearchService::quote_loaded, this, &EquityResearchScreen::on_quote_loaded);
    connect(&svc, &services::equity::EquityResearchService::info_loaded, this, &EquityResearchScreen::on_info_loaded);

    // ── Per-tab freshness tracking ──────────────────────────────────────────
    // Stamped by widget, not by index — the tab order has shifted twice and
    // the hardcoded indices here were left pointing at the wrong tabs both
    // times (technicals_loaded stamped AI Forecast, peers stamped TALIpp, …).
    // Overview/Analysis share the load_symbol triple (quote+info+history), so
    // any of those signals stamps both.
    // Trailing-argument-drop: Qt allows zero-arg lambda slots on signals
    // with any number of arguments, which sidesteps name churn in the
    // service's payload structs.
    connect(&svc, &services::equity::EquityResearchService::quote_loaded, this,
            [this]() { mark_tab_loaded(overview_tab_); mark_tab_loaded(analysis_tab_); });
    connect(&svc, &services::equity::EquityResearchService::info_loaded, this,
            [this]() { mark_tab_loaded(overview_tab_); mark_tab_loaded(analysis_tab_); });
    connect(&svc, &services::equity::EquityResearchService::historical_loaded, this,
            [this]() { mark_tab_loaded(overview_tab_); mark_tab_loaded(analysis_tab_); });
    connect(&svc, &services::equity::EquityResearchService::financials_loaded, this,
            [this]() { mark_tab_loaded(financials_tab_); });
    connect(&svc, &services::equity::EquityResearchService::technicals_loaded, this,
            [this]() { mark_tab_loaded(technicals_tab_); });
    connect(&svc, &services::equity::EquityResearchService::peers_loaded, this,
            [this]() { mark_tab_loaded(peers_tab_); });
    connect(&svc, &services::equity::EquityResearchService::news_loaded, this,
            [this]() { mark_tab_loaded(news_tab_); });

    // 1-Hz ticker keeps the relative time ("30s ago") readable as it ages.
    freshness_ticker_ = new QTimer(this);
    freshness_ticker_->setInterval(1000);
    connect(freshness_ticker_, &QTimer::timeout, this, &EquityResearchScreen::update_freshness_chip);
    freshness_ticker_->start();

    // Re-evaluate the market-status badge every 30s. Cheap (a few QDateTime
    // ops + a string compare) and ensures the badge flips at the open/close
    // bell without the user needing to click anything.
    mkt_status_timer_ = new QTimer(this);
    mkt_status_timer_->setInterval(30 * 1000);
    connect(mkt_status_timer_, &QTimer::timeout, this,
            &EquityResearchScreen::update_market_status_badge);
    mkt_status_timer_->start();

    // Finnhub live-tick stream — pushes price updates in <200ms instead of
    // the 20s REST poll. Free tier covers US equities; non-US tickers
    // subscribe successfully but never emit (silent fallback to REST).
    // Ticks are throttled to ~3 Hz here: liquid US names print dozens of
    // trades per second and a label setText per tick is wasteful — the
    // user can't read updates faster than the eye refresh anyway.
    connect(&services::finnhub::FinnhubLiveTicks::instance(),
            &services::finnhub::FinnhubLiveTicks::tick, this,
            [this](QString sym, double price, qint64 /*ts_ms*/, qint64 /*size*/) {
                if (sym != current_symbol_) return;
                if (!price_label_ || !change_label_) return;
                // Throttle: skip if a tick landed in the last 300ms.
                static qint64 s_last_paint_ms = 0;
                const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
                if (now_ms - s_last_paint_ms < 300) return;
                s_last_paint_ms = now_ms;

                const QString cs = EquityOverviewTab::currency_symbol(
                    current_currency_.isEmpty() ? "USD" : current_currency_);
                price_label_->setText(QString("%1%2").arg(cs).arg(price, 0, 'f', 2));
                // Deliberately does NOT touch the freshness chip: a live
                // price tick updates this one label, not the panel's OHLC,
                // fundamentals or chart. Stamping the chip here told the
                // user the whole tab was current when only the price was.
            });

    // ── Keyboard shortcuts ──────────────────────────────────────────────────
    // Pro-tool muscle memory: hand on the keyboard, eyes on the chart.
    // Context-scoped to ER's widget tree so they don't fire when other
    // screens hold focus. Tooltip-visible bindings:
    //   1..5         → period 1mo / 3mo / 6mo / 1y / 5y
    //   L            → log-scale toggle
    //   V            → volume strip toggle
    //   S            → SMA50 toggle (most-used moving average)
    //   E            → earnings markers toggle
    //   C            → open comparison overlay dialog
    //   D            → open custom date-range picker
    //   /            → focus the inline search input
    //   Esc          → dismiss search popup
    auto bind = [this](const QString& key, std::function<void()> action) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, std::move(action));
    };
    auto bind_period = [this](const QString& key, int slot) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this,
                [this, slot]() { if (overview_tab_) overview_tab_->shortcut_set_period(slot); });
    };
    bind_period("1", 1);
    bind_period("2", 2);
    bind_period("3", 3);
    bind_period("4", 4);
    bind_period("5", 5);
    bind("L", [this]() { if (overview_tab_) overview_tab_->shortcut_toggle_log(); });
    bind("V", [this]() { if (overview_tab_) overview_tab_->shortcut_toggle_volume(); });
    bind("S", [this]() { if (overview_tab_) overview_tab_->shortcut_toggle_sma50(); });
    bind("E", [this]() { if (overview_tab_) overview_tab_->shortcut_toggle_earnings(); });
    bind("C", [this]() { if (overview_tab_) overview_tab_->shortcut_open_comparison(); });
    bind("D", [this]() { if (overview_tab_) overview_tab_->shortcut_open_range_picker(); });
    bind("/", [this]() {
        if (inline_search_input_) {
            inline_search_input_->setFocus();
            inline_search_input_->selectAll();
        }
    });
    bind("Escape", [this]() {
        if (inline_search_popup_ && inline_search_popup_->isVisible())
            inline_search_popup_->hide();
        else if (inline_search_input_ && inline_search_input_->hasFocus())
            inline_search_input_->clear();
    });

    // Listen for navigation from CommandBar asset search
    EventBus::instance().subscribe("equity_research.load_symbol", [this](const QVariantMap& payload) {
        const QString symbol = payload.value("symbol").toString();
        if (!symbol.isEmpty())
            load_symbol(symbol);
    });

    // Default symbol
    load_symbol("AAPL");

    // Drop any dragged symbol onto the screen to jump the research view
    // to that ticker. Uses the existing load_symbol path so caching,
    // signals, and the EventBus publish stay consistent.
    symbol_dnd::installDropFilter(this, [this](const SymbolRef& ref, SymbolGroup) {
        if (ref.is_valid())
            load_symbol(ref.symbol);
    });
}

void EquityResearchScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refresh_timer_->start();
    // Force-fresh on tab activation: invalidate the quote cache and re-fetch
    // just the quote. Info/historical stay warm — they don't move fast enough
    // to justify a refetch on every screen activation. The user can force a
    // full reload by re-submitting the symbol in the search bar.
    if (!current_symbol_.isEmpty()) {
        services::MarketDataService::instance().invalidate_quotes({current_symbol_});
        services::equity::EquityResearchService::instance().fetch_quote(current_symbol_);
    }
}

void EquityResearchScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    refresh_timer_->stop();
}

// ── UI construction ───────────────────────────────────────────────────────────
void EquityResearchScreen::build_ui() {
    setStyleSheet(QString("background:%1; color:%2;").arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    vl->addWidget(build_title_bar());
    vl->addWidget(build_quote_bar());

    // ── Tabs ─────────────────────────────────────────────────────────────────
    tab_widget_ = new QTabWidget;
    tab_widget_->setDocumentMode(true);
    tab_widget_->setStyleSheet(QString(R"(
        QTabWidget::pane { border:0; background:%1; }
        QTabBar::tab {
            background:%2; color:%3; padding:8px 18px;
            border:0; border-bottom:2px solid transparent;
            font-size:12px; letter-spacing:1px;
        }
        QTabBar::tab:selected { color:%4; border-bottom:2px solid %4; }
        QTabBar::tab:hover { color:%5; }
    )")
                                   .arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(),
                                        ui::colors::AMBER(), ui::colors::TEXT_PRIMARY()));

    overview_tab_ = new EquityOverviewTab;
    financials_tab_ = new EquityFinancialsTab;
    analysis_tab_ = new EquityAnalysisTab;
    earnings_tab_ = new EquityEarningsTab;
    ai_tab_ = new EquityAiTab;
    technicals_tab_ = new EquityTechnicalsTab;
    talipp_tab_ = new EquityTalippTab;
    peers_tab_ = new EquityPeersTab;
    news_tab_ = new EquityNewsTab;
    sentiment_tab_ = new EquitySentimentTab;
    // Relationships tab — reuses the full RelationshipMapScreen widget in
    // embedded mode (header/status bar hidden, since ER's title bar already
    // exposes the symbol + freshness chip). The screen still owns its filter
    // panel, detail panel, layout selector, and the inline ANALYZE search,
    // which remains a useful per-tab override when the user wants to inspect
    // a related entity without leaving ER.
    relationships_tab_ = new RelationshipMapScreen;
    relationships_tab_->set_embedded_mode(true);

    tab_widget_->addTab(overview_tab_, "Overview");
    tab_widget_->addTab(financials_tab_, "Financials");
    tab_widget_->addTab(analysis_tab_, "Analysis");
    // Earnings sits directly after Analysis: it is the same "what is this
    // company worth" question narrowed to the next print, and the two are
    // read together.
    tab_widget_->addTab(earnings_tab_, "Earnings");
    tab_widget_->addTab(ai_tab_, "AI Forecast");
    tab_widget_->addTab(technicals_tab_, "Technicals");
    tab_widget_->addTab(talipp_tab_, "TALIpp");
    tab_widget_->addTab(peers_tab_, "Peers");
    tab_widget_->addTab(news_tab_, "News");
    tab_widget_->addTab(sentiment_tab_, "Sentiment");
    tab_widget_->addTab(relationships_tab_, "Relationships");

    connect(tab_widget_, &QTabWidget::currentChanged, this, &EquityResearchScreen::on_tab_changed);

    // Peers ✓ checkbox ↔ comparison overlay (two-way binding). The Peers
    // tab is a discovery surface; the canonical comparison state lives in
    // EquityOverviewTab::comp_state_. Tick → add_comparison; untick →
    // remove_comparison. The overview tab broadcasts comparisons_changed
    // on every mutation (including chip-✕ removals and the implicit clear
    // on primary-symbol switch) so the Peers checkbox column stays in sync
    // even when the user mutates the set elsewhere.
    connect(peers_tab_, &EquityPeersTab::add_to_comparison_requested,
            overview_tab_, &EquityOverviewTab::add_comparison);
    connect(peers_tab_, &EquityPeersTab::remove_from_comparison_requested,
            overview_tab_, &EquityOverviewTab::remove_comparison);
    connect(overview_tab_, &EquityOverviewTab::comparisons_changed,
            peers_tab_, &EquityPeersTab::set_active_comparisons);

    vl->addWidget(tab_widget_, 1);
}

QWidget* EquityResearchScreen::build_title_bar() {
    auto* container = new QWidget(this);
    container->setFixedHeight(48);
    container->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(container);
    hl->setContentsMargins(16, 8, 16, 8);
    hl->setSpacing(12);

    auto* title = new QLabel("EQUITY RESEARCH");
    title->setStyleSheet(
        QString("color:%1; font-size:14px; font-weight:700; letter-spacing:2px;").arg(ui::colors::AMBER()));
    hl->addWidget(title);

    symbol_label_ = new QLabel;
    symbol_label_->setStyleSheet(QString("color:%1; font-size:14px; font-weight:600;").arg(ui::colors::TEXT_PRIMARY()));
    symbol_label_->setCursor(Qt::OpenHandCursor);
    symbol_label_->setToolTip("Drag to broadcast this symbol to any panel");
    // Drag-out: pull the current symbol from the live member so the filter
    // always ships the most recent ticker, not whatever was loaded at build.
    symbol_dnd::installDragSource(
        symbol_label_,
        [this]() { return current_symbol(); },
        link_group_);
    hl->addWidget(symbol_label_);

    // Visual breathing room before the inline search input.
    hl->addSpacing(24);

    // Inline symbol search with autocomplete dropdown — mirrors the top
    // CommandBar's /stock picker. Three input paths all converge on
    // load_symbol(sym):
    //   • Type → debounced search → click a result in the popup.
    //   • Type → arrow keys to highlight in popup → Enter.
    //   • Type a literal symbol (no popup interaction) → Enter.
    //
    // The hint label after this field still advertises the global command
    // bar's broader /fund, /index, … syntax. If the user types one of
    // those prefixes here we strip it transparently.
    inline_search_input_ = new QLineEdit;
    inline_search_input_->setPlaceholderText("Search symbol — type and Enter");
    inline_search_input_->setFixedWidth(220);
    inline_search_input_->setClearButtonEnabled(true);
    inline_search_input_->setStyleSheet(QString(
        "QLineEdit { background:%1; color:%2; border:1px solid %3;"
        " border-radius:2px; padding:3px 8px; font-size:12px;"
        " font-family:'Consolas',monospace; }"
        "QLineEdit:focus { border-color:%4; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    hl->addWidget(inline_search_input_);

    // Popup dropdown — borderless top-level mirroring CommandBar's working
    // pattern (Qt::ToolTip + WA_ShowWithoutActivating). Notes from that
    // implementation that we deliberately match here:
    //   - DO NOT add Qt::Popup: grabs keyboard, blocks typing.
    //   - DO NOT add Qt::WindowDoesNotAcceptFocus: on X11 some WMs treat
    //     that as "deactivate parent window," which made the QLineEdit
    //     stop rendering typed characters until refocused.
    //   - DO NOT use Qt::Tool: X11 can place it centre-screen on first
    //     map before our setGeometry() runs.
    inline_search_popup_ = new QListWidget(this);
    inline_search_popup_->setWindowFlags(
        Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    inline_search_popup_->setAttribute(Qt::WA_ShowWithoutActivating);
    inline_search_popup_->setUniformItemSizes(true);
    inline_search_popup_->setFocusPolicy(Qt::NoFocus);
    inline_search_popup_->setMouseTracking(true);
    inline_search_popup_->setMaximumHeight(280);
    inline_search_popup_->setStyleSheet(QString(
        "QListWidget { background:%1; color:%2; border:1px solid %3;"
        " font-family:'Consolas',monospace; font-size:12px;"
        " outline:0; padding:2px; }"
        "QListWidget::item { padding:4px 8px; }"
        "QListWidget::item:selected { background:%4; color:%5; }"
        "QListWidget::item:hover { background:%6; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
             ui::colors::BORDER_MED(), ui::colors::AMBER_DIM(),
             ui::colors::AMBER(), ui::colors::BG_RAISED()));
    inline_search_popup_->hide();
    inline_search_popup_->setItemDelegate(new SearchResultDelegate(inline_search_popup_));

    // Debounced search on text change.
    connect(inline_search_input_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
        const QString stripped = strip_search_prefix(text).trimmed();
        if (stripped.isEmpty()) {
            inline_search_popup_->hide();
            return;
        }
        services::equity::EquityResearchService::instance().schedule_search(stripped);
    });

    // search_results_loaded broadcasts to anyone subscribed; we only
    // populate (and show) the popup if our input currently has focus —
    // otherwise the top CommandBar would also show a phantom dropdown
    // every time user types in the global bar.
    auto& svc = services::equity::EquityResearchService::instance();
    connect(&svc, &services::equity::EquityResearchService::search_results_loaded, this,
            [this](QVector<services::equity::SearchResult> results) {
        if (!inline_search_input_ || !inline_search_input_->hasFocus()) return;
        inline_search_popup_->clear();
        for (const auto& r : results) {
            // The delegate paints from the structured roles — no display
            // text is set on the item itself. A11y / fallback readers fall
            // back to the (empty) display string, which we accept for now;
            // a richer accessibleText is a future polish.
            auto* it = new QListWidgetItem;
            it->setData(kRoleSymbol,   r.symbol);
            it->setData(kRoleName,     r.name);
            it->setData(kRoleExchange, r.exchange);
            it->setData(kRoleCurrency, r.currency);
            it->setData(kRoleType,     r.type);
            inline_search_popup_->addItem(it);
        }
        if (inline_search_popup_->count() == 0) {
            inline_search_popup_->hide();
            return;
        }
        inline_search_popup_->setCurrentRow(0);
        position_inline_search_popup();
        inline_search_popup_->show();
        inline_search_popup_->raise();
    });

    // Click a suggestion → load it.
    connect(inline_search_popup_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) {
        if (!it) return;
        const QString sym = it->data(Qt::UserRole).toString();
        if (!sym.isEmpty()) load_symbol(sym);
        inline_search_input_->clear();
        inline_search_popup_->hide();
    });
    connect(inline_search_popup_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) {
        if (!it) return;
        const QString sym = it->data(Qt::UserRole).toString();
        if (!sym.isEmpty()) load_symbol(sym);
        inline_search_input_->clear();
        inline_search_popup_->hide();
    });

    // Enter on the input: prefer current popup selection, else fall back
    // to literal typed symbol (with /prefix stripped).
    connect(inline_search_input_, &QLineEdit::returnPressed, this, [this]() {
        if (inline_search_popup_->isVisible() && inline_search_popup_->currentItem()) {
            const QString sym = inline_search_popup_->currentItem()->data(Qt::UserRole).toString();
            if (!sym.isEmpty()) {
                load_symbol(sym);
                inline_search_input_->clear();
                inline_search_popup_->hide();
                return;
            }
        }
        const QString sym = strip_search_prefix(inline_search_input_->text()).trimmed().toUpper();
        if (sym.isEmpty()) return;
        load_symbol(sym);
        inline_search_input_->clear();
        inline_search_popup_->hide();
    });

    // Up/Down keys navigate the popup; Escape closes it.
    inline_search_input_->installEventFilter(this);

    // Dismiss the popup when focus leaves the input → popup region.
    // Application-level focusChanged is more reliable than a per-widget
    // FocusOut + timer hack, especially on X11 where transient focus
    // can land on the popup itself when it first maps. (Same approach
    // CommandBar uses for its top-level dropdown.)
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (!inline_search_popup_ || !inline_search_input_) return;
        if (!now) return;  // spurious during popup map
        const bool inside = (now == inline_search_input_ ||
                             now == inline_search_popup_ ||
                             inline_search_popup_->isAncestorOf(now));
        if (!inside)
            inline_search_popup_->hide();
    });

    hl->addStretch();

    auto* hint = new QLabel("Use /stock, /fund, /index... in command bar to search");
    hint->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(hint);

    return container;
}

QWidget* EquityResearchScreen::build_quote_bar() {
    auto* bar = new QFrame;
    bar->setFixedHeight(44);
    bar->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);
    hl->setSpacing(24);

    auto make_label = [&](const QString& txt, const QString& color = "") -> QLabel* {
        auto* l = new QLabel(txt);
        QString style = "font-size:13px; font-weight:600;";
        if (!color.isEmpty())
            style += "color:" + color + ";";
        else
            style += "color:" + QString(ui::colors::TEXT_SECONDARY()) + ";";
        l->setStyleSheet(style);
        hl->addWidget(l);
        return l;
    };

    sym_label_ = make_label("—", ui::colors::AMBER());

    // Market-status badge. Hidden until on_info_loaded fills in the
    // exchange code — without that we don't know which trading calendar
    // applies, so showing a default would be a lie. Re-positioning style
    // is applied in update_market_status_badge.
    mkt_status_label_ = new QLabel("");
    mkt_status_label_->setVisible(false);
    mkt_status_label_->setToolTip("Market session status for this listing's exchange");
    hl->addWidget(mkt_status_label_);

    price_label_ = make_label("—", ui::colors::TEXT_PRIMARY());
    change_label_ = make_label("—");
    vol_label_ = make_label("VOL: —");
    hl_label_ = make_label("H/L: —");
    // Populated from StockInfo in on_info_loaded. It previously had no
    // writer at all and rendered a permanent em-dash, which reads as "this
    // company has no market cap" rather than "nothing filled this in".
    mktcap_label_ = make_label("MKT CAP: —");
    rec_label_ = make_label("—");
    hl->addStretch();

    // Right-aligned freshness chip — shows when the active tab's data
    // last arrived. Empty until the first signal lands.
    freshness_label_ = new QLabel("");
    freshness_label_->setStyleSheet(
        QString("font-size:12px; font-weight:500; color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    freshness_label_->setToolTip("When this tab's data was last refreshed");
    hl->addWidget(freshness_label_);

    return bar;
}

// ── Slots ─────────────────────────────────────────────────────────────────────
void EquityResearchScreen::on_quote_loaded(services::equity::QuoteData q) {
    update_quote_bar(q);
}

namespace {
// Compact market-cap text for the quote bar. Local rather than widening
// EquityOverviewTab's private API for a single call site.
QString format_market_cap(double v) {
    const double a = std::abs(v);
    if (a >= 1e12) return QStringLiteral("%1T").arg(v / 1e12, 0, 'f', 2);
    if (a >= 1e9)  return QStringLiteral("%1B").arg(v / 1e9,  0, 'f', 2);
    if (a >= 1e6)  return QStringLiteral("%1M").arg(v / 1e6,  0, 'f', 2);
    return QString::number(v, 'f', 0);
}
} // namespace

void EquityResearchScreen::on_info_loaded(services::equity::StockInfo info) {
    if (info.symbol != current_symbol_)
        return;
    current_currency_ = info.currency;
    current_exchange_ = info.exchange;
    update_market_status_badge();
    if (mktcap_label_) {
        // The label had no writer at all until now — a permanent em-dash
        // reads as "this company has no market cap", not "nothing filled
        // this in". Absent data still renders the dash, but only when it is
        // genuinely absent.
        mktcap_label_->setText(info.market_cap > 0
                                   ? QStringLiteral("MKT CAP: ") + format_market_cap(info.market_cap)
                                   : QStringLiteral("MKT CAP: \u2014"));
    }
    if (ai_tab_) ai_tab_->set_info(info);   // fundamentals enrich the AI prompt
}

void EquityResearchScreen::on_tab_changed(int index) {
    ScreenStateManager::instance().notify_changed(this);
    update_freshness_chip();
    if (current_symbol_.isEmpty())
        return;

    auto& svc = services::equity::EquityResearchService::instance();

    // Dispatch on the widget, not the index: the tab order has changed twice
    // (AI Forecast, then Earnings) and each time a positional switch silently
    // pointed at the wrong tab.
    QWidget* tab = tab_widget_ ? tab_widget_->widget(index) : nullptr;
    if (!tab)
        return;

    if (tab == financials_tab_) {
        financials_tab_->set_symbol(current_symbol_);
        svc.fetch_financials(current_symbol_);
    } else if (tab == analysis_tab_) {
        analysis_tab_->set_symbol(current_symbol_);
        // Re-trigger load_symbol — cache will re-emit info_loaded immediately
        svc.load_symbol(current_symbol_);
    } else if (tab == earnings_tab_) {
        // Earnings owns its own QueryStore subscription; set_symbol is
        // idempotent per ticker so re-activating won't re-fetch.
        earnings_tab_->set_symbol(current_symbol_);
    } else if (tab == ai_tab_) {
        // AI Forecast — set_symbol() subscribes to the candle stream itself;
        // load_symbol() re-emits info_loaded so the prompt gets fundamentals.
        ai_tab_->set_symbol(current_symbol_);
        svc.load_symbol(current_symbol_);
    } else if (tab == technicals_tab_) {
        technicals_tab_->set_symbol(current_symbol_);
        // The tab knows which (period, interval) it is showing; refreshing the
        // daily default here would warm an entry nobody is looking at.
        technicals_tab_->refresh();
    } else if (tab == talipp_tab_) {
        talipp_tab_->set_symbol(current_symbol_);
    } else if (tab == peers_tab_) {
        peers_tab_->set_symbol(current_symbol_);
    } else if (tab == news_tab_) {
        news_tab_->set_symbol(current_symbol_);
        svc.fetch_news(current_symbol_);
    } else if (tab == sentiment_tab_) {
        sentiment_tab_->set_symbol(current_symbol_);
    } else if (tab == relationships_tab_) {
        // Relationships tab — lazy fetch on activation only. The
        // RelationshipMapService::fetch call is multi-second and can spawn
        // a Python subprocess, so we don't kick it from load_symbol().
        // set_symbol() inside RelationshipMapScreen is idempotent for the
        // same ticker, so re-activating the tab won't re-fire the request.
        relationships_tab_->set_symbol(current_symbol_);
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void EquityResearchScreen::load_symbol(const QString& symbol_in, bool force) {
    // Defensive sanitize: if a caller passes "/stock AAPL" or similar
    // (CommandBar fallback path, EventBus payload from elsewhere, inline
    // search), strip the slash-command prefix and uppercase. Without this
    // the title-bar label ends up showing literal "/STOCK AAPL".
    QString symbol = symbol_in.trimmed();
    if (symbol.startsWith('/')) {
        const int sp = symbol.indexOf(QChar(' '));
        symbol = (sp >= 0) ? symbol.mid(sp + 1).trimmed() : QString();
    }
    symbol = symbol.toUpper();
    if (symbol.isEmpty())
        return;
    if (!force && symbol == current_symbol_)
        return;
    current_symbol_ = symbol;
    // Make the AI chat aware of what the user is looking at — only while this
    // screen is on-screen, so a construction-time / default load doesn't claim
    // the user is "viewing" a symbol they never opened (ambient context).
    if (isVisible())
        services::AppContextService::instance().set_current_symbol(symbol);

    // Update title bar to the new symbol immediately.
    symbol_label_->setText(symbol);
    sym_label_->setText(symbol);

    // Clear quote-bar values so the header doesn't show the previous ticker's
    // price next to the new ticker's name during the cold-load window.
    // on_quote_loaded fills these in when the new symbol's data arrives.
    clear_quote_bar();

    // Overview tab owns its own historical fetch (with the period it has
    // selected) — see EquityOverviewTab::set_symbol. The screen no longer
    // calls svc.load_symbol() here; the overview tab triggers the full
    // quote+info+historical bundle itself, and signals fan out to the rest.
    overview_tab_->set_symbol(symbol, force);

    // Forward to every tab so each filters its own signals correctly when
    // the responses land. Tabs whose data hasn't been fetched yet just sit
    // empty until the prefetch below resolves.
    if (financials_tab_) financials_tab_->set_symbol(symbol);
    if (analysis_tab_) analysis_tab_->set_symbol(symbol);
    if (technicals_tab_) technicals_tab_->set_symbol(symbol);
    if (talipp_tab_) talipp_tab_->set_symbol(symbol);
    if (peers_tab_) peers_tab_->set_symbol(symbol);
    if (news_tab_) news_tab_->set_symbol(symbol);
    if (sentiment_tab_) sentiment_tab_->set_symbol(symbol);
    // Earnings: forward only when it's the visible tab. The fetch pulls three
    // years of daily bars plus four estimate frames — too heavy to fire for
    // every ticker the user flips past. on_tab_changed picks it up on
    // activation, and set_symbol is idempotent per ticker.
    if (earnings_tab_ && tab_widget_ && tab_widget_->currentWidget() == earnings_tab_)
        earnings_tab_->set_symbol(symbol);
    // AI Forecast + Relationships tabs: forward only if currently visible.
    // Both are heavy on symbol-change — AI subscribes to candles and may fire
    // an auto-forecast (an LLM call); Relationships spawns a Python graph fetch.
    // We don't want either running in the background for every symbol the user
    // flips through; the tab_changed handler picks them up on activation.
    QWidget* cur = tab_widget_ ? tab_widget_->currentWidget() : nullptr;
    if (ai_tab_ && cur == ai_tab_)
        ai_tab_->set_symbol(symbol);
    if (relationships_tab_ && cur == relationships_tab_)
        relationships_tab_->set_symbol(symbol);

    // Prefetch for inactive tabs so whichever tab the user opens next is
    // already populated (or rendering from cache). Overview's own bundle
    // (quote+info+historical) is fired by overview_tab_->set_symbol() above.
    auto& svc = services::equity::EquityResearchService::instance();
    svc.fetch_financials(symbol);
    svc.fetch_technicals(symbol);
    svc.fetch_news(symbol);
    // peers + sentiment have their own fetch APIs — invoked by the tabs on
    // first show. Skipping eager prefetch since they don't lock the UI.

    // Analyst recommendation-trend chip — Finnhub-sourced. Subscribing here
    // (rather than waiting for tab activation) keeps the chip visible the
    // moment the user lands on any tab. No-op when no FINNHUB_API_KEY is
    // configured: subscribe_recommendations resolves an empty vector.
    refresh_recommendation_chip();

    // Switch the live-tick websocket subscription to the new symbol. The
    // service no-ops when no Finnhub key is set, so we don't need to gate
    // the call here.
    services::finnhub::FinnhubLiveTicks::instance().subscribe(symbol);

    // Publish to linked group so Watchlist/EquityTrading/News/etc. follow.
    if (link_group_ != SymbolGroup::None) {
        SymbolContext::instance().set_group_symbol(
            link_group_, SymbolRef::equity(symbol), this);
    }
}

void EquityResearchScreen::clear_quote_bar() {
    if (price_label_) price_label_->setText("\xe2\x80\x94");
    if (change_label_) {
        change_label_->setText("\xe2\x80\x94");
        change_label_->setStyleSheet(
            QString("font-size:13px; font-weight:600; color:%1;").arg(ui::colors::TEXT_SECONDARY()));
    }
    if (vol_label_) vol_label_->setText("VOL: \xe2\x80\x94");
    if (hl_label_) hl_label_->setText("H/L: \xe2\x80\x94");
    if (mktcap_label_) mktcap_label_->setText("MKT CAP: \xe2\x80\x94");
    if (rec_label_) rec_label_->setText("\xe2\x80\x94");
    // Drop the cached exchange — the new symbol may list elsewhere, and
    // showing the previous ticker's session status would be a lie. The
    // badge re-appears when on_info_loaded fires for the new symbol.
    current_exchange_.clear();
    if (mkt_status_label_) mkt_status_label_->setVisible(false);
}

// ── IGroupLinked ─────────────────────────────────────────────────────────────

void EquityResearchScreen::on_group_symbol_changed(const SymbolRef& ref) {
    if (ref.is_valid())
        load_symbol(ref.symbol);
}

SymbolRef EquityResearchScreen::current_symbol() const {
    if (current_symbol_.isEmpty())
        return {};
    return SymbolRef::equity(current_symbol_);
}

void EquityResearchScreen::update_quote_bar(const services::equity::QuoteData& q) {
    if (q.symbol != current_symbol_)
        return;

    const QString cs = EquityOverviewTab::currency_symbol(current_currency_.isEmpty() ? "USD" : current_currency_);

    sym_label_->setText(q.symbol);
    price_label_->setText(QString("%1%2").arg(cs).arg(q.price, 0, 'f', 2));

    bool up = q.change_pct >= 0;
    QString arrow = up ? "\xe2\x96\xb2" : "\xe2\x96\xbc";
    QString chg_color = up ? ui::colors::POSITIVE() : ui::colors::NEGATIVE();
    change_label_->setText(QString("%1%2  %3%4%")
                               .arg(up ? "+" : "")
                               .arg(q.change, 0, 'f', 2)
                               .arg(arrow)
                               .arg(qAbs(q.change_pct), 0, 'f', 2));
    change_label_->setStyleSheet(QString("font-size:13px; font-weight:600; color:%1;").arg(chg_color));

    auto fmt_vol = [](double v) -> QString {
        if (v >= 1e9)
            return QString("%1B").arg(v / 1e9, 0, 'f', 1);
        if (v >= 1e6)
            return QString("%1M").arg(v / 1e6, 0, 'f', 1);
        if (v >= 1e3)
            return QString("%1K").arg(v / 1e3, 0, 'f', 0);
        return QString::number(static_cast<qint64>(v));
    };

    vol_label_->setText("VOL: " + fmt_vol(q.volume));
    hl_label_->setText(QString("H:%1%2  L:%1%3").arg(cs).arg(q.high, 0, 'f', 2).arg(q.low, 0, 'f', 2));
}

// ── Market-status badge ──────────────────────────────────────────────────────
//
// Renders a small "OPEN" / "PRE" / "AFTER" / "CLOSED" chip next to the
// ticker. Driven by the listing's exchange code from StockInfo (yfinance
// returns Yahoo's short codes — NYQ, NMS, NGM, ASE, PCX, …) and the
// current wall-clock in the exchange's local timezone.
//
// Scope for Pass 2a: US equity venues + the major non-US ones we already
// surface elsewhere in the app (LSE, TSX, TYO, HKG, NSE/BSE, German /
// Euronext). Unrecognised codes show the raw venue code in a muted
// "no status" style — better than misleading the user with a guess.
// US exchange holidays, computed rather than tabulated so the app does not
// expire at the end of a hardcoded year.
//
// NYSE/NASDAQ observe: New Year's Day, MLK Day (3rd Mon Jan), Washington's
// Birthday (3rd Mon Feb), Good Friday, Memorial Day (last Mon May),
// Juneteenth, Independence Day, Labor Day (1st Mon Sep), Thanksgiving
// (4th Thu Nov), Christmas. Fixed-date holidays falling on a weekend are
// observed on the adjacent weekday.
static bool us_market_holiday(const QDate& d) {
    const int y = d.year();

    // Fixed-date holidays, shifted to the observed weekday.
    const auto observed = [](QDate fixed) {
        if (fixed.dayOfWeek() == 6) return fixed.addDays(-1); // Sat → Fri
        if (fixed.dayOfWeek() == 7) return fixed.addDays(1);  // Sun → Mon
        return fixed;
    };
    for (const QDate& fixed : {QDate(y, 1, 1), QDate(y, 6, 19), QDate(y, 7, 4), QDate(y, 12, 25)}) {
        if (d == observed(fixed))
            return true;
    }
    // A 1 January falling on a Saturday is observed on the previous 31 Dec.
    if (QDate(y + 1, 1, 1).dayOfWeek() == 6 && d == QDate(y, 12, 31))
        return true;

    // Nth weekday-of-month holidays.
    const auto nth_dow = [](int year, int month, int dow, int n) {
        QDate first(year, month, 1);
        int delta = (dow - first.dayOfWeek() + 7) % 7;
        return first.addDays(delta + 7 * (n - 1));
    };
    const auto last_dow = [](int year, int month, int dow) {
        QDate last(year, month, QDate(year, month, 1).daysInMonth());
        return last.addDays(-((last.dayOfWeek() - dow + 7) % 7));
    };
    if (d == nth_dow(y, 1, 1, 3))  return true;  // MLK Day
    if (d == nth_dow(y, 2, 1, 3))  return true;  // Washington's Birthday
    if (d == last_dow(y, 5, 1))    return true;  // Memorial Day
    if (d == nth_dow(y, 9, 1, 1))  return true;  // Labor Day
    if (d == nth_dow(y, 11, 4, 4)) return true;  // Thanksgiving

    // Good Friday — two days before Easter (anonymous Gregorian algorithm).
    const int a = y % 19, b = y / 100, c = y % 100;
    const int dd = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
    const int h = (19 * a + b - dd - g + 15) % 30;
    const int i = c / 4, k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31;
    const int day = ((h + l - 7 * m + 114) % 31) + 1;
    if (d == QDate(y, month, day).addDays(-2))
        return true;

    return false;
}

void EquityResearchScreen::update_market_status_badge() {
    if (!mkt_status_label_) return;
    if (current_exchange_.isEmpty()) {
        mkt_status_label_->setVisible(false);
        return;
    }

    // Trading-session profile for one venue. `tz` is an IANA zone the
    // QTimeZone constructor recognises; pre_h/regular_h/after_h are
    // (start_hour, end_hour) windows in that zone. A window of (-1, -1)
    // disables that phase (most non-US venues have no pre/after session
    // we track here).
    struct Session {
        QString tz;
        QPair<int, int> pre;      // hours-of-day (24h), inclusive start, exclusive end
        QPair<int, int> regular;
        QPair<int, int> after;
        // Half-hour offsets for venues like NSE (09:15–15:30 IST). Encoded
        // as start_minute / end_minute on top of the hour fields.
        int regular_start_min = 0;
        int regular_end_min   = 0;
    };

    // Map Yahoo exchange code → session. Codes sourced from yfinance .info
    // (`exchange`) and cross-checked against yahooquery's mapping table.
    // Single-source registry keeps the badge correct even if symbols move
    // across venues (e.g. NYSE → NYSE Arca delisting transfer).
    static const auto kSessions = [] {
        QHash<QString, Session> m;
        // PRE extends to 10 (not 9) so the 09:00–09:30 minutes don't fall
        // into a CLOSED gap before regular opens at 09:30. Check order in
        // the if/else below evaluates `regular` first, so 09:30+ correctly
        // shows OPEN — pre only wins for the 09:00–09:29 strip.
        const Session us  {"America/New_York", {4, 10}, {9, 16},  {16, 20}, 30, 0};
        for (const char* code : {"NYQ", "NMS", "NGM", "NCM", "ASE", "PCX",
                                  "BTS", "OEM", "OQB", "OQX", "PNK", "YHD"})
            m.insert(QString::fromLatin1(code), us);
        m.insert("LSE", {"Europe/London",     {-1, -1}, {8, 16}, {-1, -1}, 0, 30});
        m.insert("IOB", {"Europe/London",     {-1, -1}, {8, 16}, {-1, -1}, 0, 30});
        m.insert("TOR", {"America/Toronto",   {-1, -1}, {9, 16}, {-1, -1}, 30, 0});
        m.insert("TSX", {"America/Toronto",   {-1, -1}, {9, 16}, {-1, -1}, 30, 0});
        m.insert("VAN", {"America/Toronto",   {-1, -1}, {9, 16}, {-1, -1}, 30, 0});
        m.insert("JPX", {"Asia/Tokyo",        {-1, -1}, {9, 15}, {-1, -1}, 0, 0});
        m.insert("TYO", {"Asia/Tokyo",        {-1, -1}, {9, 15}, {-1, -1}, 0, 0});
        m.insert("HKG", {"Asia/Hong_Kong",    {-1, -1}, {9, 16}, {-1, -1}, 30, 0});
        m.insert("SHH", {"Asia/Shanghai",     {-1, -1}, {9, 15}, {-1, -1}, 30, 0});
        m.insert("SHZ", {"Asia/Shanghai",     {-1, -1}, {9, 15}, {-1, -1}, 30, 0});
        m.insert("NSI", {"Asia/Kolkata",      {-1, -1}, {9, 15}, {-1, -1}, 15, 30});
        m.insert("BSE", {"Asia/Kolkata",      {-1, -1}, {9, 15}, {-1, -1}, 15, 30});
        m.insert("GER", {"Europe/Berlin",     {-1, -1}, {9, 17}, {-1, -1}, 0, 30});
        m.insert("FRA", {"Europe/Berlin",     {-1, -1}, {9, 17}, {-1, -1}, 0, 30});
        m.insert("AMS", {"Europe/Amsterdam",  {-1, -1}, {9, 17}, {-1, -1}, 0, 30});
        m.insert("PAR", {"Europe/Paris",      {-1, -1}, {9, 17}, {-1, -1}, 0, 30});
        m.insert("BRU", {"Europe/Brussels",   {-1, -1}, {9, 17}, {-1, -1}, 0, 30});
        m.insert("ASX", {"Australia/Sydney",  {-1, -1}, {10, 16}, {-1, -1}, 0, 0});
        return m;
    }();

    const auto it = kSessions.find(current_exchange_);

    // Style helpers — match the rest of the quote bar's 13px weight.
    auto chip_ss = [](const QString& color, bool muted = false) {
        return QString("font-size:11px; font-weight:700; "
                       "background:rgba(0,0,0,80); border:1px solid %1; "
                       "color:%2; padding:1px 6px; border-radius:2px;")
            .arg(color, muted ? ui::colors::TEXT_SECONDARY() : color);
    };

    if (it == kSessions.end()) {
        // Unknown venue — show the raw code muted, no status guess.
        // Tooltip is repointed so the user doesn't misread the venue
        // code (e.g. "BVB", "JNB") as a session status.
        mkt_status_label_->setText(current_exchange_);
        mkt_status_label_->setStyleSheet(chip_ss(ui::colors::BORDER_MED(), true));
        mkt_status_label_->setToolTip(
            QString("Listed on %1 — no session calendar tracked for this venue.")
                .arg(current_exchange_));
        mkt_status_label_->setVisible(true);
        return;
    }

    // US exchange holidays ARE tracked (see us_market_holiday below) — the
    // badge previously said OPEN on 4 July and Thanksgiving, which is not a
    // scope limit so much as a wrong answer on the days it is most obviously
    // wrong. Non-US venues still have no holiday data, so their badge says
    // so rather than asserting a session it cannot verify.

    const Session& s = it.value();
    const bool is_us = (s.tz == QLatin1String("America/New_York"));
    const QTimeZone tz(s.tz.toUtf8());
    if (!tz.isValid()) {
        mkt_status_label_->setVisible(false);
        return;
    }
    const QDateTime local = QDateTime::currentDateTime().toTimeZone(tz);
    const int dow = local.date().dayOfWeek();   // 1=Mon … 7=Sun
    const bool holiday = is_us && us_market_holiday(local.date());
    const int mins_of_day = local.time().hour() * 60 + local.time().minute();

    auto in_window = [&](QPair<int, int> hours, int start_min, int end_min) {
        if (hours.first < 0) return false;
        const int from = hours.first * 60 + start_min;
        const int to   = hours.second * 60 + end_min;
        return mins_of_day >= from && mins_of_day < to;
    };

    QString status;
    QString color;
    if (dow >= 6 || holiday) {
        status = "CLOSED";
        color  = ui::colors::TEXT_SECONDARY();
    } else if (in_window(s.regular, s.regular_start_min, s.regular_end_min)) {
        status = "OPEN";
        color  = ui::colors::POSITIVE();
    } else if (in_window(s.pre, 0, 0)) {
        status = "PRE";
        color  = ui::colors::WARNING();
    } else if (in_window(s.after, 0, 0)) {
        status = "AFTER";
        color  = ui::colors::WARNING();
    } else {
        status = "CLOSED";
        color  = ui::colors::TEXT_SECONDARY();
    }

    mkt_status_label_->setText(status);
    mkt_status_label_->setStyleSheet(chip_ss(color, status == "CLOSED"));
    mkt_status_label_->setToolTip(
        QString("%1 · %2 (local %3)")
            .arg(current_exchange_, status, local.time().toString("HH:mm")));
    mkt_status_label_->setVisible(true);
}

// ── Recommendation-trend chip (Finnhub) ──────────────────────────────────────
//
// Renders a compact buy/hold/sell summary in rec_label_, with a direction
// arrow indicating whether sentiment is firming or weakening across the
// last two monthly buckets. Tooltip shows the full breakdown including
// strong-buy / strong-sell. No-op when no FINNHUB_API_KEY: the service
// resolves an empty vector, and we hide the chip until data arrives.
void EquityResearchScreen::refresh_recommendation_chip() {
    if (!rec_label_) return;
    if (!services::finnhub::FinnhubService::instance().has_api_key()) {
        rec_label_->setText("");
        rec_label_->setToolTip("Add a Finnhub API key in Settings → Credentials "
                               "to see analyst recommendations.");
        rec_label_->setVisible(false);
        return;
    }

    const QString sym = current_symbol_;
    rec_label_->setVisible(false);    // hide until callback lands
    services::finnhub::FinnhubService::instance().subscribe_recommendations(
        this, sym,
        [this, sym](const services::query::QueryStore::State& s) {
            // Late callback from a previous symbol — drop on the floor.
            if (sym != current_symbol_) return;
            if (!rec_label_) return;
            if (!s.data.isValid() || s.data.isNull()) {
                rec_label_->setVisible(false);
                return;
            }
            const auto rows =
                s.data.value<QVector<services::finnhub::FinnhubRecTrend>>();
            if (rows.isEmpty()) {
                rec_label_->setVisible(false);
                return;
            }

            // Finnhub returns periods newest-first; the first row is the most
            // recent month. Bucket strong_buy with buy, strong_sell with sell
            // for the compact display; the tooltip preserves the breakdown.
            const auto& latest = rows.first();
            const int b = latest.strong_buy + latest.buy;
            const int h = latest.hold;
            const int s_ = latest.sell + latest.strong_sell;
            const int total = b + h + s_;
            if (total <= 0) {
                rec_label_->setVisible(false);
                return;
            }

            // Direction arrow: compare buy% latest vs prior bucket. Only
            // meaningful with at least two periods.
            QString arrow = "\xe2\x80\xa2";   // bullet — no prior data
            QString arrow_color = ui::colors::TEXT_SECONDARY();
            if (rows.size() >= 2) {
                const auto& prior = rows.at(1);
                const int b_prev = prior.strong_buy + prior.buy;
                const int total_prev = b_prev + prior.hold
                                       + prior.sell + prior.strong_sell;
                if (total_prev > 0) {
                    const double pct_now  = double(b)      / total;
                    const double pct_prev = double(b_prev) / total_prev;
                    if (pct_now > pct_prev + 0.02) {
                        arrow = "\xe2\x96\xb2";          // ▲ improving
                        arrow_color = ui::colors::POSITIVE();
                    } else if (pct_now < pct_prev - 0.02) {
                        arrow = "\xe2\x96\xbc";          // ▼ weakening
                        arrow_color = ui::colors::NEGATIVE();
                    } else {
                        arrow = "\xe2\x80\xa2";          // • flat
                    }
                }
            }

            rec_label_->setText(
                QString("<span style='color:%5;'>%1</span> "
                        "<span style='color:%6;'>%2B</span>"
                        "<span style='color:%7;'>/%3H</span>"
                        "<span style='color:%8;'>/%4S</span>")
                    .arg(arrow).arg(b).arg(h).arg(s_)
                    .arg(arrow_color,
                         ui::colors::POSITIVE(),
                         ui::colors::TEXT_SECONDARY(),
                         ui::colors::NEGATIVE()));
            rec_label_->setStyleSheet("font-size:12px; font-weight:600;");
            rec_label_->setTextFormat(Qt::RichText);
            rec_label_->setToolTip(
                QString("Analyst recommendations (Finnhub, %1)\n"
                        "Strong Buy: %2\n"
                        "Buy: %3\n"
                        "Hold: %4\n"
                        "Sell: %5\n"
                        "Strong Sell: %6")
                    .arg(latest.period)
                    .arg(latest.strong_buy)
                    .arg(latest.buy)
                    .arg(latest.hold)
                    .arg(latest.sell)
                    .arg(latest.strong_sell));
            rec_label_->setVisible(true);
        });
}

QVariantMap EquityResearchScreen::save_state() const {
    // Persist the tab's name alongside its index: inserting a tab shifts every
    // index after it, so a saved layout would silently reopen on a different
    // tab after an upgrade. The index stays for backward compatibility with
    // states written before the name was recorded.
    const int idx = tab_widget_ ? tab_widget_->currentIndex() : 0;
    return {{"symbol", current_symbol_},
            {"tab_index", idx},
            {"tab_name", tab_widget_ ? tab_widget_->tabText(idx) : QString()}};
}

void EquityResearchScreen::restore_state(const QVariantMap& state) {
    const QString sym = state.value("symbol").toString();
    if (!sym.isEmpty()) {
        // Route through load_symbol() so the title/quote bar, overview tab,
        // and active sub-tab all update. Pre-mutating current_symbol_ here
        // would make load_symbol() no-op via its equality guard, leaving the
        // title bar stuck on whatever the ctor initialised it to.
        load_symbol(sym);
    }
    if (tab_widget_) {
        const QString name = state.value("tab_name").toString();
        int idx = -1;
        if (!name.isEmpty()) {
            for (int i = 0; i < tab_widget_->count(); ++i) {
                if (tab_widget_->tabText(i) == name) { idx = i; break; }
            }
        }
        if (idx < 0)
            idx = state.value("tab_index", 0).toInt();
        if (idx >= 0 && idx < tab_widget_->count())
            tab_widget_->setCurrentIndex(idx);
    }
}

// ── Freshness chip ───────────────────────────────────────────────────────────

void EquityResearchScreen::mark_tab_loaded(int tab_index) {
    tab_loaded_at_[tab_index] = QDateTime::currentDateTime();
    if (tab_widget_ && tab_widget_->currentIndex() == tab_index)
        update_freshness_chip();
}

void EquityResearchScreen::mark_tab_loaded(QWidget* tab) {
    if (!tab_widget_ || !tab)
        return;
    const int idx = tab_widget_->indexOf(tab);
    if (idx >= 0)
        mark_tab_loaded(idx);
}

void EquityResearchScreen::update_freshness_chip() {
    if (!freshness_label_ || !tab_widget_) return;
    const int idx = tab_widget_->currentIndex();

    // Prefer the tab's own record of when its data was FETCHED upstream.
    // tab_loaded_at_ records when a signal reached the UI, which a cache hit
    // satisfies instantly — so the chip used to read "just now" over numbers
    // that were hours old. Fall back to it only for tabs that cannot yet
    // report provenance.
    QDateTime t;
    if (overview_tab_ && tab_widget_->currentWidget() == overview_tab_)
        t = overview_tab_->data_as_of();
    if (technicals_tab_ && tab_widget_->currentWidget() == technicals_tab_)
        t = technicals_tab_->data_as_of();
    if (!t.isValid()) {
        const auto it = tab_loaded_at_.constFind(idx);
        if (it == tab_loaded_at_.constEnd()) {
            // No data has arrived for the active tab yet — keep chip blank
            // rather than show a misleading age.
            freshness_label_->setText("");
            return;
        }
        t = it.value();
    }
    const qint64 secs = t.secsTo(QDateTime::currentDateTime());
    QString rel;
    if (secs < 5)        rel = "just now";
    else if (secs < 60)  rel = QString("%1s ago").arg(secs);
    else if (secs < 3600) rel = QString("%1m ago").arg(secs / 60);
    else if (secs < 86400) rel = QString("%1h ago").arg(secs / 3600);
    else                  rel = QString("%1d ago").arg(secs / 86400);
    freshness_label_->setText(QString("as of %1 · %2").arg(t.toString("HH:mm:ss"), rel));
}

// ── Inline search helpers ────────────────────────────────────────────────────

QString EquityResearchScreen::strip_search_prefix(const QString& raw) const {
    // Strip a leading "/<word>" (e.g. "/stock", "/fund", "/index") plus the
    // following whitespace, so users who type the global command-bar syntax
    // here by reflex still get a clean symbol query.
    const QString s = raw.trimmed();
    if (!s.startsWith('/')) return s;
    const int sp = s.indexOf(QChar(' '));
    if (sp < 0) return QString();        // just "/stock" with nothing after
    return s.mid(sp + 1).trimmed();
}

void EquityResearchScreen::position_inline_search_popup() {
    if (!inline_search_input_ || !inline_search_popup_) return;
    const QPoint global_below =
        inline_search_input_->mapToGlobal(QPoint(0, inline_search_input_->height()));
    const int w = std::max(inline_search_input_->width(), 320);
    const int rows = inline_search_popup_->count();
    const int h = std::min(std::max(rows, 1) * 26 + 8, 280);
    // setGeometry is more reliable than move+resize on Linux X11 for
    // top-level windows (the WM occasionally ignores move()/resize()
    // pairs and keeps the previous geometry).
    inline_search_popup_->setGeometry(global_below.x(), global_below.y(), w, h);
}

bool EquityResearchScreen::eventFilter(QObject* watched, QEvent* event) {
    if (watched == inline_search_input_ && inline_search_popup_) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (inline_search_popup_->isVisible()) {
                if (ke->key() == Qt::Key_Down) {
                    const int next = std::min(inline_search_popup_->currentRow() + 1,
                                              inline_search_popup_->count() - 1);
                    inline_search_popup_->setCurrentRow(next);
                    return true;
                }
                if (ke->key() == Qt::Key_Up) {
                    const int prev = std::max(inline_search_popup_->currentRow() - 1, 0);
                    inline_search_popup_->setCurrentRow(prev);
                    return true;
                }
                if (ke->key() == Qt::Key_Escape) {
                    inline_search_popup_->hide();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace fincept::screens
