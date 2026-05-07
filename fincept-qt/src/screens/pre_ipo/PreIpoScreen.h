// src/screens/pre_ipo/PreIpoScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QHideEvent>
#include <QLabel>
#include <QShowEvent>
#include <QWidget>

namespace fincept::screens {

class CompanyListPanel;
class CompanyDetailPanel;
class IpoPipelinePanel;

/// PRE-IPO screen — three-panel layout tracking private companies, unicorns,
/// SEC filings, and the IPO pipeline.
///
/// Layout (left-to-right via QSplitter):
///   CompanyListPanel (240px fixed) | CompanyDetailPanel (flex) | IpoPipelinePanel (220px fixed)
class PreIpoScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit PreIpoScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "pre_ipo"; }
    int state_version() const override { return 1; }

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
    void refresh_theme();

    // ── Sub-panels ────────────────────────────────────────────────────────────
    CompanyListPanel*   list_panel_    = nullptr;
    CompanyDetailPanel* detail_panel_  = nullptr;
    IpoPipelinePanel*   pipeline_panel_ = nullptr;

    // ── Top bar ───────────────────────────────────────────────────────────────
    QLabel* status_lbl_ = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    QString selected_company_id_;
    bool    data_loaded_ = false;
    bool    first_show_  = true;
};

} // namespace fincept::screens
