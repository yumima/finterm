#pragma once
// MarketsOverview — Markets ▸ OVERVIEW: a configurable canvas (the dashboard's
// own grid canvas and widget registry) of quote lists, market-condition
// widgets and a linked chart. Drag, resize, add (+ WIDGET), remove (a tile's
// ✕), edit each list in place; saved per user ("markets_overview_layout").
//
// Security linking: a Chart (linked) tile on this canvas takes every click
// made on it (MarketChartDialog::show_for routes there).
//
// First open: the user's old PANELS become Quote List tiles (title, symbols
// and columns kept) and the old CONDITIONS page's lists keep their edits.

#include "screens/dashboard/canvas/GridLayout.h"

#include <QWidget>

class QScrollArea;
class QTimer;

namespace finterm::screens {

class DashboardCanvas;

class MarketsOverview : public QWidget {
    Q_OBJECT
  public:
    explicit MarketsOverview(QWidget* parent = nullptr);

    void add_widget_dialog();
    void reset_layout();
    void refresh_all();

    /// The first-open layout: migrated panels, chart, condition widgets.
    static GridLayout default_layout();

  protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;

  private:
    void save();
    void restore();
    DashboardCanvas* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QTimer* save_timer_ = nullptr;
    bool restored_ = false;
};

} // namespace finterm::screens
