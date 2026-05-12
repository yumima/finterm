// src/screens/power_trader/PowerTraderScreen.cpp
#include "screens/power_trader/PowerTraderScreen.h"

#include "screens/power_trader/CabinetPanel.h"
#include "screens/power_trader/CommitteePanel.h"
#include "screens/power_trader/CompareView.h"
#include "screens/power_trader/DataSourceDialog.h"
#include "screens/power_trader/InsiderWatchPanel.h"
#include "screens/power_trader/PracticePanel.h"
#include "screens/power_trader/SignalBuilderPanel.h"
#include "screens/power_trader/MemberProfilePanel.h"
#include "screens/power_trader/OverviewPanel.h"
#include "screens/power_trader/PartyPanel.h"
#include "screens/power_trader/PowerTraderService.h"
#include "screens/power_trader/RankingsPanel.h"
#include "screens/power_trader/TradesFeedPanel.h"
#include "ui/theme/Theme.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMenu>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::power_trader {

PowerTraderScreen::PowerTraderScreen(QWidget* parent) : QWidget(parent) {
    load_watchlist();
    load_ticker_watchlist();
    build_ui();
    auto& svc = PowerTraderService::instance();
    connect(&svc, &PowerTraderService::data_loaded,    this, &PowerTraderScreen::on_data_loaded);
    connect(&svc, &PowerTraderService::error_occurred, this, &PowerTraderScreen::on_error);
}

void PowerTraderScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);

    // First-run: prompt for the user's Congress.gov API key if they haven't
    // entered one yet. The key unlocks the rich live data set; if the user
    // skips, the script still works with senate.gov fallback + KNOWN_MEMBERS.
    // Only ask once per process so re-showing the screen doesn't re-prompt.
    static bool shown_key_dialog_ = false;
    if (!shown_key_dialog_ && !DataSourceDialog::has_key()) {
        shown_key_dialog_ = true;
        DataSourceDialog dlg(this);
        dlg.exec();  // ignore return — both Save and Skip are valid paths
    }

    // First-run onboarding overlay — show once, then never again. Flag is
    // persisted under QSettings power_trader/onboarded.
    static bool shown_onboarding_ = false;
    if (!shown_onboarding_ &&
        !QSettings().value(QStringLiteral("power_trader/onboarded"), false).toBool()) {
        shown_onboarding_ = true;
        QTimer::singleShot(150, this, [this]() { show_onboarding_overlay(); });
    }

    if (!PowerTraderService::instance().is_loaded()) {
        show_loading();
        PowerTraderService::instance().load_data();
    } else {
        PowerTraderService::instance().load_data();
    }
}
void PowerTraderScreen::hideEvent(QHideEvent* e) { QWidget::hideEvent(e); }

// ── UI construction ───────────────────────────────────────────────────────────

