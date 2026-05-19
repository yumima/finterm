// src/screens/equity_research/EquityPeersTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class EquityPeersTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityPeersTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

  signals:
    /// Emitted when the user clicks a SYMBOL cell. The hosting screen
    /// routes this to EquityOverviewTab::add_comparison so the chart
    /// gains a comparison overlay — same code path as typing into the
    /// inline COMP input. Suppressed for the row matching the current
    /// primary symbol (a self-comparison is pointless).
    void add_to_comparison_requested(const QString& symbol);

  private slots:
    void on_load_clicked();
    /// Translate cellClicked(row, col) into add_to_comparison_requested when
    /// the user clicked column 0 (SYMBOL).
    void on_cell_clicked(int row, int column);

  private:
    void apply_peers_state(const services::query::QueryStore::State& s);
    void build_ui();
    void populate_table(const QVector<services::equity::PeerData>& peers);
    QStringList default_peers(const QString& symbol) const;

    QString current_symbol_;
    QLineEdit* peers_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTableWidget* peer_table_ = nullptr;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
