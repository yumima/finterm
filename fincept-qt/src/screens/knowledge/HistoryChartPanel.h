#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QChartView;

namespace fincept::knowledge {

/// Mini sparkline chart of a primary ticker's price history (1y daily).
/// Shown in the rail alongside live values. The metric label clarifies
/// what the sparkline tracks (always close price for v1 — the relationship
/// to the entry's metric is implicit and noted in the label).
class HistoryChartPanel : public QWidget {
    Q_OBJECT
  public:
    HistoryChartPanel(const QString& ticker, const QString& period, QWidget* parent = nullptr);

  private:
    void load();

    QString ticker_;
    QString period_;
    QChartView* view_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace fincept::knowledge
