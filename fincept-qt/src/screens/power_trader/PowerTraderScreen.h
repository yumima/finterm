// src/screens/power_trader/PowerTraderScreen.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QWidget>

namespace fincept::screens {
class LeaderboardPanel;
class TradesFeedPanel;
class MemberDetailPanel;
} // namespace fincept::screens

namespace fincept::power_trader {

/// Top-level POWER TRADER screen — tracks U.S. congressional stock trade
/// disclosures filed under the STOCK Act.
///
/// Layout (horizontal QSplitter):
///   Left  (250px): LeaderboardPanel  — member ranking table
///   Center (flex): TradesFeedPanel   — chronological trade feed + filters
///   Right (300px): MemberDetailPanel — per-member detail
///
/// Registered as screen_id "power_trader" in DockScreenRouter.
class PowerTraderScreen : public QWidget {
    Q_OBJECT
  public:
    explicit PowerTraderScreen(QWidget* parent = nullptr);

  signals:
    /// Emitted when the user wants to cross-navigate to another screen
    /// (e.g. open a ticker in Equity Research). Connected in MainWindow.
    void navigate_to_screen(QString screen_id, QString ticker);

  private slots:
    void on_data_loaded(PowerTraderSummary summary);
    void on_error_occurred(QString error);
    void on_member_selected(const QString& member_id);

  private:
    void build_ui();
    void show_loading();
    void show_error(const QString& msg);
    void show_content();
    void update_timestamp();

    // Top bar
    QLabel*  last_updated_label_ = nullptr;

    // Content stacker: 0=loading, 1=error, 2=content
    QStackedWidget* stack_      = nullptr;
    QWidget*        loading_pg_ = nullptr;
    QWidget*        error_pg_   = nullptr;
    QWidget*        content_pg_ = nullptr;
    QLabel*         error_msg_  = nullptr;

    // Content panels
    QSplitter*                     splitter_    = nullptr;
    fincept::screens::LeaderboardPanel*  leaderboard_ = nullptr;
    fincept::screens::TradesFeedPanel*   feed_        = nullptr;
    fincept::screens::MemberDetailPanel* detail_      = nullptr;

    PowerTraderSummary summary_;
};

} // namespace fincept::power_trader
