// src/screens/economics/panels/EurostatPanel.cpp
// Eurostat extra data panel.
//
// Response shape (Eurostat SDMX-JSON v2.0):
//   { version, class, label, source, updated,
//     value: {"0": 78.9, "1": 78.0, ...},   <- flat indexed values
//     id:    ["freq","indic_bt",...,"geo","time"],
//     size:  [1, 1, ..., 1, N],               <- last dim is time
//     dimension: {
//       time: { category: { index: {"1953-01":0, ...}, label: {"1953-01":"1953-01",...} } }
//       ...
//     }
//   }
// Flatten: value index i -> time position = i % time_size -> period string via index map.
#include "screens/economics/panels/EurostatPanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QSet>
#include <QVector>

namespace fincept::screens {
namespace {

static constexpr const char* kEurostatScript = "eurostat_extra_data.py";
static constexpr const char* kEurostatSourceId = "eurostat";
static constexpr const char* kEurostatColor = "#0369A1"; // EU blue

struct EurostatDataset {
    QString label;
    QString command;
    bool has_country; // whether this command takes a country arg
};

static const QList<EurostatDataset> kEurostatDatasets = {
    {"Industrial Production (2015=100, SA)", "industrial", true},
    {"Retail Trade Volume (2015=100, SA)", "retail", true},
    {"Primary Energy Production (GWh)", "energy", true},
    {"Goods Trade Balance vs World (EUR mn, SA)", "trade", true},
    {"Construction Output (2015=100, SA)", "construction", true},
    {"Nights at Tourist Accommodation", "tourism", true},
};

static const QList<QPair<QString, QString>> kEurostatCountries = {
    {"Germany", "DE"},     {"France", "FR"},      {"Italy", "IT"},          {"Spain", "ES"},   {"Netherlands", "NL"},
    {"Poland", "PL"},      {"Belgium", "BE"},     {"Sweden", "SE"},         {"Austria", "AT"}, {"Denmark", "DK"},
    {"Finland", "FI"},     {"Portugal", "PT"},    {"Czech Republic", "CZ"}, {"Romania", "RO"}, {"Hungary", "HU"},
    {"EU27", "EU27_2020"}, {"Euro Area", "EA20"},
};

} // namespace

// ── SDMX-JSON flattener ───────────────────────────────────────────────────────

QJsonArray EurostatPanel::flatten_sdmx(const QJsonObject& response, int* series_count) {
    if (series_count)
        *series_count = 0;
    if (response.contains("error"))
        return {};

    const QJsonArray id_arr = response["id"].toArray();
    const QJsonArray size_arr = response["size"].toArray();
    const QJsonObject dims = response["dimension"].toObject();
    const QJsonObject vals = response["value"].toObject();

    if (id_arr.isEmpty() || id_arr.size() != size_arr.size() || vals.isEmpty())
        return {};

    // JSON-stat is row-major over id[] with sizes size[]. Decode every
    // dimension (not just time) so observations of different series are
    // never collapsed into one {period, value} stream.
    const int ndim = id_arr.size();
    QVector<int> sizes(ndim);
    QVector<QMap<int, QString>> labels(ndim); // position -> category label (code for time)
    int time_dim = -1;
    for (int d = 0; d < ndim; ++d) {
        const QString id = id_arr[d].toString();
        sizes[d] = qMax(1, size_arr[d].toInt(1));
        if (id == QLatin1String("time"))
            time_dim = d;
        const QJsonObject cat = dims[id].toObject()["category"].toObject();
        const QJsonObject index = cat["index"].toObject();
        const QJsonObject label = cat["label"].toObject();
        for (auto it = index.constBegin(); it != index.constEnd(); ++it)
            labels[d][it.value().toInt()] = (id == QLatin1String("time")) ? it.key() : label.value(it.key()).toString(it.key());
    }
    if (time_dim < 0)
        return {};

    QSet<QString> series_ids;
    QList<QJsonValue> sorted;
    for (auto it = vals.constBegin(); it != vals.constEnd(); ++it) {
        if (!it.value().isDouble())
            continue; // null / missing observation — skip, never 0
        int flat = it.key().toInt();
        QVector<int> pos(ndim);
        for (int d = ndim - 1; d >= 0; --d) {
            pos[d] = flat % sizes[d];
            flat /= sizes[d];
        }
        QJsonObject row;
        row["period"] = labels[time_dim].value(pos[time_dim], QString::number(pos[time_dim]));
        row["value"] = it.value().toDouble();
        QString sid;
        for (int d = 0; d < ndim; ++d) {
            if (d == time_dim || sizes[d] <= 1)
                continue;
            const QString lbl = labels[d].value(pos[d]);
            row[id_arr[d].toString()] = lbl;
            sid += lbl + QLatin1Char('|');
        }
        series_ids.insert(sid);
        sorted.append(row);
    }

    // Sort by series then period — copy to QList to avoid QJsonValueRef swap issue on MSVC
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject()["period"].toString() < b.toObject()["period"].toString();
    });
    QJsonArray rows;
    for (const auto& v : sorted)
        rows.append(v);
    if (series_count)
        *series_count = series_ids.size();
    return rows;
}

