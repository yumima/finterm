#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace fincept::knowledge {

/// "Your portfolio exposure" — pulls active holdings from PortfolioHoldings
/// and flags those matching the entry's exposure criterion (e.g. "P/E > 30").
/// If the user has no portfolio data, shows a friendly empty state.
class ExposurePanel : public QWidget {
    Q_OBJECT
  public:
    /// @param entry the knowledge entry; uses entry.exposure_criterion +
    ///        entry.exposure_tickers to drive analysis.
    explicit ExposurePanel(const KnowledgeEntry& entry, QWidget* parent = nullptr);

  private:
    void load_holdings_and_evaluate();
    void render_results(int total, const QStringList& matching, const QString& criterion);

    KnowledgeEntry entry_;
    QVBoxLayout* root_ = nullptr;
    QLabel* status_ = nullptr;
};

} // namespace fincept::knowledge
