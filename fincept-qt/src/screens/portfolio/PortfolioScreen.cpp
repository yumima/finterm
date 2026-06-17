// src/screens/portfolio/PortfolioScreen.cpp
#include "screens/portfolio/PortfolioScreen.h"

#include "core/session/ScreenStateManager.h"
#include "core/symbol/SymbolContext.h"
#include "core/symbol/SymbolRef.h"
#include "services/app_context/AppContextService.h"
#include "screens/portfolio/PortfolioInsightsPanel.h"
#include "screens/portfolio/PortfolioBlotter.h"
#include "screens/portfolio/PortfolioCommandBar.h"
#include "screens/portfolio/PortfolioDetailWrapper.h"
#include "screens/portfolio/PortfolioDialogs.h"
#include "screens/portfolio/PortfolioFFNView.h"
#include "screens/portfolio/PortfolioHeatmap.h"
#include "screens/portfolio/PortfolioOrderPanel.h"
#include "screens/portfolio/PortfolioPerfChart.h"
#include "screens/portfolio/PortfolioSectorPanel.h"
#include "screens/portfolio/PortfolioStatsRibbon.h"
#include "screens/portfolio/PortfolioStatusBar.h"
#include "screens/portfolio/PortfolioTxnPanel.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "services/file_manager/FileManagerService.h"
#include "services/markets/MarketDataService.h"
#include "services/portfolio/PortfolioService.h"
#include "storage/repositories/SettingsRepository.h"
#include "ui/theme/Theme.h"

#include <QFileDialog>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace fincept::screens {

PortfolioScreen::PortfolioScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
    refresh_theme(); // Apply theme-aware font sizes and colors on first build

    // Connect to PortfolioService signals
    auto& svc = services::PortfolioService::instance();
    connect(&svc, &services::PortfolioService::portfolios_loaded, this, &PortfolioScreen::on_portfolios_loaded);
    connect(&svc, &services::PortfolioService::summary_loaded, this, &PortfolioScreen::on_summary_loaded);
    connect(&svc, &services::PortfolioService::summary_error, this, &PortfolioScreen::on_summary_error);
    connect(&svc, &services::PortfolioService::metrics_computed, this, &PortfolioScreen::on_metrics_computed);
    connect(&svc, &services::PortfolioService::portfolio_created, this, &PortfolioScreen::on_portfolio_created);
    connect(&svc, &services::PortfolioService::portfolio_deleted, this, &PortfolioScreen::on_portfolio_deleted);
    connect(&svc, &services::PortfolioService::asset_added, this, &PortfolioScreen::on_asset_changed);
    connect(&svc, &services::PortfolioService::asset_sold, this, &PortfolioScreen::on_asset_changed);
    connect(&svc, &services::PortfolioService::snapshots_loaded, this, &PortfolioScreen::on_snapshots_loaded);
    connect(&svc, &services::PortfolioService::transactions_loaded, this, [this](QVector<portfolio::Transaction> txns) {
        if (txn_panel_)
            txn_panel_->set_transactions(txns);
    });
    connect(&svc, &services::PortfolioService::correlation_computed, this, [this](QHash<QString, double> matrix) {
        if (sector_panel_)
            sector_panel_->set_correlation(matrix);
    });
    connect(&svc, &services::PortfolioService::portfolio_fundamentals_loaded, this,
            [this](QString portfolio_id, portfolio::PortfolioFundamentals f) {
                if (portfolio_id == selected_id_ && heatmap_)
                    heatmap_->set_portfolio_fundamentals(portfolio_id, f);
            });
    connect(&svc, &services::PortfolioService::spy_history_loaded, this,
            [this](QStringList /*dates*/, QVector<double> /*closes*/) {
                // Recompute metrics now that SPY data is available for OLS beta.
                // The chart consumes the per-symbol benchmark_history_loaded
                // signal below — SPY here is purely a Beta signal.
                if (summary_loaded_)
                    services::PortfolioService::instance().compute_metrics(current_summary_);
            });
    connect(&svc, &services::PortfolioService::benchmark_history_loaded, this,
            [this](QString symbol, QStringList dates, QVector<double> closes) {
                if (!perf_chart_)
                    return;
                // Per-symbol focus mode: the chart filters by its current
                // focus_symbol() so a stale fetch from a previous selection
                // is harmlessly dropped on the chart side.
                perf_chart_->set_focus_history(symbol, dates, closes);
                if (!summary_loaded_)
                    return;
                // Benchmark overlay path — only the portfolio's currency
                // benchmark (SPY for USD, ^GSPTSE for CAD, etc.) drives the
                // overlay label and currency-normalisation logic.
                const QString want = services::PortfolioService::default_benchmark_for_currency(
                    current_summary_.portfolio.currency);
                if (symbol != want)
                    return; // ignore secondary fetches (focus symbol, SPY-for-Beta)
                perf_chart_->set_benchmark_history(symbol, dates, closes);
            });
    connect(&svc, &services::PortfolioService::risk_free_rate_loaded, this, [this](double /*rate*/) {
        // Recompute metrics with updated risk-free rate for Sharpe
        if (summary_loaded_)
            services::PortfolioService::instance().compute_metrics(current_summary_);
    });
    // After yfinance backfill lands, refresh snapshots and metrics so Beta/MDD
    // populate without requiring a manual refresh.
    connect(&svc, &services::PortfolioService::history_backfilled, this,
            [this](QString portfolio_id, int point_count) {
                if (point_count <= 0 || !summary_loaded_ || portfolio_id != selected_id_)
                    return;
                services::PortfolioService::instance().load_snapshots(portfolio_id);
                services::PortfolioService::instance().compute_metrics(current_summary_);
            });

    // Restore persisted refresh interval (P17)
    {
        auto r = SettingsRepository::instance().get("portfolio.refresh_interval_ms");
        if (r.is_ok() && !r.value().isEmpty()) {
            bool ok = false;
            const int saved = r.value().toInt(&ok);
            if (ok && saved >= 10000) // sanity: minimum 10 s
                refresh_interval_ms_ = saved;
        }
    }

    // Refresh timer (P3: only set interval, don't start)
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(refresh_interval_ms_);
    // Timer ticks → background polling; don't bypass the cache (let TTL govern).
    connect(refresh_timer_, &QTimer::timeout, this,
            [this]() { request_refresh(/*force_fresh=*/false); });
    command_bar_->set_refresh_interval(refresh_interval_ms_);

    // DataHub debounce: collapses a burst of per-symbol quote arrivals
    // (one per held symbol when the DataHub scheduler ticks) into a single
    // aggregate rebuild + UI repaint pass.
    hub_refresh_timer_ = new QTimer(this);
    hub_refresh_timer_->setSingleShot(true);
    hub_refresh_timer_->setInterval(200);
    connect(hub_refresh_timer_, &QTimer::timeout, this,
            &PortfolioScreen::rebuild_summary_aggregates_and_refresh);

    // Load portfolios
    svc.load_portfolios();

    // Theme change: refresh all child component styles
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed, this,
            [this](const ui::ThemeTokens&) { refresh_theme(); });
}