void PowerTraderScreen::build_ui() {
    setStyleSheet(QString("background:%1; color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(build_top_bar());
    root->addWidget(build_body_strip());

    stack_ = new QStackedWidget;

    // Page 0 — skeleton-style loading. Mirrors the production layout shape
    // (sidebar column, tab strip, content grid) with muted placeholder blocks
    // and a subtle pulse animation so the wait is less jarring than a bare
    // "Loading…" label. The Senate scrape can take 30–180s; the visual cue
    // helps users understand the screen is working, not frozen.
    auto* loading_page = new QWidget;
    loading_page->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    {
        auto* outer = new QVBoxLayout(loading_page);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // Faux tab strip
        auto* tabs = new QWidget(loading_page);
        tabs->setFixedHeight(36);
        tabs->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* tl = new QHBoxLayout(tabs);
        tl->setContentsMargins(8, 8, 8, 0);
        tl->setSpacing(6);
        for (int i = 0; i < 7; ++i) {
            auto* placeholder = new QWidget(tabs);
            placeholder->setFixedSize(80, 22);
            placeholder->setStyleSheet(
                QString("background:%1; border-radius:2px;").arg(ui::colors::BG_RAISED()));
            tl->addWidget(placeholder);
        }
        tl->addStretch();
        outer->addWidget(tabs);

        // Faux sidebar + content
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);

        // Sidebar shape
        auto* side = new QWidget(loading_page);
        side->setFixedWidth(240);
        side->setStyleSheet(QString("background:%1; border-right:1px solid %2;")
                                .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
        auto* sl = new QVBoxLayout(side);
        sl->setContentsMargins(10, 10, 10, 10);
        sl->setSpacing(8);
        for (int i = 0; i < 12; ++i) {
            auto* line = new QWidget(side);
            line->setFixedHeight(18);
            line->setStyleSheet(
                QString("background:%1; border-radius:2px;").arg(ui::colors::BG_RAISED()));
            sl->addWidget(line);
        }
        sl->addStretch();
        row->addWidget(side);

        // Content area shape
        auto* content = new QWidget(loading_page);
        content->setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
        auto* cl = new QVBoxLayout(content);
        cl->setContentsMargins(24, 24, 24, 24);
        cl->setSpacing(14);

        loading_lbl_ = new QLabel(
            QStringLiteral("Fetching congressional disclosures from Senate eFD + House FDS\xe2\x80\xa6\n"
                           "Cold start: 30\xe2\x80\x9360 seconds. Warm cache: instant."));
        loading_lbl_->setAlignment(Qt::AlignLeft);
        loading_lbl_->setStyleSheet(
            QString("color:%1; font-size:12px;").arg(ui::colors::TEXT_SECONDARY()));
        cl->addWidget(loading_lbl_);

        for (int i = 0; i < 6; ++i) {
            auto* bar = new QWidget(content);
            bar->setFixedHeight(40);
            bar->setStyleSheet(
                QString("background:%1; border-radius:2px;").arg(ui::colors::BG_SURFACE()));
            cl->addWidget(bar);
        }
        cl->addStretch();
        row->addWidget(content, 1);

        outer->addLayout(row, 1);
    }
    stack_->addWidget(loading_page);

    // Page 1 — error
    auto* error_page = new QWidget;
    {
        auto* el = new QVBoxLayout(error_page);
        el->setAlignment(Qt::AlignCenter);
        el->setSpacing(14);
        el->addStretch();

        error_lbl_ = new QLabel;
        error_lbl_->setAlignment(Qt::AlignCenter);
        error_lbl_->setWordWrap(true);
        error_lbl_->setStyleSheet(
            QString("color:%1; font-size:13px; max-width:500px;")
                .arg(ui::colors::TEXT_SECONDARY()));
        el->addWidget(error_lbl_);

        // Action buttons below the error message
        auto* btn_row = new QHBoxLayout;
        btn_row->setSpacing(10);
        btn_row->setAlignment(Qt::AlignCenter);

        auto* retry_btn = new QPushButton("↻  Retry");
        retry_btn->setCursor(Qt::PointingHandCursor);
        retry_btn->setStyleSheet(
            QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                    "border-radius:3px;padding:8px 18px;font-size:12px;font-weight:600;}"
                    "QPushButton:hover{border-color:%1;}")
                .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
        connect(retry_btn, &QPushButton::clicked, this, [this]() {
            show_loading();
            PowerTraderService::instance().load_data();
        });
        btn_row->addWidget(retry_btn);

        el->addLayout(btn_row);
        el->addStretch();
    }
    stack_->addWidget(error_page);

    // Page 2 — content: view_stack_ switches between Congress and Cabinet views
    content_area_ = new QWidget;
    {
        auto* hl = new QHBoxLayout(content_area_);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);

        view_stack_ = new QStackedWidget(content_area_);

        // ── Page 0: Congress view (sidebar + tabs) ────────────────────────────
        congress_view_ = new QWidget(view_stack_);
        {
            auto* cl = new QHBoxLayout(congress_view_);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setSpacing(0);
            cl->addWidget(build_member_sidebar());

            tab_widget_ = new QTabWidget;
            tab_widget_->setDocumentMode(true);
            tab_widget_->setStyleSheet(QString(R"(
                QTabWidget::pane { border:0; background:%1; }
                QTabBar::tab {
                    background:%2; color:%3; padding:8px 16px;
                    border:0; border-bottom:2px solid transparent;
                    font-size:13px; font-weight:700; letter-spacing:1px;
                }
                QTabBar::tab:selected { color:%4; border-bottom:2px solid %4; }
                QTabBar::tab:hover:!selected { color:%5; }
            )").arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(),
                    ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(),
                    ui::colors::TEXT_PRIMARY()));

            overview_panel_  = new screens::OverviewPanel;
            rankings_panel_  = new screens::RankingsPanel;
            member_panel_    = new screens::MemberProfilePanel;
            feed_panel_      = new screens::TradesFeedPanel;
            committee_panel_ = new screens::CommitteePanel;
            party_panel_     = new screens::PartyPanel;
            insider_panel_   = new screens::InsiderWatchPanel;
            signal_panel_    = new screens::SignalBuilderPanel;
            practice_panel_  = new screens::PracticePanel;

            tab_widget_->addTab(overview_panel_,  "Overview");
            tab_widget_->addTab(rankings_panel_,  "Rankings");
            // Member tab removed — member_panel_ is shown as a slide-in
            // drawer (build_member_drawer()) so member selection doesn't
            // force-navigate away from the user's current tab context.
            tab_widget_->addTab(feed_panel_,      "Feed");
            tab_widget_->addTab(committee_panel_, "By Committee");
            tab_widget_->addTab(party_panel_,     "Party Intel");
            tab_widget_->addTab(insider_panel_,   "⚠ Insider Watch");

            // Signal Builder + Practice are folded into a single tab with a
            // sub-tab bar — both are research/strategy-builder views and the
            // top-level tab strip was getting crowded. Sub-tab bar is a
            // simple QPushButton row driving a QStackedWidget.
            sb_practice_tab_ = new QWidget;
            {
                auto* spl = new QVBoxLayout(sb_practice_tab_);
                spl->setContentsMargins(0, 0, 0, 0);
                spl->setSpacing(0);

                auto* sub_bar = new QWidget(sb_practice_tab_);
                sub_bar->setFixedHeight(30);
                sub_bar->setStyleSheet(
                    QString("background:%1; border-bottom:1px solid %2;")
                        .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
                auto* sbl = new QHBoxLayout(sub_bar);
                sbl->setContentsMargins(10, 0, 10, 0);
                sbl->setSpacing(2);

                sb_practice_stack_ = new QStackedWidget(sb_practice_tab_);
                sb_practice_stack_->addWidget(signal_panel_);
                sb_practice_stack_->addWidget(practice_panel_);

                const QString pill_ss =
                    QString("QPushButton{background:transparent;color:%1;"
                            "border:1px solid %2;border-radius:2px;"
                            "padding:2px 14px;font-size:12px;font-weight:700;"
                            "letter-spacing:0.5px;}"
                            "QPushButton:checked{background:%3;color:%4;"
                            "border-color:%3;}"
                            "QPushButton:hover:!checked{border-color:%5;}")
                        .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                             ui::colors::AMBER_DIM(),      ui::colors::AMBER(),
                             ui::colors::TEXT_SECONDARY());

                auto* builder_btn = new QPushButton(QStringLiteral("BUILDER"), sub_bar);
                builder_btn->setCheckable(true);
                builder_btn->setChecked(true);
                builder_btn->setCursor(Qt::PointingHandCursor);
                builder_btn->setStyleSheet(pill_ss);
                sbl->addWidget(builder_btn);

                auto* practice_btn = new QPushButton(QStringLiteral("PRACTICE"), sub_bar);
                practice_btn->setCheckable(true);
                practice_btn->setCursor(Qt::PointingHandCursor);
                practice_btn->setStyleSheet(pill_ss);
                sbl->addWidget(practice_btn);

                sbl->addStretch();

                auto* sub_group = new QButtonGroup(sb_practice_tab_);
                sub_group->setExclusive(true);
                sub_group->addButton(builder_btn, 0);
                sub_group->addButton(practice_btn, 1);
                connect(sub_group, &QButtonGroup::idClicked,
                        sb_practice_stack_, &QStackedWidget::setCurrentIndex);
                // Reverse direction — keep the pill checked-state in sync if
                // the stack flips programmatically (e.g. navigate-to-builder).
                connect(sb_practice_stack_, &QStackedWidget::currentChanged,
                        this, [builder_btn, practice_btn](int idx) {
                            (idx == 0 ? builder_btn : practice_btn)->setChecked(true);
                        });

                spl->addWidget(sub_bar);
                spl->addWidget(sb_practice_stack_, 1);
            }
            tab_widget_->addTab(sb_practice_tab_, "Signal Builder");

            compare_view_ = new screens::CompareView;
            tab_widget_->addTab(compare_view_, "Compare");
            connect(compare_view_, &screens::CompareView::member_selected,
                    this, &PowerTraderScreen::on_member_selected);

            connect(overview_panel_,  &screens::OverviewPanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(rankings_panel_,  &screens::RankingsPanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(feed_panel_,      &screens::TradesFeedPanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(feed_panel_,      &screens::TradesFeedPanel::ticker_subscription_toggled,
                    this, &PowerTraderScreen::toggle_ticker_watchlist);
            feed_panel_->set_member_watchlist(watchlist_);
            feed_panel_->set_ticker_watchlist(ticker_watchlist_);
            connect(committee_panel_, &screens::CommitteePanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(insider_panel_,   &screens::InsiderWatchPanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(practice_panel_, &screens::PracticePanel::navigate_to_signal_builder,
                    this, [this]() {
                        // After folding, Signal Builder and Practice share one
                        // tab. Switch to that tab and flip the sub-stack to
                        // the BUILDER sub-page (index 0).
                        tab_widget_->setCurrentWidget(sb_practice_tab_);
                        if (sb_practice_stack_) sb_practice_stack_->setCurrentIndex(0);
                    });
            connect(practice_panel_, &screens::PracticePanel::preset_requested,
                    signal_panel_,    &screens::SignalBuilderPanel::on_preset_clicked);
            connect(signal_panel_,    &screens::SignalBuilderPanel::member_selected,
                    this, &PowerTraderScreen::on_member_selected);
            connect(member_panel_,    &screens::MemberProfilePanel::navigate_to_markets,
                    this, [this](const QString& ticker) {
                        emit navigate_to_screen(QStringLiteral("markets"), ticker);
                    });

            cl->addWidget(tab_widget_, 1);

            // Member-detail drawer overlay (replaces former Member tab).
            // Built AFTER tab_widget_ is added so we can attach an event filter
            // to track its geometry and keep the drawer in sync on resize.
            build_member_drawer();
        }
        view_stack_->addWidget(congress_view_);  // index 0

        // ── Page 1: Cabinet view (full-width, no congress sidebar) ────────────
        cabinet_panel_ = new screens::CabinetPanel(view_stack_);
        view_stack_->addWidget(cabinet_panel_);  // index 1

        hl->addWidget(view_stack_, 1);
    }
    stack_->addWidget(content_area_);

    root->addWidget(stack_, 1);
    show_loading();
}

QWidget* PowerTraderScreen::build_top_bar() {
    auto* bar = new QWidget;
    bar->setFixedHeight(62);
    bar->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* vl = new QVBoxLayout(bar);
    vl->setContentsMargins(14, 6, 14, 4);
    vl->setSpacing(3);

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(10);

    auto* title = new QLabel("POWER TRADER");
    title->setStyleSheet(QString("color:%1; font-size:13px; font-weight:700; "
                                 "letter-spacing:2px;").arg(ui::colors::AMBER()));

    auto* sub = new QLabel("Congressional Stock Disclosures · STOCK Act");
    sub->setStyleSheet(QString("color:%1; font-size:12px;")
                           .arg(ui::colors::TEXT_SECONDARY()));

    timestamp_lbl_ = new QLabel;
    timestamp_lbl_->setStyleSheet(QString("color:%1; font-size:12px;")
                                       .arg(ui::colors::TEXT_SECONDARY()));

    refresh_btn_ = new QPushButton("\xe2\x9f\xb3 Refresh");
    refresh_btn_->setFixedHeight(22);
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;padding:1px 8px;font-size:12px;font-weight:600;}"
                "QPushButton:hover{border-color:%1;background:%3;}")
            .arg(ui::colors::AMBER(), ui::colors::BORDER_DIM(), ui::colors::BG_RAISED()));
    connect(refresh_btn_, &QPushButton::clicked, this, [this]() {
        show_loading();
        PowerTraderService::instance().load_data();
    });

    row1->addWidget(title);
    row1->addWidget(sub);
    row1->addStretch();
    row1->addWidget(timestamp_lbl_);

    row1->addWidget(refresh_btn_);

    auto* row2 = new QHBoxLayout;
    row2->setSpacing(0);

    auto* src_lbl = new QLabel("Data:");
    src_lbl->setStyleSheet(QString("color:%1; font-size:12px;")
                               .arg(ui::colors::TEXT_SECONDARY()));
    row2->addWidget(src_lbl);

    struct Source { const char* label; const char* url; };
    static const Source sources[] = {
        {"Senate eFTS",      "https://efts.senate.gov/"},
        {"House FDS",        "https://disclosures-clerk.house.gov/"},
        {"Congress.gov API", "https://api.congress.gov/"},
        {"OGE Form 278",     "https://www.oge.gov/web/oge.nsf/Public+Financial+Disclosure+Reports"},
    };
    const QString btn_ss =
        QString("QPushButton{color:%1;font-size:12px;text-decoration:underline;"
                "background:transparent;border:none;padding:0;margin:0;}"
                "QPushButton:hover{color:%2;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER());

    bool first = true;
    for (const auto& src : sources) {
        if (!first) {
            auto* sep = new QLabel("\xc2\xb7");
            sep->setStyleSheet(QString("color:%1; font-size:12px; padding:0 5px;")
                                   .arg(ui::colors::BORDER_BRIGHT()));
            row2->addWidget(sep);
        }
        first = false;
        auto* btn = new QPushButton(src.label);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        btn->setStyleSheet(btn_ss);
        const QString url = QString::fromLatin1(src.url);
        connect(btn, &QPushButton::clicked, this, [url]() {
            QDesktopServices::openUrl(QUrl(url));
        });
        row2->addWidget(btn);
    }
    row2->addStretch();

    vl->addLayout(row1);
    vl->addLayout(row2);
    return bar;
}

QWidget* PowerTraderScreen::build_body_strip() {
    auto* strip = new QWidget;
    strip->setFixedHeight(34);
    strip->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                             .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(strip);
    hl->setContentsMargins(10, 4, 14, 4);
    hl->setSpacing(4);

    auto* lbl = new QLabel("VIEW:");
    lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:0.5px;")
                           .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(lbl);

    struct Btn { const char* label; BodyFilter filter; };
    const Btn btns[] = {
        {"ALL",     BodyFilter::All},
        {"SENATE",  BodyFilter::Senate},
        {"HOUSE",   BodyFilter::House},
        {"CABINET", BodyFilter::Cabinet},
    };

    body_btn_group_ = new QButtonGroup(this);
    body_btn_group_->setExclusive(true);

    const QString pill_base =
        QString("QPushButton{background:%1;color:%2;border:1px solid %3;"
                "border-radius:2px;padding:3px 12px;font-size:12px;font-weight:700;"
                "letter-spacing:0.5px;}"
                "QPushButton:checked{background:%4;color:%5;border-color:%4;}"
                "QPushButton:hover:!checked{border-color:%2;}")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY(),
                 ui::colors::BORDER_DIM(), ui::colors::AMBER_DIM(), ui::colors::AMBER());

    for (const auto& b : btns) {
        auto* btn = new QPushButton(b.label);
        btn->setCheckable(true);
        btn->setStyleSheet(pill_base);
        btn->setCursor(Qt::PointingHandCursor);
        const BodyFilter bf = b.filter;
        connect(btn, &QPushButton::clicked, this, [this, bf]() {
            on_body_filter_changed(bf);
        });
        body_btn_group_->addButton(btn);
        hl->addWidget(btn);
        if (b.filter == BodyFilter::All)
            btn->setChecked(true);
    }

    // Visual separator between VIEW and RANGE
    auto* gap = new QLabel("\xc2\xa0\xc2\xa0\xe2\x94\x82\xc2\xa0\xc2\xa0");  //  ·│·
    gap->setStyleSheet(QString("color:%1;").arg(ui::colors::BORDER_DIM()));
    hl->addWidget(gap);

    // ── RANGE pills — drives PowerTraderService::set_days_back ───────────────
    auto* range_lbl = new QLabel("RANGE:");
    range_lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; letter-spacing:0.5px;")
                                 .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(range_lbl);

    struct Range { const char* label; int days; };
    const Range ranges[] = {
        {"30d", 30}, {"90d", 90}, {"1y", 365}, {"2y", 730},
    };
    range_btn_group_ = new QButtonGroup(this);
    range_btn_group_->setExclusive(true);
    const int active_days = power_trader::PowerTraderService::instance().days_back();
    for (const auto& r : ranges) {
        auto* btn = new QPushButton(r.label);
        btn->setCheckable(true);
        btn->setStyleSheet(pill_base);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(QStringLiteral("Show trades disclosed within the last %1 days").arg(r.days));
        const int days = r.days;
        connect(btn, &QPushButton::clicked, this, [this, days]() {
            on_range_changed(days);
        });
        range_btn_group_->addButton(btn);
        hl->addWidget(btn);
        if (active_days == days) btn->setChecked(true);
    }

    // Second separator + WATCHLIST filter (persisted star list).
    auto* gap2 = new QLabel("\xc2\xa0\xc2\xa0\xe2\x94\x82\xc2\xa0\xc2\xa0");
    gap2->setStyleSheet(QString("color:%1;").arg(ui::colors::BORDER_DIM()));
    hl->addWidget(gap2);

    watchlist_filter_ = new QPushButton(QStringLiteral("\xe2\x98\x85 WATCHLIST"));  // ★
    watchlist_filter_->setCheckable(true);
    watchlist_filter_->setStyleSheet(pill_base);
    watchlist_filter_->setCursor(Qt::PointingHandCursor);
    watchlist_filter_->setToolTip(
        QStringLiteral("Show only members you've added to your watchlist "
                       "(right-click a member to add)"));
    connect(watchlist_filter_, &QPushButton::toggled,
            this, &PowerTraderScreen::on_watchlist_filter_toggled);
    hl->addWidget(watchlist_filter_);

    hl->addStretch();

    auto* info = new QLabel("Cabinet: annual OGE Form 278 (not real-time PTRs)");
    info->setStyleSheet(QString("color:%1; font-size:12px; font-style:italic;")
                            .arg(ui::colors::TEXT_SECONDARY()));
    hl->addWidget(info);

    return strip;
}

QWidget* PowerTraderScreen::build_member_sidebar() {
    auto* sidebar = new QWidget;
    sidebar->setFixedWidth(240);
    sidebar->setStyleSheet(QString("background:%1; border-right:1px solid %2;")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(sidebar);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* hdr = new QLabel("MEMBERS  ·  RANKED BY ALPHA");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet(
        QString("color:%1; font-size:12px; font-weight:700; letter-spacing:0.5px;"
                "padding-left:10px; border-bottom:1px solid %2;")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM()));
    vl->addWidget(hdr);

    member_search_ = new QLineEdit;
    member_search_->setPlaceholderText("Search\xe2\x80\xa6");
    member_search_->setFixedHeight(30);
    member_search_->setStyleSheet(
        QString("QLineEdit{background:%1;color:%2;border:none;border-bottom:1px solid %3;"
                "padding:0 8px;font-size:12px;}"
                "QLineEdit:focus{border-bottom-color:%4;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(),
                 ui::colors::BORDER_DIM(), ui::colors::AMBER()));
    connect(member_search_, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int i = 0; i < member_list_->count(); ++i) {
            auto* item = member_list_->item(i);
            item->setHidden(!t.isEmpty() &&
                            !item->text().contains(t, Qt::CaseInsensitive));
        }
    });
    vl->addWidget(member_search_);

    member_list_ = new QListWidget;
    member_list_->setFrameShape(QFrame::NoFrame);
    member_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    member_list_->setStyleSheet(
        QString("QListWidget{background:%1;border:none;outline:none;}"
                "QListWidget::item{padding:5px 10px;border-bottom:1px solid %2;"
                "font-size:12px;color:%3;}"
                "QListWidget::item:selected{background:rgba(217,119,6,0.15);color:%4;}"
                "QListWidget::item:hover:!selected{background:%5;}"
                "QScrollBar:vertical{width:4px;background:%1;}"
                "QScrollBar::handle:vertical{background:%2;}")
            .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(),
                 ui::colors::TEXT_PRIMARY(), ui::colors::AMBER(),
                 ui::colors::BG_RAISED()));
    connect(member_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* cur, QListWidgetItem*) {
                if (!cur) return;
                const QString mid = cur->data(Qt::UserRole).toString();
                if (!mid.isEmpty())
                    on_member_selected(mid);
            });

    // Right-click on a member → toggle watchlist. Marker is a star prepended
    // to the name in populate_member_list; the set is persisted via QSettings.
    member_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(member_list_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                auto* item = member_list_->itemAt(pos);
                if (!item) return;
                const QString mid = item->data(Qt::UserRole).toString();
                if (mid.isEmpty()) return;
                QMenu menu(this);
                const bool watched = watchlist_.contains(mid);
                QAction* watch_act = menu.addAction(
                    watched ? QStringLiteral("Remove from watchlist")
                            : QStringLiteral("Add to watchlist"));
                QAction* compare_act = menu.addAction(QStringLiteral("Add to Compare"));
                QAction* chosen = menu.exec(member_list_->viewport()->mapToGlobal(pos));
                if (chosen == watch_act) {
                    toggle_watchlist(mid);
                } else if (chosen == compare_act && compare_view_) {
                    compare_view_->add_member(mid);
                    if (tab_widget_)
                        tab_widget_->setCurrentWidget(compare_view_);
                }
            });
    vl->addWidget(member_list_, 1);
    return sidebar;
}

