// src/screens/power_trader/PartyPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// "PARTY INTEL" tab — Democrat vs Republican side-by-side comparison.
///
/// Wide-screen two-column layout:
///   Left  (50%) — Democrat aggregate stats, top sectors, top tickers
///   Right (50%) — Republican aggregate stats, top sectors, top tickers
class PartyPanel : public QWidget {
    Q_OBJECT
  public:
    explicit PartyPanel(QWidget* parent = nullptr);

    void set_data(const power_trader::PowerTraderSummary& summary);

  signals:
    void member_selected(QString member_id);

  private:
    void build_ui();
    void populate_party_column(QWidget* col, const power_trader::PartyStats& ps,
                               const QString& color);

    // Democrat column widgets
    QLabel*       d_title_     = nullptr;
    QLabel*       d_stats_     = nullptr;
    QTableWidget* d_sectors_   = nullptr;
    QTableWidget* d_tickers_   = nullptr;

    // Republican column widgets
    QLabel*       r_title_     = nullptr;
    QLabel*       r_stats_     = nullptr;
    QTableWidget* r_sectors_   = nullptr;
    QTableWidget* r_tickers_   = nullptr;

    // Comparison bar
    QLabel*       compare_bar_ = nullptr;
};

} // namespace fincept::screens
