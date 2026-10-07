// src/screens/geopolitics/TradeAnalysisPanel.cpp
#include "screens/geopolitics/TradeAnalysisPanel.h"

#include "services/geopolitics/GeopoliticsService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollArea>
#include <QTableWidget>

namespace fincept::screens {

using namespace fincept::services::geo;

TradeAnalysisPanel::TradeAnalysisPanel(QWidget* parent) : QWidget(parent) {
    build_ui();
    connect_service();
}

void TradeAnalysisPanel::connect_service() {
    auto& svc = GeopoliticsService::instance();
    connect(&svc, &GeopoliticsService::trade_result_ready, this, &TradeAnalysisPanel::on_trade_result);
    connect(&svc, &GeopoliticsService::error_occurred, this, &TradeAnalysisPanel::on_error);
}

// ── Shared style helpers ──────────────────────────────────────────────────────
static QString warn_rgb() {
    QColor c(ui::colors::WARNING());
    return QString("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

static QString combo_style() {
    return QString("QComboBox { background:%1; color:%2; border:1px solid %3;"
                   "font-family:%4; font-size:%5px; padding:4px 8px; }"
                   "QComboBox:focus { border-color:%6; }"
                   "QComboBox::drop-down { border:none; width:18px; }"
                   "QComboBox QAbstractItemView { background:%1; color:%2; "
                   "selection-background-color:rgba(%7,0.15); border:1px solid %3; }")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
        .arg(ui::fonts::DATA_FAMILY)
        .arg(ui::fonts::SMALL)
        .arg(ui::colors::WARNING())
        .arg(warn_rgb());
}

static QString spin_style() {
    return QString("QDoubleSpinBox { background:%1; color:%2; border:1px solid %3;"
                   "font-family:%4; font-size:%5px; padding:4px 6px; }"
                   "QDoubleSpinBox:focus { border-color:%6; }")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
        .arg(ui::fonts::DATA_FAMILY)
        .arg(ui::fonts::SMALL)
        .arg(ui::colors::WARNING());
}

static QWidget* make_field(const QString& label_text, QWidget* input, QWidget* parent, const QString& hint = {}) {
    auto* group = new QWidget(parent);
    auto* vl = new QVBoxLayout(group);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(3);

    auto* lbl = new QLabel(label_text, group);
    lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3; letter-spacing:1px;")
                           .arg(ui::colors::TEXT_SECONDARY())
                           .arg(ui::fonts::TINY)
                           .arg(ui::fonts::DATA_FAMILY));
    vl->addWidget(lbl);

    input->setParent(group);
    vl->addWidget(input);

    if (!hint.isEmpty()) {
        auto* h = new QLabel(hint, group);
        h->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::TINY)
                             .arg(ui::fonts::DATA_FAMILY));
        h->setWordWrap(true);
        vl->addWidget(h);
    }
    return group;
}

void TradeAnalysisPanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(48);
    header->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    auto* hhl = new QHBoxLayout(header);
    hhl->setContentsMargins(16, 0, 16, 0);
    auto* title = new QLabel("TRADE GEOPOLITICS ANALYSIS", header);
    title->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3; letter-spacing:1px;")
                             .arg(ui::colors::WARNING())
                             .arg(ui::fonts::TINY)
                             .arg(ui::fonts::DATA_FAMILY()));
    hhl->addWidget(title);
    hhl->addStretch();
    status_label_ = new QLabel(header);
    status_label_->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                                     .arg(ui::colors::TEXT_SECONDARY())
                                     .arg(ui::fonts::SMALL)
                                     .arg(ui::fonts::DATA_FAMILY));
    hhl->addWidget(status_label_);
    root->addWidget(header);

    // Scrollable content
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border:none; background:transparent; }");

    auto* content = new QWidget(scroll);
    auto* cvl = new QVBoxLayout(content);
    cvl->setContentsMargins(16, 16, 16, 16);
    cvl->setSpacing(12);

    // ── Analysis Type selector ────────────────────────────────────────────────
    auto* type_combo = new QComboBox;
    type_combo->setStyleSheet(combo_style());
    type_combo->addItem("Benefits & Costs of Trade", "benefits_costs");
    type_combo->addItem("Trade Restrictions Analysis", "restrictions");
    type_combo->addItem("Trading Blocs Analysis", "trading_blocs");
    type_combo->addItem("Trade Barrier Removal Impact", "barrier_removal");
    cvl->addWidget(make_field("ANALYSIS TYPE", type_combo, content));

    // ── Tab widget for params per analysis type ───────────────────────────────
    tabs_ = new QTabWidget(content);
    tabs_->setStyleSheet(QString("QTabWidget::pane { border:1px solid %1; background:%2; }"
                                 "QTabBar::tab { background:%3; color:%4; padding:5px 16px;"
                                 "font-family:%5; font-size:%6px; border:1px solid %1; border-bottom:none; }"
                                 "QTabBar::tab:selected { background:%2; color:%7; font-weight:700;"
                                 "border-bottom:2px solid %7; }")
                             .arg(ui::colors::BORDER_DIM(), ui::colors::BG_SURFACE(), ui::colors::BG_RAISED())
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(ui::fonts::SMALL)
                             .arg(ui::colors::WARNING()));

    // ── Page 0: Benefits & Costs ──────────────────────────────────────────────
    auto* p0 = new QWidget(this);
    auto* p0l = new QVBoxLayout(p0);
    p0l->setContentsMargins(12, 12, 12, 12);
    p0l->setSpacing(10);

    auto* hint0 = new QLabel(
        "Analyzes efficiency gains, consumer benefits, growth effects, and adjustment costs of international trade.",
        p0);
    hint0->setWordWrap(true);
    hint0->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::SMALL)
                             .arg(ui::fonts::DATA_FAMILY));
    p0l->addWidget(hint0);

    auto* vol_spin = new QDoubleSpinBox;
    vol_spin->setRange(0, 100);
    vol_spin->setValue(30);
    vol_spin->setSuffix("% of GDP");
    vol_spin->setStyleSheet(spin_style());
    p0l->addWidget(make_field("TRADE VOLUME", vol_spin, p0, "Trade as % of GDP (world avg ~60%)"));

    auto* price_spin = new QDoubleSpinBox;
    price_spin->setRange(0, 50);
    price_spin->setValue(5);
    price_spin->setSuffix("%");
    price_spin->setStyleSheet(spin_style());
    p0l->addWidget(
        make_field("EXPECTED PRICE REDUCTION", price_spin, p0, "Consumer price reduction from trade openness"));

    auto* cons_spin = new QDoubleSpinBox;
    cons_spin->setRange(0, 100);
    cons_spin->setValue(30);
    cons_spin->setSuffix("% of consumption");
    cons_spin->setStyleSheet(spin_style());
    p0l->addWidget(make_field("TRADED GOODS SHARE", cons_spin, p0, "Share of consumption from tradeable goods"));

    auto* eff_spin = new QDoubleSpinBox;
    eff_spin->setRange(0, 100);
    eff_spin->setDecimals(1);
    eff_spin->setValue(0);
    eff_spin->setSuffix("%");
    eff_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    eff_spin->setStyleSheet(spin_style());
    p0l->addWidget(make_field("ASSUMED EFFICIENCY GAIN", eff_spin, p0,
                              "Your assumption: % efficiency gain on traded volume (unset = not quantified)"));

    p0l->addStretch();

    // Store spinboxes for use in run button
    auto* run0 = new QPushButton("RUN ANALYSIS", p0);
    run0->setCursor(Qt::PointingHandCursor);
    run0->setStyleSheet([&]() {
        QColor w(ui::colors::WARNING());
        return QString("QPushButton { background:%1; color:%2; font-family:%3; font-size:%4px;"
                       "font-weight:700; border:none; padding:6px 16px; }"
                       "QPushButton:hover { background:%5; }")
            .arg(ui::colors::WARNING())
            .arg(ui::colors::BG_BASE())
            .arg(ui::fonts::DATA_FAMILY())
            .arg(ui::fonts::SMALL)
            .arg(w.darker(120).name());
    }());
    connect(run0, &QPushButton::clicked, this, [this, vol_spin, price_spin, cons_spin, eff_spin]() {
        status_label_->setText("Analyzing...");
        QJsonObject p;
        if (eff_spin->value() > 0)
            p["efficiency_gain_percent"] = eff_spin->value();
        p["trade_volume_gdp"] = vol_spin->value();
        p["price_reduction_percent"] = price_spin->value();
        p["traded_goods_consumption"] = cons_spin->value();
        GeopoliticsService::instance().analyze_trade_benefits(p);
    });
    p0l->addWidget(run0);
    tabs_->addTab(p0, "Benefits & Costs");

    // ── Page 1: Restrictions ──────────────────────────────────────────────────
    auto* p1 = new QWidget(this);
    auto* p1l = new QVBoxLayout(p1);
    p1l->setContentsMargins(12, 12, 12, 12);
    p1l->setSpacing(10);

    auto* hint1 =
        new QLabel("Analyzes economic impact of tariffs, quotas, export subsidies, and non-tariff barriers.", p1);
    hint1->setWordWrap(true);
    hint1->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::SMALL)
                             .arg(ui::fonts::DATA_FAMILY));
    p1l->addWidget(hint1);

    auto* tariff_spin = new QDoubleSpinBox;
    tariff_spin->setRange(0, 200);
    tariff_spin->setValue(10);
    tariff_spin->setSuffix("%");
    tariff_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("TARIFF RATE", tariff_spin, p1, "Import duty rate (e.g. 10% = standard MFN tariff)"));

    // Quantifying the tariff needs the market's import demand elasticity and
    // pass-through; they start unset ("—") and are only sent when entered.
    auto* r_elast_spin = new QDoubleSpinBox;
    r_elast_spin->setRange(0, 20);
    r_elast_spin->setDecimals(2);
    r_elast_spin->setValue(0);
    r_elast_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    r_elast_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("IMPORT DEMAND ELASTICITY", r_elast_spin, p1,
                              "Magnitude of the import demand price elasticity for this market (your estimate)"));

    auto* r_pt_spin = new QDoubleSpinBox;
    r_pt_spin->setRange(0, 100);
    r_pt_spin->setDecimals(0);
    r_pt_spin->setValue(100);
    r_pt_spin->setSuffix("%");
    r_pt_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("TARIFF PASS-THROUGH", r_pt_spin, p1,
                              "Share of the tariff passed into domestic import prices (100% = small-country case)"));

    auto* r_imp_spin = new QDoubleSpinBox;
    r_imp_spin->setRange(0, 1e6);
    r_imp_spin->setDecimals(2);
    r_imp_spin->setValue(0);
    r_imp_spin->setSuffix("B USD");
    r_imp_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    r_imp_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("IMPORT VALUE (OPTIONAL)", r_imp_spin, p1,
                              "Border value of imports at zero tariff; adds revenue and welfare amounts"));

    auto* quota_spin = new QDoubleSpinBox;
    quota_spin->setRange(0, 1000000);
    quota_spin->setValue(1000);
    quota_spin->setSuffix(" units");
    quota_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("QUOTA VOLUME", quota_spin, p1, "Annual import quantity limit"));

    auto* subsidy_spin = new QDoubleSpinBox;
    subsidy_spin->setRange(0, 100);
    subsidy_spin->setValue(5);
    subsidy_spin->setSuffix("%");
    subsidy_spin->setStyleSheet(spin_style());
    p1l->addWidget(make_field("EXPORT SUBSIDY RATE", subsidy_spin, p1, "Government subsidy as % of export value"));

    auto* dev_combo = new QComboBox;
    dev_combo->setStyleSheet(combo_style());
    dev_combo->addItem("Developed Economy", "developed");
    dev_combo->addItem("Developing Economy", "developing");
    dev_combo->addItem("Middle Income", "middle");
    p1l->addWidget(make_field("DEVELOPMENT LEVEL", dev_combo, p1));

    auto* maturity_combo = new QComboBox;
    maturity_combo->setStyleSheet(combo_style());
    maturity_combo->addItem("Mature Industry", "mature");
    maturity_combo->addItem("Infant Industry", "infant");
    maturity_combo->addItem("Emerging Industry", "emerging");
    p1l->addWidget(make_field("INDUSTRY MATURITY", maturity_combo, p1));

    p1l->addStretch();

    auto* run1 = new QPushButton("RUN ANALYSIS", p1);
    run1->setCursor(Qt::PointingHandCursor);
    run1->setStyleSheet([&]() {
        QColor w(ui::colors::WARNING());
        return QString("QPushButton { background:%1; color:%2; font-family:%3; font-size:%4px;"
                       "font-weight:700; border:none; padding:6px 16px; }"
                       "QPushButton:hover { background:%5; }")
            .arg(ui::colors::WARNING())
            .arg(ui::colors::BG_BASE())
            .arg(ui::fonts::DATA_FAMILY())
            .arg(ui::fonts::SMALL)
            .arg(w.darker(120).name());
    }());
    connect(run1, &QPushButton::clicked, this,
            [this, tariff_spin, quota_spin, subsidy_spin, dev_combo, maturity_combo, r_elast_spin, r_pt_spin,
             r_imp_spin]() {
                status_label_->setText("Analyzing...");
                QJsonObject p;
                p["tariff_rate"] = tariff_spin->value();
                if (r_elast_spin->value() > 0) {
                    p["import_demand_elasticity"] = r_elast_spin->value();
                    p["pass_through_pct"] = r_pt_spin->value();
                }
                if (r_imp_spin->value() > 0)
                    p["import_value"] = r_imp_spin->value();
                p["quota_volume"] = quota_spin->value();
                p["subsidy_rate"] = subsidy_spin->value();
                p["development_level"] = dev_combo->currentData().toString();
                p["industry_maturity"] = maturity_combo->currentData().toString();
                GeopoliticsService::instance().analyze_trade_restrictions(p);
            });
    p1l->addWidget(run1);
    tabs_->addTab(p1, "Restrictions");

    // ── Page 2: Trading Blocs ─────────────────────────────────────────────────
    auto* p2 = new QWidget(this);
    auto* p2l = new QVBoxLayout(p2);
    p2l->setContentsMargins(12, 12, 12, 12);
    p2l->setSpacing(10);

    auto* hint2 =
        new QLabel("Analyzes trade creation vs. diversion effects for regional trade blocs and economic unions.", p2);
    hint2->setWordWrap(true);
    hint2->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::SMALL)
                             .arg(ui::fonts::DATA_FAMILY));
    p2l->addWidget(hint2);

    auto* bloc_combo = new QComboBox;
    bloc_combo->setStyleSheet(combo_style());
    bloc_combo->addItem("Free Trade Area (e.g. USMCA, ASEAN)", "fta");
    bloc_combo->addItem("Customs Union (e.g. EU, Mercosur)", "customs_union");
    bloc_combo->addItem("Common Market (e.g. EU Single Market)", "common_market");
    bloc_combo->addItem("Economic Union (e.g. European Union)", "economic_union");
    p2l->addWidget(make_field("INTEGRATION TYPE", bloc_combo, p2));

    auto* tc_spin = new QDoubleSpinBox;
    tc_spin->setRange(0, 1000);
    tc_spin->setValue(100);
    tc_spin->setSuffix("B USD");
    tc_spin->setStyleSheet(spin_style());
    p2l->addWidget(make_field("TRADE CREATION ESTIMATE", tc_spin, p2, "Expected new trade generated within bloc"));

    auto* td_spin = new QDoubleSpinBox;
    td_spin->setRange(0, 1000);
    td_spin->setValue(30);
    td_spin->setSuffix("B USD");
    td_spin->setStyleSheet(spin_style());
    p2l->addWidget(make_field("TRADE DIVERSION ESTIMATE", td_spin, p2, "Trade diverted from efficient non-members"));

    p2l->addStretch();

    auto* run2 = new QPushButton("RUN ANALYSIS", p2);
    run2->setCursor(Qt::PointingHandCursor);
    run2->setStyleSheet([&]() {
        QColor w(ui::colors::WARNING());
        return QString("QPushButton { background:%1; color:%2; font-family:%3; font-size:%4px;"
                       "font-weight:700; border:none; padding:6px 16px; }"
                       "QPushButton:hover { background:%5; }")
            .arg(ui::colors::WARNING())
            .arg(ui::colors::BG_BASE())
            .arg(ui::fonts::DATA_FAMILY())
            .arg(ui::fonts::SMALL)
            .arg(w.darker(120).name());
    }());
    connect(run2, &QPushButton::clicked, this, [this, bloc_combo, tc_spin, td_spin]() {
        status_label_->setText("Analyzing...");
        QJsonObject p;
        p["integration_type"] = bloc_combo->currentData().toString();
        p["trade_creation"] = tc_spin->value();
        p["trade_diversion"] = td_spin->value();
        GeopoliticsService::instance().analyze_trading_blocs(p);
    });
    p2l->addWidget(run2);
    tabs_->addTab(p2, "Trading Blocs");

    // ── Page 3: Barrier Removal ───────────────────────────────────────────────
    auto* p3 = new QWidget(this);
    auto* p3l = new QVBoxLayout(p3);
    p3l->setContentsMargins(12, 12, 12, 12);
    p3l->setSpacing(10);

    auto* hint3 = new QLabel("Cuts an ad valorem tariff from its current level to a new level and computes the "
                             "import price and volume response (iso-elastic import demand), tariff revenue, "
                             "consumer surplus and net welfare change. All assumptions are the inputs below.",
                             p3);
    hint3->setWordWrap(true);
    hint3->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::fonts::SMALL)
                             .arg(ui::fonts::DATA_FAMILY));
    p3l->addWidget(hint3);

    auto make_pct_spin = [](double max, double val, bool required) {
        auto* sp = new QDoubleSpinBox;
        sp->setRange(0, max);
        sp->setDecimals(2);
        sp->setValue(val);
        sp->setSuffix("%");
        if (required)
            sp->setSpecialValueText(fincept::ui::formatting::placeholder());
        sp->setStyleSheet(spin_style());
        return sp;
    };

    auto* cur_tariff_spin = make_pct_spin(500, 0, true);
    p3l->addWidget(make_field("CURRENT TARIFF LEVEL", cur_tariff_spin, p3, "Ad valorem tariff today (e.g. 20%)"));

    auto* new_tariff_spin = make_pct_spin(500, 0, false);
    p3l->addWidget(make_field("NEW TARIFF LEVEL", new_tariff_spin, p3,
                              "Tariff after liberalization (a level, not a % reduction; 0% = full removal)"));

    auto* elast_spin = new QDoubleSpinBox;
    elast_spin->setRange(0, 20);
    elast_spin->setDecimals(2);
    elast_spin->setValue(0);
    elast_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    elast_spin->setStyleSheet(spin_style());
    p3l->addWidget(make_field("IMPORT DEMAND ELASTICITY", elast_spin, p3,
                              "Magnitude of the import demand price elasticity (your estimate for this market)"));

    auto* pt_spin = make_pct_spin(100, 100, false);
    pt_spin->setDecimals(0);
    p3l->addWidget(make_field("TARIFF PASS-THROUGH", pt_spin, p3,
                              "Share of the tariff change reaching domestic import prices; the rest moves the "
                              "foreign border price (100% = small-country case)"));

    auto* imp_spin = new QDoubleSpinBox;
    imp_spin->setRange(0, 1e6);
    imp_spin->setDecimals(2);
    imp_spin->setValue(0);
    imp_spin->setSuffix("B USD");
    imp_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    imp_spin->setStyleSheet(spin_style());
    p3l->addWidget(make_field("IMPORT VALUE", imp_spin, p3, "Border (pre-tariff) value of affected imports today"));

    auto* gdp_spin = new QDoubleSpinBox;
    gdp_spin->setRange(0, 1e6);
    gdp_spin->setDecimals(1);
    gdp_spin->setValue(0);
    gdp_spin->setSuffix("B USD");
    gdp_spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    gdp_spin->setStyleSheet(spin_style());
    p3l->addWidget(make_field("GDP (OPTIONAL)", gdp_spin, p3, "Expresses the welfare change as % of GDP"));

    p3l->addStretch();

    auto* run3 = new QPushButton("RUN ANALYSIS", p3);
    run3->setCursor(Qt::PointingHandCursor);
    run3->setStyleSheet([&]() {
        QColor w(ui::colors::WARNING());
        return QString("QPushButton { background:%1; color:%2; font-family:%3; font-size:%4px;"
                       "font-weight:700; border:none; padding:6px 16px; }"
                       "QPushButton:hover { background:%5; }")
            .arg(ui::colors::WARNING())
            .arg(ui::colors::BG_BASE())
            .arg(ui::fonts::DATA_FAMILY())
            .arg(ui::fonts::SMALL)
            .arg(w.darker(120).name());
    }());
    connect(run3, &QPushButton::clicked, this,
            [this, cur_tariff_spin, new_tariff_spin, elast_spin, pt_spin, imp_spin, gdp_spin]() {
                if (cur_tariff_spin->value() <= 0 || elast_spin->value() <= 0 || imp_spin->value() <= 0) {
                    status_label_->setText("Enter current tariff, import demand elasticity and import value");
                    return;
                }
                if (new_tariff_spin->value() > cur_tariff_spin->value()) {
                    status_label_->setText("New tariff must not exceed the current tariff");
                    return;
                }
                status_label_->setText("Analyzing...");
                QJsonObject p;
                p["current_tariff_pct"] = cur_tariff_spin->value();
                p["new_tariff_pct"] = new_tariff_spin->value();
                p["import_demand_elasticity"] = elast_spin->value();
                p["pass_through_pct"] = pt_spin->value();
                p["import_value"] = imp_spin->value();
                if (gdp_spin->value() > 0)
                    p["gdp"] = gdp_spin->value();
                GeopoliticsService::instance().analyze_barrier_removal(p);
            });
    p3l->addWidget(run3);
    tabs_->addTab(p3, "Barrier Removal");

    // Sync analysis type combo → tab
    connect(type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), tabs_, &QTabWidget::setCurrentIndex);
    connect(tabs_, &QTabWidget::currentChanged, type_combo, &QComboBox::setCurrentIndex);

    cvl->addWidget(tabs_);

    // Results area
    results_container_ = new QWidget(content);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    cvl->addWidget(results_container_);

    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

