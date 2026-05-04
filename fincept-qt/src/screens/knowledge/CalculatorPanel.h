#pragma once

#include "screens/knowledge/KnowledgeTypes.h"

#include <QString>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;

namespace fincept::knowledge {

/// Tiny interactive formula widget for "ratio" entries. Two numeric inputs
/// produce a live result. Supports result formats "ratio" / "percent" /
/// "currency". For now only "ratio" kind is implemented (price ÷ EPS,
/// price ÷ book, EV ÷ EBITDA, etc).
class CalculatorPanel : public QWidget {
    Q_OBJECT
  public:
    explicit CalculatorPanel(const Calculator& calc, QWidget* parent = nullptr);

  private:
    void recompute();

    Calculator calc_;
    QDoubleSpinBox* num_input_ = nullptr;
    QDoubleSpinBox* den_input_ = nullptr;
    QLabel* result_label_ = nullptr;
};

} // namespace fincept::knowledge