// ── Member detail drawer ──────────────────────────────────────────────────────

void PowerTraderScreen::build_member_drawer() {
    // Drawer is a child of congress_view_, not part of any layout — we drive
    // its geometry manually so it overlays tab_widget_'s area when shown.
    // This lets the user dismiss the detail and return to whichever tab they
    // were on, instead of being auto-navigated.
    member_drawer_ = new QWidget(congress_view_);
    member_drawer_->setStyleSheet(
        QString("QWidget#powerTraderMemberDrawer { background:%1; "
                " border-left:2px solid %2; }")
            .arg(ui::colors::BG_BASE(), ui::colors::AMBER()));
    member_drawer_->setObjectName(QStringLiteral("powerTraderMemberDrawer"));

    auto* vl = new QVBoxLayout(member_drawer_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    // Top bar with title + close button. Close returns the user to whichever
    // tab they had selected before opening the drawer.
    auto* bar = new QWidget(member_drawer_);
    bar->setFixedHeight(34);
    bar->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(14, 0, 8, 0);
    bl->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("MEMBER DETAIL"), bar);
    title->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;"
                                 "letter-spacing:1.5px;background:transparent;")
                             .arg(ui::colors::AMBER()));
    bl->addWidget(title);
    bl->addStretch();

    auto* close_btn = new QPushButton(QStringLiteral("\xc3\x97"), bar);  // ×
    close_btn->setFixedSize(26, 22);
    close_btn->setCursor(Qt::PointingHandCursor);
    close_btn->setToolTip(QStringLiteral("Close (Esc)"));
    close_btn->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;font-size:14px;font-weight:700;}"
                "QPushButton:hover{color:%3;border-color:%3;background:%4;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                 ui::colors::AMBER(),          ui::colors::BG_RAISED()));
    connect(close_btn, &QPushButton::clicked, this, &PowerTraderScreen::hide_member_drawer);
    bl->addWidget(close_btn);

    vl->addWidget(bar);

    // member_panel_ is reparented inside the drawer. Previously it was a tab
    // of tab_widget_; here it lives only in the drawer.
    member_panel_->setParent(member_drawer_);
    vl->addWidget(member_panel_, 1);

    member_drawer_->setVisible(false);
    member_drawer_->raise();

    // Track the tab area's geometry so the drawer follows on resize.
    tab_widget_->installEventFilter(this);

    // Esc closes the drawer.
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), member_drawer_);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &PowerTraderScreen::hide_member_drawer);
}

