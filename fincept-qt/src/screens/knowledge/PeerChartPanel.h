#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QChartView;
class QLabel;

namespace fincept::knowledge {

/// Bar chart of a single metric across a list of peer tickers. Each bar
/// is one ticker; the metric is fetched via MarketDataService::fetch_info
/// and plucked from InfoData. Uses the entry's primary_metric.
class PeerChartPanel : public QWidget {
    Q_OBJECT
  public:
    PeerChartPanel(const QStringList& peers, const QString& metric, QWidget* parent = nullptr);

  private:
    void load();
    QStringList peers_;
    QString metric_;
    QChartView* view_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace fincept::knowledge