void TradeAnalysisPanel::display_result(const QJsonObject& payload) {
    while (results_layout_->count() > 0) {
        auto* item = results_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    auto* sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    results_layout_->addWidget(sep);

    auto* hdr = new QLabel("ANALYSIS RESULTS");
    hdr->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3;"
                               "letter-spacing:1px; padding:8px 0 4px 0;")
                           .arg(ui::colors::WARNING())
                           .arg(ui::fonts::TINY)
                           .arg(ui::fonts::DATA_FAMILY()));
    results_layout_->addWidget(hdr);

    // Flatten and display JSON as a table
    QVector<QPair<QString, QString>> rows;
    std::function<void(const QJsonObject&, const QString&)> flatten = [&](const QJsonObject& obj,
                                                                          const QString& prefix) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QString key = prefix.isEmpty() ? it.key() : prefix + " › " + it.key();
            if (it.value().isObject()) {
                flatten(it.value().toObject(), key);
            } else if (it.value().isArray()) {
                QStringList items;
                for (const auto& v : it.value().toArray())
                    items << (v.isDouble() ? QString::number(v.toDouble(), 'g', 6) : v.toString());
                rows.append({key, items.join(", ")});
            } else {
                QString val;
                if (it.value().isDouble()) {
                    // *_pct values are already in percent units.
                    val = it.key().endsWith(QStringLiteral("_pct"))
                              ? fincept::ui::formatting::format_percent(it.value().toDouble())
                              : QString::number(it.value().toDouble(), 'f', 2);
                } else if (it.value().isBool()) {
                    val = it.value().toBool() ? "YES" : "NO";
                } else if (it.value().isNull()) {
                    val = fincept::ui::formatting::placeholder();
                } else {
                    val = it.value().toString();
                }
                if (!val.isEmpty())
                    rows.append({key, val});
            }
        }
    };
    flatten(payload, "");

    auto* table = new QTableWidget(rows.size(), 2);
    table->setHorizontalHeaderLabels({"Metric", "Value"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(24);
    table->setStyleSheet(QString("QTableWidget { background:%1; color:%2; gridline-color:%3;"
                                 "font-family:%4; font-size:%5px; border:1px solid %3; }"
                                 "QTableWidget::item { padding:3px 8px; }"
                                 "QHeaderView::section { background:%6; color:%7; font-weight:700;"
                                 "padding:4px 8px; border:1px solid %3; font-family:%4; font-size:%5px; }"
                                 "QTableWidget::item:alternate { background:%8; }")
                             .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM())
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(ui::fonts::SMALL)
                             .arg(ui::colors::BG_RAISED())
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::colors::ROW_ALT()));

    for (int r = 0; r < rows.size(); ++r) {
        auto key = rows[r].first;
        key.replace('_', ' ');
        table->setItem(r, 0, new QTableWidgetItem(key));
        auto* vi = new QTableWidgetItem(rows[r].second);
        vi->setToolTip(rows[r].second);
        table->setItem(r, 1, vi);
    }
    results_layout_->addWidget(table);
    status_label_->setText(QString("%1 fields").arg(rows.size()));
}

