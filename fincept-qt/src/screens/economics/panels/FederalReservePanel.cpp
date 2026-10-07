// src/screens/economics/panels/FederalReservePanel.cpp
// US Federal Reserve data — Fed Funds Rate, SOFR, Treasury Rates, Yield Curve, Money Supply.
// Script: federal_reserve_data.py  |  No API key required.
//
// Response shape: { success, endpoint, data:[{date, rate|...}] }
#include "screens/economics/panels/FederalReservePanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>

#include <algorithm>
#include <cmath>

namespace fincept::screens {
namespace {

static constexpr const char* kFederalReserveScript = "federal_reserve_data.py";
static constexpr const char* kFederalReserveSourceId = "federal_reserve";
static constexpr const char* kFederalReserveColor = "#DC2626"; // Fed red
} // namespace

struct FedSeries {
    QString label;
    QString command;
    QStringList args;
    // Headline column + date column for the stat cards; empty value_key for
    // multi-column / cross-sectional tables (no single series → "—").
    QString value_key = {};
    QString date_key = {};
};

static const QList<FedSeries> kFedReserveSeries = {
    {"Federal Funds Rate", "federal_funds_rate", {}, "rate", "date"},
    {"SOFR Overnight Rate", "sofr_rate", {}, "rate", "date"},
    {"Treasury Rates (Yield Curve)", "treasury_rates", {}},
    {"Yield Curve (single date)", "yield_curve", {}},
    {"Money Supply (M1/M2; stats: M2)", "money_measures", {}, "M2", "month"},
    // Central Bank Holdings omitted: the script's SOMA endpoint is an unimplemented stub.
};

FederalReservePanel::FederalReservePanel(QWidget* parent)
    : EconPanelBase(kFederalReserveSourceId, kFederalReserveColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &FederalReservePanel::on_result);
}

void FederalReservePanel::activate() {
    show_empty("Select a series and click FETCH\n"
               "Source: Federal Reserve Economic Data — federal_reserve_data.py\n"
               "No API key required");
}

void FederalReservePanel::build_controls(QHBoxLayout* thl) {
    auto* lbl = new QLabel("SERIES");
    lbl->setStyleSheet(ctrl_label_style());

    series_combo_ = new QComboBox;
    for (const auto& s : kFedReserveSeries)
        series_combo_->addItem(s.label, s.command);
    series_combo_->setFixedHeight(26);
    series_combo_->setMinimumWidth(240);

    thl->addWidget(lbl);
    thl->addWidget(series_combo_);
}

void FederalReservePanel::on_fetch() {
    const int idx = series_combo_->currentIndex();
    const auto& series = kFedReserveSeries[idx];

    show_loading("Fetching Federal Reserve: " + series.label + "…");
    services::EconomicsService::instance().execute(kFederalReserveSourceId, kFederalReserveScript, series.command,
                                                   series.args, "fed_" + series.command);
}

void FederalReservePanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kFederalReserveSourceId)
        return;
    if (!request_id.startsWith("fed_"))
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

    // Response: { success, endpoint, data:[{date, <value cols>}] }
    QJsonArray rows = result.data["data"].toArray();

    // Some endpoints nest differently — handle yield_curve which may use "rates" key
    if (rows.isEmpty())
        rows = result.data["rates"].toArray();

    // federal_reserve_data.py returns rates as decimal fractions (0.0388).
    // Show them in percent like every other rate in the terminal. Volumes and
    // money-stock levels are not rates and are left alone.
    auto is_rate_key = [](const QString& k) {
        return k == QLatin1String("rate") || k.startsWith(QLatin1String("target_range_")) ||
               k.startsWith(QLatin1String("percentile_")) || k.startsWith(QLatin1String("intraday_")) ||
               k == QLatin1String("standard_deviation") || k.startsWith(QLatin1String("month_")) ||
               k.startsWith(QLatin1String("year_"));
    };

    // Filter: keep rows that have at least one non-empty numeric-ish value beyond "date"
    QJsonArray clean;
    for (const auto& rv : rows) {
        QJsonObject r = rv.toObject();
        for (auto it = r.begin(); it != r.end(); ++it)
            if (is_rate_key(it.key()) && it.value().isDouble())
                it.value() = std::round(it.value().toDouble() * 100.0 * 1e6) / 1e6; // % with fp noise trimmed
        bool has_val = false;
        for (auto it = r.begin(); it != r.end(); ++it) {
            if (it.key() == "date")
                continue;
            if (it.value().isDouble() || !it.value().toString().isEmpty()) {
                has_val = true;
                break;
            }
        }
        if (has_val)
            clean.append(r);
    }

    if (clean.isEmpty()) {
        show_error("No data returned");
        return;
    }

    // Resolve the series from the request id ("fed_<command>"), not the combo.
    const QString cmd = request_id.mid(4);
    int idx = -1;
    for (int i = 0; i < kFedReserveSeries.size(); ++i)
        if (kFedReserveSeries[i].command == cmd)
            idx = i;

    // The yield curve arrives sorted by maturity *string* (month_1, …, year_1,
    // year_10, year_2, …); order it by tenor.
    if (cmd == QLatin1String("yield_curve")) {
        auto tenor_months = [](const QString& m) {
            const int n = m.section('_', 1, 1).toInt();
            return m.startsWith(QLatin1String("year_")) ? n * 12 : n;
        };
        QList<QJsonValue> sorted(clean.begin(), clean.end());
        std::stable_sort(sorted.begin(), sorted.end(), [&](const QJsonValue& a, const QJsonValue& b) {
            return tenor_months(a.toObject()["maturity"].toString()) < tenor_months(b.toObject()["maturity"].toString());
        });
        clean = QJsonArray();
        for (const auto& v : sorted)
            clean.append(v);
    }

    const bool known = idx >= 0;
    const QString title = "Federal Reserve: " + (known ? kFedReserveSeries[idx].label : cmd) +
                          (cmd == QLatin1String("money_measures") ? QStringLiteral(" ($ billions)")
                                                                  : QStringLiteral(" (rates in %)"));

    display(clean, title, known ? kFedReserveSeries[idx].value_key : QString(),
            known ? kFedReserveSeries[idx].date_key : QString());
    LOG_INFO("FederalReservePanel", QString("Displayed %1 rows: %2").arg(clean.size()).arg(title));
}

} // namespace fincept::screens
