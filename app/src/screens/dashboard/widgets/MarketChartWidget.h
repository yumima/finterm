#pragma once
// MarketChartWidget — the market chart (MarketChartView, the chart window's
// own view) as a canvas tile, LINKED to the canvas it sits on: clicking a row
// in any other widget there loads that symbol here instead of opening the
// chart window (Bloomberg-style security linking). Used on Markets ▸ OVERVIEW
// and available on the DASHBOARD.
//
// Config: {"symbol": "^GSPC", "label": "S&P 500"} — the last symbol shown,
// so the tile reopens where it was left.

#include "screens/dashboard/widgets/BaseWidget.h"

#include <QJsonObject>
#include <QPointer>

namespace finterm::screens {
class MarketChartView;
class SelectionLink;
}

namespace finterm::screens::widgets {

class MarketChartWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit MarketChartWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);
    ~MarketChartWidget() override;
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void link(bool on);
    MarketChartView* view_ = nullptr;
    QString symbol_ = QStringLiteral("^GSPC");
    QString label_ = QStringLiteral("S&P 500");
    QPointer<SelectionLink> link_; ///< the selection of the canvas this chart is on
    bool counted_ = false;
};

} // namespace finterm::screens::widgets
