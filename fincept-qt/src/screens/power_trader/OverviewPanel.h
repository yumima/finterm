// src/screens/power_trader/OverviewPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Big-picture aggregate intelligence panel for the POWER TRADER screen.
///
/// Layout (vertical, scrollable):
///   Row 1 — 4 stat tiles: MEMBERS TRACKED, TOTAL DISCLOSED, BEAT SPY, MOST ACTIVE CMTE
///   Row 2 — Side-by-side: TOP TRADERS (alpha bar chart) | SECTOR EXPOSURE (bar chart)
///   Row 3 — Full-width: COMMITTEE INSIDER CORRELATION table
class OverviewPanel : public QWidget {
    Q_OBJECT
  public:
    explicit OverviewPanel(QWidget* parent = nullptr);

    void set_data(const power_trader::PowerTraderSummary& summary,
                  const QVector<power_trader::SectorExposure>& sectors,
                  const QVector<power_trader::CommitteeInsiderSignal>& insider_signals);
    void refresh_theme();

  signals:
    void member_selected(QString member_id);

  private:
    void build_ui();

    // Stat tile value labels
    QLabel* stat_members_     = nullptr;
    QLabel* stat_disclosed_   = nullptr;
    QLabel* stat_beat_spy_    = nullptr;
    QLabel* stat_cmte_        = nullptr;

    // Alpha bar chart container (Row 2 left)
    QWidget* alpha_chart_     = nullptr;

    // Sector bar chart container (Row 2 right)
    QWidget* sector_chart_    = nullptr;

    // Committee correlation table (Row 3)
    QTableWidget* signal_table_ = nullptr;

    // Scroll wrapper
    QScrollArea* scroll_area_ = nullptr;
    QWidget*     scroll_body_ = nullptr;

    // Data caches for painting
    QVector<power_trader::CongressMember>         members_cache_;
    QVector<power_trader::SectorExposure>         sectors_cache_;
    QVector<power_trader::CommitteeInsiderSignal> signals_cache_;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void populate_stats();
    void populate_alpha_chart();
    void populate_sector_chart();
    void populate_signal_table();

    static QString fmt_amount(double v);
    static QString fmt_pct(double v);
};

} // namespace fincept::screens