void PowerTraderScreen::show_member_drawer() {
    if (!member_drawer_ || !tab_widget_) return;
    const QPoint pos = tab_widget_->mapTo(congress_view_, QPoint(0, 0));
    member_drawer_->setGeometry(pos.x(), pos.y(),
                                tab_widget_->width(), tab_widget_->height());
    member_drawer_->raise();
    member_drawer_->show();
    member_drawer_->setFocus();
}

void PowerTraderScreen::hide_member_drawer() {
    if (member_drawer_) member_drawer_->setVisible(false);
}

bool PowerTraderScreen::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == tab_widget_ && member_drawer_ && member_drawer_->isVisible() &&
        (ev->type() == QEvent::Resize || ev->type() == QEvent::Move)) {
        const QPoint pos = tab_widget_->mapTo(congress_view_, QPoint(0, 0));
        member_drawer_->setGeometry(pos.x(), pos.y(),
                                    tab_widget_->width(), tab_widget_->height());
    }
    return QWidget::eventFilter(obj, ev);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void PowerTraderScreen::populate_member_list(const QVector<CongressMember>& members) {
    member_list_->clear();

    const bool only_watched = watchlist_filter_ && watchlist_filter_->isChecked();

    auto sorted = members;
    std::sort(sorted.begin(), sorted.end(),
              [](const CongressMember& a, const CongressMember& b) {
                  return a.alpha_ytd > b.alpha_ytd;
              });
    const auto& svc = PowerTraderService::instance();
    for (const auto& m : sorted) {
        const bool starred = watchlist_.contains(m.id);
        if (only_watched && !starred) continue;
        auto* item = new QListWidgetItem;
        const QString star_tag  = starred ? QStringLiteral("\xe2\x98\x85 ") : QString();  // ★
        const QString party_tag = m.party.isEmpty() ? "" : "[" + m.party + "] ";
        const QString chb_tag   = m.chamber == MemberChamber::Senate ? "SEN" : "HSE";
        const QString sign      = m.alpha_ytd >= 0 ? "+" : "";
        // Signal-score indicator: a tiny block character whose colour reflects
        // the member's average per-trade signal score. ▌ green = low, amber =
        // mid, red = high. Gives the user a glance-level cue about each
        // member's overall insider-signal intensity without needing to open
        // the detail drawer.
        const double sig = svc.avg_signal_score(m.id);
        const QString sig_glyph = QStringLiteral("\xe2\x96\x8c");  // ▌
        item->setText(star_tag + party_tag + m.full_name + "  " + sig_glyph + "\n"
                      + chb_tag + "  " + m.state + "  "
                      + sign + QString::number(m.alpha_ytd, 'f', 1) + "% alpha");
        item->setData(Qt::UserRole, m.id);
        // We can't colorize a portion of a QListWidgetItem's plain text — but
        // we can attach a tooltip with the numeric score so hover discloses
        // the value, and we set foreground to amber for high-signal members
        // so the whole row reads "watch this one."
        if (sig >= 60.0) {
            item->setForeground(QColor(ui::colors::AMBER()));
            item->setToolTip(QStringLiteral("Avg signal: %1 / 100  (high)").arg(sig, 0, 'f', 0));
        } else if (sig >= 40.0) {
            item->setToolTip(QStringLiteral("Avg signal: %1 / 100  (moderate)").arg(sig, 0, 'f', 0));
        } else {
            item->setToolTip(QStringLiteral("Avg signal: %1 / 100  (low)").arg(sig, 0, 'f', 0));
        }
        member_list_->addItem(item);
    }

    // Re-apply any active search filter — clear/addItem above resets each
    // item's hidden state, so an in-progress search query would otherwise
    // be ignored on the freshly inserted items.
    if (member_search_) {
        const QString q = member_search_->text();
        if (!q.isEmpty()) {
            for (int i = 0; i < member_list_->count(); ++i) {
                auto* item = member_list_->item(i);
                item->setHidden(!item->text().contains(q, Qt::CaseInsensitive));
            }
        }
    }
}