void TradeAnalysisPanel::on_trade_result(const QString& context, const QJsonObject& payload) {
    if (context == "trade_benefits" || context == "trade_restrictions" || context == "trade_blocs" ||
        context == "trade_barrier_removal")
        display_result(payload);
}

void TradeAnalysisPanel::on_error(const QString& context, const QString& message) {
    if (context != "trade_benefits" && context != "trade_restrictions" && context != "trade_blocs" &&
        context != "trade_barrier_removal")
        return;
    status_label_->setText("Error");
    while (results_layout_->count() > 0) {
        auto* item = results_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    auto* err = new QLabel(QString("[%1] %2").arg(context, message));
    err->setWordWrap(true);
    err->setStyleSheet([&]() {
        QColor neg(ui::colors::NEGATIVE());
        auto neg_rgb = QString("%1,%2,%3").arg(neg.red()).arg(neg.green()).arg(neg.blue());
        return QString("color:%1; font-size:%2px; font-family:%3; padding:12px;"
                       "background:rgba(%4,0.08); border:1px solid rgba(%4,0.3);")
            .arg(ui::colors::NEGATIVE())
            .arg(ui::fonts::SMALL)
            .arg(ui::fonts::DATA_FAMILY())
            .arg(neg_rgb);
    }());
    results_layout_->addWidget(err);
}

} // namespace fincept::screens
