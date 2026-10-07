// src/screens/economics/panels/FiscalDataPanel.cpp
// US Treasury FiscalData — debt to penny, interest rates, interest expense,
// rates of exchange, record-setting auctions.
// Script: fiscal_data.py  |  No API key required.
//
// Response shape: { data:[{record_date, <value cols>}] } for the raw commands,
// { data:[{date, value}] } for `fetch <indicator>`.
#include "screens/economics/panels/FiscalDataPanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

namespace fincept::screens {
namespace {

static constexpr const char* kFiscalDataScript = "fiscal_data.py";
static constexpr const char* kFiscalDataSourceId = "fiscal_data";
static constexpr const char* kFiscalDataColor = "#0EA5E9"; // Treasury blue
} // namespace

struct FiscalSeries {
    QString label;
    QString command;
    QStringList args;
    // Row fields for the stat cards. Empty value_key = a cross-sectional
    // table (several securities/currencies per date) with no single series.
    QString value_key;
    QString date_key;
};

// Single-series entries use the script's `fetch` command, which filters the
// endpoint to one security / one total and returns {date, value}.
static const QList<FiscalSeries> kFiscalDataSeries = {
    {"Debt to the Penny (Total Public Debt Outstanding)", "debt-to-penny", {"--all"}, "tot_pub_debt_out_amt",
     "record_date"},
    {"Avg Interest Rate — Total Interest-bearing Debt", "fetch", {"avg_rate_total"}, "value", "date"},
    {"Avg Interest Rate — Treasury Bills", "fetch", {"avg_rate_tbills"}, "value", "date"},
    {"Avg Interest Rate — Treasury Notes", "fetch", {"avg_rate_tnotes"}, "value", "date"},
    {"Avg Interest Rate — Treasury Bonds", "fetch", {"avg_rate_tbonds"}, "value", "date"},
    {"Avg Interest Rate — TIPS", "fetch", {"avg_rate_tips"}, "value", "date"},
    {"Avg Interest Rate — Floating Rate Notes", "fetch", {"avg_rate_frn"}, "value", "date"},
    {"Avg Interest Rates — All Securities (table)", "avg-interest-rates", {"--all"}, {}, {}},
    {"Interest Expense — Monthly Total", "fetch", {"interest_expense_monthly"}, "value", "date"},
    {"Interest Expense — by Category (table)", "interest-expense", {"--all"}, {}, {}},
    {"Treasury Reporting Rates of Exchange (per USD, table)", "exchange-rates", {"--limit=1000"}, {}, {}},
    {"Record-Setting Auctions (table)", "record-auctions", {}, {}, {}},
};

FiscalDataPanel::FiscalDataPanel(QWidget* parent) : EconPanelBase(kFiscalDataSourceId, kFiscalDataColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &FiscalDataPanel::on_result);
}

void FiscalDataPanel::activate() {
    show_empty("Select a dataset and click FETCH\n"
               "Source: US Treasury FiscalData API (fiscaldata.treasury.gov)\n"
               "No API key required");
}

void FiscalDataPanel::build_controls(QHBoxLayout* thl) {
    auto* lbl = new QLabel("DATASET");
    lbl->setStyleSheet(ctrl_label_style());

    series_combo_ = new QComboBox;
    for (const auto& s : kFiscalDataSeries)
        series_combo_->addItem(s.label, s.command);
    series_combo_->setFixedHeight(26);
    series_combo_->setMinimumWidth(240);

    thl->addWidget(lbl);
    thl->addWidget(series_combo_);
}

void FiscalDataPanel::on_fetch() {
    const int idx = series_combo_->currentIndex();
    const auto& series = kFiscalDataSeries[idx];

    show_loading("Fetching FiscalData: " + series.label + "…");

    // Encode the dataset index so the result is interpreted with the keys of
    // the dataset that was requested, even if the combo changed meanwhile.
    services::EconomicsService::instance().execute(kFiscalDataSourceId, kFiscalDataScript, series.command, series.args,
                                                   "fiscal_" + QString::number(idx) + "_" +
                                                       QString(series.command).replace('-', '_'));
}

void FiscalDataPanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kFiscalDataSourceId)
        return;
    if (!request_id.startsWith("fiscal_"))
        return;
    if (!result.success) {
        show_error(result.error);
        return;
    }

    const QString inline_err = result.data["error"].toString();
    if (!inline_err.isEmpty()) {
        show_error(inline_err);
        return;
    }

    // Response: { data:[{record_date, ...}] }
    QJsonArray rows = result.data["data"].toArray();

    if (rows.isEmpty()) {
        show_error("No data returned");
        return;
    }

    bool idx_ok = false;
    const int idx = request_id.section('_', 1, 1).toInt(&idx_ok);
    if (!idx_ok || idx < 0 || idx >= kFiscalDataSeries.size()) {
        // Unknown dataset (e.g. a stale cached request id): show the table,
        // but don't guess a value column.
        display(rows, "FiscalData: " + request_id.mid(7));
        return;
    }
    const auto& series = kFiscalDataSeries[idx];
    const QString title = "FiscalData: " + series.label;

    display(rows, title, series.value_key, series.date_key);
    LOG_INFO("FiscalDataPanel", QString("Displayed %1 rows: %2").arg(rows.size()).arg(title));
}

} // namespace fincept::screens