void PowerTraderScreen::show_loading() { stack_->setCurrentIndex(0); }
void PowerTraderScreen::show_error(const QString& msg) {
    error_lbl_->setText("Failed to load data:\n" + msg);
    stack_->setCurrentIndex(1);
}
void PowerTraderScreen::show_content() { stack_->setCurrentIndex(2); }

// ── Slots ─────────────────────────────────────────────────────────────────────

void PowerTraderScreen::on_data_loaded(PowerTraderSummary summary) {
    current_summary_ = summary;
    timestamp_lbl_->setText(
        QString("Updated %1")
            .arg(summary.last_updated.toLocalTime().toString("hh:mm")));

    // Respect the active VIEW filter on (re)load. A naive populate from
    // `summary.members` would clobber the sidebar back to ALL members even
    // when the user has SENATE or HOUSE selected — same for the panels'
    // table data. Route everything through the filter so refresh stays
    // consistent with the VIEW chip state.
    const auto filtered = PowerTraderService::instance().filtered_summary(active_body_);

    populate_member_list(filtered.members);

    const auto sectors          = PowerTraderService::instance().sector_breakdown();
    const auto insider_signals  = PowerTraderService::instance().committee_insider_signals();
    const auto committee_groups = PowerTraderService::instance().committee_groups();
    const auto watch_list       = PowerTraderService::instance().insider_watch_list();
    const auto cmtes            = PowerTraderService::instance().available_committees();

    overview_panel_ ->set_data(filtered, sectors, insider_signals);
    rankings_panel_ ->set_data(filtered);
    feed_panel_     ->set_trades(filtered.recent_trades);
    feed_panel_     ->set_available_committees(cmtes);
    committee_panel_->set_data(filtered, committee_groups);
    party_panel_    ->set_data(filtered);
    insider_panel_  ->set_data(watch_list);
    signal_panel_   ->set_data(filtered);
    practice_panel_ ->set_data(filtered);
    if (compare_view_)
        compare_view_->set_summary(filtered);

    // Pre-select highest-alpha member on first load (from the filtered set,
    // so a SENATE filter selects a senator rather than someone hidden from
    // the sidebar).
    if (selected_member_id_.isEmpty() && !filtered.members.isEmpty()) {
        auto sorted = filtered.members;
        std::sort(sorted.begin(), sorted.end(),
                  [](const CongressMember& a, const CongressMember& b) {
                      return a.alpha_ytd > b.alpha_ytd;
                  });
        selected_member_id_ = sorted.first().id;
    }
    if (!selected_member_id_.isEmpty())
        on_member_selected(selected_member_id_);

    show_content();
}

