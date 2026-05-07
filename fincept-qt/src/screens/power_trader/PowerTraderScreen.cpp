// src/screens/power_trader/PowerTraderScreen.cpp
#include "screens/power_trader/PowerTraderScreen.h"

#include "screens/power_trader/LeaderboardPanel.h"
#include "screens/power_trader/MemberDetailPanel.h"
#include "screens/power_trader/PowerTraderService.h"
#include "screens/power_trader/TradesFeedPanel.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace fincept::power_trader {

PowerTraderScreen::PowerTraderScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    auto& svc = PowerTraderService::instance();
    connect(&svc, &PowerTraderService::data_loaded,
            this, &PowerTraderScreen::on_data_loaded);
    connect(&svc, &PowerTraderService::error_occurred,
            this, &PowerTraderScreen::on_error_occurred);

    show_loading();
    svc.load_data();
}

// ── UI construction ───────────────────────────────────────────────────────────

void PowerTraderScreen::build_ui() {
    setStyleSheet(QString("QWidget { background:%1; color:%2; }")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────────────────────
    auto* top_bar = new QWidget(this);
    top_bar->setFixedHeight(52);
    top_bar->setStyleSheet(
        QString("QWidget { background:%1; border-bottom:1px solid %2; }")
            .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_MED()));

    auto* top_layout = new QHBoxLayout(top_bar);
    top_layout->setContentsMargins(16, 0, 16, 0);
    top_layout->setSpacing(12);

    // Title group
    auto* title_group = new QWidget(top_bar);
    title_group->setStyleSheet("QWidget { background:transparent; }");
    auto* tg_layout = new QVBoxLayout(title_group);
    tg_layout->setContentsMargins(0, 0, 0, 0);
    tg_layout->setSpacing(1);

    auto* title_label = new QLabel(QStringLiteral("POWER TRADER"), title_group);
    title_label->setStyleSheet(
        QString("QLabel { color:%1; font-size:15px; font-weight:700;"
                " letter-spacing:1.5px; background:transparent; }")
            .arg(ui::colors::AMBER()));
    tg_layout->addWidget(title_label);

    auto* subtitle_label = new QLabel(
        QStringLiteral("Tracking congressional stock disclosures (STOCK Act)"), title_group);
    subtitle_label->setStyleSheet(
        QString("QLabel { color:%1; font-size:10px; background:transparent; }")
            .arg(ui::colors::TEXT_SECONDARY()));
    tg_layout->addWidget(subtitle_label);

    top_layout->addWidget(title_group);
    top_layout->addStretch();

    // Last updated timestamp
    last_updated_label_ = new QLabel(QStringLiteral("—"), top_bar);
    last_updated_label_->setStyleSheet(
        QString("QLabel { color:%1; font-size:10px; background:transparent; }")
            .arg(ui::colors::TEXT_TERTIARY()));
    top_layout->addWidget(last_updated_label_);

    // Refresh button
    auto* refresh_btn = new QPushButton(QStringLiteral("⟳  Refresh"), top_bar);
    refresh_btn->setFixedHeight(28);
    refresh_btn->setCursor(Qt::PointingHandCursor);
    refresh_btn->setStyleSheet(
        QString("QPushButton { background:%1; color:%2; border:1px solid %3;"
                "  border-radius:3px; padding:0 12px; font-size:11px; font-weight:600; }"
                "QPushButton:hover { background:%4; border-color:%2; }"
                "QPushButton:pressed { background:%5; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::AMBER(), ui::colors::AMBER_DIM(),
                 ui::colors::BG_RAISED(), ui::colors::BG_BASE()));
    connect(refresh_btn, &QPushButton::clicked, this, [this]() {
        show_loading();
        PowerTraderService::instance().load_data();
    });
    top_layout->addWidget(refresh_btn);

    root->addWidget(top_bar);

    // ── Content stacker ───────────────────────────────────────────────────────
    stack_ = new QStackedWidget(this);

    // Page 0 — Loading
    loading_pg_ = new QWidget(this);
    {
        auto* ll = new QVBoxLayout(loading_pg_);
        ll->setAlignment(Qt::AlignCenter);
        auto* lbl = new QLabel(QStringLiteral("Loading congressional trade data…"), loading_pg_);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(QString("QLabel { color:%1; font-size:13px; }").arg(ui::colors::TEXT_SECONDARY()));
        ll->addWidget(lbl);
    }
    stack_->addWidget(loading_pg_);

    // Page 1 — Error
    error_pg_ = new QWidget(this);
    {
        auto* el = new QVBoxLayout(error_pg_);
        el->setAlignment(Qt::AlignCenter);
        error_msg_ = new QLabel(QStringLiteral("Error"), error_pg_);
        error_msg_->setAlignment(Qt::AlignCenter);
        error_msg_->setWordWrap(true);
        error_msg_->setStyleSheet(
            QString("QLabel { color:%1; font-size:13px; max-width:400px; }")
                .arg(ui::colors::NEGATIVE()));
        el->addWidget(error_msg_);

        auto* retry_btn = new QPushButton(QStringLiteral("Retry"), error_pg_);
        retry_btn->setFixedSize(120, 32);
        retry_btn->setCursor(Qt::PointingHandCursor);
        retry_btn->setStyleSheet(
            QString("QPushButton { background:%1; color:%2; border:1px solid %3;"
                    "  border-radius:3px; font-size:12px; }"
                    "QPushButton:hover { background:%4; }")
                .arg(ui::colors::BG_SURFACE(), ui::colors::AMBER(), ui::colors::AMBER_DIM(),
                     ui::colors::BG_RAISED()));
        connect(retry_btn, &QPushButton::clicked, this, [this]() {
            show_loading();
            PowerTraderService::instance().load_data();
        });
        el->addWidget(retry_btn, 0, Qt::AlignCenter);
    }
    stack_->addWidget(error_pg_);

    // Page 2 — Content
    content_pg_ = new QWidget(this);
    {
        auto* cl = new QVBoxLayout(content_pg_);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(0);

        splitter_ = new QSplitter(Qt::Horizontal, content_pg_);
        splitter_->setHandleWidth(1);
        splitter_->setStyleSheet(
            QString("QSplitter::handle { background:%1; }").arg(ui::colors::BORDER_DIM()));

        leaderboard_ = new fincept::screens::LeaderboardPanel(splitter_);
        feed_        = new fincept::screens::TradesFeedPanel(splitter_);
        detail_      = new fincept::screens::MemberDetailPanel(splitter_);

        splitter_->addWidget(leaderboard_);
        splitter_->addWidget(feed_);
        splitter_->addWidget(detail_);

        // Set initial sizes: 250 / flex / 300
        splitter_->setSizes({250, 9999, 300});
        splitter_->setStretchFactor(0, 0);
        splitter_->setStretchFactor(1, 1);
        splitter_->setStretchFactor(2, 0);

        cl->addWidget(splitter_);
    }
    stack_->addWidget(content_pg_);

    root->addWidget(stack_, 1);

    // ── Wire panel cross-selection signals ────────────────────────────────────
    connect(leaderboard_, &fincept::screens::LeaderboardPanel::member_selected,
            this, &PowerTraderScreen::on_member_selected);
    connect(feed_, &fincept::screens::TradesFeedPanel::member_selected,
            this, &PowerTraderScreen::on_member_selected);
}

