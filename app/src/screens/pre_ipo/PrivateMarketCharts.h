// src/screens/pre_ipo/PrivateMarketCharts.h
#pragma once
// Charts for the Private Market screen.
//
//   price_history — the NPM-style company chart: the marketplace's daily MODEL
//                   price (line), the price per share of each priced round as
//                   a step (what the last primary/tender cleared at, held
//                   until the next one), and mutual-fund N-PORT marks as dots.
//   index_history — the C50 liquidity index.
//
// Both sit in a RangeChart: a row of 6M / 1Y / 2Y / MAX buttons over the
// chart, which rebuilds on click.

#include "screens/pre_ipo/PreIpoTypes.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

QT_FORWARD_DECLARE_CLASS(QChartView)
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace finterm::screens::widgets {

/// Range-switching chart host. `build(days)` returns a fresh chart for the
/// last `days` days (0 = everything) and may set a footnote.
class RangeChart : public QWidget {
    Q_OBJECT
  public:
    using Builder = std::function<QChartView*(int days, QString* note)>;
    explicit RangeChart(Builder build, int initial_days = 365, QWidget* parent = nullptr);

  private:
    void show_range(int days);
    Builder build_;
    QVBoxLayout* lay_ = nullptr;
    QChartView* view_ = nullptr;
    QLabel* note_ = nullptr;
    QVector<QPair<int, QPushButton*>> buttons_;
};

namespace pm_charts {

/// The company chart. Returns nullptr when there is nothing to plot (no model
/// series, fewer than two priced rounds and no marks). `note` says what was
/// left out (fund marks on another share basis).
QChartView* price_history(const pre_ipo::PrivateCompany& c, int days, QString* note);

/// True when price_history would plot something.
bool has_price_history(const pre_ipo::PrivateCompany& c);

/// The C50 index line.
QChartView* index_history(const QVector<pre_ipo::PricePoint>& pts, int days);

} // namespace pm_charts

} // namespace finterm::screens::widgets