void PowerTraderScreen::on_error(const QString& msg) {
    show_error(msg);
}

void PowerTraderScreen::on_member_selected(const QString& member_id) {
    selected_member_id_ = member_id;

    for (int i = 0; i < member_list_->count(); ++i) {
        auto* item = member_list_->item(i);
        if (item->data(Qt::UserRole).toString() == member_id) {
            member_list_->blockSignals(true);
            member_list_->setCurrentItem(item);
            member_list_->blockSignals(false);
            break;
        }
    }

    CongressMember member;
    for (const auto& m : current_summary_.members)
        if (m.id == member_id) { member = m; break; }
    if (member.id.isEmpty()) return;

    member_panel_->set_member(member);
    feed_panel_->set_selected_member(member_id);
    // Show the member detail in the slide-in drawer instead of force-
    // navigating to a Member tab — preserves the user's current tab context.
    show_member_drawer();
}

void PowerTraderScreen::on_body_filter_changed(BodyFilter body) {
    active_body_ = body;

    if (body == BodyFilter::Cabinet) {
        view_stack_->setCurrentIndex(1);   // full-width cabinet page
        cabinet_panel_->activate();
        return;
    }

    view_stack_->setCurrentIndex(0);   // congress view

    if (!PowerTraderService::instance().is_loaded()) return;

    const auto filtered  = PowerTraderService::instance().filtered_summary(body);
    const auto sectors   = PowerTraderService::instance().sector_breakdown();
    const auto cmte_signals   = PowerTraderService::instance().committee_insider_signals();
    const auto cmte_grps = PowerTraderService::instance().committee_groups();
    const auto watch     = PowerTraderService::instance().insider_watch_list();

    populate_member_list(filtered.members);
    overview_panel_ ->set_data(filtered, sectors, cmte_signals);
    rankings_panel_ ->set_data(filtered);
    feed_panel_     ->set_trades(filtered.recent_trades);
    committee_panel_->set_data(filtered, cmte_grps);
    party_panel_    ->set_data(filtered);
    insider_panel_  ->set_data(watch);
    signal_panel_   ->set_data(filtered);
    practice_panel_ ->set_data(filtered);
    if (compare_view_)
        compare_view_->set_summary(filtered);
}

