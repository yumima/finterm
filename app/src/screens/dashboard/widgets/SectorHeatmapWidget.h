#pragma once
#include "ui/RenderCoalescer.h"
#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"

#include <QGridLayout>
#include <QHash>
#include <QLabel>

namespace finterm::screens::widgets {

/// Sector heatmap — fetches sector ETFs (XLK, XLV, XLF, etc.) via yfinance
/// and renders a color-intensity grid.
///
/// Subscribes to `market:quote:<ETF>` on the DataHub for each sector ETF.
/// On every delivery the grid is rebuilt from `row_cache_` so intensity
/// tints stay in sync.
class SectorHeatmapWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit SectorHeatmapWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void apply_styles();
    void refresh_data();
    void populate(const QVector<services::QuoteData>& quotes);

    void hub_subscribe_all();
    void hub_unsubscribe_all();
    /// (Re)create the grid container/layout. Called from the ctor and again
    /// after a full-batch fetch error replaced it via set_error().
    void build_grid();
    /// Rebuild `QVector<QuoteData>` from the row cache (in declared symbol
    /// order) and hand to `populate()`.
    void rebuild_from_cache();

    QGridLayout* grid_ = nullptr;
    /// Persistent cells, built once per symbol set and then updated in place:
    /// rebuilding 33 styled widgets per live tick was the heatmap's whole cost.
    struct Cell {
        QWidget* frame = nullptr;
        QLabel* chg = nullptr;
        QString bg;        ///< tint last applied (restyle only when it changes)
        QString chg_color; ///< change colour last applied
    };
    QStringList cell_syms_; ///< symbols the grid holds, in order; empty = rebuild
    QHash<QString, Cell> cells_;
    QWidget* grid_container_ = nullptr;

    static QStringList sector_symbols();
    static QMap<QString, QString> sector_labels();

    QHash<QString, services::QuoteData> row_cache_;
    bool hub_active_ = false;

  private:
    /// Coalesces the per-quote re-render: one rebuild_from_cache() per tick batch.
    ui::RenderCoalescer quote_render_{this, [this]() { rebuild_from_cache(); }};
};

} // namespace finterm::screens::widgets
