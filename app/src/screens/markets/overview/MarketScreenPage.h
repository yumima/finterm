#pragma once
// MarketScreenPage — one screen of MARKETS: a canvas of widgets (the
// dashboard's grid canvas and widget registry) the user arranges — quote
// lists, market-condition widgets, futures widgets, a linked chart.
//
// Every screen has the same structure; they differ only in the widgets on
// them. A screen starts from a template (MarketTemplates.h — OVERVIEW,
// EQUITY, RATES …, or blank) and is then the user's: drag, resize,
// + WIDGET, a tile's ✕, each list edited in place. Saved per screen
// ("markets_screen:<id>").
//
// Clicks on this screen select through its own SelectionLink: its chart and
// futures widgets follow; other screens never do. The same widget may sit on
// several screens — they read the same hub topics, so one fetch serves all.

#include "screens/dashboard/canvas/GridLayout.h"

#include <QWidget>

class QScrollArea;
class QTimer;

namespace finterm::screens {

class DashboardCanvas;

class MarketScreenPage : public QWidget {
    Q_OBJECT
  public:
    MarketScreenPage(const QString& id, const QString& template_id, QWidget* parent = nullptr);

    QString id() const { return id_; }
    QString template_id() const { return template_; }
    DashboardCanvas* canvas() const { return canvas_; }

    void add_widget_dialog();
    /// Back to the screen's template.
    void reset_layout();
    void refresh_all();
    /// Load a layout (a duplicate's) and save it as this screen's.
    void load_layout(const GridLayout& layout);
    GridLayout layout() const;
    /// Drop this screen's saved layout (the screen is being deleted).
    void forget();
    /// Select `symbol` on this screen (its chart and futures widgets follow).
    void select(const QString& symbol, const QString& label);

  protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    void showEvent(QShowEvent* e) override;

  private:
    void save();
    void restore();
    /// Rows stretched to the view's height. A page is built on its first
    /// show, after its view got its size, so a load must fit by itself.
    void fit_to_view();
    QString key() const;

    QString id_;
    QString template_;
    DashboardCanvas* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QTimer* save_timer_ = nullptr;
    bool restored_ = false;
};

} // namespace finterm::screens