void PortfolioScreen::build_ui() {
    setObjectName("portfolioScreen");
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Command bar
    command_bar_ = new PortfolioCommandBar(this);
    layout->addWidget(command_bar_);

    connect(command_bar_, &PortfolioCommandBar::portfolio_selected, this, &PortfolioScreen::on_portfolio_selected);
    connect(command_bar_, &PortfolioCommandBar::create_requested, this, &PortfolioScreen::on_create_requested);
    connect(command_bar_, &PortfolioCommandBar::delete_requested, this, &PortfolioScreen::on_delete_requested);
    // User clicked the refresh button → bypass cache so they always feel "live".
    connect(command_bar_, &PortfolioCommandBar::refresh_requested, this,
            [this]() { request_refresh(/*force_fresh=*/true); });
    connect(command_bar_, &PortfolioCommandBar::refresh_interval_changed, this,
            &PortfolioScreen::on_refresh_interval_changed);
    connect(command_bar_, &PortfolioCommandBar::detail_view_selected, this, &PortfolioScreen::on_detail_view_selected);
    connect(command_bar_, &PortfolioCommandBar::buy_requested, this, &PortfolioScreen::on_buy_requested);
    connect(command_bar_, &PortfolioCommandBar::sell_requested, this, &PortfolioScreen::on_sell_requested);
    connect(command_bar_, &PortfolioCommandBar::dividend_requested, this, [this]() {
        if (selected_id_.isEmpty() || current_summary_.holdings.isEmpty())
            return;
        QStringList syms;
        for (const auto& h : current_summary_.holdings)
            syms.append(h.symbol);
        AddDividendDialog dlg(syms, this);
        if (dlg.exec() == QDialog::Accepted) {
            const double qty = [&]() -> double {
                for (const auto& h : current_summary_.holdings)
                    if (h.symbol == dlg.symbol())
                        return h.quantity;
                return 0.0;
            }();
            const double total = qty * dlg.amount_per_share();
            services::PortfolioService::instance().record_dividend(
                selected_id_, dlg.symbol(), qty, dlg.amount_per_share(), total, dlg.date(), dlg.notes());
        }
    });
    connect(command_bar_, &PortfolioCommandBar::ffn_toggled, this, [this]() {
        show_ffn_ = !show_ffn_;
        if (show_ffn_) {
            active_detail_ = std::nullopt;
            command_bar_->set_detail_view(std::nullopt);
            ffn_view_->set_data(current_summary_, current_summary_.portfolio.currency);
        }
        command_bar_->set_ffn_active(show_ffn_);
        update_content_state();
    });

    // Stats ribbon
    stats_ribbon_ = new PortfolioStatsRibbon(this);
    layout->addWidget(stats_ribbon_);

    // Content stack
    content_stack_ = new QStackedWidget(this);
    layout->addWidget(content_stack_, 1);

    empty_state_ = build_empty_state();
    loading_state_ = build_loading_state();

    // Main view: heatmap (left) + center (chart/sector/blotter) + order panel (right)
    main_view_ = build_main_view();

    // Detail view wrapper (lazily constructed views inside)
    detail_wrapper_ = new PortfolioDetailWrapper(this);
    connect(detail_wrapper_, &PortfolioDetailWrapper::back_requested, this, [this]() {
        active_detail_ = std::nullopt;
        command_bar_->set_detail_view(std::nullopt);
        update_content_state();
    });
    // AnalyticsSectorsView → filter the main blotter by the clicked sector.
    // Mirrors the PortfolioSectorPanel::sector_selected wiring below so both
    // entry points behave identically.
    connect(detail_wrapper_, &PortfolioDetailWrapper::sector_selected, this, [this](const QString& sector) {
        if (!blotter_)
            return;
        if (sector.isEmpty()) {
            blotter_->set_sector_filter({});
            return;
        }
        QStringList matching;
        for (const auto& h : current_summary_.holdings) {
            QString h_sector = h.sector.isEmpty() ? QStringLiteral("Unclassified") : h.sector;
            if (h_sector == sector)
                matching.append(h.symbol);
        }
        blotter_->set_sector_filter(matching);
    });

    // FFN view
    ffn_view_ = new PortfolioFFNView(this);
    connect(ffn_view_, &PortfolioFFNView::back_requested, this, [this]() {
        show_ffn_ = false;
        command_bar_->set_ffn_active(false);
        update_content_state();
    });

    content_stack_->addWidget(empty_state_);    // index 0
    content_stack_->addWidget(loading_state_);  // index 1
    content_stack_->addWidget(main_view_);      // index 2
    content_stack_->addWidget(detail_wrapper_); // index 3
    content_stack_->addWidget(ffn_view_);       // index 4

    content_stack_->setCurrentIndex(0);

    // Status bar
    status_bar_ = new PortfolioStatusBar(this);
    layout->addWidget(status_bar_);

    // ── Insights dock (unified AI + Agent right-hand panel) ─────────────────
    // Sits above all other widgets as a child overlay, positioned in
    // resizeEvent so it tracks window size. A scrim behind it dims the rest
    // of the screen so the user knows focus has moved.
    insights_scrim_ = new QWidget(this);
    insights_scrim_->setObjectName("PortfolioInsightsScrim");
    insights_scrim_->setStyleSheet("#PortfolioInsightsScrim { background:rgba(0,0,0,0.45); }");
    insights_scrim_->hide();

    insights_panel_ = new PortfolioInsightsPanel(this);
    connect(insights_panel_, &PortfolioInsightsPanel::close_requested, this, [this]() {
        insights_scrim_->hide();
    });

    // Wire export/import/AI/Agent signals from CommandBar
    connect(command_bar_, &PortfolioCommandBar::export_csv_requested, this, [this]() {
        if (selected_id_.isEmpty())
            return;
        QString path = QFileDialog::getSaveFileName(this, "Export CSV", "portfolio.csv", "CSV Files (*.csv)");
        if (!path.isEmpty()) {
            services::PortfolioService::instance().export_csv(selected_id_, path);
            services::FileManagerService::instance().import_file(path, "portfolio");
        }
    });
    connect(command_bar_, &PortfolioCommandBar::export_json_requested, this, [this]() {
        if (selected_id_.isEmpty())
            return;
        QString path = QFileDialog::getSaveFileName(this, "Export JSON", "portfolio.json", "JSON Files (*.json)");
        if (!path.isEmpty()) {
            services::PortfolioService::instance().export_json(selected_id_, path);
            services::FileManagerService::instance().import_file(path, "portfolio");
        }
    });
    connect(command_bar_, &PortfolioCommandBar::import_requested, this, [this]() {
        ImportPortfolioDialog dlg(portfolios_, this);
        if (dlg.exec() == QDialog::Accepted) {
            services::PortfolioService::instance().import_json(dlg.file_path(), dlg.mode(), dlg.merge_target_id());
        }
    });
    auto open_insights = [this](PortfolioInsightsPanel::Tab tab) {
        if (!summary_loaded_)
            return;
        insights_panel_->set_summary(current_summary_);
        const int top = command_bar_->height();
        const int bottom_reserve = status_bar_ ? status_bar_->height() : 0;
        const int h = qMax(200, height() - top - bottom_reserve);
        insights_scrim_->setGeometry(0, top, width(), h);
        insights_scrim_->show();
        insights_scrim_->raise();
        insights_panel_->setFixedHeight(h);
        insights_panel_->clamp_width(static_cast<int>(width() * 0.9));
        insights_panel_->move(width() - insights_panel_->width(), top);
        insights_panel_->raise();
        insights_panel_->open_tab(tab);
    };
    connect(command_bar_, &PortfolioCommandBar::ai_analyze_requested, this,
            [open_insights]() { open_insights(PortfolioInsightsPanel::Tab::AI); });
    connect(command_bar_, &PortfolioCommandBar::agent_run_requested, this,
            [open_insights]() { open_insights(PortfolioInsightsPanel::Tab::Agent); });

    // Wire import completion
    connect(&services::PortfolioService::instance(), &services::PortfolioService::import_complete, this,
            [this](portfolio::ImportResult result) {
                if (result.portfolio_id.isEmpty()) {
                    QString detail = result.errors.isEmpty()
                                         ? QString("Import failed with no details.")
                                         : result.errors.join("\n");
                    QMessageBox::warning(this, "Portfolio Import Failed",
                                         "Could not import the portfolio.\n\n" + detail +
                                             "\n\nExpected format:\n"
                                             "{\n"
                                             "  \"portfolio_name\": \"My Portfolio\",\n"
                                             "  \"currency\": \"USD\",\n"
                                             "  \"owner\": \"...\",\n"
                                             "  \"transactions\": [\n"
                                             "    {\"date\": \"YYYY-MM-DD\", \"symbol\": \"AAPL\", \"type\": \"BUY\",\n"
                                             "     \"quantity\": 10, \"price\": 150.0}\n"
                                             "  ]\n"
                                             "}");
                    return;
                }
                on_portfolio_selected(result.portfolio_id);
            });
}

