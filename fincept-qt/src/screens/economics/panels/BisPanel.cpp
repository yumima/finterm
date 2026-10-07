// src/screens/economics/panels/BisPanel.cpp
// BIS SDMX API — no API key required.
// Command: fetch <dataflow> <country> [start] [end]; bis_data.py pins each
// flow's key to ONE series (policy rate, nominal broad EER, period-average
// FX, CPI YoY, private credit % GDP, real property prices).
// Response: { "success": true, "data": [{date, value}], "metadata": {...} }
#include "screens/economics/panels/BisPanel.h"

#include "core/logging/Logger.h"
#include "services/economics/EconomicsService.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

namespace fincept::screens {
namespace {

static constexpr const char* kBisScript = "bis_data.py";
static constexpr const char* kBisSourceId = "bis";
static constexpr const char* kBisColor = "#9D4EDD"; // purple
} // namespace

// Dataset combo entries: { display label, BIS dataflow, country hint, default country }
// (BIS publishes no long/short-term market interest-rate flows, and the old
// multi-series "economic overview" was not a single series, so neither is here.)
struct BisDataset {
    QString label;
    QString command; // BIS dataflow passed to `fetch`
    QString country_hint;
    QString default_country;
};

static const QList<BisDataset> kBisDatasets = {
    {"Central Bank Policy Rate (%)", "WS_CBPOL", "e.g. US, GB, JP, XM", "US"},
    {"Nominal Effective Exchange Rate (broad, 2020=100)", "WS_EER", "e.g. US, GB, JP", "US"},
    {"Exchange Rate (national currency per USD, avg)", "WS_XRU", "e.g. GB, JP, XM, AU", "GB"},
    {"Consumer Prices (YoY %)", "WS_LONG_CPI", "e.g. US, DE, JP, GB", "US"},
    {"Credit to Private Non-Fin. Sector (% of GDP)", "WS_TC", "e.g. US, CN, JP", "US"},
    {"Real Residential Property Prices (2010=100)", "WS_SPP", "e.g. US, GB, DE, AU", "US"},
};

// ── Constructor ───────────────────────────────────────────────────────────────

BisPanel::BisPanel(QWidget* parent) : EconPanelBase(kBisSourceId, kBisColor, parent) {
    build_base_ui(this);
    connect(&services::EconomicsService::instance(), &services::EconomicsService::result_ready, this,
            &BisPanel::on_result);
}

void BisPanel::activate() {
    show_empty("Select a dataset and country code, then click FETCH\n"
               "BIS data is free — no API key required");
}

// ── Controls ──────────────────────────────────────────────────────────────────

void BisPanel::build_controls(QHBoxLayout* thl) {
    auto make_lbl = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setStyleSheet(ctrl_label_style());
        return l;
    };

    dataset_combo_ = new QComboBox;
    for (const auto& d : kBisDatasets)
        dataset_combo_->addItem(d.label);
    dataset_combo_->setFixedHeight(26);
    dataset_combo_->setMinimumWidth(230);
    connect(dataset_combo_, &QComboBox::currentIndexChanged, this, &BisPanel::on_dataset_changed);

    country_input_ = new QLineEdit;
    country_input_->setPlaceholderText("Country code");
    country_input_->setText("US");
    country_input_->setFixedHeight(26);
    country_input_->setFixedWidth(70);

    start_input_ = new QLineEdit;
    start_input_->setPlaceholderText("Start year");
    start_input_->setFixedHeight(26);
    start_input_->setFixedWidth(70);

    end_input_ = new QLineEdit;
    end_input_->setPlaceholderText("End year");
    end_input_->setFixedHeight(26);
    end_input_->setFixedWidth(70);

    thl->addWidget(make_lbl("DATASET"));
    thl->addWidget(dataset_combo_);
    thl->addWidget(make_lbl("COUNTRY"));
    thl->addWidget(country_input_);
    thl->addWidget(make_lbl("FROM"));
    thl->addWidget(start_input_);
    thl->addWidget(make_lbl("TO"));
    thl->addWidget(end_input_);
}

void BisPanel::on_dataset_changed(int index) {
    if (index < 0 || index >= kBisDatasets.size())
        return;
    const auto& ds = kBisDatasets[index];
    country_input_->setPlaceholderText(ds.country_hint);
    // If user hasn't typed anything custom, fill in default
    if (country_input_->text().isEmpty() ||
        country_input_->text() == kBisDatasets[qMax(0, index - 1)].default_country) {
        country_input_->setText(ds.default_country);
    }
}

// ── Fetch ─────────────────────────────────────────────────────────────────────

void BisPanel::on_fetch() {
    const int idx = dataset_combo_->currentIndex();
    if (idx < 0 || idx >= kBisDatasets.size())
        return;
    const auto& ds = kBisDatasets[idx];

    const QString country = country_input_->text().trimmed().toUpper();
    const QString start = start_input_->text().trimmed();
    const QString end = end_input_->text().trimmed();
    if (country.isEmpty()) {
        show_empty("Enter a country code (e.g. US, GB, JP)");
        return;
    }

    QStringList args = {ds.command, country};
    if (!start.isEmpty()) {
        args << start;
        if (!end.isEmpty())
            args << end;
    }

    show_loading("Fetching BIS " + ds.label + "…");
    services::EconomicsService::instance().execute(kBisSourceId, kBisScript, "fetch", args,
                                                   "bis_" + ds.command + "_" + country + "_" + start + "_" + end);
}

// ── Result ────────────────────────────────────────────────────────────────────

void BisPanel::on_result(const QString& request_id, const services::EconomicsResult& result) {
    if (result.source_id != kBisSourceId)
        return;

    if (!result.success) {
        show_error(result.error);
        return;
    }

    // fetch response: { "success": true, "data": [{date, value[, series]}], "metadata": {...} }
    const QJsonArray rows = result.data["data"].toArray();

    if (rows.isEmpty()) {
        show_empty("No data returned — try a different country or date range");
        return;
    }

    // Build title from metadata
    const QString title = "BIS: " + dataset_combo_->currentText() + " — " + country_input_->text().trimmed().toUpper();

    // "series" appears only if the key matched several series (kept apart by
    // the script) — then there is no single series for the stat cards.
    const bool multi = result.data["metadata"].toObject()["series_count"].toInt(1) > 1;
    if (multi)
        display(rows, title);
    else
        display(rows, title, QStringLiteral("value"), QStringLiteral("date"));
    LOG_INFO("BisPanel", QString("Displayed %1 rows for %2").arg(rows.size()).arg(request_id));
}

} // namespace fincept::screens
