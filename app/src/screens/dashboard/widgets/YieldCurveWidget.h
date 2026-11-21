#pragma once
// YieldCurveWidget — the US Treasury par yield curve: today's against a month
// and a year ago, and a list of spreads (2s10s, 3M10Y, 5s30s by default) with
// their 1-day / 1-month change and where they sit in the past year.
//
// One source: ConditionService's cond:ust — the Treasury's daily par curve
// (end of day), the same script and cache entry Markets ▸ WORLD reads its 2Y
// and 2s10s from. Painted from the last answer at once, refreshed by the hub.
// Click a tenor on the curve or a spread row: its chart. The spread list is
// edited in place (+ ADD / −), config {"spreads": ["y10-y2", …]}.

#include "screens/dashboard/widgets/BaseWidget.h"
#include "ui/RenderCoalescer.h"

#include <QDate>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace finterm::ui {
class DataTable;
}

namespace finterm::screens::widgets {

class YieldCurveCanvas;

class YieldCurveWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit YieldCurveWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

    /// Tenor keys in maturity order and their labels.
    static const QStringList& tenors();
    static QString tenor_label(const QString& key);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void render();
    void add_spread();
    QJsonObject data_;
    bool fresh_ = false;
    QString error_;
    bool subscribed_ = false;
    QStringList spreads_;
    YieldCurveCanvas* canvas_ = nullptr;
    ui::DataTable* table_ = nullptr;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