// Build one CTA card used in the empty state. Each card is a clickable,
// equal-weight tile: icon, title, subtitle, accent bar at the bottom.
static QPushButton* make_cta_card(const QString& glyph, const QString& title, const QString& subtitle,
                                  const QString& accent_hex, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(220, 140);
    btn->setObjectName("pfCtaCard");
    btn->setProperty("accent", accent_hex);
    btn->setStyleSheet(
        QString("QPushButton#pfCtaCard { background:%1; color:%2; border:1px solid %3; border-radius:4px;"
                "  text-align:left; padding:16px; }"
                "QPushButton#pfCtaCard:hover { border-color:%4; background:%5; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), accent_hex,
                 ui::colors::BG_HOVER()));

    auto* v = new QVBoxLayout(btn);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);
    v->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto* icon = new QLabel(glyph);
    icon->setStyleSheet(QString("color:%1; font-size:22px; background:transparent; border:none;").arg(accent_hex));
    v->addWidget(icon);

    v->addSpacing(4);

    auto* h = new QLabel(title);
    h->setStyleSheet(QString("color:%1; font-size:13px; font-weight:700; letter-spacing:0.5px;"
                             " background:transparent; border:none;")
                         .arg(ui::colors::TEXT_PRIMARY()));
    v->addWidget(h);

    auto* p = new QLabel(subtitle);
    p->setWordWrap(true);
    p->setStyleSheet(QString("color:%1; font-size:12px; line-height:1.4; background:transparent; border:none;")
                         .arg(ui::colors::TEXT_SECONDARY()));
    v->addWidget(p);

    v->addStretch(1);

    // Accent bar at bottom
    auto* bar = new QWidget;
    bar->setFixedHeight(2);
    bar->setStyleSheet(QString("background:%1; border:none;").arg(accent_hex));
    v->addWidget(bar);

    return btn;
}

QWidget* PortfolioScreen::build_empty_state() {
    auto* w = new QWidget(this);
    w->setObjectName("pfEmptyState");
    w->setStyleSheet(QString("#pfEmptyState { background:%1; }").arg(ui::colors::BG_BASE()));

    auto* outer = new QVBoxLayout(w);
    outer->setAlignment(Qt::AlignCenter);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(0);

    // Inner content block (keeps width capped so it doesn't stretch)
    auto* content = new QWidget(w);
    content->setStyleSheet("background:transparent;");
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignCenter);

    auto* accent_dot = new QLabel("\u25C6"); // diamond
    accent_dot->setAlignment(Qt::AlignCenter);
    accent_dot->setStyleSheet(QString("color:%1; font-size:14px; letter-spacing:4px;").arg(ui::colors::AMBER()));
    layout->addWidget(accent_dot);

    auto* title = new QLabel("PORTFOLIO WORKSPACE");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(
        QString("color:%1; font-size:18px; font-weight:700; letter-spacing:2px;").arg(ui::colors::TEXT_PRIMARY()));
    layout->addWidget(title);

    auto* sub = new QLabel("Create or import a portfolio to get started.");
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet(QString("color:%1; font-size:12px; letter-spacing:0.2px;").arg(ui::colors::TEXT_SECONDARY()));
    layout->addWidget(sub);

    layout->addSpacing(24);

    // CTA card row: Create / Import — equal weight. No "demo" card: finterm
    // shows only real data, so there is no sample/fabricated portfolio.
    auto* card_row = new QHBoxLayout;
    card_row->setAlignment(Qt::AlignCenter);
    card_row->setSpacing(14);

    auto* create_card = make_cta_card("\u25A2", "CREATE NEW",
                                      "Start a fresh portfolio. Name it, pick a currency, "
                                      "and add holdings one at a time.",
                                      ui::colors::AMBER(), content);
    connect(create_card, &QPushButton::clicked, this, &PortfolioScreen::on_create_requested);

    auto* import_card = make_cta_card("\u2913", "IMPORT JSON",
                                      "Load an existing portfolio from an exported JSON file. "
                                      "Merge into an existing portfolio or create a new one.",
                                      ui::colors::CYAN(), content);
    connect(import_card, &QPushButton::clicked, command_bar_, &PortfolioCommandBar::import_requested);

    card_row->addWidget(create_card);
    card_row->addWidget(import_card);
    layout->addLayout(card_row);

    outer->addWidget(content, 0, Qt::AlignCenter);
    return w;
}

QWidget* PortfolioScreen::build_loading_state() {
    auto* w = new QWidget(this);
    w->setObjectName("pfLoadingState");
    w->setStyleSheet(QString("#pfLoadingState { background:%1; }").arg(ui::colors::BG_BASE()));

    auto* outer = new QVBoxLayout(w);
    outer->setAlignment(Qt::AlignCenter);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(14);

    // Animated bar skeleton — three stacked pills hinting at the ribbon/chart/table layout.
    auto make_bar = [](int h, int w_px, const QString& color, QWidget* parent) {
        auto* bar = new QFrame(parent);
        bar->setFixedSize(w_px, h);
        bar->setStyleSheet(QString("background:%1; border-radius:3px;").arg(color));
        return bar;
    };

    auto* bar1 = make_bar(14, 360, ui::colors::BG_HOVER(), w);
    auto* bar2 = make_bar(86, 360, ui::colors::BG_SURFACE(), w);
    auto* bar3 = make_bar(14, 280, ui::colors::BG_HOVER(), w);
    auto* bar4 = make_bar(14, 220, ui::colors::BG_HOVER(), w);

    auto* row1 = new QHBoxLayout;
    row1->setAlignment(Qt::AlignCenter);
    row1->addWidget(bar1);
    outer->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->setAlignment(Qt::AlignCenter);
    row2->addWidget(bar2);
    outer->addLayout(row2);

    auto* row3 = new QHBoxLayout;
    row3->setAlignment(Qt::AlignCenter);
    row3->addWidget(bar3);
    outer->addLayout(row3);

    auto* row4 = new QHBoxLayout;
    row4->setAlignment(Qt::AlignCenter);
    row4->addWidget(bar4);
    outer->addLayout(row4);

    // Subtle label beneath skeleton
    auto* text = new QLabel("Loading portfolio data…");
    text->setAlignment(Qt::AlignCenter);
    text->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:600; letter-spacing:0.8px;").arg(ui::colors::AMBER()));
    outer->addWidget(text);

    // Pulsing opacity animation for the skeleton group (subtle, ≤20 fps per P9)
    auto* effect = new QGraphicsOpacityEffect(w);
    effect->setOpacity(1.0);
    w->setGraphicsEffect(effect);
    auto* anim = new QPropertyAnimation(effect, "opacity", w);
    anim->setDuration(1200);
    anim->setStartValue(0.55);
    anim->setEndValue(1.0);
    anim->setLoopCount(-1);
    anim->setEasingCurve(QEasingCurve::InOutSine);
    // Ping-pong: swap start/end each loop
    connect(anim, &QPropertyAnimation::finished, anim, [anim]() {
        auto s = anim->startValue();
        anim->setStartValue(anim->endValue());
        anim->setEndValue(s);
    });
    anim->start();

    return w;
}

