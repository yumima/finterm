// src/screens/power_trader/PowerTraderScreen.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QWidget>

// Forward declarations to avoid full header pulls in .h
namespace fincept::screens {
class OverviewPanel;
class RankingsPanel;
class MemberProfilePanel;
class TradesFeedPanel;
}

namespace fincept::power_trader {

/// Main POWER TRADER screen.
///
/// Layout:
///   Left sidebar (200px) — searchable member list, sorted by alpha by default
///   Right area (flex)    — QTabWidget with 4 tabs:
///     OVERVIEW   big-picture: stat tiles, sector exposure, committee correlation
///     RANKINGS   8 ranking dimensions with visual bar charts
///     MEMBER     full per-member deep dive: portfolio curve + holdings + signals
///     FEED       chronological trade disclosure feed (filterable)
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

  private:
    void build_ui();
    QWidget* build_top_bar();
    QWidget* build_member_sidebar();
    void populate_member_list(const QVector<CongressMember>& members);
    void show_content();
    void show_loading();
    void show_error(const QString& msg);

    // ── Top bar ───────────────────────────────────────────────────────────────
    QLabel*      timestamp_lbl_ = nullptr;
    QPushButton* refresh_btn_   = nullptr;

    // ── Sidebar ───────────────────────────────────────────────────────────────
    QLineEdit*   member_search_ = nullptr;
    QListWidget* member_list_   = nullptr;

    // ── Main tabs ─────────────────────────────────────────────────────────────
    QTabWidget*                  tab_widget_      = nullptr;
    screens::OverviewPanel*      overview_panel_  = nullptr;
    screens::RankingsPanel*      rankings_panel_  = nullptr;
    screens::MemberProfilePanel* member_panel_    = nullptr;
    screens::TradesFeedPanel*    feed_panel_      = nullptr;

    // ── Loading / error / content stack ──────────────────────────────────────
    QStackedWidget* stack_       = nullptr;
    QLabel*         loading_lbl_ = nullptr;
    QLabel*         error_lbl_   = nullptr;
    QWidget*        content_area_= nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    PowerTraderSummary current_summary_;
    QString            selected_member_id_;
};

} // namespace fincept::power_trader
