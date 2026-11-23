#pragma once
// SelectionLink — the symbol selected on ONE canvas (a Markets screen, the
// dashboard): a click on any row there selects it, and the canvas's linked
// widgets follow — the chart, the futures curve / months / spread / COT /
// terms / ticket. Clicks on another canvas never reach it (each screen has its
// own link; Bloomberg's per-monitor security linking).
//
// One link per DashboardCanvas, created on first use as the canvas's child.
// MarketChartDialog::show_for() selects through the link of the canvas the
// clicked widget sits on, and opens the chart window only when that canvas
// shows no linked chart.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

namespace finterm::screens {

class SelectionLink : public QObject {
    Q_OBJECT
  public:
    /// The link of the canvas `w` sits on (nearest DashboardCanvas
    /// ancestor, `w` itself included); nullptr when it is on none.
    static SelectionLink* of(QWidget* w);

    void select(const QString& symbol, const QString& label);
    QString symbol() const { return symbol_; }
    QString label() const { return label_; }

    /// A visible linked chart on this canvas (counted by the chart tiles).
    void add_chart() { ++charts_; }
    void remove_chart() { charts_ = charts_ > 0 ? charts_ - 1 : 0; }
    bool has_chart() const { return charts_ > 0; }

  signals:
    void selected(const QString& symbol, const QString& label);

  private:
    explicit SelectionLink(QObject* parent) : QObject(parent) {}
    QString symbol_, label_;
    int charts_ = 0;
};

} // namespace finterm::screens
