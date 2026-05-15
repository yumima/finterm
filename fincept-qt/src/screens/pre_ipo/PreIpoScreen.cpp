// src/screens/pre_ipo/PreIpoScreen.cpp
#include "screens/pre_ipo/PreIpoScreen.h"

#include "core/logging/Logger.h"
#include "screens/pre_ipo/IpoWatchView.h"
#include "services/pre_ipo/PreIpoService.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::ui;
using namespace fincept::pre_ipo;

// ── Constructor ───────────────────────────────────────────────────────────────

PreIpoScreen::PreIpoScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    auto& svc = services::PreIpoService::instance();
    connect(&svc, &services::PreIpoService::data_loaded, this, &PreIpoScreen::on_data_loaded);
    connect(&svc, &services::PreIpoService::error_occurred, this, &PreIpoScreen::on_error);
    // company_updated wiring removed — the old detail/list panels are gone.
    connect(&ui::ThemeManager::instance(), &ui::ThemeManager::theme_changed,
            this, [this](const ui::ThemeTokens&) { refresh_theme(); });

    refresh_theme();

    LOG_INFO("PreIpo", "Screen constructed");
}

// ── IStatefulScreen ───────────────────────────────────────────────────────────

void PreIpoScreen::restore_state(const QVariantMap& state) {
    // Legacy state had {selected_company, active_tab}. The tab concept is
    // gone (single IpoWatchView), so only the selection key still makes
    // sense — it's currently unused by the consolidated view but kept here
    // so a future per-deal-detail deep-link can resurrect it without
    // breaking persistence.
    selected_company_id_ = state.value("selected_company", "").toString();
}

QVariantMap PreIpoScreen::save_state() const {
    return {
        {"selected_company", selected_company_id_},
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

    // The screen is now a single IpoWatchView — the legacy Picks/Screener/
    // Markets/Pipeline tabs all surfaced the same Nasdaq pipeline data via
    // inconsistent layouts; IpoWatchView consolidates that into one
    // Bloomberg-style screen with lens tabs (Calendar/Performance/Bookrunners)
    // for the same dataset cut different ways. The PreIpoService Python
    // fetchers stay registered but unused — keeping them around lets us
    // re-enable an EDGAR-driven PIPELINE lens later without re-plumbing.
    ipo_watch_view_ = new fincept::screens::widgets::IpoWatchView;
    root->addWidget(ipo_watch_view_, 1);
}

void PreIpoScreen::refresh_theme() {
    setStyleSheet(QString("#preIpoScreen{background:%1;}").arg(colors::BG_BASE()));
}

// ── Legacy slots ─────────────────────────────────────────────────────────────
// PreIpoService still emits signals from the legacy Form D / S-1 / N-PORT
// fetchers, but the IpoWatchView consolidated screen no longer consumes them.
// We keep the slot signatures wired (Q_OBJECT moc requires the declarations
// stay in PreIpoScreen.h) but they're no-ops so future re-introduction of an
// EDGAR-driven PIPELINE lens has somewhere to hook in.

void PreIpoScreen::on_data_loaded(PreIpoSummary summary) {
    Q_UNUSED(summary);
}
void PreIpoScreen::on_company_selected(const QString& id) {
    selected_company_id_ = id;
}
void PreIpoScreen::on_error(const QString& msg) {
    LOG_WARN("PreIpo", msg);
}

} // namespace fincept::screens