void PortfolioScreen::update_content_state() {
    bool has_portfolios = !portfolios_.isEmpty();
    bool has_selection = !selected_id_.isEmpty();

    if (!has_portfolios || !has_selection) {
        // No portfolios or nothing selected — clean empty state
        content_stack_->setCurrentIndex(0);
        stats_ribbon_->setVisible(false);
        status_bar_->setVisible(false);
        command_bar_->setVisible(!has_portfolios ? false : true);
        if (has_portfolios) {
            command_bar_->set_has_selection(false);
        }
    } else if (!summary_loaded_) {
        content_stack_->setCurrentIndex(1); // loading
        stats_ribbon_->setVisible(false);
        status_bar_->setVisible(true);
        command_bar_->setVisible(true);
        command_bar_->set_has_selection(false);
    } else if (show_ffn_) {
        content_stack_->setCurrentIndex(4); // FFN view
        stats_ribbon_->setVisible(false);
        status_bar_->setVisible(true);
        command_bar_->setVisible(true);
        command_bar_->set_has_selection(true);
    } else if (active_detail_.has_value()) {
        content_stack_->setCurrentIndex(3); // detail view
        stats_ribbon_->setVisible(false);
        status_bar_->setVisible(true);
        command_bar_->setVisible(true);
        command_bar_->set_has_selection(true);
    } else {
        content_stack_->setCurrentIndex(2); // main view
        stats_ribbon_->setVisible(true);
        status_bar_->setVisible(true);
        command_bar_->setVisible(true);
        command_bar_->set_has_selection(true);
    }
}

// ── Lifecycle (P3) ───────────────────────────────────────────────────────────

void PortfolioScreen::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh_timer_->start();
    status_bar_->start_clock();
    // Selection sticks across tab switches and dock layout reflows. Don't
    // clear selected_symbol_ here — clearing it would let "Open in Equity
    // Research" (which triggers a dock split → hide/show cycle on this
    // pane) silently revert the user's selection to whatever the most
    // recent summary's top-weighted holding happens to be.
    if (!selected_id_.isEmpty())
        request_refresh(/*force_fresh=*/true);
    // Resume the DataHub live-quote stream if we already have a summary
    // (so the user sees live updates without waiting for the next 20s tick).
    if (summary_loaded_)
        hub_resubscribe_holdings();
}

void PortfolioScreen::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    refresh_timer_->stop();
    hub_unsubscribe_all();
    status_bar_->stop_clock();
}

// ── Slots ────────────────────────────────────────────────────────────────────

void PortfolioScreen::on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios) {
    portfolios_ = portfolios;
    command_bar_->set_portfolios(portfolios);

    if (portfolios.isEmpty()) {
        command_bar_->set_has_portfolios(false);
        selected_id_.clear();
        summary_loaded_ = false;
    } else {
        command_bar_->set_has_portfolios(true);
        // Auto-select first portfolio if none selected
        if (selected_id_.isEmpty()) {
            on_portfolio_selected(portfolios.first().id);
        }
    }
    update_content_state();
}

void PortfolioScreen::on_portfolio_selected(const QString& id) {
    if (id == selected_id_ && summary_loaded_)
        return;

    // We don't invalidate the quote cache here. The new portfolio's holdings
    // aren't known until load_summary returns, so a blanket invalidation
    // would only nuke unrelated cached quotes (markets panels, dashboard
    // widgets). The 30s TTL plus DataHub's min_interval keep data fresh
    // enough; if the user really wants force-fresh they can hit refresh.

    selected_id_ = id;
    // Make the AI chat aware of the active portfolio — only while this screen is
    // visible, so an init-time auto-select doesn't claim a "current view".
    if (isVisible())
        services::AppContextService::instance().set_active_portfolio(id);
    ScreenStateManager::instance().notify_changed(this);
    summary_loaded_ = false;
    active_detail_ = std::nullopt;
    command_bar_->set_detail_view(std::nullopt);
    if (txn_panel_)
        txn_panel_->clear();
    if (blotter_)
        blotter_->set_sector_filter({});

    // Reset per-symbol focus from the previous portfolio. This extends the
    // heatmap "PORTFOLIO" header reset (see lambda below at portfolio_view_
    // requested) — same clear of selected_symbol_ + perf chart focus +
    // blotter selection, plus two more things the header reset doesn't need:
    //   • heatmap_->set_selected_symbol({}) — the header reset doesn't call
    //     this because the heatmap is the *source* of the action; here the
    //     dropdown is the source, so we have to push the deselect into the
    //     heatmap explicitly.
    //   • order_panel_->set_holding(nullptr) — the order panel holds a raw
    //     pointer into current_summary_.holdings, which is about to be
    //     replaced wholesale by on_summary_loaded; the header reset doesn't
    //     swap summaries so it doesn't need this. Without it, a stale
    //     pointer can survive between the switch and the panel's next
    //     interaction.
    //
    // The chart bug this addresses: PortfolioPerfChart::update_chart() early-
    // routes through update_chart_focus() whenever focus_symbol_ is set, so
    // without clear_focus_symbol() the chart keeps rendering the previous
    // portfolio's focused-symbol curve even after on_snapshots_loaded()
    // feeds it new data.
    //
    // Heatmap fundamentals (TGT LOW / MEAN / HIGH, P/E, yield, consensus)
    // are also cleared here so the panel never briefly shows the prior
    // portfolio's analyst targets between switch and re-emission from
    // PortfolioService::fetch_portfolio_fundamentals (which now re-emits
    // the cached value on a repeat visit).
    selected_symbol_.clear();
    if (perf_chart_)
        perf_chart_->clear_focus_symbol();
    if (heatmap_) {
        heatmap_->set_selected_symbol({});
        heatmap_->clear_fundamentals();
        // BETA + risk gauge + concentration / volatility also live in the
        // heatmap analyst panel; their data arrives on a separate signal
        // (metrics_computed via compute_metrics), so without this they'd
        // show the prior portfolio's values for ~100-200 ms while the
        // SPY regression runs.
        heatmap_->clear_metrics();
    }
    if (blotter_)
        blotter_->set_selected_symbol({});
    if (order_panel_)
        order_panel_->set_holding(nullptr);

    // New portfolio = different holdings cohort. Drop the cached correlation
    // key so the next summary triggers a fresh fetch_correlation. Benchmark
    // history and risk-free rate are session-scoped (data isn't
    // portfolio-specific), so don't reset those.
    last_correlation_syms_.clear();

    // Find portfolio and update UI
    for (const auto& p : portfolios_) {
        if (p.id == id) {
            command_bar_->set_selected_portfolio(p);
            status_bar_->set_portfolio_name(p.name);
            break;
        }
    }

    update_content_state();
    services::PortfolioService::instance().load_summary(id);
}

