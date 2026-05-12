// src/screens/pre_ipo/PreIpoScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QHideEvent>
#include <QShowEvent>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace fincept::screens {

class CompanyListPanel;
class CompanyDetailPanel;
class IpoPipelinePanel;
class PicksView;
class ScreenerView;
class PipelineView;

/// PRE-IPO terminal — tabbed shell mapping to Scan / Research / Compare / Pick.
///
/// Tabs:
///   1. Picks    — daily digest of investable signals (default landing).
///   2. Screener — dense sortable universe table for scanning.
///   3. Markets  — per-company dossier (list + detail + pipeline rail).
///   4. Pipeline — full S-1 / F-1 pipeline view with amendment cadence.
class PreIpoScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit PreIpoScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "pre_ipo"; }
    int state_version() const override { return 2; }

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private slots:
    void on_data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void on_company_selected(const QString& id);
    void on_error(const QString& msg);

  private:
    void build_ui();
    QWidget* build_top_bar();
    QWidget* build_markets_tab();
    void refresh_theme();
    void switch_tab(int idx);

    // Tab bar
    QVector<QPushButton*> tab_btns_;
    QStackedWidget*       stack_ = nullptr;
    int                   active_tab_ = 0;

    // Views
    PicksView*          picks_view_     = nullptr;
    ScreenerView*       screener_view_  = nullptr;
    PipelineView*       pipeline_view_  = nullptr;
    // Markets tab is composed of these three legacy widgets in a splitter.
    CompanyListPanel*   list_panel_     = nullptr;
    CompanyDetailPanel* detail_panel_   = nullptr;
    IpoPipelinePanel*   pipeline_panel_ = nullptr;

    // Top bar
    QLabel* status_lbl_ = nullptr;

    // State
    QString selected_company_id_;
    fincept::pre_ipo::PreIpoSummary last_summary_;
    bool    data_loaded_ = false;
    bool    first_show_  = true;
};

} // namespace fincept::screens
