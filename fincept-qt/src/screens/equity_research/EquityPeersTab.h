// src/screens/equity_research/EquityPeersTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class EquityPeersTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityPeersTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

    /// Sync the per-row QCheckBox widgets in the COMP column (last) to
    /// reflect the live set of comparison tickers held by
    /// EquityOverviewTab. Called by the screen in response to
    /// EquityOverviewTab::comparisons_changed — keeps the column truthful
    /// when the user adds/removes via the inline COMP input or the chip ✕
    /// on another tab.
    void set_active_comparisons(const QStringList& symbols);

  signals:
    /// Emitted when the user ticks the COMP checkbox on a peer row. The
    /// hosting screen routes this to EquityOverviewTab::add_comparison so
    /// the chart gains a comparison overlay — same code path as typing
    /// into the inline COMP input.
    void add_to_comparison_requested(const QString& symbol);
    /// Symmetric to the above: emitted when the user unchecks the COMP
    /// box on a peer row that is currently in the comparison set.
    void remove_from_comparison_requested(const QString& symbol);

  private slots:
    void on_load_clicked();

  private:
    void apply_peers_state(const services::query::QueryStore::State& s);
    void build_ui();
    void populate_table(const QVector<services::equity::PeerData>& peers);
    QStringList default_peers(const QString& symbol) const;
    /// Re-apply active_comp_set_ to the QCheckBox at the last column of
    /// each row. Walks visual rows and reads the SYMBOL cell text — so
    /// after a sort, each checkbox reflects the symbol now occupying that
    /// row (cellWidget identity does NOT move with sort in QTableWidget).
    /// Uses QCheckBox::blockSignals so programmatic setChecked doesn't
    /// echo back as a user toggle.
    void refresh_check_column();

    QString current_symbol_;
    QLineEdit* peers_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTableWidget* peer_table_ = nullptr;

    /// Current set of tickers that are overlaid on the Overview chart.
    /// Maintained by set_active_comparisons (signal from overview tab) so
    /// populate_table + refresh_check_column can render the right state.
    QSet<QString> active_comp_set_;

    /// Last sort column + order applied by the user. Used so a click on
    /// the COMP header (which is meaningless to sort by) bounces back
    /// to the previously-active sort without flipping its order.
    int last_sort_col_ = -1;
    Qt::SortOrder last_sort_order_ = Qt::AscendingOrder;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