void PowerTraderScreen::on_range_changed(int days) {
    // Selecting a new range invalidates the cached summary and triggers a
    // fresh scrape — for the Senate side that can take 30–180s, so show the
    // loading state immediately so the user understands the click registered.
    if (days == PowerTraderService::instance().days_back()) return;
    show_loading();
    PowerTraderService::instance().set_days_back(days);
}

// ── Onboarding overlay (first-run only) ──────────────────────────────────────

void PowerTraderScreen::show_onboarding_overlay() {
    auto* overlay = new QFrame(this);
    overlay->setObjectName(QStringLiteral("powerTraderOnboardingOverlay"));
    overlay->setStyleSheet(
        QString("QFrame#powerTraderOnboardingOverlay {"
                "  background:rgba(8,8,8,0.85); }"
                "QLabel { background:transparent; }")
            .arg(ui::colors::BG_BASE()));
    overlay->setGeometry(0, 0, width(), height());
    overlay->raise();

    auto* card = new QFrame(overlay);
    card->setObjectName(QStringLiteral("powerTraderOnboardingCard"));
    card->setStyleSheet(
        QString("QFrame#powerTraderOnboardingCard {"
                "  background:%1; border:2px solid %2; border-radius:4px; }"
                "QLabel { background:transparent; }")
            .arg(ui::colors::BG_SURFACE(), ui::colors::AMBER()));
    card->setFixedSize(520, 360);

    auto place_card = [overlay, card]() {
        card->move((overlay->width()  - card->width())  / 2,
                   (overlay->height() - card->height()) / 2);
    };
    place_card();

    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(24, 22, 24, 22);
    vl->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("WELCOME TO POWER TRADER"), card);
    title->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;"
                                 " letter-spacing:2px;").arg(ui::colors::AMBER()));
    vl->addWidget(title);

    auto* body = new QLabel(card);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setStyleSheet(QString("color:%1; font-size:12px; line-height:1.4;")
                            .arg(ui::colors::TEXT_PRIMARY()));
    body->setText(QStringLiteral(
        "Tracks U.S. Congressional stock disclosures filed under the STOCK Act "
        "(2012) and Cabinet OGE Form 278 holdings.<br/><br/>"

        "<b>How to use it</b><br/>"
        "&nbsp;&bull; Pick a chamber via the <b>VIEW</b> filter (ALL / SENATE / HOUSE / CABINET).<br/>"
        "&nbsp;&bull; Pick a time window via <b>RANGE</b> (30d / 90d / 1y / 2y).<br/>"
        "&nbsp;&bull; Click a member in the sidebar to open their detail drawer.<br/>"
        "&nbsp;&bull; Right-click a member to add them to your <b>WATCHLIST</b>.<br/>"
        "&nbsp;&bull; Hover any (est.) value to see how it was modelled.<br/><br/>"

        "<i>All trade values are estimates from disclosed amount ranges. "
        "Educational / informational use only — not investment advice.</i>"));
    vl->addWidget(body, 1);

    auto* btn_row = new QHBoxLayout;
    btn_row->addStretch();
    auto* got_it = new QPushButton(QStringLiteral("GOT IT"), card);
    got_it->setFixedSize(110, 30);
    got_it->setCursor(Qt::PointingHandCursor);
    got_it->setStyleSheet(
        QString("QPushButton{background:%1; color:%2; border:none; border-radius:2px;"
                " font-size:12px; font-weight:700; letter-spacing:1px;}"
                "QPushButton:hover{background:%3;}")
            .arg(ui::colors::AMBER(), ui::colors::BG_BASE(), ui::colors::AMBER_DIM()));
    btn_row->addWidget(got_it);
    vl->addLayout(btn_row);

    connect(got_it, &QPushButton::clicked, this, [overlay]() {
        QSettings().setValue(QStringLiteral("power_trader/onboarded"), true);
        overlay->deleteLater();
    });

    overlay->show();
}

