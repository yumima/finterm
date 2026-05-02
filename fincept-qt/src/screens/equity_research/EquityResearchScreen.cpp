// src/screens/equity_research/EquityResearchScreen.cpp
#include "screens/equity_research/EquityResearchScreen.h"

#include "core/events/EventBus.h"
#include "core/session/ScreenStateManager.h"
#include "core/symbol/SymbolContext.h"
#include "core/symbol/SymbolDragSource.h"
#include "screens/equity_research/EquityAnalysisTab.h"
#include "screens/equity_research/EquityFinancialsTab.h"
#include "screens/equity_research/EquityNewsTab.h"
#include "screens/equity_research/EquityOverviewTab.h"
#include "screens/equity_research/EquityPeersTab.h"
#include "screens/equity_research/EquitySentimentTab.h"
#include "screens/equity_research/EquityTalippTab.h"
#include "screens/equity_research/EquityTechnicalsTab.h"
#include "services/equity/EquityResearchService.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace fincept::screens {

EquityResearchScreen::EquityResearchScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    // Refresh timer — only started in showEvent
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(30 * 1000); // 30s quote refresh
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        if (!current_symbol_.isEmpty())
            services::equity::EquityResearchService::instance().load_symbol(current_symbol_);
    });

    // Wire service signals
    auto& svc = services::equity::EquityResearchService::instance();
    connect(&svc, &services::equity::EquityResearchService::quote_loaded, this, &EquityResearchScreen::on_quote_loaded);
    connect(&svc, &services::equity::EquityResearchService::info_loaded, this, &EquityResearchScreen::on_info_loaded);

    // ── Per-tab freshness tracking ──────────────────────────────────────────
    // Tab indices match the order tabs are added in build_ui:
    //   0 Overview, 1 Financials, 2 Analysis, 3 Technicals, 4 TALipp,
    //   5 Peers, 6 News, 7 Sentiment.
    // Overview/Analysis share the load_symbol triple (quote+info+history),
    // so any of those signals stamps both.
    // Trailing-argument-drop: Qt allows zero-arg lambda slots on signals
    // with any number of arguments, which sidesteps name churn in the
    // service's payload structs.
    connect(&svc, &services::equity::EquityResearchService::quote_loaded, this,
            [this]() { mark_tab_loaded(0); mark_tab_loaded(2); });
    connect(&svc, &services::equity::EquityResearchService::info_loaded, this,
            [this]() { mark_tab_loaded(0); mark_tab_loaded(2); });
    connect(&svc, &services::equity::EquityResearchService::historical_loaded, this,
            [this]() { mark_tab_loaded(0); mark_tab_loaded(2); });
    connect(&svc, &services::equity::EquityResearchService::financials_loaded, this,
            [this]() { mark_tab_loaded(1); });
    connect(&svc, &services::equity::EquityResearchService::technicals_loaded, this,
            [this]() { mark_tab_loaded(3); });
    connect(&svc, &services::equity::EquityResearchService::peers_loaded, this,
            [this]() { mark_tab_loaded(5); });
    connect(&svc, &services::equity::EquityResearchService::news_loaded, this,
            [this]() { mark_tab_loaded(6); });

    // 1-Hz ticker keeps the relative time ("30s ago") readable as it ages.
    freshness_ticker_ = new QTimer(this);
    freshness_ticker_->setInterval(1000);
    connect(freshness_ticker_, &QTimer::timeout, this, &EquityResearchScreen::update_freshness_chip);
    freshness_ticker_->start();

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
            font-size:12px; text-transform:uppercase; letter-spacing:1px;
        }
        QTabBar::tab:selected { color:%4; border-bottom:2px solid %4; }
        QTabBar::tab:hover { color:%5; }
    )")
                                   .arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(), ui::colors::TEXT_SECONDARY(),
                                        ui::colors::AMBER(), ui::colors::TEXT_PRIMARY()));

    overview_tab_ = new EquityOverviewTab;
    financials_tab_ = new EquityFinancialsTab;
    analysis_tab_ = new EquityAnalysisTab;
    technicals_tab_ = new EquityTechnicalsTab;
    talipp_tab_ = new EquityTalippTab;
    peers_tab_ = new EquityPeersTab;
    news_tab_ = new EquityNewsTab;
    sentiment_tab_ = new EquitySentimentTab;

    tab_widget_->addTab(overview_tab_, "Overview");
    tab_widget_->addTab(financials_tab_, "Financials");
    tab_widget_->addTab(analysis_tab_, "Analysis");
    tab_widget_->addTab(technicals_tab_, "Technicals");
    tab_widget_->addTab(talipp_tab_, "TALIpp");
    tab_widget_->addTab(peers_tab_, "Peers");
    tab_widget_->addTab(news_tab_, "News");
    tab_widget_->addTab(sentiment_tab_, "Sentiment");

    connect(tab_widget_, &QTabWidget::currentChanged, this, &EquityResearchScreen::on_tab_changed);

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
            QString line = r.symbol;
            if (!r.name.isEmpty())  line += "  " + r.name;
            if (!r.exchange.isEmpty()) line += "  · " + r.exchange;
            auto* it = new QListWidgetItem(line);
            it->setData(Qt::UserRole, r.symbol);
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
    hint->setStyleSheet(QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_TERTIARY()));
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
    price_label_ = make_label("—", ui::colors::TEXT_PRIMARY());
    change_label_ = make_label("—");
    vol_label_ = make_label("VOL: —");
    hl_label_ = make_label("H/L: —");
    mktcap_label_ = make_label("MKT CAP: —");
    rec_label_ = make_label("—");
    hl->addStretch();

    // Right-aligned freshness chip — shows when the active tab's data
    // last arrived. Empty until the first signal lands.
    freshness_label_ = new QLabel("");
    freshness_label_->setStyleSheet(
        QString("font-size:11px; font-weight:500; color:%1;").arg(ui::colors::TEXT_DIM()));
    freshness_label_->setToolTip("When this tab's data was last refreshed");
    hl->addWidget(freshness_label_);

    return bar;
}

