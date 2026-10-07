// src/screens/economics/panels/AkShareChinaPanel.cpp
// AkShare China macroeconomic data panel. No API key required.
//
// Response shape: { success:true, data:[{...}], count, timestamp, source }
// Column names are in Chinese — displayed as-is in the table.
// Working commands: cpi, ppi, gdp, pmi
#include "screens/economics/panels/AkShareChinaPanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

namespace fincept::screens {
namespace {

static constexpr const char* kAkShareChinaScript = "akshare_economics_china.py";
static constexpr const char* kAkShareChinaSourceId = "akshare_cn";
static constexpr const char* kAkShareChinaColor = "#DC2626"; // China red
} // namespace

struct AkCnSeries {
    QString label;
    QString command;
    QString description;
    // akshare column feeding the stat cards (named in the title so the
    // reader knows which measure LATEST/CHANGE describe) and the period
    // column it is ordered by. akshare returns these newest-first.
    QString value_key;
    QString date_key;
    QString stats_label;
};

// GDP periods are cumulative-quarter strings ("2025年第1-2季度", "2025年第1季度")
// that do not sort chronologically as text, so GDP gets no date key: MIN/MAX/AVG
// only, LATEST/CHANGE "—".
static const QList<AkCnSeries> kAkShareSeries = {
    {"CPI (Consumer Price Index)", "cpi", "Monthly CPI year-on-year & month-on-month", QString::fromUtf8("全国-同比增长"),
     QString::fromUtf8("月份"), "national YoY %"},
    {"PPI (Producer Price Index)", "ppi", "Monthly PPI change rates", QString::fromUtf8("当月同比增长"),
     QString::fromUtf8("月份"), "YoY %"},
    {"GDP (Gross Domestic Product)", "gdp", "Quarterly GDP by expenditure approach",
     QString::fromUtf8("国内生产总值-同比增长"), QString(), "GDP YoY %"},
    {"PMI (Manufacturing & Services)", "pmi", "Monthly PMI — official NBS data", QString::fromUtf8("制造业-指数"),
     QString::fromUtf8("月份"), "manufacturing index"},
};

AkShareChinaPanel::AkShareChinaPanel(QWidget* parent)
    : EconPanelBase(kAkShareChinaSourceId, kAkShareChinaColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &AkShareChinaPanel::on_result);
}

void AkShareChinaPanel::activate() {
    show_empty("Select a series and click FETCH\n"
               "Source: AkShare — China NBS macroeconomic data (no API key required)\n"
               "Note: column headers are in Chinese as provided by the source");
}

void AkShareChinaPanel::build_controls(QHBoxLayout* thl) {
    auto* lbl = new QLabel("SERIES");
    lbl->setStyleSheet(ctrl_label_style());

    series_combo_ = new QComboBox;
    for (const auto& s : kAkShareSeries)
        series_combo_->addItem(s.label, s.command);
    series_combo_->setFixedHeight(26);
    series_combo_->setMinimumWidth(260);
    series_combo_->setToolTip("Data from China National Bureau of Statistics via AkShare");

    thl->addWidget(lbl);
    thl->addWidget(series_combo_);
}

void AkShareChinaPanel::on_fetch() {
    const int idx = series_combo_->currentIndex();
    const auto& series = kAkShareSeries[idx];

    show_loading("Fetching AkShare China: " + series.label + "…");
    services::EconomicsService::instance().execute(kAkShareChinaSourceId, kAkShareChinaScript, series.command, {},
                                                   "akcn_" + series.command);
}

void AkShareChinaPanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kAkShareChinaSourceId)
        return;
    if (!request_id.startsWith("akcn_"))
        return;
    if (!result.success) {
        show_error(result.error);
        return;
    }

    // Response: {success, data:[{...}], count}
    QJsonArray rows = result.data["data"].toArray();

    if (rows.isEmpty()) {
        const QString err = result.data["error"].toString();
        show_error(err.isEmpty() ? "No data returned" : err);
        return;
    }

    // request_id is "akcn_<command>": resolve the series from it, not from the
    // combo, so a selection change during the fetch can't mislabel the stats.
    const QString cmd = request_id.mid(5);
    const AkCnSeries* series = nullptr;
    for (const auto& s : kAkShareSeries)
        if (s.command == cmd)
            series = &s;
    if (!series) {
        display(rows, "AkShare China: " + cmd);
        return;
    }
    const QString title = "AkShare China: " + series->label + " — stats: " + series->stats_label;

    display(rows, title, series->value_key, series->date_key);
    LOG_INFO("AkShareChinaPanel", QString("Displayed %1 rows: %2").arg(rows.size()).arg(title));
}

} // namespace fincept::screens