// ── Watchlist (per-user follows, persisted across sessions) ──────────────────

void PowerTraderScreen::load_watchlist() {
    const QStringList ids = QSettings()
        .value(QStringLiteral("power_trader/watchlist")).toStringList();
    watchlist_ = QSet<QString>(ids.begin(), ids.end());
}

void PowerTraderScreen::save_watchlist() {
    QStringList ids(watchlist_.begin(), watchlist_.end());
    ids.sort();
    QSettings().setValue(QStringLiteral("power_trader/watchlist"), ids);
}

void PowerTraderScreen::toggle_watchlist(const QString& member_id) {
    if (member_id.isEmpty()) return;
    if (watchlist_.contains(member_id))
        watchlist_.remove(member_id);
    else
        watchlist_.insert(member_id);
    save_watchlist();
    // Re-render sidebar to update star markers and (if watchlist filter is
    // active) the visible subset.
    populate_member_list(PowerTraderService::instance().filtered_summary(active_body_).members);
    if (feed_panel_)
        feed_panel_->set_member_watchlist(watchlist_);
}

void PowerTraderScreen::load_ticker_watchlist() {
    const QStringList syms = QSettings()
        .value(QStringLiteral("power_trader/ticker_watchlist")).toStringList();
    ticker_watchlist_ = QSet<QString>(syms.begin(), syms.end());
}

void PowerTraderScreen::save_ticker_watchlist() {
    QStringList syms(ticker_watchlist_.begin(), ticker_watchlist_.end());
    syms.sort();
    QSettings().setValue(QStringLiteral("power_trader/ticker_watchlist"), syms);
}

void PowerTraderScreen::toggle_ticker_watchlist(const QString& ticker) {
    if (ticker.isEmpty()) return;
    if (ticker_watchlist_.contains(ticker))
        ticker_watchlist_.remove(ticker);
    else
        ticker_watchlist_.insert(ticker);
    save_ticker_watchlist();
    if (feed_panel_)
        feed_panel_->set_ticker_watchlist(ticker_watchlist_);
}

void PowerTraderScreen::on_watchlist_filter_toggled(bool /*only_watched*/) {
    // The filter is read inside populate_member_list. Just trigger a redraw.
    populate_member_list(PowerTraderService::instance().filtered_summary(active_body_).members);
}

} // namespace fincept::power_trader
