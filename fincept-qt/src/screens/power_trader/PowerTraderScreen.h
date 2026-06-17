// src/screens/power_trader/PowerTraderScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/power_trader/PowerTraderTypes.h"

#include <QButtonGroup>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QVariantMap>
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
class PracticePanel;
class SignalBuilderPanel;
class CabinetPanel;
class CompareView;
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
class PowerTraderScreen : public QWidget, public fincept::screens::IStatefulScreen {
    Q_OBJECT
  public:
    explicit PowerTraderScreen(QWidget* parent = nullptr);

    // IStatefulScreen — cross-session persistence. Within-session preservation
    // is automatic via DockScreenRouter's screen-instance cache; this only
    // matters across app restarts. Saves the user's selected congress member,
    // body filter, and active tab so a relaunch returns them to the same view.
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "power_trader"; }

  signals:
    void navigate_to_screen(QString screen_id, QString ticker);
    // "Paper-buy the same" from a power-trader's trade: the app shell routes
    // this to the Portfolio screen and opens its BUY ticket pre-filled.
    void request_paper_buy(QString ticker);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void on_data_loaded(fincept::power_trader::PowerTraderSummary summary);
    void on_error(const QString& msg);
    void on_member_selected(const QString& member_id);
    void on_body_filter_changed(BodyFilter body);
    void on_range_changed(int days);
    void on_watchlist_filter_toggled(bool only_watched);
    void toggle_watchlist(const QString& member_id);

  private:
    void build_ui();
    QWidget* build_top_bar();
    QWidget* build_body_strip();
    QWidget* build_member_sidebar();
    void populate_member_list(const QVector<CongressMember>& members);
    void show_content();
    void show_loading();
    void show_error(const QString& msg);
    void show_onboarding_overlay();
    void refresh_all_panels();

    // ── Top bar ───────────────────────────────────────────────────────────────
    QLabel*      timestamp_lbl_ = nullptr;
    QPushButton* refresh_btn_   = nullptr;

    // ── Body strip ────────────────────────────────────────────────────────────
    QButtonGroup* body_btn_group_   = nullptr;
    BodyFilter    active_body_      = BodyFilter::All;
    QButtonGroup* range_btn_group_  = nullptr;
    QPushButton*  watchlist_filter_ = nullptr;  // toggle: show only watched

    // ── Per-user watchlist (persisted) ───────────────────────────────────────
    // Set of member IDs the user has followed. Persisted via QSettings under
    // power_trader/watchlist as a stringlist. Sidebar prepends a star to
    // watched entries and the WATCHLIST filter pill restricts the visible
    // member list to this set.
    QSet<QString> watchlist_;
    void load_watchlist();
    void save_watchlist();

    // Ticker subscriptions — second axis of the "follow X for activity" UX.
    // Persisted under power_trader/ticker_watchlist. TradesFeedPanel offers a
    // right-click "Follow $X" on any ticker cell and a SUBS filter pill that
    // restricts the feed to followed-member OR followed-ticker trades.
    QSet<QString> ticker_watchlist_;
    void load_ticker_watchlist();
    void save_ticker_watchlist();
    void toggle_ticker_watchlist(const QString& ticker);

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
    screens::PracticePanel*      practice_panel_   = nullptr;
    screens::CompareView*        compare_view_     = nullptr;

    // Signal Builder + Practice are folded into one tab with a sub-stack.
    // Index 0 = signal_panel_, index 1 = practice_panel_.
    QWidget*        sb_practice_tab_   = nullptr;
    QStackedWidget* sb_practice_stack_ = nullptr;

    // ── Loading / error / content stack ──────────────────────────────────────
    QStackedWidget* stack_           = nullptr;
    QLabel*         loading_lbl_     = nullptr;
    QLabel*         error_lbl_       = nullptr;
    QWidget*        content_area_    = nullptr;

    // ── Congress vs Cabinet page switch ──────────────────────────────────────
    QStackedWidget* view_stack_      = nullptr;  // 0=congress, 1=cabinet
    QWidget*        congress_view_   = nullptr;  // master-detail: [sidebar|tabs] | member_panel_

    // ── State ─────────────────────────────────────────────────────────────────
    PowerTraderSummary current_summary_;
    QString            selected_member_id_;
};

} // namespace fincept::power_trader
