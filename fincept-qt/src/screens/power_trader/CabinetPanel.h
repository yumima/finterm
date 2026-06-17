// src/screens/power_trader/CabinetPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// CABINET panel — Executive Branch OGE Form 278 annual financial disclosures.
///
/// Unlike the congressional PTR view (individual trades), this shows:
///   • Holdings snapshot — what each cabinet official owns
///   • Conflict-of-interest analysis — holdings vs regulatory domain
///   • Sector exposure — what industries the cabinet collectively holds
///   • Rankings — by conflict score, portfolio size, conflict count
///
/// Wide-screen layout:
///   Top    — 5 stat tiles + data source note
///   Left   (35%) — cabinet member list ranked by conflict score
///   Right  (65%) — tabbed detail for selected member:
///     [Holdings] [Conflicts] [Sector] + side context pane
class CabinetPanel : public QWidget {
    Q_OBJECT
  public:
    explicit CabinetPanel(QWidget* parent = nullptr);

    /// Called when CABINET body filter is activated. Triggers Python data load
    /// if not already loaded; connects to the service signal.
    void activate();

  private slots:
    void on_cabinet_loaded(fincept::power_trader::CabinetSummary summary);
    void on_member_selected(int row);

  private:
    void build_ui();
    void build_loading_page();
    void build_content_page();
    void populate_member_list();
    void populate_stat_tiles();
    void show_member(const power_trader::CabinetMember& m);
    void populate_holdings_tab(const power_trader::CabinetMember& m);
    void populate_conflicts_tab(const power_trader::CabinetMember& m);
    void populate_sector_tab(const power_trader::CabinetMember& m);
    void populate_overview_tab();

    // ── State ─────────────────────────────────────────────────────────────────
    power_trader::CabinetSummary summary_;
    int selected_row_ = -1;
    bool first_member_shown_ = false; // gate the one-time jump to the Holdings tab

    // ── Page stack ────────────────────────────────────────────────────────────
    QStackedWidget* stack_         = nullptr;  // 0=loading, 1=content

    // ── Stat tiles ────────────────────────────────────────────────────────────
    QLabel* stat_members_    = nullptr;
    QLabel* stat_total_lo_   = nullptr;
    QLabel* stat_total_hi_   = nullptr;
    QLabel* stat_avg_conf_   = nullptr;
    QLabel* stat_top_conf_   = nullptr;
    QLabel* source_note_     = nullptr;

    // ── Member list (left) ────────────────────────────────────────────────────
    QTableWidget* member_table_    = nullptr;

    // ── Detail tabs (right) ───────────────────────────────────────────────────
    QTabWidget*   detail_tabs_     = nullptr;

    // Holdings tab widgets
    QLabel*       h_header_        = nullptr;
    QTableWidget* h_table_         = nullptr;

    // Conflicts tab widgets
    QLabel*       c_header_        = nullptr;
    QLabel*       c_domain_        = nullptr;
    QWidget*      c_flags_widget_  = nullptr;
    QVBoxLayout*  c_flags_layout_  = nullptr;

    // Sector tab widgets
    QLabel*       s_header_        = nullptr;
    QTableWidget* s_table_         = nullptr;

    // Overview tab widgets
    QLabel*       o_header_        = nullptr;
    QTableWidget* o_ranking_table_ = nullptr;
    QTableWidget* o_sector_table_  = nullptr;
};

} // namespace fincept::screens
