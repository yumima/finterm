// src/screens/economics/panels/FinceptMacroPanel.cpp
// Fincept Macro — proprietary macro data source.
// The script fincept_macro.py does not yet exist.
// This panel shows a Coming Soon state with description of planned data.
// When fincept_macro.py is ready, implement on_fetch() and on_result() here.
#include "screens/economics/panels/FinceptMacroPanel.h"

#include "core/logging/Logger.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace fincept::screens {
namespace {

static constexpr const char* kFinceptMacroSourceId = "fincept";
static constexpr const char* kFinceptMacroColor = "#d97706"; // amber
} // namespace

FinceptMacroPanel::FinceptMacroPanel(QWidget* parent)
    : EconPanelBase(kFinceptMacroSourceId, kFinceptMacroColor, parent) {
    build_base_ui(this);
    // No service connection — Coming Soon panel
}

void FinceptMacroPanel::activate() {
    show_empty("Proprietary macro data — not available in this fork\n\n"
               "This panel was wired to a SaaS-only macro data source in the upstream\n"
               "project. The localhost fork does not have access to that source.\n\n"
               "For comparable data, use the FRED, DBnomics, or Gov Data panels.");
}

void FinceptMacroPanel::build_controls(QHBoxLayout* thl) {
    auto* lbl = new QLabel("PROPRIETARY MACRO — N/A IN THIS FORK");
    lbl->setStyleSheet(ctrl_label_style() + "letter-spacing:1px;");
    thl->addWidget(lbl);
}

void FinceptMacroPanel::on_fetch() {
    show_empty("Not available in this fork — see FRED / DBnomics / Gov Data instead.");
}

void FinceptMacroPanel::on_result(const QString& /*request_id*/, const services::EconomicsResult& /*result*/) {
    // No-op until fincept_macro.py is implemented
}

} // namespace fincept::screens
