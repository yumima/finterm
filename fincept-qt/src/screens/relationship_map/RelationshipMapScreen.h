// src/screens/relationship_map/RelationshipMapScreen.h
#pragma once

#include "screens/IStatefulScreen.h"
#include "screens/relationship_map/RelationshipMapTypes.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

namespace fincept::relmap {
class RelationshipGraphScene;
class RelationshipGraphView;
} // namespace fincept::relmap

namespace fincept::screens {

/// Corporate Intelligence Relationship Map — full screen with search, graph,
/// filter panel, detail panel, legend, and status bar.
class RelationshipMapScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit RelationshipMapScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "relationship_map"; }
    int state_version() const override { return 1; }

    /// Load a ticker programmatically — same flow as the user typing it into
    /// the inline search and hitting ANALYZE. Used when the screen is hosted
    /// inside another screen (e.g. EQUITY RESEARCH's Relationships tab).
    void set_symbol(const QString& ticker);

    /// Hide the title bar, status bar, and brand chip. Use when embedding
    /// inside a host screen that has its own header — keeps the graph,
    /// filter panel, detail panel, layout selector, FIT button, and search
    /// (which still functions as a tab-local override). Default = full screen.
    void set_embedded_mode(bool on);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    void build_ui();
    void on_search();
    void on_search_text_changed(const QString& text);
    void fire_asset_search(const QString& query);
    void on_asset_results(const QJsonArray& results);
    void show_dropdown();
    void hide_dropdown();
    void on_data_ready(const fincept::relmap::RelationshipData& data);
    void on_fetch_failed(const QString& error);
    void on_progress(int percent, const QString& message);
    void on_node_selected();
    void rebuild_graph();
    void update_status_bar();
    QWidget* build_filter_panel();
    QWidget* build_detail_panel();
    QWidget* build_legend();

    // Graph
    relmap::RelationshipGraphScene* scene_ = nullptr;
    relmap::RelationshipGraphView* view_ = nullptr;

    // Header
    QWidget* header_bar_ = nullptr;  // wraps title + search + buttons — hidable for embedded mode
    QWidget* status_bar_ = nullptr;  // bottom brand strip — hidable for embedded mode
    QLineEdit* search_input_ = nullptr;
    QListWidget* search_dropdown_ = nullptr;
    QTimer* search_debounce_ = nullptr;
    QString pending_query_;
    QComboBox* layout_combo_ = nullptr;
    QPushButton* filter_btn_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLabel* progress_label_ = nullptr;

    // Panels
    QWidget* filter_panel_ = nullptr;
    QWidget* detail_panel_ = nullptr;
    QWidget* legend_widget_ = nullptr;

    // Detail panel labels
    QLabel* detail_title_ = nullptr;
    QLabel* detail_category_ = nullptr;
    QWidget* detail_props_container_ = nullptr;

    // Status bar
    QLabel* status_nodes_ = nullptr;
    QLabel* status_quality_ = nullptr;
    QLabel* status_brand_ = nullptr;

    // State
    relmap::FilterState filters_;
    relmap::LayoutMode layout_mode_ = relmap::LayoutMode::Layered;
    relmap::RelationshipData current_data_;
    bool has_data_ = false;
    QString loaded_ticker_;       // ticker currently shown in the graph
    // The ticker we most recently asked the service to fetch. Used to filter
    // data_ready emissions: RelationshipMapService is a singleton with a
    // single shared signal, so when ER embeds a RelationshipMapScreen the
    // embedded instance sees data_ready for *any* ticker the service emits —
    // including a stale AAPL fetch that races our IONQ request. Comparing
    // payload.company.ticker against this lets us discard the stale emission
    // instead of repainting the graph with the wrong company.
    QString pending_ticker_;
    bool search_input_focused_ = false; // only show dropdown when user is actively typing
};

} // namespace fincept::screens
