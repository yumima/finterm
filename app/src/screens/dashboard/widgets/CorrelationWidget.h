#pragma once
// CorrelationWidget — a correlation matrix of daily changes over a rolling
// window (30 / 63 / 90 / 126 / 252 trading days, 63 = three months by
// default; a title-bar control).
//
// Data: each symbol's daily closes from ConditionService's cond:stats:<sym> —
// the same bars the condition rows and the chart's 5Y view read — paired on
// the dates both have (conditions::correlation: log returns for prices, first
// differences for yields). Cell colour: blue negative → red positive.
//
// Click a cell: the pair's rolling correlation chart. The symbol list is
// edited in place — the chips under the matrix ("×" removes, "+" adds) —
// config {"rows": [{symbol, label}], "window": 63}.

#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/dashboard/widgets/ConditionTableWidget.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QVector>

class QComboBox;
class QHBoxLayout;

namespace finterm::screens::widgets {

class CorrelationMatrix;

class CorrelationWidget : public BaseWidget {
    Q_OBJECT
  public:
    CorrelationWidget(const QString& title, QVector<ConditionRowDef> defaults, const QJsonObject& cfg = {},
                      QWidget* parent = nullptr);
    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void on_theme_changed() override;

  private:
    void subscribe_all();
    void unsubscribe_all();
    void render();
    void rebuild_chips();
    void add_row();
    QString label_of(const ConditionRowDef& r) const;

    QVector<ConditionRowDef> defaults_;
    QVector<ConditionRowDef> rows_;
    int window_ = 63;
    QHash<QString, QJsonObject> stats_; ///< by symbol
    QSet<QString> fresh_;
    bool subscribed_ = false;
    CorrelationMatrix* matrix_ = nullptr;
    QWidget* chips_ = nullptr;
    QHBoxLayout* chips_layout_ = nullptr;
    QComboBox* window_box_ = nullptr;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
