#pragma once
#include <QPushButton>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace fincept::screens::futures {

class FuturesPanelBase;
class FuturesWatchlistPanel;
class FuturesTermStructurePanel;
class FuturesSpreadPanel;
class FuturesSettlementsPanel;
class FuturesHeatmapPanel;
class FuturesChartPanel;
class FuturesChinaPanel;

/// Futures terminal — top-level screen.
///
/// Layout:
///   ┌─ Header bar: branding + asset class tab strip (INDEX RATES ENERGY …) ─┐
///   ├──────────────────────────────────────────────────────────────────────┤
///   │  ┌─ Heatmap (full width) ─────────────────────────────────────────┐  │
///   │  ├─ Watchlist ─┬─ Term Structure ─┬─ Continuous Chart ──────────  │  │
///   │  ├─ Settlements ─────────────────┬─ Spread ──────────────────────  │  │
///   │  └────────────────────────────────────────────────────────────────  │  │
///   └──────────────────────────────────────────────────────────────────────┘
///
/// Selecting CHINA replaces the panel grid with FuturesChinaPanel (akshare data).
class FuturesScreen : public QWidget {
    Q_OBJECT
  public:
    explicit FuturesScreen(QWidget* parent = nullptr);

  private:
    void build_header();
    void build_body();
    void set_active_class(const QString& cls);
    void refresh_all();
    void apply_theme();

    QString active_class_;

    // Header
    QWidget*               header_bar_  = nullptr;
    QVector<QPushButton*>  class_btns_;

    // Body
    QWidget*               body_         = nullptr;
    QWidget*               grid_host_    = nullptr;   // shown when not CHINA
    FuturesHeatmapPanel*   heatmap_      = nullptr;
    FuturesWatchlistPanel* watchlist_    = nullptr;
    FuturesTermStructurePanel* term_     = nullptr;
    FuturesChartPanel*     chart_        = nullptr;
    FuturesSettlementsPanel* settlements_ = nullptr;
    FuturesSpreadPanel*    spread_       = nullptr;
    FuturesChinaPanel*     china_        = nullptr;

    QTimer* refresh_timer_ = nullptr;
};

} // namespace fincept::screens::futures

// Convenience alias so MainWindow can reference it as `screens::FuturesScreen`.
namespace fincept::screens {
using FuturesScreen = futures::FuturesScreen;
}
