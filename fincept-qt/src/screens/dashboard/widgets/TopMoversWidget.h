#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"
#include "ui/tables/DataTable.h"

#include <QHash>
#include <QPushButton>

#include <algorithm>

namespace fincept::screens::widgets {

/// Top movers widget with gainers/losers tab toggle.
///
/// Uses yfinance's `day_gainers` / `day_losers` predefined screeners so the
/// rows are the actual day's top movers across US equities — not a sort
/// over a hardcoded watchlist, which was the previous behavior and produced
/// misleading "biggest gainer" rows (e.g. PLTR +0.19%) on quiet days when
/// none of the 12 hardcoded symbols had a big move.
///
/// Sparklines are subscribed via DataHub for whichever symbols the screener
/// returned, so the inline mini-charts keep working.
class TopMoversWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit TopMoversWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void apply_styles();
    void refresh_data();
    void show_tab(bool gainers);
    /// (Re)create the tab bar + table. Called from the ctor and again after a
    /// fetch error tore the body down via set_error(), so a later successful
    /// refresh has live widgets to render into.
    void build_body();

    /// Fire-and-forget DataHub sparkline subscriptions for the symbols we
    /// just discovered from the screener. Idempotent — re-subscribes only
    /// for symbols we don't already have a sparkline cache entry for.
    void resubscribe_sparklines();
    /// Render the currently-selected gainers / losers list into the table.
    void rebuild_from_cache();

    ui::DataTable* table_ = nullptr;
    QPushButton* gainers_btn_ = nullptr;
    QPushButton* losers_btn_ = nullptr;
    bool showing_gainers_ = true;

    services::MarketDataService::TopMovers cached_movers_;
    QHash<QString, QVector<double>>        sparkline_cache_;
    QStringList                            subscribed_symbols_;
    bool                                   hub_active_ = false;
};

} // namespace fincept::screens::widgets