void PortfolioScreen::on_summary_loaded(portfolio::PortfolioSummary summary) {
    if (summary.portfolio.id != selected_id_)
        return; // Stale response

    current_summary_ = summary;
    summary_loaded_ = true;

    command_bar_->set_summary(summary);
    command_bar_->set_refreshing(false);
    stats_ribbon_->set_summary(summary);
    status_bar_->set_summary(summary);

    update_main_view_data();
    update_content_state();

    // (Re)subscribe to per-symbol quote topics if the symbol set changed —
    // this is the live channel that keeps day-chg% / price fresh between
    // PortfolioService::refresh_summary rebuilds.
    if (isVisible())
        hub_resubscribe_holdings();

    // No auto-selection. With an empty selection the chart/heatmap/blotter
    // stay on the portfolio-level (aggregate) view, which is the desired
    // default. Auto-picking the top-weighted holding here previously made
    // a single ticker (whichever held the largest weight) act as a phantom
    // default and overrode the user's selection on every refresh.

    // Trigger metrics computation
    services::PortfolioService::instance().compute_metrics(summary);

    // Fetch analyst targets, P/E, yield and consensus for the heatmap
    // portfolio detail panel. Runs once per summary load (not every tick).
    services::PortfolioService::instance().fetch_portfolio_fundamentals(summary.portfolio.id);

    // Load performance history for the chart
    services::PortfolioService::instance().load_snapshots(summary.portfolio.id);

    // Load recent transactions for the history panel
    if (txn_panel_)
        services::PortfolioService::instance().load_transactions(summary.portfolio.id, 50);

    // Fetch real 30-day correlation for the sector panel — but only when
    // the symbol set actually changed. The previous code re-spawned the
    // Python correlation process on every 20-second refresh tick, even
    // when holdings hadn't moved.
    if (!summary.holdings.isEmpty()) {
        QStringList syms;
        syms.reserve(summary.holdings.size());
        for (const auto& h : summary.holdings)
            syms.append(h.symbol);
        // Sorted comparison so reorder-only summary updates don't re-fetch.
        QStringList syms_sorted = syms;
        std::sort(syms_sorted.begin(), syms_sorted.end());
        if (syms_sorted != last_correlation_syms_) {
            last_correlation_syms_ = syms_sorted;
            services::PortfolioService::instance().fetch_correlation(syms);
        }
    }

    // Fetch benchmark history for perf chart overlay — once per benchmark
    // per session. Use the portfolio's currency to pick a sensible default
    // index (TSX for CAD, SPY for USD, FTSE for GBP, …). We also always
    // fetch SPY because Beta in compute_metrics() regresses against SPY
    // regardless of currency. The service-level cache makes repeat calls
    // cheap, but skipping them outright here also saves the QString
    // marshalling and signal dispatch on every refresh tick.
    {
        auto& svc = services::PortfolioService::instance();
        const QString bench = services::PortfolioService::default_benchmark_for_currency(
            summary.portfolio.currency);
        if (!fetched_benchmarks_.contains(bench)) {
            fetched_benchmarks_.insert(bench);
            svc.fetch_benchmark_history(bench, "1y");
        }
        if (bench != QStringLiteral("SPY") && !fetched_benchmarks_.contains(QStringLiteral("SPY"))) {
            fetched_benchmarks_.insert(QStringLiteral("SPY"));
            svc.fetch_benchmark_history("SPY", "1y");
        }
    }

    // Fetch live risk-free rate (DGS10) for Sharpe computation — server
    // caches 24h, so we only need to ask once per session.
    if (!risk_free_fetched_) {
        risk_free_fetched_ = true;
        services::PortfolioService::instance().fetch_risk_free_rate();
    }
}

void PortfolioScreen::on_summary_error(QString portfolio_id, QString /*error*/) {
    if (portfolio_id != selected_id_)
        return;
    // Show empty state with error — for now just revert to empty
    summary_loaded_ = false;
    update_content_state();
}

void PortfolioScreen::on_metrics_computed(portfolio::ComputedMetrics metrics) {
    current_metrics_ = metrics;
    stats_ribbon_->set_metrics(metrics);
    if (heatmap_)
        heatmap_->set_metrics(metrics);
    if (detail_wrapper_)
        detail_wrapper_->update_metrics(metrics);
}

void PortfolioScreen::on_snapshots_loaded(QString portfolio_id, QVector<portfolio::PortfolioSnapshot> snapshots) {
    if (portfolio_id != selected_id_)
        return;
    if (perf_chart_)
        perf_chart_->set_snapshots(snapshots);
    if (detail_wrapper_)
        detail_wrapper_->update_snapshots(snapshots);
}

void PortfolioScreen::on_portfolio_created(portfolio::Portfolio portfolio) {
    // portfolios_loaded fires asynchronously after creation, so the new portfolio
    // may not be in portfolios_ yet when on_portfolio_selected looks it up.
    // Append it immediately so the selector label and status bar show the correct
    // name without waiting for the reload round-trip.
    bool already_present = false;
    for (const auto& p : portfolios_) {
        if (p.id == portfolio.id) {
            already_present = true;
            break;
        }
    }
    if (!already_present)
        portfolios_.append(portfolio);

    on_portfolio_selected(portfolio.id);
}

void PortfolioScreen::on_portfolio_deleted(QString id) {
    if (id == selected_id_) {
        selected_id_.clear();
        summary_loaded_ = false;
        update_content_state();
    }
}

void PortfolioScreen::on_asset_changed(QString portfolio_id) {
    if (portfolio_id == selected_id_) {
        services::PortfolioService::instance().refresh_summary(portfolio_id);
    }
}

void PortfolioScreen::on_create_requested() {
    CreatePortfolioDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        services::PortfolioService::instance().create_portfolio(dlg.name(), dlg.owner(), dlg.currency());
    }
}

void PortfolioScreen::on_delete_requested(const QString& id) {
    QString name;
    for (const auto& p : portfolios_) {
        if (p.id == id) {
            name = p.name;
            break;
        }
    }

    ConfirmDeleteDialog dlg(name, this);
    if (dlg.exec() == QDialog::Accepted) {
        services::PortfolioService::instance().delete_portfolio(id);
    }
}

void PortfolioScreen::on_detail_view_selected(portfolio::DetailView view) {
    if (active_detail_.has_value() && *active_detail_ == view) {
        // Toggle off — go back to main view
        active_detail_ = std::nullopt;
    } else {
        active_detail_ = view;
        // Show the detail view with current data
        detail_wrapper_->show_view(view, current_summary_, current_summary_.portfolio.currency);
    }
    command_bar_->set_detail_view(active_detail_);
    update_content_state();
}

void PortfolioScreen::on_refresh_interval_changed(int ms) {
    refresh_interval_ms_ = ms;
    refresh_timer_->setInterval(ms);
    SettingsRepository::instance().set("portfolio.refresh_interval_ms", QString::number(ms), "portfolio");
}

void PortfolioScreen::request_refresh(bool force_fresh) {
    if (selected_id_.isEmpty())
        return;
    if (force_fresh) {
        // Invalidate only THIS portfolio's holdings — not the entire
        // "market:*" bucket. The old code passed {} which dropped every
        // other consumer's cache too (markets panels, dashboard widgets),
        // forcing them to re-fetch on next show. Scoped invalidation
        // means each screen only pays for its own freshness.
        //
        // On first show current_summary_.holdings is empty; nothing to
        // invalidate. The subsequent fetch_quotes path checks the cache
        // (≤30s old) anyway, so the user sees recent data immediately.
        QStringList syms;
        syms.reserve(current_summary_.holdings.size());
        for (const auto& h : current_summary_.holdings)
            syms.append(h.symbol);
        if (!syms.isEmpty())
            services::MarketDataService::instance().invalidate_quotes(syms);
        // Reset debounce keys so user-initiated refresh actually re-runs the
        // side-fetches even when the symbol set is unchanged. Timer-driven
        // ticks (force_fresh=false) keep the debounce so we don't re-spawn
        // the correlation Python process every 20 seconds.
        last_correlation_syms_.clear();
        fetched_benchmarks_.clear();
        risk_free_fetched_ = false;
    }
    command_bar_->set_refreshing(true);
    services::PortfolioService::instance().refresh_summary(selected_id_);
}

// ── DataHub live-quote integration ───────────────────────────────────────────
//
// The dashboard's PortfolioSummaryWidget gets fresh prices via DataHub
// publishes; the top-level Portfolio screen used to rely solely on its
// 20s QTimer + PortfolioService::refresh_summary, which means the headline
// chg% can lag behind the dashboard. We subscribe to the same per-symbol
// topics here and patch the in-memory summary on each delivery — the heavier
// PortfolioService refresh still runs on its own cadence for sector /
// correlation / snapshot work that DataHub doesn't carry.

