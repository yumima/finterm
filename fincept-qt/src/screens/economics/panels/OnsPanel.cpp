// src/screens/economics/panels/OnsPanel.cpp
// UK Office for National Statistics — GDP, inflation, labour market, trade, public debt.
// Script: ons_data.py  |  No API key required.
//
// Response shape: { success, series, label, unit, count, data:[{date, value}] }
#include "screens/economics/panels/OnsPanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

namespace fincept::screens {
namespace {

static constexpr const char* kOnsScript = "ons_data.py";
static constexpr const char* kOnsSourceId = "ons";
static constexpr const char* kOnsColor = "#005EB8"; // ONS blue
} // namespace

struct OnsSeries {
    QString label;
    QString command;
};

// Commands must match ONS_SERIES in ons_data.py (one CDID each).
static const QList<OnsSeries> kOnsSeries = {
    {"GDP (Chained Volume, SA)", "gdp"},       {"CPI Annual Rate", "cpi"},
    {"CPIH Annual Rate", "cpih"},              {"RPI Annual Rate", "rpi"},
    {"Unemployment Rate", "unemployment"},     {"Employment Rate (16-64)", "employment"},
    {"Trade Balance (BoP, quarterly)", "trade_balance"}, {"Average Weekly Earnings", "avg_earnings"},
    {"Public Sector Net Debt (% GDP)", "public_debt"},
};

OnsPanel::OnsPanel(QWidget* parent) : EconPanelBase(kOnsSourceId, kOnsColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &OnsPanel::on_result);
}

void OnsPanel::activate() {
    show_empty("Select a series and click FETCH\n"
               "Source: UK Office for National Statistics (no API key required)\n"
               "Data via ons.gov.uk time series — GDP, inflation, labour market, public finances");
}

void OnsPanel::build_controls(QHBoxLayout* thl) {
    auto* lbl = new QLabel("SERIES");
    lbl->setStyleSheet(ctrl_label_style());

    series_combo_ = new QComboBox;
    for (const auto& s : kOnsSeries)
        series_combo_->addItem(s.label, s.command);
    series_combo_->setFixedHeight(26);
    series_combo_->setMinimumWidth(240);

    thl->addWidget(lbl);
    thl->addWidget(series_combo_);
}

void OnsPanel::on_fetch() {
    const int idx = series_combo_->currentIndex();
    const auto& series = kOnsSeries[idx];

    show_loading("Fetching ONS: " + series.label + "…");
    services::EconomicsService::instance().execute(kOnsSourceId, kOnsScript, series.command, {},
                                                   "ons_" + series.command);
}

void OnsPanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kOnsSourceId)
        return;
    if (!request_id.startsWith("ons_"))
        return;
    if (!result.success) {
        show_error(result.error);
        return;
    }

    // Check inline error
    const QString inline_err = result.data["error"].toString();
    if (!inline_err.isEmpty()) {
        show_error(inline_err);
        return;
    }

    // Response: { success, series, label, unit, count, data:[{date, value}] }
    QJsonArray rows = result.data["data"].toArray();

    if (rows.isEmpty()) {
        show_error("No data returned");
        return;
    }

    const QString label = result.data["label"].toString();
    const QString title =
        "ONS: " +
        (label.isEmpty() ? (series_combo_->currentIndex() >= 0 ? kOnsSeries[series_combo_->currentIndex()].label
                                                               : request_id.mid(4))
                         : label); // the script's label already carries the unit

    display(rows, title, QStringLiteral("value"), QStringLiteral("date"));
    LOG_INFO("OnsPanel", QString("Displayed %1 rows: %2").arg(rows.size()).arg(title));
}

} // namespace fincept::screens
