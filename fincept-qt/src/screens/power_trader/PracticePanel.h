// src/screens/power_trader/PracticePanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Practice tab — Phase 3 of the Power Trader suite.
///
/// Four sub-sections (tab bar within the tab):
///   RESEARCH     — Academic findings on congressional alpha vs SPY
///   SIGNAL GUIDE — How to interpret each of the 8 signal factors with examples
///   SECTOR MAP   — Live committee → sector heat map from current summary
///   STRATEGIES   — Pre-configured signal presets with rationale and backtest notes
///
/// All content uses the app's own live summary data where possible, making it
/// interactive rather than static documentation.
class PracticePanel : public QWidget {
    Q_OBJECT
  public:
    explicit PracticePanel(QWidget* parent = nullptr);

    /// Called whenever Power Trader data refreshes — updates live sections.
    void set_data(const power_trader::PowerTraderSummary& summary);

  signals:
    void navigate_to_signal_builder();  // "Try this" buttons jump to Signal Builder
    void preset_requested(QString preset_id);

  private:
    void build_ui();
    QWidget* build_research_tab();
    QWidget* build_signal_guide_tab();
    QWidget* build_sector_map_tab();
    QWidget* build_strategies_tab();

    void refresh_sector_map();

    // ── Sub-tabs ──────────────────────────────────────────────────────────────
    QTabWidget* sub_tabs_     = nullptr;

    // ── Sector map (live) ─────────────────────────────────────────────────────
    QTableWidget* sector_map_table_ = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    power_trader::PowerTraderSummary summary_;
};

} // namespace fincept::screens