void PortfolioScreen::hub_resubscribe_holdings() {
    if (current_summary_.holdings.isEmpty())
        return;

    // Compute the sorted symbol set; skip if it matches what we're already
    // subscribed to. This avoids tearing down and re-establishing N subs
    // every 20s when refresh_summary fires with the same holdings.
    QStringList new_syms;
    new_syms.reserve(current_summary_.holdings.size());
    for (const auto& h : current_summary_.holdings)
        new_syms.append(h.symbol);
    new_syms.sort();
    if (hub_active_ && new_syms == hub_subscribed_syms_)
        return;

    auto& hub = datahub::DataHub::instance();
    hub.unsubscribe(this);
    hub_subscribed_syms_ = new_syms;
    hub_active_ = false;

    QStringList topics;
    topics.reserve(new_syms.size());
    for (const QString& sym : new_syms) {
        const QString topic = QStringLiteral("market:quote:") + sym;
        topics.append(topic);
        hub.subscribe(this, topic, [this, sym](const QVariant& v) {
            if (!v.canConvert<services::QuoteData>())
                return;
            const auto q = v.value<services::QuoteData>();
            for (auto& h : current_summary_.holdings) {
                if (h.symbol != sym)
                    continue;
                h.current_price = q.price;
                h.day_change = q.change;
                h.day_change_percent = q.change_pct;
                h.day_high = q.high;
                h.day_low = q.low;
                h.day_volume = q.volume;
                h.bid = q.bid;
                h.ask = q.ask;
                h.bid_size = q.bid_size;
                h.ask_size = q.ask_size;
                h.market_value = h.quantity * h.current_price;
                h.unrealized_pnl = h.market_value - h.cost_basis;
                h.unrealized_pnl_percent = (h.cost_basis > 0)
                    ? (h.unrealized_pnl / h.cost_basis) * 100.0 : 0;
                break;
            }
            if (!hub_refresh_timer_->isActive())
                hub_refresh_timer_->start();
        });
    }
    hub_active_ = true;
    // Force the first publish: the DataHub min_interval gate could otherwise
    // delay the cold-start paint by several seconds.
    hub.request(topics, /*force=*/true);
}

void PortfolioScreen::hub_unsubscribe_all() {
    if (!hub_active_)
        return;
    datahub::DataHub::instance().unsubscribe(this);
    hub_active_ = false;
}

void PortfolioScreen::rebuild_summary_aggregates_and_refresh() {
    if (!summary_loaded_ || current_summary_.holdings.isEmpty())
        return;

    double total_mv = 0;
    double total_cost = 0;
    double total_day = 0;
    int gainers = 0;
    int losers = 0;
    for (const auto& h : current_summary_.holdings) {
        total_mv += h.market_value;
        total_cost += h.cost_basis;
        total_day += h.day_change * h.quantity;
        if (h.unrealized_pnl >= 0) ++gainers; else ++losers;
    }
    for (auto& h : current_summary_.holdings)
        h.weight = (total_mv > 0) ? (h.market_value / total_mv) * 100.0 : 0;

    current_summary_.total_market_value = total_mv;
    current_summary_.total_cost_basis = total_cost;
    current_summary_.total_unrealized_pnl = total_mv - total_cost;
    current_summary_.total_unrealized_pnl_percent = (total_cost > 0)
        ? ((total_mv - total_cost) / total_cost) * 100.0 : 0;
    current_summary_.total_day_change = total_day;
    current_summary_.total_day_change_percent = (total_mv - total_day > 0)
        ? (total_day / (total_mv - total_day)) * 100.0 : 0;
    current_summary_.gainers = gainers;
    current_summary_.losers = losers;
    current_summary_.last_updated = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    command_bar_->set_summary(current_summary_);
    stats_ribbon_->set_summary(current_summary_);
    status_bar_->set_summary(current_summary_);
    update_main_view_data();
}

// ── Phase 3: Main view construction ──────────────────────────────────────────