// ── State transitions ─────────────────────────────────────────────────────────

void PowerTraderScreen::show_loading() {
    stack_->setCurrentIndex(0);
}

void PowerTraderScreen::show_error(const QString& msg) {
    error_msg_->setText(msg);
    stack_->setCurrentIndex(1);
}

void PowerTraderScreen::show_content() {
    stack_->setCurrentIndex(2);
}

void PowerTraderScreen::update_timestamp() {
    if (!summary_.loaded || !summary_.last_updated.isValid()) {
        last_updated_label_->setText(QStringLiteral("—"));
        return;
    }
    last_updated_label_->setText(
        QStringLiteral("Updated ") +
        summary_.last_updated.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
}

// ── Slot handlers ─────────────────────────────────────────────────────────────

void PowerTraderScreen::on_data_loaded(PowerTraderSummary summary) {
    summary_ = summary;
    update_timestamp();

    leaderboard_->set_members(summary_.members);
    feed_->set_trades(summary_.recent_trades);
    detail_->clear();

    show_content();
}

void PowerTraderScreen::on_error_occurred(QString error) {
    show_error(error.isEmpty()
                   ? QStringLiteral("Failed to load congressional trade data.\nPlease try again.")
                   : error);
}

void PowerTraderScreen::on_member_selected(const QString& member_id) {
    // Find member struct
    const auto& members = summary_.members;
    auto it = std::find_if(members.begin(), members.end(),
                           [&](const CongressMember& m) { return m.id == member_id; });
    if (it == members.end())
        return;

    leaderboard_->set_selected(member_id);
    feed_->set_selected_member(member_id);

    const auto trades = PowerTraderService::instance().trades_for_member(member_id);
    detail_->set_member(*it, trades);
}

} // namespace fincept::power_trader
