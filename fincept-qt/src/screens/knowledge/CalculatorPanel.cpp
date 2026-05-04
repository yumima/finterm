#include "screens/knowledge/CalculatorPanel.h"

#include "ui/theme/Theme.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace fincept::knowledge {

namespace {
constexpr const char* MONO = "font-family: 'Consolas','Courier New',monospace;";

QString input_ss() {
    return QString("QDoubleSpinBox { background: %1; color: %2; border: 1px solid %3;"
                   " padding: 4px 6px; font-size: 12px; %4 }"
                   "QDoubleSpinBox:focus { border-color: %5; }"
                   "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; }")
        .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), MONO,
             ui::colors::AMBER());
}

QString format_result(double v, const QString& fmt) {
    if (std::isnan(v) || std::isinf(v))
        return QString::fromUtf8("—");
    if (fmt == "percent")
        return QString("%1%").arg(v * 100.0, 0, 'f', 2);
    if (fmt == "currency")
        return QString("$%1").arg(v, 0, 'f', 2);
    // ratio
    return QString("%1×").arg(v, 0, 'f', 2);
}

} // namespace

CalculatorPanel::CalculatorPanel(const Calculator& calc, QWidget* parent) : QWidget(parent), calc_(calc) {
    setStyleSheet("background: transparent;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto make_input = [&](const QString& label_text, double initial) -> QDoubleSpinBox* {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* lbl = new QLabel(label_text, this);
        lbl->setFixedWidth(120);
        lbl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; %2")
                               .arg(ui::colors::TEXT_SECONDARY(), MONO));
        row->addWidget(lbl);
        auto* sb = new QDoubleSpinBox(this);
        sb->setRange(0.0, 1e12);
        sb->setDecimals(2);
        sb->setSingleStep(0.5);
        sb->setValue(initial);
        sb->setStyleSheet(input_ss());
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        row->addWidget(sb, 1);
        root->addLayout(row);
        return sb;
    };

    num_input_ = make_input(calc_.numerator_label.isEmpty() ? "Numerator" : calc_.numerator_label,
                            calc_.numerator_default);
    den_input_ = make_input(calc_.denominator_label.isEmpty() ? "Denominator" : calc_.denominator_label,
                            calc_.denominator_default);

    // Result row
    auto* result_row = new QHBoxLayout;
    result_row->setSpacing(8);
    result_row->setContentsMargins(0, 4, 0, 0);
    auto* rl = new QLabel(calc_.result_label.isEmpty() ? "Result" : calc_.result_label, this);
    rl->setFixedWidth(120);
    rl->setStyleSheet(QString("color: %1; background: transparent; font-size: 11px; font-weight: bold;"
                              " letter-spacing: 0.5px; %2")
                          .arg(ui::colors::TEXT_TERTIARY(), MONO));
    result_row->addWidget(rl);
    result_label_ = new QLabel("—", this);
    result_label_->setStyleSheet(QString("color: %1; background: transparent; font-size: 14px; font-weight: bold; %2")
                                     .arg(ui::colors::AMBER(), MONO));
    result_row->addWidget(result_label_, 1);
    root->addLayout(result_row);

    connect(num_input_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { recompute(); });
    connect(den_input_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { recompute(); });

    recompute();
}

void CalculatorPanel::recompute() {
    const double n = num_input_->value();
    const double d = den_input_->value();
    if (d == 0.0) {
        result_label_->setText(QString::fromUtf8("—"));
        return;
    }
    result_label_->setText(format_result(n / d, calc_.result_format));
}

} // namespace fincept::knowledge