QWidget* PortfolioScreen::build_main_view() {
    auto* w = new QWidget(this);
    w->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    auto* h_layout = new QHBoxLayout(w);
    h_layout->setContentsMargins(0, 0, 0, 0);
    h_layout->setSpacing(0);

    // Left: Heatmap (220px)
    heatmap_ = new PortfolioHeatmap;
    connect(heatmap_, &PortfolioHeatmap::symbol_selected, this, &PortfolioScreen::on_symbol_selected);
    // Heatmap pane's "PORTFOLIO" title doubles as a home-view affordance:
    // clicking it deselects the symbol and returns the perf chart and any
    // dependent panels to portfolio-level (whole-NAV) view.
    connect(heatmap_, &PortfolioHeatmap::portfolio_view_requested, this, [this]() {
        selected_symbol_.clear();
        if (perf_chart_) perf_chart_->clear_focus_symbol();
        if (blotter_)    blotter_->set_selected_symbol({});
    });
    h_layout->addWidget(heatmap_);

    // Center: chart + sector (top), blotter (bottom)
    auto* center = new QWidget(this);
    auto* center_layout = new QVBoxLayout(center);
    center_layout->setContentsMargins(0, 0, 0, 0);
    center_layout->setSpacing(0);

    // Top: perf chart + sector panel side by side
    auto* top_split = new QSplitter(Qt::Horizontal);
    top_split->setHandleWidth(1);
    top_split->setStyleSheet(QString("QSplitter::handle { background:%1; }").arg(ui::colors::BORDER_DIM()));

    perf_chart_ = new PortfolioPerfChart;
    // Trigger backfill when the user clicks a period that needs more history
    // than we have cached. PortfolioService re-emits history_backfilled when
    // done, which routes back through the chart via load_snapshots.
    connect(perf_chart_, &PortfolioPerfChart::backfill_period_requested, this,
            [this](const QString& period) {
                if (selected_id_.isEmpty())
                    return;
                services::PortfolioService::instance().backfill_history(selected_id_, period);
            });
    // Per-symbol focus: refetch daily closes when the user picks a row or
    // changes the period in focus mode.
    connect(perf_chart_, &PortfolioPerfChart::focus_symbol_period_requested, this,
            [](const QString& symbol, const QString& period) {
                services::PortfolioService::instance().fetch_benchmark_history(symbol, period);
            });
    // Intraday-rendered periods (1D always, 1W in focus mode). Focus mode
    // passes the focused symbol along with the desired yfinance period/
    // interval; aggregate 1D passes an empty symbol and the service
    // ignores the period/interval (it only fans out 1d/1m).
    connect(perf_chart_, &PortfolioPerfChart::intraday_requested, this,
            [this](const QString& symbol, const QString& period,
                   const QString& interval) {
                auto& svc = services::PortfolioService::instance();
                if (!symbol.isEmpty()) {
                    svc.fetch_symbol_intraday(symbol, period, interval);
                } else if (!selected_id_.isEmpty()) {
                    svc.fetch_portfolio_intraday(selected_id_);
                }
            });
    connect(&services::PortfolioService::instance(),
            &services::PortfolioService::symbol_intraday_loaded,
            perf_chart_, &PortfolioPerfChart::set_symbol_intraday);
    connect(&services::PortfolioService::instance(),
            &services::PortfolioService::portfolio_intraday_loaded, perf_chart_,
            [this](const QString& portfolio_id, const QVector<qint64>& ts,
                   const QVector<double>& navs) {
                // Discard stale fetches if the user switched portfolios mid-flight.
                if (portfolio_id == selected_id_)
                    perf_chart_->set_portfolio_intraday(ts, navs);
            });
    sector_panel_ = new PortfolioSectorPanel;
    connect(sector_panel_, &PortfolioSectorPanel::sector_selected, this, [this](const QString& sector) {
        if (sector.isEmpty()) {
            blotter_->set_sector_filter({});
            return;
        }
        QStringList matching;
        for (const auto& h : current_summary_.holdings) {
            QString h_sector = h.sector.isEmpty() ? QStringLiteral("Unclassified") : h.sector;
            if (h_sector == sector)
                matching.append(h.symbol);
        }
        blotter_->set_sector_filter(matching);
    });

    top_split->addWidget(perf_chart_);
    top_split->addWidget(sector_panel_);
    top_split->setStretchFactor(0, 7); // ~70%
    top_split->setStretchFactor(1, 3); // ~30%
    sector_panel_->setMinimumWidth(180);

    center_layout->addWidget(top_split, 40); // 40% of vertical space

    // Separator
    auto* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    center_layout->addWidget(sep);

    // Positions section header: title + count badge + filter field
    auto* blotter_section = new QWidget(this);
    auto* blotter_layout = new QVBoxLayout(blotter_section);
    blotter_layout->setContentsMargins(0, 0, 0, 0);
    blotter_layout->setSpacing(0);

    auto* header_row = new QWidget(this);
    header_row->setFixedHeight(32);
    header_row->setObjectName("pfPositionsHeader");
    header_row->setStyleSheet(QString("#pfPositionsHeader { background:%1; border-bottom:1px solid %2; }")
                                  .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* header_hl = new QHBoxLayout(header_row);
    header_hl->setContentsMargins(12, 0, 10, 0);
    header_hl->setSpacing(8);

    // Accent tick + section title
    auto* title_tick = new QLabel;
    title_tick->setFixedSize(3, 14);
    title_tick->setStyleSheet(QString("background:%1; border-radius:1px;").arg(ui::colors::AMBER()));
    header_hl->addWidget(title_tick);

    auto* title = new QLabel("POSITIONS");
    title->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:1.2px; background:transparent;")
                             .arg(ui::colors::TEXT_PRIMARY()));
    header_hl->addWidget(title);

    // Count badge
    positions_count_label_ = new QLabel("0");
    positions_count_label_->setAlignment(Qt::AlignCenter);
    positions_count_label_->setMinimumWidth(22);
    positions_count_label_->setFixedHeight(16);
    positions_count_label_->setStyleSheet(
        QString("color:%1; background:%2; border:1px solid %3; border-radius:8px;"
                "  font-size:12px; font-weight:700; padding:0 6px;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    header_hl->addWidget(positions_count_label_);

    header_hl->addStretch(1);

    // Filter field — right-aligned, framed so it reads as an input, not a label
    auto* filter_wrap = new QWidget(this);
    filter_wrap->setFixedHeight(22);
    filter_wrap->setMinimumWidth(200);
    filter_wrap->setObjectName("pfFilterWrap");
    filter_wrap->setStyleSheet(
        QString("#pfFilterWrap { background:%1; border:1px solid %2; border-radius:2px; }"
                "#pfFilterWrap:focus-within { border-color:%3; }")
            .arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    auto* filter_hl = new QHBoxLayout(filter_wrap);
    filter_hl->setContentsMargins(8, 0, 8, 0);
    filter_hl->setSpacing(6);

    auto* filter_icon = new QLabel("\u2315"); // ⌕ search glyph
    filter_icon->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent; border:none;").arg(ui::colors::TEXT_SECONDARY()));
    filter_hl->addWidget(filter_icon);

    auto* filter_edit = new QLineEdit;
    filter_edit->setPlaceholderText("Filter positions…");
    filter_edit->setStyleSheet(QString("QLineEdit { background:transparent; color:%1; border:none;"
                                       "  font-size:12px; font-family:%2; }"
                                       "QLineEdit:focus { color:%3; }")
                                   .arg(ui::colors::TEXT_SECONDARY(), ui::fonts::DATA_FAMILY, ui::colors::TEXT_PRIMARY()));
    filter_hl->addWidget(filter_edit, 1);

    header_hl->addWidget(filter_wrap);

    blotter_layout->addWidget(header_row);

    // Bottom: positions blotter
    blotter_ = new PortfolioBlotter;
    connect(blotter_, &PortfolioBlotter::symbol_selected, this, &PortfolioScreen::on_symbol_selected);
    connect(blotter_, &PortfolioBlotter::edit_transaction_requested, this, [this](const QString& symbol) {
        // Load transactions for this symbol, show edit dialog for the most recent one
        services::PortfolioService::instance().load_transactions(selected_id_, 100);
        QPointer<PortfolioScreen> self = this;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(
            &services::PortfolioService::instance(), &services::PortfolioService::transactions_loaded, this,
            [this, self, symbol, conn](QVector<portfolio::Transaction> txns) {
                disconnect(*conn);
                if (!self)
                    return;
                // Find most recent transaction for this symbol
                portfolio::Transaction* match = nullptr;
                for (auto& t : txns) {
                    if (t.symbol == symbol) {
                        match = &t;
                        break;
                    }
                }
                if (!match)
                    return;
                EditTransactionDialog dlg(*match, this);
                if (dlg.exec() == QDialog::Accepted) {
                    services::PortfolioService::instance().update_transaction(match->id, dlg.quantity(), dlg.price(),
                                                                              dlg.date(), dlg.notes());
                }
            },
            Qt::SingleShotConnection);
    });
    connect(blotter_, &PortfolioBlotter::delete_position_requested, this, [this](const QString& symbol) {
        auto* h = find_holding(symbol);
        if (!h)
            return;
        ConfirmDeleteDialog dlg(QString("%1 (%2 shares)").arg(symbol).arg(h->quantity, 0, 'f', 2), this);
        if (dlg.exec() == QDialog::Accepted) {
            services::PortfolioService::instance().sell_asset(selected_id_, symbol, h->quantity, h->current_price);
        }
    });
    connect(filter_edit, &QLineEdit::textChanged, blotter_, &PortfolioBlotter::set_filter);

    // Heatmap right-click context menu emits the same intent signals as the
    // blotter. Forward signal-to-signal so the existing dialog handlers
    // (wired above) fire from both sources without duplication.
    if (heatmap_) {
        connect(heatmap_, &PortfolioHeatmap::edit_transaction_requested, blotter_,
                &PortfolioBlotter::edit_transaction_requested);
        connect(heatmap_, &PortfolioHeatmap::delete_position_requested, blotter_,
                &PortfolioBlotter::delete_position_requested);
    }

    blotter_layout->addWidget(blotter_, 1);

    // Transaction history panel below blotter
    txn_panel_ = new PortfolioTxnPanel;
    txn_panel_->setMinimumHeight(80);
    txn_panel_->setMaximumHeight(160);
    blotter_layout->addWidget(txn_panel_);

    center_layout->addWidget(blotter_section, 60); // 60% of vertical space

    h_layout->addWidget(center, 1); // center takes full remaining space

    // Order panel is a floating overlay (parented to PortfolioScreen, not in layout)
    // so BUY/SELL slides in from the right edge without reflowing the main grid.
    order_panel_ = new PortfolioOrderPanel(this);
    order_panel_->setVisible(false);
    order_panel_->raise();
    connect(order_panel_, &PortfolioOrderPanel::close_requested, this, &PortfolioScreen::on_order_panel_close);
    connect(order_panel_, &PortfolioOrderPanel::buy_submitted, this, [this]() {
        AddAssetDialog dlg(this, pending_buy_symbol_);
        pending_buy_symbol_.clear();  // one-shot prefill
        if (dlg.exec() == QDialog::Accepted) {
            services::PortfolioService::instance().add_asset(selected_id_, dlg.symbol(), dlg.quantity(), dlg.price());
        }
    });
    connect(order_panel_, &PortfolioOrderPanel::sell_submitted, this, [this]() {
        auto* h = find_holding(selected_symbol_);
        if (!h)
            return;
        SellAssetDialog dlg(h->symbol, h->quantity, this);
        if (dlg.exec() == QDialog::Accepted) {
            services::PortfolioService::instance().sell_asset(selected_id_, h->symbol, dlg.quantity(), dlg.price());
        }
    });

    order_panel_anim_ = new QPropertyAnimation(order_panel_, "geometry", this);
    order_panel_anim_->setDuration(180);
    order_panel_anim_->setEasingCurve(QEasingCurve::OutCubic);

    return w;
}

void PortfolioScreen::update_main_view_data() {
    if (!heatmap_)
        return;

    QString currency = current_summary_.portfolio.currency;

    heatmap_->set_holdings(current_summary_.holdings);
    heatmap_->set_currency(currency);

    perf_chart_->set_summary(current_summary_);
    perf_chart_->set_currency(currency);

    sector_panel_->set_holdings(current_summary_.holdings);
    sector_panel_->set_currency(currency);

    blotter_->set_holdings(current_summary_.holdings);
    if (positions_count_label_)
        positions_count_label_->setText(QString::number(current_summary_.holdings.size()));

    order_panel_->set_currency(currency);

    // Re-select the current symbol
    if (!selected_symbol_.isEmpty()) {
        heatmap_->set_selected_symbol(selected_symbol_);
        blotter_->set_selected_symbol(selected_symbol_);
        order_panel_->set_holding(find_holding(selected_symbol_));
    }
}

void PortfolioScreen::refresh_theme() {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));

    if (command_bar_)
        command_bar_->refresh_theme();
    if (stats_ribbon_)
        stats_ribbon_->refresh_theme();
    if (perf_chart_)
        perf_chart_->refresh_theme();
    if (heatmap_)
        heatmap_->refresh_theme();
    if (blotter_)
        blotter_->refresh_theme();
    if (txn_panel_)
        txn_panel_->refresh_theme();
}

void PortfolioScreen::on_symbol_selected(const QString& symbol) {
    selected_symbol_ = symbol;
    if (heatmap_)
        heatmap_->set_selected_symbol(symbol);
    if (blotter_)
        blotter_->set_selected_symbol(symbol);
    if (order_panel_)
        order_panel_->set_holding(find_holding(symbol));
    if (perf_chart_) {
        if (symbol.isEmpty())
            perf_chart_->clear_focus_symbol();
        else
            perf_chart_->set_focus_symbol(symbol); // emits focus_symbol_period_requested → fetch
    }

    // Publish to the linked group so other panels (Equity Research, Watchlist
    // …) follow the selection. Only when actually linked.
    if (link_group_ != SymbolGroup::None && !symbol.isEmpty()) {
        SymbolContext::instance().set_group_symbol(
            link_group_, SymbolRef::equity(symbol), this);
    }
}

void PortfolioScreen::on_buy_requested() {
    order_panel_visible_ = true;
    order_panel_->set_side("BUY");
    animate_order_panel_in();
}

void PortfolioScreen::open_buy_for(const QString& symbol) {
    // Stash the ticker so the buy_submitted lambda prefills AddAssetDialog,
    // then drive the normal BUY flow. If no portfolio is selected yet there's
    // nothing to add to, but the panel still opens so the user can pick one.
    pending_buy_symbol_ = symbol.trimmed().toUpper();
    on_buy_requested();
}

void PortfolioScreen::on_sell_requested() {
    order_panel_visible_ = true;
    order_panel_->set_side("SELL");
    animate_order_panel_in();
}

void PortfolioScreen::on_order_panel_close() {
    order_panel_visible_ = false;
    order_panel_->setVisible(false);
    pending_buy_symbol_.clear(); // don't leak a paper-buy prefill into a later manual buy
}

// ── Order panel overlay helpers ──────────────────────────────────────────────

void PortfolioScreen::reposition_order_panel() {
    if (!order_panel_ || !order_panel_visible_)
        return;

    const int panel_w = order_panel_->width();
    // Anchor below command bar + stats ribbon; stop above status bar.
    const int top = command_bar_->height() + (stats_ribbon_->isVisible() ? stats_ribbon_->height() : 0);
    const int bottom = status_bar_->isVisible() ? status_bar_->height() : 0;
    const int h = height() - top - bottom;
    order_panel_->setGeometry(width() - panel_w, top, panel_w, h);
    order_panel_->raise();
}

void PortfolioScreen::animate_order_panel_in() {
    if (!order_panel_)
        return;

    const int panel_w = order_panel_->width();
    const int top = command_bar_->height() + (stats_ribbon_->isVisible() ? stats_ribbon_->height() : 0);
    const int bottom = status_bar_->isVisible() ? status_bar_->height() : 0;
    const int h = height() - top - bottom;

    if (!order_panel_->isVisible()) {
        // Start fully off-screen to the right, then slide into place.
        order_panel_->setGeometry(width(), top, panel_w, h);
        order_panel_->setVisible(true);
    }
    order_panel_->raise();

    order_panel_anim_->stop();
    order_panel_anim_->setStartValue(order_panel_->geometry());
    order_panel_anim_->setEndValue(QRect(width() - panel_w, top, panel_w, h));
    order_panel_anim_->start();
}

void PortfolioScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    reposition_order_panel();

    // Keep the insights dock (and its scrim) glued to the right edge when
    // the window is resized.
    if (insights_panel_ && command_bar_) {
        const int top = command_bar_->height();
        const int bottom_reserve = status_bar_ ? status_bar_->height() : 0;
        const int h = qMax(200, height() - top - bottom_reserve);
        if (insights_scrim_ && insights_scrim_->isVisible())
            insights_scrim_->setGeometry(0, top, width(), h);
        if (insights_panel_->isVisible()) {
            insights_panel_->setFixedHeight(h);
            insights_panel_->clamp_width(static_cast<int>(width() * 0.9));
            insights_panel_->move(width() - insights_panel_->width(), top);
        }
    }
}

