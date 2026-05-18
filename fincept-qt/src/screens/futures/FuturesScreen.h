#pragma once
#include <QHideEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
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
class FuturesCotPanel;
class FuturesExpiryPanel;

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
/// All asset-class tabs (including CHINA) share the same grid; CHINA's
/// underlying symbols are onshore-index spot proxies in the ContractDef
/// catalog. The akshare-backed FuturesChinaPanel that used to replace the
/// grid has been retired — coverage of SHFE/DCE commodity futures will
/// land back as a panel within the grid in a follow-up pass.
class FuturesScreen : public QWidget {
    Q_OBJECT
  public:
    explicit FuturesScreen(QWidget* parent = nullptr);

  protected:
    // Visibility-aware refresh: when the screen is hidden, suspend the
    // local refresh tick and release the shared FuturesQuoteCache. When
    // shown, resume both. Eliminates background Yahoo traffic when the
    // user isn't looking at FUTURES.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void build_header();
    void build_body();
    /// User-facing: handles tab-button click. Coalesces rapid clicks via
    /// class_change_timer_ so each panel only re-fetches once when the user
    /// lands on a class, instead of cascading async fetches per click.
    void request_active_class(const QString& cls);
    /// Internal: actually propagate the class change to every panel. Called
    /// only after the debounce timer fires (or immediately on first set).
    void apply_active_class(const QString& cls);
    void refresh_all();
    void apply_theme();

    QString active_class_;
    QString pending_class_;
    QTimer* class_change_timer_ = nullptr;

    // Header
    QWidget*               header_bar_  = nullptr;
    QVector<QPushButton*>  class_btns_;

    // Body
    QWidget*               body_         = nullptr;
    QWidget*               grid_host_    = nullptr;
    FuturesHeatmapPanel*   heatmap_      = nullptr;
    FuturesWatchlistPanel* watchlist_    = nullptr;
    FuturesTermStructurePanel* term_     = nullptr;
    FuturesChartPanel*     chart_        = nullptr;
    FuturesSettlementsPanel* settlements_ = nullptr;
    FuturesSpreadPanel*    spread_       = nullptr;
    FuturesCotPanel*       cot_          = nullptr;
    FuturesExpiryPanel*    expiry_       = nullptr;

    QTimer* refresh_timer_ = nullptr;

    // Row splitters held as members so we can:
    //   1) align column widths on first show (compute rails from body width
    //      rather than rely on differing sizeHint() between row1 and row2)
    //   2) keep the two rows in lockstep via splitterMoved sync — dragging
    //      a divider in either row mirrors the other so the Bloomberg-style
    //      grid keeps its column alignment.
    QSplitter* row1_split_ = nullptr;
    QSplitter* row2_split_ = nullptr;
    bool       syncing_splits_ = false;
    bool       initial_align_done_ = false;
};

} // namespace fincept::screens::futures

// Convenience alias so MainWindow can reference it as `screens::FuturesScreen`.
namespace fincept::screens {
using FuturesScreen = futures::FuturesScreen;
}
