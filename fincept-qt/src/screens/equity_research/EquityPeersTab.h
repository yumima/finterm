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

    /// Sync the per-row checkboxes in column 0 to reflect the live set of
    /// comparison tickers held by EquityOverviewTab. Called by the screen
    /// in response to EquityOverviewTab::comparisons_changed — keeps the
    /// "is this peer currently overlaid?" column truthful even when the
    /// user adds/removes via the COMP input or the chip ✕ on another tab.
    void set_active_comparisons(const QStringList& symbols);

  signals:
    /// Emitted when the user ticks the ✓ checkbox on a peer row. The
    /// hosting screen routes this to EquityOverviewTab::add_comparison so
    /// the chart gains a comparison overlay — same code path as typing
    /// into the inline COMP input. Suppressed for the row matching the
    /// current primary symbol (a self-comparison is pointless).
    void add_to_comparison_requested(const QString& symbol);
    /// Symmetric to the above: emitted when the user unchecks the ✓ box
    /// on a peer row that is currently in the comparison set.
    void remove_from_comparison_requested(const QString& symbol);

  private slots:
    void on_load_clicked();
    /// React to the user toggling a ✓ checkbox in column 0 of peer_table_.
    /// Emits add_to_comparison_requested or remove_from_comparison_requested
    /// based on the new check state. Ignored during populate_table /
    /// set_active_comparisons (we set check state programmatically there
    /// and don't want a feedback loop) — gated by suppress_check_signal_.
    void on_item_changed(QTableWidgetItem* item);

  private:
    void apply_peers_state(const services::query::QueryStore::State& s);
    void build_ui();
    void populate_table(const QVector<services::equity::PeerData>& peers);
    QStringList default_peers(const QString& symbol) const;
    /// Apply active_comp_set_ to the existing rows' column-0 checkboxes,
    /// taking care to suppress on_item_changed for the duration so we
    /// don't echo our own writes back as user toggles.
    void refresh_check_column();

    QString current_symbol_;
    QLineEdit* peers_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTableWidget* peer_table_ = nullptr;

    /// Current set of tickers that are overlaid on the Overview chart.
    /// Maintained by set_active_comparisons (signal from overview tab) so
    /// populate_table + refresh_check_column can render the right state.
    QSet<QString> active_comp_set_;
    /// Re-entrancy guard: itemChanged fires both for user clicks AND for
    /// our own setCheckState calls. True while we're writing checks
    /// programmatically — short-circuits on_item_changed.
    bool suppress_check_signal_ = false;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