const portfolio::HoldingWithQuote* PortfolioScreen::find_holding(const QString& symbol) const {
    for (const auto& h : current_summary_.holdings) {
        if (h.symbol == symbol)
            return &h;
    }
    return nullptr;
}

QVariantMap PortfolioScreen::save_state() const {
    return {{"portfolio_id", selected_id_}, {"symbol", selected_symbol_}};
}

void PortfolioScreen::restore_state(const QVariantMap& state) {
    const QString id = state.value("portfolio_id").toString();
    if (!id.isEmpty())
        on_portfolio_selected(id);
    // Symbol focus is intentionally *not* restored — Portfolio always opens
    // on the aggregate HOLDINGS view (showEvent enforces the same on tab
    // switch). Symbol selection is opt-in per session.
}

// ── IGroupLinked ─────────────────────────────────────────────────────────────

SymbolRef PortfolioScreen::current_symbol() const {
    if (selected_symbol_.isEmpty())
        return {};
    return SymbolRef::equity(selected_symbol_);
}

void PortfolioScreen::on_group_symbol_changed(const SymbolRef& ref) {
    if (!ref.is_valid())
        return;
    // Only react if the symbol is actually held — otherwise the group is
    // pointing at a ticker the user can't act on here, and silently
    // selecting a phantom would be misleading.
    if (!find_holding(ref.symbol))
        return;
    if (selected_symbol_ == ref.symbol)
        return;
    selected_symbol_ = ref.symbol;
    if (heatmap_)
        heatmap_->set_selected_symbol(ref.symbol);
    if (blotter_)
        blotter_->set_selected_symbol(ref.symbol);
    if (order_panel_)
        order_panel_->set_holding(find_holding(ref.symbol));
}

} // namespace fincept::screens
