// src/screens/power_trader/PowerTraderScreen.cpp
#include "screens/power_trader/PowerTraderScreen.h"

#include "screens/power_trader/MemberProfilePanel.h"
#include "screens/power_trader/OverviewPanel.h"
#include "screens/power_trader/PowerTraderService.h"
#include "screens/power_trader/RankingsPanel.h"
#include "screens/power_trader/TradesFeedPanel.h"
#include "ui/theme/Theme.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QShowEvent>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

namespace fincept::power_trader {

PowerTraderScreen::PowerTraderScreen(QWidget* parent) : QWidget(parent) {
    build_ui();

    auto& svc = PowerTraderService::instance();
    connect(&svc, &PowerTraderService::data_loaded,   this, &PowerTraderScreen::on_data_loaded);
    connect(&svc, &PowerTraderService::error_occurred,this, &PowerTraderScreen::on_error);
}

void PowerTraderScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!PowerTraderService::instance().is_loaded()) {
        show_loading();
        PowerTraderService::instance().load_data();
    } else {
        // Re-emit cached data so panels refresh on tab re-activation
        PowerTraderService::instance().load_data();
    }
}

void PowerTraderScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
}

// ── UI construction ───────────────────────────────────────────────────────────

void PowerTraderScreen::build_ui() {
    setStyleSheet(QString("background:%1; color:%2;")
                      .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(build_top_bar());

    // ── Three-state stack: loading | error | content ──────────────────────────
    stack_ = new QStackedWidget;

    // Page 0 — loading
    auto* loading_page = new QWidget;
    {
        auto* ll = new QVBoxLayout(loading_page);
        loading_lbl_ = new QLabel("Loading congressional trade data…");
        loading_lbl_->setAlignment(Qt::AlignCenter);
        loading_lbl_->setStyleSheet(QString("color:%1; font-size:14px;")
                                         .arg(ui::colors::TEXT_SECONDARY()));
        ll->addStretch();
        ll->addWidget(loading_lbl_);
        ll->addStretch();
    }
    stack_->addWidget(loading_page);

    // Page 1 — error
    auto* error_page = new QWidget;
    {
        auto* el = new QVBoxLayout(error_page);
        error_lbl_ = new QLabel;
        error_lbl_->setAlignment(Qt::AlignCenter);
        error_lbl_->setWordWrap(true);
        error_lbl_->setStyleSheet(QString("color:%1; font-size:13px;")
                                       .arg(ui::colors::NEGATIVE()));
        el->addStretch();
        el->addWidget(error_lbl_);
        el->addStretch();
    }
    stack_->addWidget(error_page);

    // Page 2 — content
    content_area_ = new QWidget;
    {
        auto* hl = new QHBoxLayout(content_area_);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(0);
        hl->addWidget(build_member_sidebar());

        // ── Tabs ──────────────────────────────────────────────────────────────
        tab_widget_ = new QTabWidget;
        tab_widget_->setDocumentMode(true);
        tab_widget_->setStyleSheet(QString(R"(
            QTabWidget::pane { border:0; background:%1; }
            QTabBar::tab {
                background:%2; color:%3; padding:8px 20px;
                border:0; border-bottom:2px solid transparent;
                font-size:11px; font-weight:700; letter-spacing:1px;
            }
            QTabBar::tab:selected { color:%4; border-bottom:2px solid %4; }
            QTabBar::tab:hover:!selected { color:%5; }
        )").arg(ui::colors::BG_BASE(), ui::colors::BG_SURFACE(),
                ui::colors::TEXT_SECONDARY(), ui::colors::AMBER(),
                ui::colors::TEXT_PRIMARY()));

        overview_panel_ = new screens::OverviewPanel;
        rankings_panel_ = new screens::RankingsPanel;
        member_panel_   = new screens::MemberProfilePanel;
        feed_panel_     = new screens::TradesFeedPanel;

        tab_widget_->addTab(overview_panel_, "Overview");
        tab_widget_->addTab(rankings_panel_, "Rankings");
        tab_widget_->addTab(member_panel_,   "Member");
        tab_widget_->addTab(feed_panel_,     "Feed");

        connect(overview_panel_, &screens::OverviewPanel::member_selected,
                this, &PowerTraderScreen::on_member_selected);
        connect(rankings_panel_, &screens::RankingsPanel::member_selected,
                this, &PowerTraderScreen::on_member_selected);
        connect(feed_panel_,     &screens::TradesFeedPanel::member_selected,
                this, &PowerTraderScreen::on_member_selected);
        connect(member_panel_,   &screens::MemberProfilePanel::navigate_to_markets,
                this, [this](const QString& ticker) {
                    emit navigate_to_screen(QStringLiteral("markets"), ticker);
                });

        hl->addWidget(tab_widget_, 1);
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

    // ── Row 1: title + subtitle + timestamp + refresh ─────────────────────────
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(10);

    auto* title = new QLabel("POWER TRADER");
    title->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700; "
                                 "letter-spacing:2px;").arg(ui::colors::AMBER()));

    auto* sub = new QLabel("Congressional Stock Disclosures · STOCK Act");
    sub->setStyleSheet(QString("color:%1; font-size:11px;")
                           .arg(ui::colors::TEXT_TERTIARY()));

    timestamp_lbl_ = new QLabel;
    timestamp_lbl_->setStyleSheet(QString("color:%1; font-size:10px;")
                                       .arg(ui::colors::TEXT_TERTIARY()));

    refresh_btn_ = new QPushButton("\xe2\x9f\xb3 Refresh");
    refresh_btn_->setFixedHeight(22);
    refresh_btn_->setCursor(Qt::PointingHandCursor);
    refresh_btn_->setStyleSheet(
        QString("QPushButton{background:transparent;color:%1;border:1px solid %2;"
                "border-radius:2px;padding:1px 8px;font-size:10px;font-weight:600;}"
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

    // ── Row 2: data source links ──────────────────────────────────────────────
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(0);

    auto* src_lbl = new QLabel("Data:");
    src_lbl->setStyleSheet(QString("color:%1; font-size:10px;")
                               .arg(ui::colors::TEXT_TERTIARY()));
    row2->addWidget(src_lbl);

    // Helper: create a clickable link label
    struct Source { const char* label; const char* url; };
    static const Source sources[] = {
        {"Senate eFTS",           "https://efts.senate.gov/"},
        {"House Disclosures",     "https://disclosures.house.gov/"},
        {"ProPublica Congress",   "https://projects.propublica.org/api-docs/congress-api/"},
        {"OpenSecrets",           "https://www.opensecrets.org/api"},
    };

    const QString btn_ss =
        QString("QPushButton{color:%1;font-size:10px;text-decoration:underline;"
                "background:transparent;border:none;padding:0;margin:0;}"
                "QPushButton:hover{color:%2;}")
            .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER());

    bool first = true;
    for (const auto& src : sources) {
        if (!first) {
            auto* sep = new QLabel("\xc2\xb7");  // middle dot separator
            sep->setStyleSheet(QString("color:%1; font-size:10px; padding:0 5px;")
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

QWidget* PowerTraderScreen::build_member_sidebar() {
    auto* sidebar = new QWidget;
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet(QString("background:%1; border-right:1px solid %2;")
                               .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(sidebar);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);

    auto* hdr = new QLabel("MEMBERS");
    hdr->setFixedHeight(32);
    hdr->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    hdr->setStyleSheet(
        QString("color:%1; font-size:10px; font-weight:700; letter-spacing:1.5px;"
                "padding-left:10px; border-bottom:1px solid %2;")
            .arg(ui::colors::TEXT_TERTIARY(), ui::colors::BORDER_DIM()));
    vl->addWidget(hdr);

    member_search_ = new QLineEdit;
    member_search_->setPlaceholderText("Search\xe2\x80\xa6");
    member_search_->setFixedHeight(30);
    member_search_->setStyleSheet(
        QString("QLineEdit{background:%1;color:%2;border:none;border-bottom:1px solid %3;"
                "padding:0 8px;font-size:11px;}"
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
                "QListWidget::item{padding:6px 10px;border-bottom:1px solid %2;"
                "font-size:11px;color:%3;}"
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
    vl->addWidget(member_list_, 1);
    return sidebar;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void PowerTraderScreen::populate_member_list(const QVector<CongressMember>& members) {
    member_list_->clear();
    auto sorted = members;
    std::sort(sorted.begin(), sorted.end(),
              [](const CongressMember& a, const CongressMember& b) {
                  return a.alpha_ytd > b.alpha_ytd;
              });
    for (const auto& m : sorted) {
        auto* item = new QListWidgetItem;
        const QString sign = m.alpha_ytd >= 0 ? "+" : "";
        item->setText(m.full_name + "\n" + sign
                      + QString::number(m.alpha_ytd, 'f', 1) + "% alpha");
        item->setData(Qt::UserRole,     m.id);
        item->setData(Qt::UserRole + 1, m.party);
        member_list_->addItem(item);
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

    populate_member_list(summary.members);

    const auto sectors = PowerTraderService::instance().sector_breakdown();
    const auto insider_signals = PowerTraderService::instance().committee_insider_signals();

    overview_panel_->set_data(summary, sectors, insider_signals);
    rankings_panel_->set_data(summary);
    feed_panel_->set_trades(summary.recent_trades);

    // Pre-select highest-alpha member on first load
    if (selected_member_id_.isEmpty() && !summary.members.isEmpty()) {
        auto sorted = summary.members;
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

    // Sync sidebar highlight without triggering recursive signal
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
    tab_widget_->setCurrentWidget(member_panel_);
}

} // namespace fincept::power_trader
