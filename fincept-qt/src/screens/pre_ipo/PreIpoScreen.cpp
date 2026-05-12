// src/screens/pre_ipo/PreIpoScreen.cpp
#include "screens/pre_ipo/PreIpoScreen.h"

#include "core/logging/Logger.h"
#include "screens/pre_ipo/CompanyDetailPanel.h"
#include "screens/pre_ipo/CompanyListPanel.h"
#include "screens/pre_ipo/IpoPipelinePanel.h"
#include "screens/pre_ipo/views/PicksView.h"
#include "screens/pre_ipo/views/PipelineView.h"
#include "screens/pre_ipo/views/ScreenerView.h"
#include "services/pre_ipo/PreIpoService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

namespace {
constexpr int kTabPicks    = 0;
constexpr int kTabScreener = 1;
constexpr int kTabMarkets  = 2;
constexpr int kTabPipeline = 3;
} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

PreIpoScreen::PreIpoScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    auto& svc = services::PreIpoService::instance();
    connect(&svc, &services::PreIpoService::data_loaded, this, &PreIpoScreen::on_data_loaded);
    connect(&svc, &services::PreIpoService::error_occurred, this, &PreIpoScreen::on_error);
    connect(&svc, &services::PreIpoService::progress, this, [this](const QString& msg) {
        if (status_lbl_) status_lbl_->setText(msg);
    });
    connect(&svc, &services::PreIpoService::company_updated, this, [this](const QString& id) {
        if (id == selected_company_id_) {
            auto company = services::PreIpoService::instance().company(id);
            detail_panel_->set_company(company);
        }
        list_panel_->set_companies(services::PreIpoService::instance().companies());
    });

    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed,
            this, [this](const ui::ThemeTokens&) { refresh_theme(); });

    refresh_theme();

    LOG_INFO("PreIpo", "Screen constructed");
}

// ── IStatefulScreen ───────────────────────────────────────────────────────────

void PreIpoScreen::restore_state(const QVariantMap& state) {
    selected_company_id_ = state.value("selected_company", "").toString();
    const int tab = state.value("active_tab", 0).toInt();
    if (tab >= 0 && tab < tab_btns_.size()) {
        active_tab_ = tab;
        switch_tab(tab);
    }
}

QVariantMap PreIpoScreen::save_state() const {
    return {
        {"selected_company", selected_company_id_},
        {"active_tab",        active_tab_},
    };
}

// ── Visibility lifecycle ──────────────────────────────────────────────────────

void PreIpoScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (first_show_) {
        first_show_ = false;
        services::PreIpoService::instance().load_data();
    }
}

void PreIpoScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
}

// ── Build UI ──────────────────────────────────────────────────────────────────