// ── Panel ─────────────────────────────────────────────────────────────────────

EurostatPanel::EurostatPanel(QWidget* parent) : EconPanelBase(kEurostatSourceId, kEurostatColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &EurostatPanel::on_result);
}

void EurostatPanel::activate() {
    show_empty("Select a dataset and country, then click FETCH\n"
               "Source: Eurostat — EU statistical office");
}

void EurostatPanel::build_controls(QHBoxLayout* thl) {
    auto lbl = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setStyleSheet(ctrl_label_style());
        return l;
    };

    dataset_combo_ = new QComboBox;
    for (const auto& d : kEurostatDatasets)
        dataset_combo_->addItem(d.label, d.command);
    dataset_combo_->setFixedHeight(26);
    dataset_combo_->setMinimumWidth(180);

    country_combo_ = new QComboBox;
    for (const auto& c : kEurostatCountries)
        country_combo_->addItem(c.first, c.second);
    country_combo_->setFixedHeight(26);

    thl->addWidget(lbl("DATASET"));
    thl->addWidget(dataset_combo_);
    thl->addWidget(lbl("COUNTRY"));
    thl->addWidget(country_combo_);
}

void EurostatPanel::on_fetch() {
    const int ds_idx = dataset_combo_->currentIndex();
    const auto& dataset = kEurostatDatasets[ds_idx];
    const QString country = country_combo_->currentData().toString();

    show_loading("Fetching Eurostat: " + dataset.label + " — " + country_combo_->currentText() + "…");

    // EconomicsService already puts the command first in argv.
    QStringList args;
    if (dataset.has_country)
        args << country;

    const QString req_id = "eurostat_" + dataset.command + "_" + country;

    services::EconomicsService::instance().execute(kEurostatSourceId, kEurostatScript, dataset.command, args, req_id);
}

void EurostatPanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kEurostatSourceId)
        return;
    if (!request_id.startsWith("eurostat_"))
        return;
    if (!result.success) {
        show_error(result.error);
        return;
    }

    const int ds_idx = dataset_combo_->currentIndex();
    if (ds_idx < 0 || ds_idx >= kEurostatDatasets.size())
        return;
    const auto& dataset = kEurostatDatasets[ds_idx];

    // Try SDMX flatten first (raw Eurostat format)
    int series_count = 0;
    QJsonArray rows = flatten_sdmx(result.data, &series_count);

    // Fallback: service may have wrapped as {data:[...]}
    if (rows.isEmpty())
        rows = result.data["data"].toArray();

    if (rows.isEmpty()) {
        // Check for explicit error in data
        const QString err = result.data["error"].toString();
        show_error(err.isEmpty() ? "No data returned for this selection" : err);
        return;
    }

    const QString title = "Eurostat: " + dataset.label + " — " + country_combo_->currentText();
    // One series → stats on {period, value}. Several series (extra dimension
    // columns in the table) → no single series, so every stat card is "—".
    if (series_count == 1)
        display(rows, title, QStringLiteral("value"), QStringLiteral("period"));
    else
        display(rows, title);
    LOG_INFO("EurostatPanel", QString("Displayed %1 rows for %2").arg(rows.size()).arg(title));
}

} // namespace fincept::screens
