#pragma once
#include "screens/dashboard/widgets/QuoteTableWidget.h"

#include <QHash>
#include <QLineEdit>
#include <QTimer>

namespace fincept::ui {
class InlineSparkline;
}

namespace fincept::screens::widgets {

/// Watchlist widget — user provides comma-separated symbols, fetches live data via yfinance.
///
/// The widget subscribes to `market:quote:<sym>` on the DataHub for each
/// symbol in the user's current symbol set. Because the symbol set is
/// dynamic (user edits the input), re-subscribe on every GO click and on
/// showEvent; unsubscribe on hideEvent.
class WatchlistWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit WatchlistWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void apply_styles();
    void refresh_data();
    void populate(const QVector<services::QuoteData>& quotes);

    /// (Re)subscribe to the hub for the current `symbols_` set.
    void hub_resubscribe();
    /// Tear down all hub subscriptions for this widget.
    void hub_unsubscribe_all();
    /// Update the table from `row_cache_` in place (rows built once per set).
    void render_from_cache();
    /// (Re)build the table's persistent rows for the current `symbols_` set.
    /// One row + one InlineSparkline per symbol, keyed by symbol.
    void rebuild_rows();

    QLineEdit* symbols_input_ = nullptr;
    QLabel* symbols_label_ = nullptr;
    QPushButton* go_btn_ = nullptr;
    ui::DataTable* table_ = nullptr;
    QStringList symbols_;

    // Persistent per-symbol row mapping: symbol -> table row index, and the
    // reusable sparkline cell widget. Built once per symbol set; cells updated
    // in place rather than clear_data() + re-add every tick.
    QHash<QString, int> row_index_;
    QHash<QString, ui::InlineSparkline*> spark_widgets_;

    QHash<QString, fincept::services::QuoteData> row_cache_;
    QHash<QString, QVector<double>>              sparkline_cache_;
    bool hub_active_ = false;

    // Coalesce quote + sparkline callbacks (2N/cycle) into one in-place update.
    QTimer* coalesce_ = nullptr;
};

} // namespace fincept::screens::widgets