// ── Slots ─────────────────────────────────────────────────────────────────────
void EquityResearchScreen::on_quote_loaded(services::equity::QuoteData q) {
    update_quote_bar(q);
}

void EquityResearchScreen::on_info_loaded(services::equity::StockInfo info) {
    if (info.symbol != current_symbol_)
        return;
    current_currency_ = info.currency;
}

void EquityResearchScreen::on_tab_changed(int index) {
    ScreenStateManager::instance().notify_changed(this);
    update_freshness_chip();
    if (current_symbol_.isEmpty())
        return;

    auto& svc = services::equity::EquityResearchService::instance();

    switch (index) {
        case 1:
            financials_tab_->set_symbol(current_symbol_);
            svc.fetch_financials(current_symbol_);
            break;
        case 2:
            analysis_tab_->set_symbol(current_symbol_);
            // Re-trigger load_symbol — cache will re-emit info_loaded immediately
            svc.load_symbol(current_symbol_);
            break;
        case 3:
            technicals_tab_->set_symbol(current_symbol_);
            svc.fetch_technicals(current_symbol_);
            break;
        case 4:
            talipp_tab_->set_symbol(current_symbol_);
            break;
        case 5:
            peers_tab_->set_symbol(current_symbol_);
            break;
        case 6:
            news_tab_->set_symbol(current_symbol_);
            svc.fetch_news(current_symbol_);
            break;
        case 7:
            sentiment_tab_->set_symbol(current_symbol_);
            break;
        default:
            break;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void EquityResearchScreen::load_symbol(const QString& symbol_in) {
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
    if (symbol.isEmpty() || symbol == current_symbol_)
        return;
    current_symbol_ = symbol;

    // Update title bar and quote bar
    symbol_label_->setText(symbol);
    sym_label_->setText(symbol);
    price_label_->setText("Loading\xe2\x80\xa6");

    // Overview always loads (tab 0 is default)
    overview_tab_->set_symbol(symbol);

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

    // Prefetch — fire every fetch in parallel so whichever tab the user
    // opens next is already populated (or rendering from a cache hit).
    // load_symbol() handles overview info + historical + quote.
    auto& svc = services::equity::EquityResearchService::instance();
    svc.load_symbol(symbol);
    svc.fetch_financials(symbol);
    svc.fetch_technicals(symbol);
    svc.fetch_news(symbol);
    // peers + sentiment have their own fetch APIs — invoked by the tabs on
    // first show. Skipping eager prefetch since they don't lock the UI.

    // Publish to linked group so Watchlist/EquityTrading/News/etc. follow.
    if (link_group_ != SymbolGroup::None) {
        SymbolContext::instance().set_group_symbol(
            link_group_, SymbolRef::equity(symbol), this);
    }
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

QVariantMap EquityResearchScreen::save_state() const {
    return {{"symbol", current_symbol_}, {"tab_index", tab_widget_ ? tab_widget_->currentIndex() : 0}};
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
        const int idx = state.value("tab_index", 0).toInt();
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

void EquityResearchScreen::update_freshness_chip() {
    if (!freshness_label_ || !tab_widget_) return;
    const int idx = tab_widget_->currentIndex();
    const auto it = tab_loaded_at_.constFind(idx);
    if (it == tab_loaded_at_.constEnd()) {
        // No data has arrived for the active tab yet — keep chip blank
        // rather than show a misleading age.
        freshness_label_->setText("");
        return;
    }
    const QDateTime& t = it.value();
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
