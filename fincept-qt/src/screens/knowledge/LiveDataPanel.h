#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;

namespace fincept::knowledge {

/// Renders the live values declared by an entry's `live_examples`.
/// Fetches via MarketDataService::fetch_info per unique ticker, then plucks
/// the requested metric per row. Values display as "—" while loading and
/// "n/a" if the metric isn't available.
class LiveDataPanel : public QWidget {
    Q_OBJECT
  public:
    explicit LiveDataPanel(const QVector<LiveExample>& examples, QWidget* parent = nullptr);

  private:
    void fetch_all();
    void apply_value(int row_index, double raw_value, bool ok);
    static QString format_metric(const QString& metric, double value);

    QVector<LiveExample> examples_;
    QGridLayout* grid_ = nullptr;
    QVector<QLabel*> value_labels_;
};

} // namespace fincept::knowledge
