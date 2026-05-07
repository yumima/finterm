// src/screens/pre_ipo/PreIpoScreen.cpp
#include "screens/pre_ipo/PreIpoScreen.h"

#include "core/logging/Logger.h"
#include "screens/pre_ipo/CompanyDetailPanel.h"
#include "screens/pre_ipo/CompanyListPanel.h"
#include "screens/pre_ipo/IpoPipelinePanel.h"
#include "services/pre_ipo/PreIpoService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;

// ── Constructor ───────────────────────────────────────────────────────────────

PreIpoScreen::PreIpoScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    auto& svc = services::PreIpoService::instance();
    connect(&svc, &services::PreIpoService::data_loaded, this, &PreIpoScreen::on_data_loaded);
    connect(&svc, &services::PreIpoService::error_occurred, this, &PreIpoScreen::on_error);
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
}

QVariantMap PreIpoScreen::save_state() const {
    return {{"selected_company", selected_company_id_}};
}

// ── Visibility lifecycle ──────────────────────────────────────────────────────

void PreIpoScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (first_show_) {
        first_show_ = false;
        services::PreIpoService::instance().load_data();
    }
    LOG_INFO("PreIpo", "Screen shown");
}

void PreIpoScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    LOG_INFO("PreIpo", "Screen hidden");
}

// ── Build UI ──────────────────────────────────────────────────────────────────

void PreIpoScreen::build_ui() {
    setObjectName("preIpoScreen");
    setStyleSheet(
        QString("#preIpoScreen { background:%1; }").arg(colors::BG_BASE()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top bar
    root->addWidget(build_top_bar());

    // Three-panel splitter
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet(
        QString("QSplitter::handle { background:%1; }"
                "QSplitter::handle:hover { background:%2; }")
            .arg(colors::BORDER_DIM(), colors::BORDER_MED()));

    list_panel_     = new CompanyListPanel;
    detail_panel_   = new CompanyDetailPanel;
    pipeline_panel_ = new IpoPipelinePanel;

    splitter->addWidget(list_panel_);
    splitter->addWidget(detail_panel_);
    splitter->addWidget(pipeline_panel_);

    // Wide-screen: center (detail) gets most space; sidebars grow proportionally
    splitter->setStretchFactor(0, 1);   // company list
    splitter->setStretchFactor(1, 4);   // detail — most space
    splitter->setStretchFactor(2, 2);   // pipeline / deal flow

    connect(list_panel_, &CompanyListPanel::company_selected,
            this, &PreIpoScreen::on_company_selected);
    connect(detail_panel_, &CompanyDetailPanel::navigate_to_markets,
            this, [](const QString& ticker) {
                LOG_INFO("PreIpo", QString("Navigate to markets: %1").arg(ticker));
                // Emit to app router in a future phase via EventBus
            });

    root->addWidget(splitter, 1);
}

QWidget* PreIpoScreen::build_top_bar() {
    auto* bar = new QWidget;
    bar->setFixedHeight(44);
    bar->setObjectName("preIpoTopBar");
    bar->setStyleSheet(
        QString("#preIpoTopBar { background:%1; border-bottom:2px solid %2; }")
            .arg(colors::BG_SURFACE(), colors::AMBER()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(16, 0, 16, 0);
    hl->setSpacing(12);

    auto* title_lbl = new QLabel("PRE-IPO");
    title_lbl->setStyleSheet(
        QString("color:%1; font-size:15px; font-weight:800; letter-spacing:2px; background:transparent;")
            .arg(colors::AMBER()));
    hl->addWidget(title_lbl);

    auto* divider = new QWidget;
    divider->setFixedSize(1, 20);
    divider->setStyleSheet(
        QString("background:%1;").arg(colors::BORDER_MED()));
    hl->addWidget(divider);

    auto* subtitle = new QLabel("Private Market Tracking");
    subtitle->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_SECONDARY()));
    hl->addWidget(subtitle);

    hl->addStretch();

    status_lbl_ = new QLabel("Loading…");
    status_lbl_->setStyleSheet(
        QString("color:%1; font-size:12px; background:transparent;").arg(colors::TEXT_TERTIARY()));
    hl->addWidget(status_lbl_);

    // Universe count badge
    auto* badge = new QLabel("PRIVATE MARKETS");
    badge->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; background:rgba(217,119,6,0.15);"
                "  border:1px solid rgba(217,119,6,0.35); border-radius:3px; padding:2px 8px;")
            .arg(colors::AMBER()));
    hl->addWidget(badge);

    return bar;
}

void PreIpoScreen::refresh_theme() {
    setStyleSheet(
        QString("#preIpoScreen { background:%1; }").arg(colors::BG_BASE()));
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PreIpoScreen::on_data_loaded(fincept::pre_ipo::PreIpoSummary summary) {
    data_loaded_ = true;

    list_panel_->set_companies(summary.companies);
    pipeline_panel_->set_pipeline(summary.ipo_pipeline);
    pipeline_panel_->set_form_d(summary.recent_form_d);

    status_lbl_->setText(
        QString("%1 companies · Updated %2")
            .arg(summary.companies.size())
            .arg(summary.last_updated.toString("hh:mm")));

    // Restore selected company if state was restored before data
    if (!selected_company_id_.isEmpty()) {
        const auto company = services::PreIpoService::instance().company(selected_company_id_);
        if (!company.id.isEmpty()) {
            detail_panel_->set_company(company);
            list_panel_->set_selected_id(selected_company_id_);
        }
    }

    LOG_INFO("PreIpo", QString("Data loaded: %1 companies").arg(summary.companies.size()));
}

void PreIpoScreen::on_company_selected(const QString& id) {
    selected_company_id_ = id;
    list_panel_->set_selected_id(id);
    const auto company = services::PreIpoService::instance().company(id);
    if (!company.id.isEmpty())
        detail_panel_->set_company(company);
    LOG_INFO("PreIpo", QString("Company selected: %1").arg(id));
}

void PreIpoScreen::on_error(const QString& msg) {
    status_lbl_->setText("Error: " + msg);
    LOG_WARN("PreIpo", msg);
}

} // namespace fincept::screens
