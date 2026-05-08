// src/screens/power_trader/PowerTraderScreen.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QButtonGroup>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QWidget>

// Forward declarations
namespace fincept::screens {
class OverviewPanel;
class RankingsPanel;
class MemberProfilePanel;
class TradesFeedPanel;
class CommitteePanel;
class PartyPanel;
class InsiderWatchPanel;
class SignalBuilderPanel;
class CabinetPanel;
}

namespace fincept::power_trader {

/// Main POWER TRADER screen — wide-screen layout.
///
/// Structure:
///   Top bar    — title · data sources · refresh · last-updated
///   Body strip — [ALL] [SENATE] [HOUSE] body-filter buttons
///   Content    — left sidebar (searchable member list, 240px)
///                + right tab widget (7 tabs):
///                  OVERVIEW · RANKINGS · MEMBER · FEED · COMMITTEE · PARTY · INSIDER WATCH
class PowerTraderScreen : public QWidget {
    Q_OBJECT
  public:
    explicit PowerTraderScreen(QWidget* parent = nullptr);

  signals:
    void navigate_to_screen(QString screen_id, QString ticker);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void on_data_loaded(fincept::power_trader::PowerTraderSummary summary);
    void on_error(const QString& msg);
    void on_member_selected(const QString& member_id);
    void on_body_filter_changed(BodyFilter body);

  private:
    void build_ui();
    QWidget* build_top_bar();
    QWidget* build_body_strip();
    QWidget* build_member_sidebar();
    void populate_member_list(const QVector<CongressMember>& members);
    void show_content();
    void show_loading();
    void show_error(const QString& msg);
    void refresh_all_panels();

    // ── Top bar ───────────────────────────────────────────────────────────────
    QLabel*      timestamp_lbl_ = nullptr;
    QPushButton* refresh_btn_   = nullptr;

    // ── Body strip ────────────────────────────────────────────────────────────
    QButtonGroup* body_btn_group_ = nullptr;
    BodyFilter    active_body_    = BodyFilter::All;

    // ── Sidebar ───────────────────────────────────────────────────────────────
    QLineEdit*   member_search_ = nullptr;
    QListWidget* member_list_   = nullptr;

    // ── Main tabs ─────────────────────────────────────────────────────────────
    QTabWidget*                  tab_widget_       = nullptr;
    screens::OverviewPanel*      overview_panel_   = nullptr;
    screens::RankingsPanel*      rankings_panel_   = nullptr;
    screens::MemberProfilePanel* member_panel_     = nullptr;
    screens::TradesFeedPanel*    feed_panel_       = nullptr;
    screens::CommitteePanel*     committee_panel_  = nullptr;
    screens::PartyPanel*         party_panel_      = nullptr;
    screens::InsiderWatchPanel*  insider_panel_    = nullptr;
    screens::CabinetPanel*       cabinet_panel_    = nullptr;
    screens::SignalBuilderPanel* signal_panel_     = nullptr;

    // ── Loading / error / content stack ──────────────────────────────────────
    QStackedWidget* stack_           = nullptr;
    QLabel*         loading_lbl_     = nullptr;
    QLabel*         error_lbl_       = nullptr;
    QWidget*        content_area_    = nullptr;

    // ── Congress vs Cabinet page switch ──────────────────────────────────────
    QStackedWidget* view_stack_      = nullptr;  // 0=congress, 1=cabinet
    QWidget*        congress_view_   = nullptr;  // sidebar + tab_widget_

    // ── State ─────────────────────────────────────────────────────────────────
    PowerTraderSummary current_summary_;
    QString            selected_member_id_;
};

} // namespace fincept::power_trader