void PreIpoScreen::build_ui() {
    setObjectName("preIpoScreen");
    setStyleSheet(QString("#preIpoScreen{background:%1;}").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(build_top_bar());

    // ── Tab bar ───────────────────────────────────────────────────────────────
    auto* tab_bar = new QWidget;
    tab_bar->setFixedHeight(40);
    tab_bar->setObjectName("preIpoTabs");
    tab_bar->setStyleSheet(
        QString("#preIpoTabs{background:%1;border-bottom:1px solid %2;}")
            .arg(colors::BG_SURFACE(), colors::BORDER_DIM()));
    auto* tabs_l = new QHBoxLayout(tab_bar);
    tabs_l->setContentsMargins(16, 0, 16, 0);
    tabs_l->setSpacing(4);

    const QStringList tab_labels{"Picks", "Screener", "Markets", "Pipeline"};
    const QStringList tab_subs{"Pick", "Scan", "Research", "Scan / Pick"};
    for (int i = 0; i < tab_labels.size(); ++i) {
        auto* b = new QPushButton(tab_labels[i]);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(28);
        b->setToolTip(tab_subs[i]);
        b->setStyleSheet(
            QString("QPushButton{background:transparent;color:%1;border:none;"
                    "  padding:0 14px;font-size:12px;font-weight:700;letter-spacing:1px;"
                    "  border-bottom:2px solid transparent;}"
                    "QPushButton:checked{color:%2;border-bottom-color:%2;}"
                    "QPushButton:hover:!checked{color:%3;}")
                .arg(colors::TEXT_SECONDARY(), colors::AMBER(), colors::TEXT_PRIMARY()));
        const int idx = i;
        connect(b, &QPushButton::clicked, this, [this, idx]() { switch_tab(idx); });
        tab_btns_.append(b);
        tabs_l->addWidget(b);
    }
    tabs_l->addStretch();
    root->addWidget(tab_bar);

    // ── Stack ─────────────────────────────────────────────────────────────────
    stack_ = new QStackedWidget;

    picks_view_ = new PicksView;
    connect(picks_view_, &PicksView::company_selected, this, [this](const QString& id) {
        on_company_selected(id);
        switch_tab(kTabMarkets);
    });
    stack_->addWidget(picks_view_);

    screener_view_ = new ScreenerView;
    connect(screener_view_, &ScreenerView::company_selected, this, [this](const QString& id) {
        on_company_selected(id);
        switch_tab(kTabMarkets);
    });
    stack_->addWidget(screener_view_);

    stack_->addWidget(build_markets_tab());

    pipeline_view_ = new PipelineView;
    connect(pipeline_view_, &PipelineView::company_selected, this, [this](const QString& id) {
        on_company_selected(id);
        switch_tab(kTabMarkets);
    });
    stack_->addWidget(pipeline_view_);

    root->addWidget(stack_, 1);

    // Initial tab
    switch_tab(kTabPicks);
}

QWidget* PreIpoScreen::build_markets_tab() {
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet(
        QString("QSplitter::handle{background:%1;}"
                "QSplitter::handle:hover{background:%2;}")
            .arg(colors::BORDER_DIM(), colors::BORDER_MED()));

    list_panel_     = new CompanyListPanel;
    detail_panel_   = new CompanyDetailPanel;
    pipeline_panel_ = new IpoPipelinePanel;

    splitter->addWidget(list_panel_);
    splitter->addWidget(detail_panel_);
    splitter->addWidget(pipeline_panel_);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    splitter->setStretchFactor(2, 2);

    connect(list_panel_, &CompanyListPanel::company_selected,
            this, &PreIpoScreen::on_company_selected);
    connect(detail_panel_, &CompanyDetailPanel::navigate_to_markets,
            this, [](const QString& ticker) {
                LOG_INFO("PreIpo", QString("Navigate to markets: %1").arg(ticker));
            });
    return splitter;
}

QWidget* PreIpoScreen::build_top_bar() {
    auto* bar = new QWidget;
    bar->setFixedHeight(44);
    bar->setObjectName("preIpoTopBar");
    bar->setStyleSheet(
        QString("#preIpoTopBar{background:%1;border-bottom:2px solid %2;}")
            .arg(colors::BG_SURFACE(), colors::AMBER()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);
    hl->setSpacing(12);

    auto* title = new QLabel("PRE-IPO TERMINAL");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:800;letter-spacing:2px;background:transparent;")
            .arg(colors::AMBER()));
    hl->addWidget(title);

    auto* divider = new QWidget;
    divider->setFixedSize(1, 20);
    divider->setStyleSheet(QString("background:%1;").arg(colors::BORDER_MED()));
    hl->addWidget(divider);

    auto* subtitle = new QLabel("Scan · Research · Pick — SEC EDGAR + Mutual Fund N-PORT marks");
    subtitle->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    hl->addWidget(subtitle);

    hl->addStretch();

    status_lbl_ = new QLabel("Loading…");
    status_lbl_->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(colors::TEXT_SECONDARY()));
    hl->addWidget(status_lbl_);

    auto* badge = new QLabel("PRIVATE MARKETS");
    badge->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:rgba(217,119,6,0.15);"
                "  border:1px solid rgba(217,119,6,0.35);border-radius:3px;padding:2px 8px;")
            .arg(colors::AMBER()));
    hl->addWidget(badge);

    return bar;
}

void PreIpoScreen::switch_tab(int idx) {
    if (idx < 0 || idx >= tab_btns_.size()) return;
    active_tab_ = idx;
    for (int i = 0; i < tab_btns_.size(); ++i) tab_btns_[i]->setChecked(i == idx);
    if (stack_) stack_->setCurrentIndex(idx);
}

void PreIpoScreen::refresh_theme() {
    setStyleSheet(QString("#preIpoScreen{background:%1;}").arg(colors::BG_BASE()));
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PreIpoScreen::on_data_loaded(PreIpoSummary summary) {
    data_loaded_ = true;
    last_summary_ = summary;

    list_panel_->set_companies(summary.companies);
    pipeline_panel_->set_pipeline(summary.ipo_pipeline);
    pipeline_panel_->set_form_d(summary.recent_form_d);
    picks_view_->set_summary(summary);
    screener_view_->set_companies(summary.companies);
    pipeline_view_->set_pipeline(summary.ipo_pipeline, summary.companies);

    status_lbl_->setText(
        QString("%1 companies · %2 pipeline filers · %3 signals · Updated %4")
            .arg(summary.companies.size())
            .arg(summary.ipo_pipeline.size())
            .arg(summary.signal_list.size())
            .arg(summary.last_updated.toString("hh:mm")));

    if (!selected_company_id_.isEmpty()) {
        const auto company = services::PreIpoService::instance().company(selected_company_id_);
        if (!company.id.isEmpty()) {
            detail_panel_->set_company(company);
            list_panel_->set_selected_id(selected_company_id_);
        }
    }

    LOG_INFO("PreIpo",
             QString("Data loaded: %1 companies, %2 pipeline").arg(summary.companies.size())
                 .arg(summary.ipo_pipeline.size()));
}

void PreIpoScreen::on_company_selected(const QString& id) {
    selected_company_id_ = id;
    list_panel_->set_selected_id(id);
    const auto company = services::PreIpoService::instance().company(id);
    if (!company.id.isEmpty()) detail_panel_->set_company(company);
}

void PreIpoScreen::on_error(const QString& msg) {
    status_lbl_->setText("Error: " + msg);
    LOG_WARN("PreIpo", msg);
}

} // namespace fincept::screens
