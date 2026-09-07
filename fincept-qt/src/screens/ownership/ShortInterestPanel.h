#pragma once
#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QLabel;

namespace fincept::screens {

/// SHORT INTEREST — FINRA's settled position, twice a month, drawn as a bar
/// chart of the last year, with the three figures that matter beside it.
///
/// The chart is one series with a labelled axis and the latest bar named;
/// the reader's question is "is it building or covering", which a dozen bars
/// answer at a glance and a scatter never will. Days to cover and short
/// interest ÷ 13F shares are the two measures with evidence behind them; the
/// daily short-volume ratio is one line, read as a trend against itself.
class ShortInterestPanel : public QWidget {
    Q_OBJECT
  public:
    explicit ShortInterestPanel(QWidget* parent = nullptr);

    void set_snapshot(const ownership::OwnershipSnapshot& snap, bool loading);
    void clear();

  private:
    class Chart;
    QLabel* title_note_ = nullptr;
    Chart*  chart_ = nullptr;
    QLabel* figures_ = nullptr;
    QLabel* foot_ = nullptr;
};

} // namespace fincept::screens
