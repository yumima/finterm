// src/screens/pre_ipo/PreIpoScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QHideEvent>
#include <QShowEvent>
#include <QWidget>

namespace fincept::screens {

namespace widgets { class IpoWatchView; }

/// IPO Watch — single-pane Bloomberg-style research screen.
///
/// Hosts a `widgets::IpoWatchView` as its only child. The class remains here
/// (rather than registering IpoWatchView directly with the dock router) so
/// the existing PreIpoService signal wiring stays available for a future
/// EDGAR-driven PIPELINE lens.
class PreIpoScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit PreIpoScreen(QWidget* parent = nullptr);

    // IStatefulScreen
    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "pre_ipo"; }
    int state_version() const override { return 3; }

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private slots:
    // Kept as no-ops — PreIpoService still emits these but the consolidated
    // IpoWatchView fetches its own data via HttpClient + MarketDataService.
    void on_data_loaded(fincept::pre_ipo::PreIpoSummary summary);
    void on_company_selected(const QString& id);
    void on_error(const QString& msg);

  private:
    void build_ui();
    void refresh_theme();

    widgets::IpoWatchView* ipo_watch_view_ = nullptr;

    // Selection persistence — kept across the v2→v3 state migration so a
    // restored layout doesn't lose the user's previously-focused deal id.
    QString selected_company_id_;
    bool    first_show_ = true;
};

} // namespace fincept::screens
