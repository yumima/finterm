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

#include <functional>

class QTableWidget;

namespace finterm::screens {

class SelectionLink : public QObject {
    Q_OBJECT
  public:
    /// The link of the canvas `w` sits on (nearest DashboardCanvas
    /// ancestor, `w` itself included); nullptr when it is on none.
    static SelectionLink* of(QWidget* w);

    /// A click: every linked widget follows (charts redraw, futures widgets
    /// switch market).
    void select(const QString& symbol, const QString& label);
    /// Nothing clicked yet: what the page's chart already draws is the
    /// selection the lists mark — no widget is moved to it (a second chart
    /// keeps its own symbol, futures widgets their own market).
    void seed(const QString& symbol, const QString& label);
    /// The page re-arranged (loaded, RESET): nothing picked, nothing marked.
    void reset();
    QString symbol() const { return symbol_; }
    QString label() const { return label_; }
    /// The selection came from a click (select), not a chart's own symbol.
    bool picked() const { return picked_; }

    /// Mark, in `table`, the row that is the canvas's selection (the one its
    /// chart draws) — and no other: a row clicked in one list is unmarked when
    /// a row of another list on the same canvas is picked. `symbol_of(row)`
    /// is the row's symbol (empty: none). Off any canvas it does nothing; the
    /// link is found when the table is first shown, and the mark kept across
    /// the table's refreshes.
    static void follow_rows(QTableWidget* table, std::function<QString(int row)> symbol_of);

    /// A visible linked chart on this canvas (counted by the chart tiles).
    void add_chart() { ++charts_; }
    void remove_chart() { charts_ = charts_ > 0 ? charts_ - 1 : 0; }
    bool has_chart() const { return charts_ > 0; }

  signals:
    void selected(const QString& symbol, const QString& label);
    /// The marked symbol changed (select or seed) — for the row marks only.
    void marked(const QString& symbol);

  private:
    explicit SelectionLink(QObject* parent) : QObject(parent) {}
    QString symbol_, label_;
    bool picked_ = false;
    int charts_ = 0;
};

} // namespace finterm::screens
