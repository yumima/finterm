// src/screens/power_trader/CommitteePanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// "BY COMMITTEE" tab — groups members and trades by committee affiliation.
///
/// Wide-screen layout (horizontal splitter):
///   Left  (35%) — scrollable committee list with trade count + correlation bar
///   Right (65%) — selected committee detail: members, top tickers, trade table
class CommitteePanel : public QWidget {
    Q_OBJECT
  public:
    explicit CommitteePanel(QWidget* parent = nullptr);

    void set_data(const power_trader::PowerTraderSummary& summary,
                  const QVector<power_trader::CommitteeGroup>& groups);

  signals:
    void member_selected(QString member_id);

  private slots:
    void on_committee_selected(int row);

  private:
    void build_ui();
    void populate_committee_list();
    void show_committee(const power_trader::CommitteeGroup& group);
    void clear_detail();

    // ── Left pane ────────────────────────────────────────────────────────────
    QTableWidget* committee_list_   = nullptr;

    // ── Right pane ───────────────────────────────────────────────────────────
    QLabel*       detail_title_     = nullptr;
    QLabel*       detail_stats_     = nullptr;
    QLabel*       detail_tickers_   = nullptr;
    QLabel*       detail_members_   = nullptr;
    QTableWidget* trade_table_      = nullptr;

    // ── Data ─────────────────────────────────────────────────────────────────
    power_trader::PowerTraderSummary          summary_;
    QVector<power_trader::CommitteeGroup>     groups_;
    int                                       selected_row_ = -1;
};

} // namespace fincept::screens
