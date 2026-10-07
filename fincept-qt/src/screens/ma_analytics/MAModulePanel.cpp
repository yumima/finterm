// src/screens/ma_analytics/MAModulePanel.cpp
#include "screens/ma_analytics/MAModulePanel.h"

#include "core/logging/Logger.h"
#include "services/ma_analytics/MAAnalyticsService.h"
#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeManager.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>

#include <cmath>
#include <initializer_list>

namespace fincept::screens {

using namespace fincept::services::ma;

// ── Helpers: create styled input widgets ─────────────────────────────────────

QDoubleSpinBox* MAModulePanel::make_double_spin(double min, double max, double val, int decimals, const QString& suffix,
                                                QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(min, max);
    spin->setValue(val);
    spin->setDecimals(decimals);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    spin->setStyleSheet(QString("QDoubleSpinBox { background:%1; color:%2; border:1px solid %3;"
                                "font-family:%4; font-size:%5px; padding:4px 6px; }"
                                "QDoubleSpinBox:focus { border-color:%6; }")
                            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
                            .arg(ui::fonts::DATA_FAMILY)
                            .arg(ui::fonts::SMALL)
                            .arg(module_.color.name()));
    return spin;
}

QSpinBox* MAModulePanel::make_int_spin(int min, int max, int val, QWidget* parent) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(min, max);
    spin->setValue(val);
    spin->setStyleSheet(QString("QSpinBox { background:%1; color:%2; border:1px solid %3;"
                                "font-family:%4; font-size:%5px; padding:4px 6px; }"
                                "QSpinBox:focus { border-color:%6; }")
                            .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
                            .arg(ui::fonts::DATA_FAMILY)
                            .arg(ui::fonts::SMALL)
                            .arg(module_.color.name()));
    return spin;
}

QPushButton* MAModulePanel::make_run_button(const QString& text, QWidget* parent) {
    auto* btn = new QPushButton(text, parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QString("QPushButton { background:%1; color:%2; font-family:%3; font-size:%4px;"
                               "font-weight:700; border:none; padding:8px 20px;"
                               "letter-spacing:1px; }"
                               "QPushButton:hover { background:%5; }"
                               "QPushButton:pressed { background:%6; }")
                           .arg(module_.color.name())
                           .arg(ui::colors::BG_BASE())
                           .arg(ui::fonts::DATA_FAMILY)
                           .arg(ui::fonts::SMALL)
                           .arg(module_.color.lighter(120).name())
                           .arg(module_.color.darker(120).name()));
    return btn;
}

QWidget* MAModulePanel::build_input_row(const QString& label, QWidget* input, QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 2, 0, 2);
    hl->setSpacing(8);
    auto* lbl = new QLabel(label, row);
    lbl->setFixedWidth(160);
    lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                           .arg(ui::colors::TEXT_SECONDARY())
                           .arg(ui::fonts::SMALL)
                           .arg(ui::fonts::DATA_FAMILY));
    hl->addWidget(lbl);
    hl->addWidget(input, 1);
    return row;
}

QWidget* MAModulePanel::build_metric_card(const QString& label, const QString& value, const QString& color,
                                          QWidget* parent) {
    auto* card = new QWidget(parent);
    card->setStyleSheet(QString("background:%1; border:1px solid %2; padding:10px;")
                            .arg(ui::colors::BG_RAISED())
                            .arg(ui::colors::BORDER_DIM()));
    auto* vl = new QVBoxLayout(card);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(4);
    auto* lbl = new QLabel(label, card);
    lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3; letter-spacing:1px;")
                           .arg(ui::colors::TEXT_SECONDARY())
                           .arg(ui::fonts::TINY)
                           .arg(ui::fonts::DATA_FAMILY));
    auto* val = new QLabel(value, card);
    val->setStyleSheet(QString("color:%1; font-size:%2px; font-weight:700; font-family:%3;")
                           .arg(color)
                           .arg(ui::fonts::HEADER)
                           .arg(ui::fonts::DATA_FAMILY));
    vl->addWidget(lbl);
    vl->addWidget(val);
    return card;
}

// ── Constructor ──────────────────────────────────────────────────────────────

MAModulePanel::MAModulePanel(const ModuleInfo& info, QWidget* parent) : QWidget(parent), module_(info) {
    build_ui();
    connect_service();
    refresh_theme();
}

void MAModulePanel::connect_service() {
    auto& svc = MAAnalyticsService::instance();
    connect(&svc, &MAAnalyticsService::result_ready, this, &MAModulePanel::on_result_ready);
    connect(&svc, &MAAnalyticsService::error_occurred, this, &MAModulePanel::on_error);
}

// ── Build UI ─────────────────────────────────────────────────────────────────

void MAModulePanel::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Module header bar
    header_bar_ = new QWidget(this);
    header_bar_->setFixedHeight(36);
    auto* hhl = new QHBoxLayout(header_bar_);
    hhl->setContentsMargins(16, 0, 16, 0);
    header_title_ = new QLabel(module_.label.toUpper(), header_bar_);
    hhl->addWidget(header_title_);

    auto* div = new QWidget(header_bar_);
    div->setFixedSize(1, 14);
    div->setObjectName("maPanelDivider");
    hhl->addWidget(div);

    header_category_ = new QLabel(module_.category.toUpper(), header_bar_);
    hhl->addWidget(header_category_);
    hhl->addStretch();

    status_label_ = new QLabel(header_bar_);
    hhl->addWidget(status_label_);
    root->addWidget(header_bar_);

    // Scrollable content area
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border:none; background:transparent; }");

    QWidget* panel_content = nullptr;
    switch (module_.id) {
        case ModuleId::Valuation:
            panel_content = build_valuation_panel();
            break;
        case ModuleId::Merger:
            panel_content = build_merger_panel();
            break;
        case ModuleId::Deals:
            panel_content = build_deals_panel();
            break;
        case ModuleId::Startup:
            panel_content = build_startup_panel();
            break;
        case ModuleId::Fairness:
            panel_content = build_fairness_panel();
            break;
        case ModuleId::Industry:
            panel_content = build_industry_panel();
            break;
        case ModuleId::Advanced:
            panel_content = build_advanced_panel();
            break;
        case ModuleId::Comparison:
            panel_content = build_comparison_panel();
            break;
    }

    scroll->setWidget(panel_content);
    root->addWidget(scroll, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// FORM BUILDER
// ═══════════════════════════════════════════════════════════════════════════════
//
// Inputs are keyed "<tab>.<json_key>" (one nesting level allowed:
// "<tab>.acquirer.net_income" -> {"acquirer": {"net_income": ...}}). A tab's
// payload is collect("<tab>"). Number fields start blank and a blank field is
// not sent: the script then names the missing required input. Fields with a
// "%" suffix are sent as fractions.

QWidget* MAModulePanel::new_tab(QVBoxLayout*& vl) {
    auto* tab = new QWidget(this);
    vl = new QVBoxLayout(tab);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(6);
    return tab;
}

void MAModulePanel::finish_tab(QWidget* tab, QVBoxLayout* vl, const QString& run_text, const QString& title,
                               std::function<void()> on_run) {
    auto* run = make_run_button(run_text, tab);
    connect(run, &QPushButton::clicked, this, [on_run = std::move(on_run)]() { on_run(); });
    vl->addWidget(run);
    vl->addStretch();
    sub_tabs_->addTab(tab, title);
}

QDoubleSpinBox* MAModulePanel::add_field(QWidget* tab, QVBoxLayout* vl, const QString& key, const QString& label,
                                         double min, double max, int decimals, const QString& suffix) {
    // One step below `min` is the "not entered" sentinel, shown as "—", so a
    // real 0 (or the range minimum) can still be entered.
    const double eps = std::pow(10.0, -decimals);
    auto* spin = make_double_spin(min - eps, max, min - eps, decimals, suffix, tab);
    spin->setSpecialValueText(fincept::ui::formatting::placeholder());
    double_inputs_[key] = spin;
    vl->addWidget(build_input_row(label, spin, tab));
    return spin;
}

QSpinBox* MAModulePanel::add_int_field(QWidget* tab, QVBoxLayout* vl, const QString& key, const QString& label,
                                       int min, int max, int val) {
    auto* spin = make_int_spin(min, max, val, tab);
    int_inputs_[key] = spin;
    vl->addWidget(build_input_row(label, spin, tab));
    return spin;
}

static QString combo_style() {
    return QString("QComboBox { background:%1; color:%2; border:1px solid %3;"
                   "font-family:%4; font-size:%5px; padding:4px 6px; }")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
        .arg(ui::fonts::DATA_FAMILY)
        .arg(ui::fonts::SMALL);
}

QComboBox* MAModulePanel::add_choice(QWidget* tab, QVBoxLayout* vl, const QString& key, const QString& label,
                                     const QStringList& items) {
    // Items are "Display|value" (or just "value").
    auto* combo = new QComboBox(tab);
    for (const QString& it : items) {
        const int bar = it.indexOf(QLatin1Char('|'));
        if (bar < 0)
            combo->addItem(it, it);
        else
            combo->addItem(it.left(bar), it.mid(bar + 1));
    }
    combo->setStyleSheet(combo_style());
    combo_inputs_[key] = combo;
    vl->addWidget(build_input_row(label, combo, tab));
    return combo;
}

QComboBox* MAModulePanel::add_text_field(QWidget* tab, QVBoxLayout* vl, const QString& key, const QString& label,
                                         const QString& placeholder, const QString& kind) {
    auto* combo = new QComboBox(tab);
    combo->setEditable(true);
    combo->setPlaceholderText(placeholder);
    combo->lineEdit()->setPlaceholderText(placeholder);
    combo->setStyleSheet(combo_style());
    combo_inputs_[key] = combo;
    text_kinds_[key] = kind;
    vl->addWidget(build_input_row(label, combo, tab));
    return combo;
}

QTextEdit* MAModulePanel::add_json_field(QWidget* tab, QVBoxLayout* vl, const QString& key,
                                         const QString& placeholder) {
    auto* te = new QTextEdit(tab);
    te->setPlaceholderText(placeholder);
    te->setMaximumHeight(110);
    te->setStyleSheet(QString("QTextEdit { background:%1; color:%2; border:1px solid %3;"
                              "font-family:%4; font-size:%5px; padding:6px; }")
                          .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
                          .arg(ui::fonts::DATA_FAMILY)
                          .arg(ui::fonts::SMALL));
    json_inputs_[key] = te;
    vl->addWidget(te);
    return te;
}

void MAModulePanel::add_section(QWidget* tab, QVBoxLayout* vl, const QString& title) {
    auto* lbl = new QLabel(title.toUpper(), tab);
    lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; font-family:%2;"
                               "letter-spacing:1px; padding-top:6px;")
                           .arg(module_.color.name())
                           .arg(ui::fonts::DATA_FAMILY));
    vl->addWidget(lbl);
}

void MAModulePanel::add_hint(QWidget* tab, QVBoxLayout* vl, const QString& text) {
    auto* lbl = new QLabel(text, tab);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                           .arg(ui::colors::TEXT_SECONDARY())
                           .arg(ui::fonts::SMALL)
                           .arg(ui::fonts::DATA_FAMILY));
    vl->addWidget(lbl);
}

bool MAModulePanel::entered(const QString& key) const {
    const auto* spin = double_inputs_.value(key);
    if (!spin)
        return false;
    const double eps = std::pow(10.0, -spin->decimals());
    return spin->value() > spin->minimum() + eps / 2.0;
}

namespace {
// Insert `v` at a possibly-dotted path ("acquirer.net_income").
void put_path(QJsonObject& root, const QString& path, const QJsonValue& v) {
    const int dot = path.indexOf(QLatin1Char('.'));
    if (dot < 0) {
        root[path] = v;
        return;
    }
    const QString head = path.left(dot);
    QJsonObject child = root.value(head).toObject();
    put_path(child, path.mid(dot + 1), v);
    root[head] = child;
}
} // namespace

QJsonObject MAModulePanel::collect(const QString& tab, QString* error) const {
    QJsonObject o;
    const QString prefix = tab + QLatin1Char('.');
    for (auto it = double_inputs_.cbegin(); it != double_inputs_.cend(); ++it) {
        if (!it.key().startsWith(prefix) || !entered(it.key()))
            continue;
        double v = it.value()->value();
        if (it.value()->suffix() == QLatin1String("%"))
            v /= 100.0;
        put_path(o, it.key().mid(prefix.size()), v);
    }
    for (auto it = int_inputs_.cbegin(); it != int_inputs_.cend(); ++it) {
        if (it.key().startsWith(prefix))
            put_path(o, it.key().mid(prefix.size()), it.value()->value());
    }
    for (auto it = combo_inputs_.cbegin(); it != combo_inputs_.cend(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QString path = it.key().mid(prefix.size());
        const QString kind = text_kinds_.value(it.key());
        if (kind.isEmpty()) {  // choice
            const QString v = it.value()->currentData().toString();
            if (v == QLatin1String("true") || v == QLatin1String("false"))
                put_path(o, path, v == QLatin1String("true"));
            else
                put_path(o, path, v);
            continue;
        }
        const QString text = it.value()->currentText().trimmed();
        if (text.isEmpty())
            continue;
        if (kind == QLatin1String("text")) {
            put_path(o, path, text);
            continue;
        }
        QJsonArray arr;
        for (const QString& part : text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts)) {
            bool ok = false;
            double v = part.toDouble(&ok);
            if (!ok) {
                if (error)
                    *error = QString("'%1' is not a number list").arg(text);
                return {};
            }
            arr.append(kind == QLatin1String("pct_list") ? v / 100.0 : v);
        }
        put_path(o, path, arr);
    }
    for (auto it = json_inputs_.cbegin(); it != json_inputs_.cend(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QString text = it.value()->toPlainText().trimmed();
        if (text.isEmpty())
            continue;
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
        if (doc.isNull()) {
            if (error)
                *error = QString("Invalid JSON: %1").arg(pe.errorString());
            return {};
        }
        put_path(o, it.key().mid(prefix.size()),
                 doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
    }
    return o;
}

bool MAModulePanel::run_guard(const QString& error) {
    if (error.isEmpty())
        return true;
    display_error(error);
    return false;
}


// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 1: VALUATION TOOLKIT
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_valuation_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;
    constexpr double kMax = 1e15;

    // ── DCF ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "All inputs are required unless marked optional. Rates in %.");
        add_section(tab, t, "Discount rate (WACC)");
        add_field(tab, t, "dcf.wacc", "WACC override (optional)", 0, 100, 2, "%");
        add_field(tab, t, "dcf.risk_free_rate", "Risk-Free Rate", 0, 20, 2, "%");
        add_field(tab, t, "dcf.beta", "Levered Beta", 0.1, 3, 2);
        add_field(tab, t, "dcf.market_risk_premium", "Equity Risk Premium", 0, 20, 2, "%");
        add_field(tab, t, "dcf.cost_of_debt", "Pre-Tax Cost of Debt", 0, 30, 2, "%");
        add_field(tab, t, "dcf.tax_rate", "Tax Rate", 0, 60, 1, "%");
        add_field(tab, t, "dcf.market_cap", "Equity Market Value ($)", 0, kMax, 0);
        add_section(tab, t, "Free cash flow (base year)");
        add_field(tab, t, "dcf.ebit", "EBIT ($)", -kMax, kMax, 0);
        add_field(tab, t, "dcf.d_and_a", "D&A ($)", 0, kMax, 0);
        add_field(tab, t, "dcf.capex", "CapEx ($)", 0, kMax, 0);
        add_field(tab, t, "dcf.change_in_nwc", "Increase in NWC ($)", -kMax, kMax, 0);
        add_text_field(tab, t, "dcf.growth_rates", "FCF Growth by Year (%)", "e.g. 8, 7, 6, 5, 4", "pct_list");
        add_section(tab, t, "Terminal value");
        add_choice(tab, t, "dcf.terminal_method", "Method",
                   {"Perpetuity growth (Gordon)|perpetuity", "Exit EV/EBITDA multiple|exit_multiple"});
        add_field(tab, t, "dcf.terminal_growth", "Terminal Growth (Gordon)", -5, 5, 2, "%");
        add_field(tab, t, "dcf.exit_multiple", "Exit Multiple (EV/EBITDA)", 0, 100, 1, "x");
        add_field(tab, t, "dcf.ebitda", "Base-Year EBITDA ($, exit multiple)", -kMax, kMax, 0);
        add_choice(tab, t, "dcf.mid_year_convention", "Discounting",
                   {"End of year|false", "Mid-year convention|true"});
        add_section(tab, t, "Equity bridge");
        add_field(tab, t, "dcf.debt", "Total Debt ($)", 0, kMax, 0);
        add_field(tab, t, "dcf.cash", "Cash ($)", 0, kMax, 0);
        add_field(tab, t, "dcf.minority_interest", "Minority Interest ($, optional)", 0, kMax, 0);
        add_field(tab, t, "dcf.preferred_stock", "Preferred Stock ($, optional)", 0, kMax, 0);
        add_field(tab, t, "dcf.shares_outstanding", "Diluted Shares Outstanding", 0, kMax, 0);
        finish_tab(tab, t, "RUN DCF ANALYSIS", "DCF", [this]() {
            QString err;
            const QJsonObject p = collect("dcf", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running DCF...");
            MAAnalyticsService::instance().calculate_dcf(p);
        });
    }

    // ── DCF sensitivity (uses the DCF tab's inputs) ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Re-values the DCF tab's inputs over a grid of WACC and terminal growth rates.");
        add_text_field(tab, t, "dcfs.wacc_range", "WACC Values (%)", "e.g. 8, 9, 10, 11, 12", "pct_list");
        add_text_field(tab, t, "dcfs.tgr_range", "Terminal Growth Values (%)", "e.g. 1.5, 2, 2.5, 3", "pct_list");
        finish_tab(tab, t, "RUN DCF SENSITIVITY", "DCF Sensitivity", [this]() {
            QString err;
            QJsonObject p = collect("dcfs", &err);
            const QJsonObject base = collect("dcf", &err);
            if (!run_guard(err))
                return;
            p["base_params"] = base;
            status_label_->setText("Running DCF sensitivity...");
            MAAnalyticsService::instance().calculate_dcf_sensitivity(p);
        });
    }

    // ── LBO returns ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Sponsor IRR / MOIC from equity in and equity out. Exit equity = exit EV - net debt at exit.");
        add_field(tab, t, "lbor.equity_invested", "Equity Invested ($)", 0, kMax, 0);
        add_field(tab, t, "lbor.exit_valuation", "Exit Enterprise Value ($)", 0, kMax, 0);
        add_field(tab, t, "lbor.exit_net_debt", "Net Debt at Exit ($)", -kMax, kMax, 0);
        add_int_field(tab, t, "lbor.holding_period", "Holding Period (yrs)", 1, 30, 5);
        add_field(tab, t, "lbor.entry_valuation", "Entry EV ($, optional)", 0, kMax, 0);
        add_text_field(tab, t, "lbor.interim_distributions", "Interim Distributions ($, optional)",
                       "per year 1..N-1, e.g. 0, 50", "num_list");
        finish_tab(tab, t, "RUN LBO RETURNS", "LBO Returns", [this]() {
            QString err;
            const QJsonObject p = collect("lbor", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running LBO returns...");
            MAAnalyticsService::instance().calculate_lbo_returns(p);
        });
    }

    // ── LBO model ──
    {
        auto* tab = new_tab(t);
        add_section(tab, t, "Entry & exit");
        add_field(tab, t, "lbo.ebitda", "LTM EBITDA ($)", 0, kMax, 0);
        add_field(tab, t, "lbo.revenue", "LTM Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "lbo.entry_multiple", "Entry EV/EBITDA", 0, 100, 2, "x");
        add_field(tab, t, "lbo.exit_multiple", "Exit EV/EBITDA", 0, 100, 2, "x");
        add_int_field(tab, t, "lbo.holding_period", "Holding Period (yrs)", 1, 15, 5);
        add_section(tab, t, "Operating assumptions");
        add_field(tab, t, "lbo.revenue_growth", "Revenue Growth (annual)", -50, 200, 2, "%");
        add_field(tab, t, "lbo.ebitda_margin", "EBITDA Margin (projected)", -100, 100, 2, "%");
        add_field(tab, t, "lbo.d_and_a_pct", "D&A (% of revenue)", 0, 100, 2, "%");
        add_field(tab, t, "lbo.capex_pct", "CapEx (% of revenue)", 0, 100, 2, "%");
        add_field(tab, t, "lbo.nwc_pct", "NWC (% of revenue change)", -100, 100, 2, "%");
        add_field(tab, t, "lbo.tax_rate", "Tax Rate", 0, 60, 1, "%");
        add_section(tab, t, "Financing");
        add_field(tab, t, "lbo.senior_debt", "Senior Debt ($)", 0, kMax, 0);
        add_field(tab, t, "lbo.senior_rate", "Senior Rate", 0, 50, 2, "%");
        add_field(tab, t, "lbo.senior_amort_pct", "Senior Mandatory Amort. (%/yr)", 0, 100, 2, "%");
        add_field(tab, t, "lbo.sub_debt", "Subordinated Debt ($, optional)", 0, kMax, 0);
        add_field(tab, t, "lbo.sub_rate", "Subordinated Rate", 0, 50, 2, "%");
        add_field(tab, t, "lbo.sweep_pct", "Cash Sweep (% of excess FCF)", 0, 100, 0, "%");
        add_field(tab, t, "lbo.transaction_fees", "Transaction Fees ($, optional)", 0, kMax, 0);
        add_field(tab, t, "lbo.financing_fees", "Financing Fees ($, optional)", 0, kMax, 0);
        finish_tab(tab, t, "BUILD LBO MODEL", "LBO Model", [this]() {
            QString err;
            const QJsonObject p = collect("lbo", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Building LBO model...");
            MAAnalyticsService::instance().build_lbo_model(p);
        });
    }

    // ── LBO sensitivity (uses the LBO Model tab's inputs) ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t,
                 "Re-runs the LBO Model tab over a grid of entry and exit multiples (base = that tab's multiples).");
        add_field(tab, t, "lbos.range", "Range (+/- turns)", 0, 10, 1, "x");
        add_int_field(tab, t, "lbos.steps", "Steps per Axis", 2, 11, 5);
        finish_tab(tab, t, "RUN LBO SENSITIVITY", "LBO Sensitivity", [this]() {
            QString err;
            QJsonObject p = collect("lbo", &err);
            const QJsonObject s = collect("lbos", &err);
            if (!run_guard(err))
                return;
            for (auto it = s.begin(); it != s.end(); ++it)
                p[it.key()] = it.value();
            status_label_->setText("Running LBO sensitivity...");
            MAAnalyticsService::instance().calculate_lbo_sensitivity(p);
        });
    }

    // ── Debt schedule ──
    {
        auto* tab = new_tab(t);
        add_section(tab, t, "Cash flow");
        add_int_field(tab, t, "ds.years", "Years", 1, 30, 5);
        add_field(tab, t, "ds.ebitda", "Year-1 EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "ds.ebitda_growth", "EBITDA Growth (annual)", -50, 200, 2, "%");
        add_field(tab, t, "ds.d_and_a", "D&A ($/yr)", 0, kMax, 0);
        add_field(tab, t, "ds.capex", "CapEx ($/yr)", 0, kMax, 0);
        add_field(tab, t, "ds.nwc_change", "Increase in NWC ($/yr)", -kMax, kMax, 0);
        add_field(tab, t, "ds.tax_rate", "Tax Rate", 0, 60, 1, "%");
        add_section(tab, t, "Debt (repaid revolver -> senior; sub is bullet)");
        add_field(tab, t, "ds.revolver", "Revolver Drawn ($, optional)", 0, kMax, 0);
        add_field(tab, t, "ds.revolver_rate", "Revolver Rate", 0, 50, 2, "%");
        add_field(tab, t, "ds.senior_debt", "Senior Debt ($)", 0, kMax, 0);
        add_field(tab, t, "ds.senior_rate", "Senior Rate", 0, 50, 2, "%");
        add_field(tab, t, "ds.senior_amort_pct", "Senior Mandatory Amort. (%/yr)", 0, 100, 2, "%");
        add_field(tab, t, "ds.sub_debt", "Subordinated Debt ($, optional)", 0, kMax, 0);
        add_field(tab, t, "ds.sub_rate", "Subordinated Rate", 0, 50, 2, "%");
        add_field(tab, t, "ds.sweep_pct", "Cash Sweep (% of excess FCF)", 0, 100, 0, "%");
        finish_tab(tab, t, "ANALYZE DEBT SCHEDULE", "Debt Schedule", [this]() {
            QString err;
            const QJsonObject p = collect("ds", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Analyzing debt schedule...");
            MAAnalyticsService::instance().analyze_lbo_debt_schedule(p);
        });
    }

    // ── Trading comps ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Live LTM figures from yfinance. EV = market cap + total debt - cash.");
        add_text_field(tab, t, "comps.target_ticker", "Target Ticker", "e.g. KO", "text");
        add_text_field(tab, t, "comps.comp_tickers", "Comparable Tickers", "e.g. PEP, KDP, MNST", "text");
        finish_tab(tab, t, "RUN TRADING COMPS", "Trading Comps", [this]() {
            QString err;
            const QJsonObject p = collect("comps", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running trading comps...");
            MAAnalyticsService::instance().calculate_trading_comps(p);
        });
    }

    // ── Precedent transactions ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "prec.target_revenue", "Target LTM Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "prec.target_ebitda", "Target LTM EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "prec.target_net_debt", "Target Net Debt ($, optional)", -kMax, kMax, 0);
        add_field(tab, t, "prec.target_shares", "Target Diluted Shares (optional)", 0, kMax, 0);
        add_hint(tab, t,
                 "Precedent deals (JSON array). Each: target, acquirer, date, enterprise_value with the target's "
                 "LTM revenue / ebitda -- or ev_revenue / ev_ebitda directly; premium_1day_pct optional.");
        add_json_field(tab, t, "prec.transactions",
                       "[{\"target\":\"...\",\"acquirer\":\"...\",\"date\":\"2025-01-15\",\"enterprise_value\":N,"
                       "\"revenue\":N,\"ebitda\":N}]");
        finish_tab(tab, t, "RUN PRECEDENT ANALYSIS", "Precedent Txns", [this]() {
            QString err;
            const QJsonObject p = collect("prec", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running precedent transactions...");
            MAAnalyticsService::instance().calculate_precedent_transactions(p);
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 2: MERGER ANALYSIS
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_merger_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;
    constexpr double kMax = 1e15;

    // ── Accretion / dilution ──
    {
        auto* tab = new_tab(t);
        add_section(tab, t, "Acquirer");
        add_field(tab, t, "ad.acquirer.net_income", "Net Income ($)", -kMax, kMax, 0);
        add_field(tab, t, "ad.acquirer.shares", "Diluted Shares", 0, kMax, 0);
        add_field(tab, t, "ad.acquirer.share_price", "Share Price ($)", 0, 1e7, 2);
        add_section(tab, t, "Target");
        add_field(tab, t, "ad.target.net_income", "Net Income ($)", -kMax, kMax, 0);
        add_section(tab, t, "Deal & financing");
        add_field(tab, t, "ad.deal_value", "Equity Purchase Price ($)", 0, kMax, 0);
        add_field(tab, t, "ad.cash_pct", "Cash Consideration", 0, 100, 1, "%");
        add_field(tab, t, "ad.cash_from_balance_sheet", "Acquirer Cash Used ($)", 0, kMax, 0);
        add_field(tab, t, "ad.debt_rate", "Pre-Tax Rate on New Debt", 0, 50, 2, "%");
        add_field(tab, t, "ad.cash_yield", "Pre-Tax Yield Forgone on Cash", 0, 50, 2, "%");
        add_field(tab, t, "ad.tax_rate", "Tax Rate", 0, 60, 1, "%");
        add_section(tab, t, "Synergies & adjustments (optional)");
        add_field(tab, t, "ad.synergies", "Run-Rate Pre-Tax Synergies ($)", -kMax, kMax, 0);
        add_field(tab, t, "ad.synergy_phase_in", "Synergies Realized This Year", 0, 100, 0, "%");
        add_field(tab, t, "ad.integration_costs", "Integration Costs This Year ($)", 0, kMax, 0);
        add_field(tab, t, "ad.new_amortization", "New D&A from Step-Ups ($)", 0, kMax, 0);
        finish_tab(tab, t, "RUN ACCRETION/DILUTION", "Accretion/Dilution", [this]() {
            QString err;
            const QJsonObject p = collect("ad", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running accretion/dilution...");
            MAAnalyticsService::instance().calculate_accretion_dilution(p);
        });
    }

    // ── Pro forma (multi-year A/D on the Accretion/Dilution tab's deal) ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Multi-year accretion/dilution for the deal on the Accretion/Dilution tab. Acquisition "
                         "debt is held constant.");
        add_int_field(tab, t, "pf.years", "Years", 1, 10, 3);
        add_field(tab, t, "pf.acquirer_ni_growth", "Acquirer Net Income Growth", -50, 200, 2, "%");
        add_field(tab, t, "pf.target_ni_growth", "Target Net Income Growth", -50, 200, 2, "%");
        add_text_field(tab, t, "pf.synergy_phase_in_by_year", "Synergy Phase-In by Year (%)", "e.g. 25, 75, 100",
                       "pct_list");
        add_text_field(tab, t, "pf.integration_costs_by_year", "Integration Costs by Year ($, optional)",
                       "e.g. 40000000, 20000000, 0", "num_list");
        finish_tab(tab, t, "BUILD PRO FORMA", "Pro Forma", [this]() {
            QString err;
            QJsonObject p = collect("ad", &err);
            const QJsonObject f = collect("pf", &err);
            if (!run_guard(err))
                return;
            p.remove("synergy_phase_in");
            p.remove("integration_costs");
            for (auto it = f.begin(); it != f.end(); ++it)
                p[it.key()] = it.value();
            status_label_->setText("Building pro forma...");
            MAAnalyticsService::instance().build_pro_forma(p);
        });
    }

    // ── Synergy valuation ──
    {
        auto* tab = new_tab(t);
        add_section(tab, t, "Run-rate synergies");
        add_field(tab, t, "syn.combined_revenue", "Combined Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "syn.revenue_synergy_pct", "Revenue Synergy (% of revenue)", 0, 100, 2, "%");
        add_field(tab, t, "syn.revenue_synergy_margin", "Margin on Synergy Revenue", 0, 100, 1, "%");
        add_field(tab, t, "syn.combined_cost_base", "Combined Cost Base ($)", 0, kMax, 0);
        add_field(tab, t, "syn.cost_synergy_pct", "Cost Synergy (% of cost base)", 0, 100, 2, "%");
        add_section(tab, t, "Valuation");
        add_field(tab, t, "syn.integration_cost", "One-Time Integration Cost ($)", 0, kMax, 0);
        add_field(tab, t, "syn.tax_rate", "Tax Rate", 0, 60, 1, "%");
        add_field(tab, t, "syn.discount_rate", "Discount Rate", 0, 50, 2, "%");
        add_int_field(tab, t, "syn.ramp_years", "Years to Full Run-Rate", 1, 10, 3);
        add_int_field(tab, t, "syn.projection_years", "Projection Years", 1, 30, 5);
        add_field(tab, t, "syn.terminal_growth", "Terminal Growth (optional)", -5, 10, 2, "%");
        finish_tab(tab, t, "VALUE SYNERGIES", "Synergies", [this]() {
            QString err;
            const QJsonObject p = collect("syn", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Valuing synergies...");
            MAAnalyticsService::instance().value_synergies_dcf(p);
        });
    }

    // ── Sources & uses ──
    {
        auto* tab = new_tab(t);
        add_section(tab, t, "Uses");
        add_field(tab, t, "su.purchase_price", "Equity Purchase Price ($)", 0, kMax, 0);
        add_field(tab, t, "su.target_debt_refinanced", "Target Debt Refinanced ($)", 0, kMax, 0);
        add_field(tab, t, "su.transaction_fees", "Transaction Fees ($)", 0, kMax, 0);
        add_field(tab, t, "su.financing_fees", "Financing Fees ($)", 0, kMax, 0);
        add_section(tab, t, "Sources");
        add_field(tab, t, "su.acquirer_cash", "Acquirer Cash ($)", 0, kMax, 0);
        add_field(tab, t, "su.new_debt", "New Debt ($)", 0, kMax, 0);
        add_field(tab, t, "su.new_equity", "New Equity Issued ($)", 0, kMax, 0);
        finish_tab(tab, t, "CALCULATE SOURCES & USES", "Sources & Uses", [this]() {
            QString err;
            const QJsonObject p = collect("su", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Calculating sources & uses...");
            MAAnalyticsService::instance().calculate_sources_uses(p);
        });
    }

    // ── Contribution ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Each party's share of combined revenue / EBITDA / net income vs the ownership split.");
        add_field(tab, t, "contrib.acquirer.revenue", "Acquirer Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "contrib.acquirer.ebitda", "Acquirer EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "contrib.acquirer.net_income", "Acquirer Net Income ($)", -kMax, kMax, 0);
        add_field(tab, t, "contrib.target.revenue", "Target Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "contrib.target.ebitda", "Target EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "contrib.target.net_income", "Target Net Income ($)", -kMax, kMax, 0);
        add_field(tab, t, "contrib_own.acquirer", "Acquirer Ownership of Combined", 0, 100, 2, "%");
        finish_tab(tab, t, "ANALYZE CONTRIBUTION", "Contribution", [this]() {
            QString err;
            QJsonObject p = collect("contrib", &err);
            if (!run_guard(err))
                return;
            if (!entered("contrib_own.acquirer")) {
                display_error("Enter the acquirer's ownership of the combined company");
                return;
            }
            // contribution_analysis.py's ownership_split is the TARGET holders' share.
            p["ownership_split"] = 1.0 - double_inputs_["contrib_own.acquirer"]->value() / 100.0;
            status_label_->setText("Analyzing contribution...");
            MAAnalyticsService::instance().analyze_contribution(p);
        });
    }

    // ── Payment structure ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "pay.purchase_price", "Purchase Price ($)", 0, kMax, 0);
        add_field(tab, t, "pay.cash_pct", "Cash Consideration", 0, 100, 1, "%");
        add_field(tab, t, "pay.cash_on_hand", "Acquirer Cash on Hand ($)", 0, kMax, 0);
        add_field(tab, t, "pay.new_debt", "New Debt Available ($)", 0, kMax, 0);
        add_field(tab, t, "pay.acquirer_share_price", "Acquirer Share Price ($)", 0, 1e7, 2);
        add_field(tab, t, "pay.acquirer_shares_outstanding", "Acquirer Shares Outstanding", 0, kMax, 0);
        add_field(tab, t, "pay.target_shares_outstanding", "Target Shares (optional)", 0, kMax, 0);
        finish_tab(tab, t, "ANALYZE PAYMENT STRUCTURE", "Payment", [this]() {
            QString err;
            const QJsonObject p = collect("pay", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Analyzing payment structure...");
            MAAnalyticsService::instance().analyze_payment_structure(p);
        });
    }

    // ── Earnout ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "earn.earnout_amount", "Earnout Payment if Met ($)", 0, kMax, 0);
        add_field(tab, t, "earn.probability", "Probability Target Is Met", 0, 100, 1, "%");
        add_int_field(tab, t, "earn.years", "Years Until Payment", 1, 10, 3);
        add_field(tab, t, "earn.discount_rate", "Discount Rate", 0, 50, 2, "%");
        add_field(tab, t, "earn.threshold", "Performance Threshold ($, info)", 0, kMax, 0);
        add_field(tab, t, "earn.base_price", "Upfront Price ($, optional)", 0, kMax, 0);
        finish_tab(tab, t, "VALUE EARNOUT", "Earnout", [this]() {
            QString err;
            const QJsonObject p = collect("earn", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Valuing earnout...");
            MAAnalyticsService::instance().value_earnout(p);
        });
    }

    // ── Exchange ratio ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "exch.acquirer_price", "Acquirer Share Price ($)", 0, 1e7, 2);
        add_field(tab, t, "exch.target_price", "Target Unaffected Price ($)", 0, 1e7, 2);
        add_field(tab, t, "exch.premium", "Offer Premium", 0, 300, 1, "%");
        add_field(tab, t, "exch.target_shares_outstanding", "Target Shares (optional)", 0, kMax, 0);
        add_field(tab, t, "exch.acquirer_shares_outstanding", "Acquirer Shares (optional)", 0, kMax, 0);
        finish_tab(tab, t, "CALCULATE EXCHANGE RATIO", "Exchange Ratio", [this]() {
            QString err;
            const QJsonObject p = collect("exch", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Calculating exchange ratio...");
            MAAnalyticsService::instance().calculate_exchange_ratio(p);
        });
    }

    // ── Collar ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "collar.collar_type", "Collar Type",
                   {"Fixed exchange ratio inside band|fixed_ratio", "Fixed value inside band|fixed_value"});
        add_field(tab, t, "collar.base_ratio", "Base Exchange Ratio", 0, 100, 4);
        add_field(tab, t, "collar.acquirer_price", "Reference Acquirer Price ($)", 0, 1e7, 2);
        add_field(tab, t, "collar.floor_price", "Floor Price ($)", 0, 1e7, 2);
        add_field(tab, t, "collar.cap_price", "Cap Price ($)", 0, 1e7, 2);
        add_field(tab, t, "collar.target_shares", "Target Shares (optional)", 0, kMax, 0);
        finish_tab(tab, t, "ANALYZE COLLAR", "Collar", [this]() {
            QString err;
            const QJsonObject p = collect("collar", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Analyzing collar...");
            MAAnalyticsService::instance().analyze_collar_mechanism(p);
        });
    }

    // ── CVR ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "cvr.type", "CVR Type", {"milestone", "revenue", "regulatory"});
        add_field(tab, t, "cvr.payment", "Payment if Triggered ($)", 0, kMax, 0);
        add_field(tab, t, "cvr.probability", "Probability of Trigger", 0, 100, 1, "%");
        add_int_field(tab, t, "cvr.years", "Years to Trigger", 1, 15, 3);
        add_field(tab, t, "cvr.discount_rate", "Discount Rate", 0, 50, 2, "%");
        add_field(tab, t, "cvr.shares_outstanding", "CVRs Outstanding (optional)", 0, kMax, 0);
        finish_tab(tab, t, "VALUE CVR", "CVR", [this]() {
            QString err;
            const QJsonObject p = collect("cvr", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Valuing CVR...");
            MAAnalyticsService::instance().value_cvr(p);
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 3: DEAL DATABASE
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_deals_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    // Controls row
    auto* ctl = new QWidget(w);
    auto* ctl_hl = new QHBoxLayout(ctl);
    ctl_hl->setContentsMargins(0, 0, 0, 0);
    ctl_hl->setSpacing(8);

    auto* scan_days = make_int_spin(1, 365, 30, ctl);
    int_inputs_["scan_days"] = scan_days;
    ctl_hl->addWidget(new QLabel("Scan Days:", ctl));
    ctl_hl->addWidget(scan_days);

    auto* scan_btn = make_run_button("SCAN SEC FILINGS", ctl);
    connect(scan_btn, &QPushButton::clicked, this, [this]() {
        status_label_->setText("Scanning SEC EDGAR...");
        MAAnalyticsService::instance().scan_filings(int_inputs_["scan_days"]->value());
    });
    ctl_hl->addWidget(scan_btn);

    auto* load_btn = make_run_button("LOAD ALL DEALS", ctl);
    connect(load_btn, &QPushButton::clicked, this, [this]() {
        status_label_->setText("Loading deals...");
        MAAnalyticsService::instance().get_all_deals();
    });
    ctl_hl->addWidget(load_btn);
    ctl_hl->addStretch();
    vl->addWidget(ctl);

    // Search
    auto* search_box = new QComboBox(w);
    search_box->setEditable(true);
    search_box->setPlaceholderText("Search deals by target, acquirer, or industry...");
    search_box->setStyleSheet(QString("QComboBox { background:%1; color:%2; border:1px solid %3;"
                                      "font-family:%4; font-size:%5px; padding:6px 10px; }")
                                  .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_MED())
                                  .arg(ui::fonts::DATA_FAMILY)
                                  .arg(ui::fonts::SMALL));
    combo_inputs_["deal_search"] = search_box;
    connect(search_box->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        auto q = combo_inputs_["deal_search"]->currentText();
        if (!q.isEmpty()) {
            status_label_->setText("Searching deals...");
            MAAnalyticsService::instance().search_deals(q);
        }
    });
    vl->addWidget(search_box);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    vl->addStretch();
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 4: STARTUP VALUATION
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_startup_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;
    constexpr double kMax = 1e15;

    // Per-method payload builders, shared with the All Methods tab.
    const QStringList berkus_factors = {"Sound Idea", "Prototype", "Quality Team", "Strategic Relationships",
                                        "Product Rollout"};
    const QStringList sc_factors = {"Management Team",  "Market Size", "Product/Technology", "Competition",
                                    "Marketing/Sales", "Need for Funding", "Other"};
    const QStringList risk_factors = {"Management",        "Stage",         "Legislation", "Manufacturing",
                                      "Sales & Marketing", "Funding",       "Competition", "Technology",
                                      "Litigation",        "International", "Reputation",  "Exit Opportunity"};

    auto berkus_payload = [this, n = berkus_factors.size()]() {
        QJsonObject p = collect("berkus");
        QJsonArray scores;
        for (int i = 0; i < n; ++i) {
            const QString k = QString("berkus_s.%1").arg(i);
            scores.append(entered(k) ? QJsonValue(double_inputs_[k]->value() / 100.0) : QJsonValue());
        }
        p["scores"] = scores;
        return p;
    };
    auto scorecard_payload = [this, n = sc_factors.size()]() {
        QJsonObject p = collect("sc");
        QJsonArray a;
        for (int i = 0; i < n; ++i) {
            const QString k = QString("sc_f.%1").arg(i);
            a.append(entered(k) ? QJsonValue(double_inputs_[k]->value()) : QJsonValue());
        }
        p["assessments"] = a;
        return p;
    };
    auto vc_payload = [this]() { return collect("vc"); };
    auto fc_payload = [this]() {
        QJsonObject p = collect("fc");
        QJsonArray sc;
        const char* names[] = {"Bull", "Base", "Bear"};
        for (int i = 0; i < 3; ++i) {
            const QString pk = QString("fc_s.prob_%1").arg(i), vk = QString("fc_s.value_%1").arg(i);
            if (!entered(pk) && !entered(vk))
                continue;
            QJsonObject s;
            s["name"] = names[i];
            s["probability"] = entered(pk) ? QJsonValue(double_inputs_[pk]->value() / 100.0) : QJsonValue();
            s["exit_value"] = entered(vk) ? QJsonValue(double_inputs_[vk]->value()) : QJsonValue();
            sc.append(s);
        }
        p["scenarios"] = sc;
        return p;
    };
    auto rf_payload = [this, n = risk_factors.size()]() {
        QJsonObject p = collect("rf");
        QJsonArray a;
        for (int i = 0; i < n; ++i)
            a.append(int_inputs_[QString("rf_f.%1").arg(i)]->value());
        p["assessments"] = a;
        return p;
    };

    // ── Berkus ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "berkus.max_value_per_factor", "Max Value per Factor ($)", 0, kMax, 0);
        for (int i = 0; i < berkus_factors.size(); ++i)
            add_field(tab, t, QString("berkus_s.%1").arg(i), berkus_factors[i] + " (0-100%)", 0, 100, 0, "%");
        finish_tab(tab, t, "CALCULATE BERKUS", "Berkus", [this, berkus_payload]() {
            status_label_->setText("Calculating Berkus...");
            MAAnalyticsService::instance().calculate_berkus(berkus_payload());
        });
    }

    // ── Scorecard ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "sc.stage", "Stage (label)", {"seed", "early", "growth"});
        add_field(tab, t, "sc.benchmark_pre_money", "Benchmark Pre-Money ($)", 0, kMax, 0);
        add_hint(tab, t, "Factor ratios vs the benchmark company (1.00x = average). Weights: Payne 30/25/15/10/10/5/5.");
        for (int i = 0; i < sc_factors.size(); ++i)
            add_field(tab, t, QString("sc_f.%1").arg(i), sc_factors[i], 0, 3, 2, "x");
        finish_tab(tab, t, "CALCULATE SCORECARD", "Scorecard", [this, scorecard_payload]() {
            status_label_->setText("Calculating scorecard...");
            MAAnalyticsService::instance().calculate_scorecard(scorecard_payload());
        });
    }

    // ── VC method ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "vc.exit_metric", "Exit-Year Metric ($)", 0, kMax, 0);
        add_field(tab, t, "vc.exit_multiple", "Exit Multiple", 0, 200, 2, "x");
        add_int_field(tab, t, "vc.years", "Years to Exit", 1, 20, 5);
        add_field(tab, t, "vc.investment", "Investment ($)", 0, kMax, 0);
        add_field(tab, t, "vc.target_return", "Target Annual Return", 0, 500, 1, "%");
        add_field(tab, t, "vc.retention_ratio", "Retention after Dilution (optional)", 0, 100, 1, "%");
        finish_tab(tab, t, "CALCULATE VC METHOD", "VC Method", [this, vc_payload]() {
            status_label_->setText("Calculating VC method...");
            MAAnalyticsService::instance().calculate_vc_method(vc_payload());
        });
    }

    // ── First Chicago ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "fc.discount_rate", "Discount Rate", 0, 200, 1, "%");
        add_int_field(tab, t, "fc.years", "Years to Exit", 1, 20, 5);
        const char* names[] = {"Bull", "Base", "Bear"};
        for (int i = 0; i < 3; ++i) {
            add_section(tab, t, QString("%1 case").arg(names[i]));
            add_field(tab, t, QString("fc_s.prob_%1").arg(i), "Probability", 0, 100, 1, "%");
            add_field(tab, t, QString("fc_s.value_%1").arg(i), "Exit Value ($)", 0, kMax, 0);
        }
        finish_tab(tab, t, "CALCULATE FIRST CHICAGO", "First Chicago", [this, fc_payload]() {
            status_label_->setText("Calculating First Chicago...");
            MAAnalyticsService::instance().calculate_first_chicago(fc_payload());
        });
    }

    // ── Risk factor summation ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "rf.base_valuation", "Base Pre-Money ($)", 0, kMax, 0);
        add_field(tab, t, "rf.adjustment_per_step", "Adjustment per Step ($)", 0, kMax, 0);
        for (int i = 0; i < risk_factors.size(); ++i)
            add_int_field(tab, t, QString("rf_f.%1").arg(i), risk_factors[i] + " (-2 to +2)", -2, 2, 0);
        finish_tab(tab, t, "CALCULATE RISK FACTOR", "Risk Factor", [this, rf_payload]() {
            status_label_->setText("Calculating risk factor summation...");
            MAAnalyticsService::instance().calculate_risk_factor(rf_payload());
        });
    }

    // ── All methods ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Runs every method with the inputs on its own tab and reports the range; a method with "
                         "missing inputs is listed under errors.");
        finish_tab(tab, t, "RUN ALL METHODS", "All Methods",
                   [this, berkus_payload, scorecard_payload, vc_payload, fc_payload, rf_payload]() {
                       QJsonObject p;
                       p["berkus"] = berkus_payload();
                       p["scorecard"] = scorecard_payload();
                       p["vc"] = vc_payload();
                       p["first_chicago"] = fc_payload();
                       p["risk_factor"] = rf_payload();
                       status_label_->setText("Running all methods...");
                       MAAnalyticsService::instance().calculate_comprehensive_startup(p);
                   });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 5: FAIRNESS OPINION
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_fairness_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;

    // ── Fairness analysis ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Per-share values. Enter a low/high range for each method you ran (DCF, comps, precedents).");
        add_field(tab, t, "fo.offer_price", "Offer Price ($/sh)", 0, 1e7, 2);
        const QStringList methods = {"DCF", "Trading Comps", "Precedent Transactions"};
        for (int i = 0; i < methods.size(); ++i) {
            add_section(tab, t, methods[i]);
            add_field(tab, t, QString("fo_m.low_%1").arg(i), "Low ($/sh)", 0, 1e7, 2);
            add_field(tab, t, QString("fo_m.high_%1").arg(i), "High ($/sh)", 0, 1e7, 2);
        }
        add_section(tab, t, "Trading range (optional)");
        add_field(tab, t, "fo.week52_low", "52-Week Low ($/sh)", 0, 1e7, 2);
        add_field(tab, t, "fo.week52_high", "52-Week High ($/sh)", 0, 1e7, 2);
        finish_tab(tab, t, "GENERATE FAIRNESS ANALYSIS", "Fairness Analysis", [this, methods]() {
            QJsonObject p = collect("fo");
            QJsonArray arr;
            for (int i = 0; i < methods.size(); ++i) {
                const QString lk = QString("fo_m.low_%1").arg(i), hk = QString("fo_m.high_%1").arg(i);
                if (!entered(lk) && !entered(hk))
                    continue;
                if (!entered(lk) || !entered(hk)) {
                    display_error(QString("%1: enter both low and high").arg(methods[i]));
                    return;
                }
                arr.append(QJsonObject{{"method", methods[i]},
                                       {"low", double_inputs_[lk]->value()},
                                       {"high", double_inputs_[hk]->value()}});
            }
            p["methods"] = arr;
            status_label_->setText("Generating fairness analysis...");
            MAAnalyticsService::instance().generate_fairness_opinion(p);
        });
    }

    // ── Premium analysis ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Premiums are measured against UNAFFECTED prices (before any leak or announcement).");
        add_field(tab, t, "pa.offer_price", "Offer Price ($/sh)", 0, 1e7, 2);
        add_field(tab, t, "pa.price_1d", "Unaffected Price, 1 Day Prior ($)", 0, 1e7, 2);
        add_field(tab, t, "pa.price_1w", "1 Week Prior ($, optional)", 0, 1e7, 2);
        add_field(tab, t, "pa.price_4w", "4 Weeks Prior ($, optional)", 0, 1e7, 2);
        add_field(tab, t, "pa.price_52w", "52-Week High ($, optional)", 0, 1e7, 2);
        add_field(tab, t, "pa.shares_outstanding", "Diluted Shares (optional)", 0, 1e15, 0);
        finish_tab(tab, t, "ANALYZE PREMIUM", "Premium Analysis", [this]() {
            status_label_->setText("Analyzing premium...");
            MAAnalyticsService::instance().analyze_premium(collect("pa"));
        });
    }

    // ── Process quality ──
    {
        auto* tab = new_tab(t);
        const QStringList pq_factors = {"Board Independence",  "Special Committee", "Independent Advisor",
                                        "Market Check",        "Negotiation Process", "Due Diligence",
                                        "Disclosure Quality",  "Timing Adequacy"};
        for (int i = 0; i < pq_factors.size(); ++i)
            add_int_field(tab, t, QString("pq_f.%1").arg(i), pq_factors[i] + " (1-5)", 1, 5, 3);
        finish_tab(tab, t, "ASSESS PROCESS QUALITY", "Process Quality", [this, n = pq_factors.size()]() {
            QJsonObject p;
            QJsonArray factors;
            for (int i = 0; i < n; ++i)
                factors.append(int_inputs_[QString("pq_f.%1").arg(i)]->value());
            p["factors"] = factors;
            status_label_->setText("Assessing process quality...");
            MAAnalyticsService::instance().assess_process_quality(p);
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 6: INDUSTRY METRICS
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_industry_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;
    constexpr double kMax = 1e18;

    // ── Technology ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Fill the fields for the chosen sector; blank fields are not used.");
        add_choice(tab, t, "tech.sector", "Sector", {"SaaS|saas", "Marketplace|marketplace", "Semiconductor|semiconductor"});
        add_section(tab, t, "SaaS");
        add_field(tab, t, "tech.arr", "ARR ($)", 0, kMax, 0);
        add_field(tab, t, "tech.growth", "ARR / Revenue Growth", -100, 1000, 1, "%");
        add_field(tab, t, "tech.gross_margin", "Gross Margin", -100, 100, 1, "%");
        add_field(tab, t, "tech.profit_margin", "FCF or EBITDA Margin", -500, 100, 1, "%");
        add_field(tab, t, "tech.nrr", "Net Revenue Retention (optional)", 0, 300, 1, "%");
        add_field(tab, t, "tech.sm_expense", "S&M Expense ($, optional)", 0, kMax, 0);
        add_field(tab, t, "tech.net_new_arr", "Net New ARR ($, optional)", -kMax, kMax, 0);
        add_field(tab, t, "tech.new_customers", "New Customers (optional)", 0, kMax, 0);
        add_field(tab, t, "tech.arpa", "ARR per Account ($, optional)", 0, kMax, 0);
        add_field(tab, t, "tech.annual_churn", "Annual Logo Churn (optional)", 0, 100, 1, "%");
        add_section(tab, t, "Marketplace / semiconductor");
        add_field(tab, t, "tech.gmv", "GMV ($)", 0, kMax, 0);
        add_field(tab, t, "tech.revenue", "Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "tech.rd_spend", "R&D Spend ($)", 0, kMax, 0);
        add_field(tab, t, "tech.backlog", "Backlog ($, optional)", 0, kMax, 0);
        add_field(tab, t, "tech.ebitda", "EBITDA ($, optional)", -kMax, kMax, 0);
        add_field(tab, t, "tech.enterprise_value", "Enterprise Value ($, optional)", 0, kMax, 0);
        finish_tab(tab, t, "CALCULATE TECH METRICS", "Technology", [this]() {
            status_label_->setText("Calculating tech metrics...");
            MAAnalyticsService::instance().calculate_tech_metrics(collect("tech"));
        });
    }

    // ── Healthcare ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "hc.sector", "Sector",
                   {"Pharma|pharma", "Biotech|biotech", "Devices|devices", "Services|services"});
        add_field(tab, t, "hc.revenue", "Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "hc.ebitda_margin", "EBITDA Margin", -500, 100, 1, "%");
        add_field(tab, t, "hc.rd_spend", "R&D Spend ($, optional)", 0, kMax, 0);
        add_field(tab, t, "hc.pipeline_npv", "Risk-Adj. Pipeline NPV ($, optional)", 0, kMax, 0);
        add_int_field(tab, t, "hc.phase3_candidates", "Phase 3 Candidates", 0, 100, 0);
        add_field(tab, t, "hc.patent_expiry_revenue", "Revenue Losing Exclusivity (optional)", 0, 100, 1, "%");
        add_field(tab, t, "hc.enterprise_value", "Enterprise Value ($, optional)", 0, kMax, 0);
        finish_tab(tab, t, "CALCULATE HC METRICS", "Healthcare", [this]() {
            status_label_->setText("Calculating healthcare metrics...");
            MAAnalyticsService::instance().calculate_healthcare_metrics(collect("hc"));
        });
    }

    // ── Financial services ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Fill the fields for the chosen sector; blank fields are not used.");
        add_choice(tab, t, "fs.sector", "Sector",
                   {"Banking|banking", "Insurance|insurance", "Asset Management|asset_management"});
        add_section(tab, t, "Banking");
        add_field(tab, t, "fs.total_assets", "Total Assets ($)", 0, kMax, 0);
        add_field(tab, t, "fs.deposits", "Deposits ($)", 0, kMax, 0);
        add_field(tab, t, "fs.gross_loans", "Gross Loans ($, optional)", 0, kMax, 0);
        add_field(tab, t, "fs.nim", "Net Interest Margin (optional)", -20, 20, 2, "%");
        add_field(tab, t, "fs.efficiency_ratio", "Efficiency Ratio (optional)", 0, 300, 1, "%");
        add_field(tab, t, "fs.cet1_ratio", "CET1 Ratio (optional)", 0, 100, 1, "%");
        add_field(tab, t, "fs.npl_ratio", "NPL Ratio (optional)", 0, 100, 2, "%");
        add_section(tab, t, "Insurance");
        add_field(tab, t, "fs.net_premiums_earned", "Net Premiums Earned ($)", 0, kMax, 0);
        add_field(tab, t, "fs.losses_incurred", "Losses Incurred ($)", 0, kMax, 0);
        add_field(tab, t, "fs.underwriting_expenses", "Underwriting Expenses ($)", 0, kMax, 0);
        add_section(tab, t, "Asset management");
        add_field(tab, t, "fs.aum", "AUM ($)", 0, kMax, 0);
        add_field(tab, t, "fs.net_flows", "Net Flows ($, optional)", -kMax, kMax, 0);
        add_section(tab, t, "Common (optional unless the sector needs it)");
        add_field(tab, t, "fs.revenue", "Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "fs.net_income", "Net Income ($)", -kMax, kMax, 0);
        add_field(tab, t, "fs.equity", "Shareholders' Equity ($)", -kMax, kMax, 0);
        add_field(tab, t, "fs.tangible_equity", "Tangible Equity ($)", -kMax, kMax, 0);
        add_field(tab, t, "fs.ebitda", "EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "fs.market_cap", "Market Cap ($)", 0, kMax, 0);
        add_field(tab, t, "fs.enterprise_value", "Enterprise Value ($)", 0, kMax, 0);
        finish_tab(tab, t, "CALCULATE FINSERV METRICS", "Financial Services", [this]() {
            status_label_->setText("Calculating financial services metrics...");
            MAAnalyticsService::instance().calculate_financial_services_metrics(collect("fs"));
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 7: ADVANCED ANALYTICS
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_advanced_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;
    constexpr double kMax = 1e15;

    // ── Monte Carlo ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, "Simulated DCF: each path draws annual revenue growth and FCF margin from normal "
                         "distributions. Results are a simulation, not a forecast.");
        add_field(tab, t, "mc.base_revenue", "Base Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "mc.rev_growth_mean", "Revenue Growth Mean", -99, 500, 2, "%");
        add_field(tab, t, "mc.rev_growth_std", "Revenue Growth Std Dev", 0, 500, 2, "%");
        add_field(tab, t, "mc.margin_mean", "FCF Margin Mean", -500, 100, 2, "%");
        add_field(tab, t, "mc.margin_std", "FCF Margin Std Dev", 0, 500, 2, "%");
        add_field(tab, t, "mc.discount_rate", "Discount Rate", 0, 100, 2, "%");
        add_field(tab, t, "mc.terminal_growth", "Terminal Growth", -50, 20, 2, "%");
        add_int_field(tab, t, "mc.projection_years", "Projection Years", 1, 30, 5);
        add_int_field(tab, t, "mc.simulations", "Simulations", 100, 200000, 10000);
        add_text_field(tab, t, "mcs.seed", "Random Seed (optional)", "e.g. 42", "text");
        finish_tab(tab, t, "RUN MONTE CARLO", "Monte Carlo", [this]() {
            QJsonObject p = collect("mc");
            const QString seed = combo_inputs_["mcs.seed"]->currentText().trimmed();
            if (!seed.isEmpty()) {
                bool ok = false;
                const int s = seed.toInt(&ok);
                if (!ok || s < 0) {
                    display_error("Seed must be a non-negative integer");
                    return;
                }
                p["seed"] = s;
            }
            status_label_->setText("Running Monte Carlo...");
            MAAnalyticsService::instance().run_monte_carlo(p);
        });
    }

    // ── Regression ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "reg.type", "Model", {"EV ~ EBITDA (OLS)|ols", "EV ~ Revenue + EBITDA + Growth|multiple"});
        add_field(tab, t, "reg.subject.revenue", "Subject Revenue ($)", 0, kMax, 0);
        add_field(tab, t, "reg.subject.ebitda", "Subject EBITDA ($)", -kMax, kMax, 0);
        add_field(tab, t, "reg.subject.growth", "Subject Growth", -100, 500, 2, "%");
        add_hint(tab, t, "Comparable companies (JSON array, at least k+2): name, ev, revenue, ebitda, growth "
                         "(decimal).");
        add_json_field(tab, t, "reg.comparables",
                       "[{\"name\":\"...\",\"ev\":N,\"revenue\":N,\"ebitda\":N,\"growth\":0.12}]");
        finish_tab(tab, t, "RUN REGRESSION", "Regression", [this]() {
            QString err;
            const QJsonObject p = collect("reg", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Running regression...");
            MAAnalyticsService::instance().run_regression(p);
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MODULE 8: DEAL COMPARISON
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* MAModulePanel::build_comparison_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(16, 16, 16, 16);
    vl->setSpacing(12);

    sub_tabs_ = new QTabWidget(w);
    apply_tab_stylesheet();
    QVBoxLayout* t = nullptr;

    const QString deal_hint = "Deals as a JSON array. premium, cash_pct and stock_pct are in PERCENT (45.3 = 45.3%); "
                              "ev_revenue / ev_ebitda are multiples; deal_value in one consistent unit.";
    const QString deal_example = "[{\"acquirer\":\"...\",\"target\":\"...\",\"deal_value\":N,\"premium\":N,"
                                 "\"ev_revenue\":N,\"ev_ebitda\":N,\"cash_pct\":N,\"stock_pct\":N,\"industry\":\"...\"}]";

    // ── Compare ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, deal_hint);
        add_json_field(tab, t, "cmp.deals", deal_example);
        finish_tab(tab, t, "COMPARE DEALS", "Compare", [this]() {
            QString err;
            const QJsonObject p = collect("cmp", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Comparing deals...");
            MAAnalyticsService::instance().compare_deals(p);
        });
    }

    // ── Rank ──
    {
        auto* tab = new_tab(t);
        add_choice(tab, t, "rank.criteria", "Rank By", {"premium", "deal_value", "ev_revenue", "ev_ebitda", "synergies"});
        add_hint(tab, t, deal_hint);
        add_json_field(tab, t, "rank.deals", deal_example);
        finish_tab(tab, t, "RANK DEALS", "Rank", [this]() {
            QString err;
            const QJsonObject p = collect("rank", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Ranking deals...");
            MAAnalyticsService::instance().rank_deals(p);
        });
    }

    // ── Benchmark ──
    {
        auto* tab = new_tab(t);
        add_field(tab, t, "bench.target_premium_pct", "Target Deal Premium", -100, 500, 1, "");
        add_hint(tab, t, "Target premium above is in percent (e.g. 32.5). " + deal_hint);
        add_json_field(tab, t, "bench.comparables", deal_example);
        finish_tab(tab, t, "BENCHMARK PREMIUM", "Benchmark", [this]() {
            QString err;
            const QJsonObject p = collect("bench", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Benchmarking premium...");
            MAAnalyticsService::instance().benchmark_deal_premium(p);
        });
    }

    // ── Payment structures ──
    {
        auto* tab = new_tab(t);
        add_hint(tab, t, deal_hint);
        add_json_field(tab, t, "pays.deals", deal_example);
        finish_tab(tab, t, "ANALYZE PAYMENT STRUCTURES", "Payment", [this]() {
            QString err;
            const QJsonObject p = collect("pays", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Analyzing payment structures...");
            MAAnalyticsService::instance().analyze_payment_structures(p);
        });
    }

    // ── Industry ──
    {
        auto* tab = new_tab(t);
        add_text_field(tab, t, "ind.industry", "Industry Filter (optional)", "e.g. Technology", "text");
        add_hint(tab, t, deal_hint);
        add_json_field(tab, t, "ind.deals", deal_example);
        finish_tab(tab, t, "ANALYZE BY INDUSTRY", "Industry", [this]() {
            QString err;
            const QJsonObject p = collect("ind", &err);
            if (!run_guard(err))
                return;
            status_label_->setText("Analyzing industry deals...");
            MAAnalyticsService::instance().analyze_industry_deals(p);
        });
    }

    vl->addWidget(sub_tabs_);

    results_container_ = new QWidget(w);
    results_layout_ = new QVBoxLayout(results_container_);
    results_layout_->setContentsMargins(0, 8, 0, 0);
    results_layout_->setSpacing(8);
    vl->addWidget(results_container_);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════════
// RESULT DISPLAY
// ═══════════════════════════════════════════════════════════════════════════════

void MAModulePanel::clear_results() {
    while (results_layout_->count() > 0) {
        auto* item = results_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void MAModulePanel::display_error(const QString& msg) {
    clear_results();
    auto* err = new QLabel(msg);
    err->setWordWrap(true);
    QColor neg(ui::colors::NEGATIVE());
    auto neg_rgb = QString("%1,%2,%3").arg(neg.red()).arg(neg.green()).arg(neg.blue());
    err->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3; padding:12px;"
                               "background:rgba(%4,0.08); border:1px solid rgba(%4,0.3);")
                           .arg(ui::colors::NEGATIVE())
                           .arg(ui::fonts::SMALL)
                           .arg(ui::fonts::DATA_FAMILY())
                           .arg(neg_rgb));
    results_layout_->addWidget(err);
    status_label_->setText("Error");
}

// Formatting follows the scripts' output naming (corporateFinance/_cli.py):
// "*_pct" (or a "%" label) = percent units, "*_x" = multiple, otherwise a
// plain number. Units are never guessed from magnitude, and null is "—".
static QString format_value(const QString& key, const QJsonValue& val) {
    namespace fmt = fincept::ui::formatting;
    if (val.isBool())
        return val.toBool() ? "YES" : "NO";
    if (val.isString())
        return val.toString();
    if (!val.isDouble())
        return fmt::placeholder();
    const double v = val.toDouble();
    const QString k = key.toLower();
    if (k.endsWith("_pct") || k.endsWith("%") || k.contains(" %"))
        return fmt::format_percent(v, 2);
    if (k.endsWith("_x") || k.contains("moic"))
        return QString::number(v, 'f', 2) + QLatin1Char('x');
    const double a = std::abs(v);
    if ((k == "year" || k.endsWith("_year") || k.endsWith("years")) && v == std::floor(v))
        return QString::number(static_cast<qint64>(v));
    if (a >= 1e5)
        return fmt::format_compact(v, 2);
    if (a >= 1e3)
        return QLocale::c().toString(v, 'f', 0);
    if (v == std::floor(v))
        return QString::number(static_cast<qint64>(v));
    if (a >= 1.0)
        return QString::number(v, 'f', 2);
    return QString::number(v, 'g', 4);
}

static QTableWidget* build_json_table(const QJsonArray& arr, const QString& accent, QWidget* parent) {
    if (arr.isEmpty())
        return nullptr;
    // Columns: union of scalar keys across all rows (a summary row may carry
    // fewer keys than the data rows).
    QStringList cols;
    for (const auto& rv : arr) {
        const auto ro = rv.toObject();
        for (auto it = ro.begin(); it != ro.end(); ++it)
            if (!it.value().isObject() && !it.value().isArray() && !cols.contains(it.key()))
                cols.append(it.key());
    }
    if (cols.isEmpty())
        return nullptr;

    auto* table = new QTableWidget(arr.size(), cols.size(), parent);
    table->setHorizontalHeaderLabels(cols);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setMaximumHeight(300);
    table->setStyleSheet(QString("QTableWidget { background:%1; color:%2; gridline-color:%3;"
                                 "font-family:%4; font-size:%5px; border:1px solid %3; }"
                                 "QTableWidget::item { padding:4px 8px; }"
                                 "QTableWidget::item:selected { background:%6; }"
                                 "QHeaderView::section { background:%7; color:%8; font-weight:700;"
                                 "padding:4px 8px; border:1px solid %3; font-family:%4; font-size:%5px; }"
                                 "QTableWidget::item:alternate { background:%9; }")
                             .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM())
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(ui::fonts::SMALL)
                             .arg(QString("rgba(%1,0.15)").arg(accent))
                             .arg(ui::colors::BG_RAISED())
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::colors::ROW_ALT()));

    for (int r = 0; r < arr.size(); ++r) {
        auto obj = arr[r].toObject();
        for (int c = 0; c < cols.size(); ++c) {
            auto* item = new QTableWidgetItem(format_value(cols[c], obj.value(cols[c])));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(r, c, item);
        }
    }
    table->resizeColumnsToContents();
    return table;
}

static QTableWidget* build_kv_table(const QJsonObject& obj, const QString& accent, QWidget* parent) {
    // Collect only scalar key-value pairs
    QStringList keys;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!it.value().isObject() && !it.value().isArray())
            keys.append(it.key());
    if (keys.isEmpty())
        return nullptr;

    auto* table = new QTableWidget(keys.size(), 2, parent);
    table->setHorizontalHeaderLabels({"Metric", "Value"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setMaximumHeight(400);
    table->setStyleSheet(QString("QTableWidget { background:%1; color:%2; gridline-color:%3;"
                                 "font-family:%4; font-size:%5px; border:1px solid %3; }"
                                 "QTableWidget::item { padding:4px 8px; }"
                                 "QTableWidget::item:selected { background:%6; }"
                                 "QHeaderView::section { background:%7; color:%8; font-weight:700;"
                                 "padding:4px 8px; border:1px solid %3; font-family:%4; font-size:%5px; }"
                                 "QTableWidget::item:alternate { background:%9; }")
                             .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM())
                             .arg(ui::fonts::DATA_FAMILY)
                             .arg(ui::fonts::SMALL)
                             .arg(QString("rgba(%1,0.15)").arg(accent))
                             .arg(ui::colors::BG_RAISED())
                             .arg(ui::colors::TEXT_SECONDARY())
                             .arg(ui::colors::ROW_ALT()));

    for (int r = 0; r < keys.size(); ++r) {
        auto label = keys[r];
        label.replace('_', ' ');
        auto* key_item = new QTableWidgetItem(label.toUpper());
        key_item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table->setItem(r, 0, key_item);
        auto* val_item = new QTableWidgetItem(format_value(keys[r], obj.value(keys[r])));
        val_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(r, 1, val_item);
    }
    table->resizeColumnsToContents();
    return table;
}

void MAModulePanel::display_result(const QJsonObject& payload) {
    clear_results();

    QString accent = QString("%1,%2,%3").arg(module_.color.red()).arg(module_.color.green()).arg(module_.color.blue());

    // Section header
    auto* header = new QLabel("RESULTS");
    header->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; font-family:%2; letter-spacing:1px;"
                                  "padding:4px 0;")
                              .arg(module_.color.name())
                              .arg(ui::fonts::DATA_FAMILY));
    results_layout_->addWidget(header);

    // 1. Metric cards for top-level scalar values
    auto* grid = new QWidget(this);
    auto* gl = new QGridLayout(grid);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(8);

    int col = 0, row = 0;
    bool has_scalars = false;
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (it.value().isObject() || it.value().isArray())
            continue;
        has_scalars = true;
        QString label = it.key();
        label.replace('_', ' ');
        auto* card = build_metric_card(label.toUpper(), format_value(it.key(), it.value()), module_.color.name(), grid);
        gl->addWidget(card, row, col);
        col++;
        if (col >= 3) {
            col = 0;
            row++;
        }
    }
    if (has_scalars)
        results_layout_->addWidget(grid);
    else
        grid->deleteLater();

    // 2. Tables for nested objects and arrays
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (it.value().isArray()) {
            auto arr = it.value().toArray();
            if (arr.isEmpty())
                continue;

            QString sec_label = it.key();
            sec_label.replace('_', ' ');
            auto* sec = new QLabel(sec_label.toUpper());
            sec->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; font-family:%2;"
                                       "letter-spacing:1px; padding:8px 0 4px 0;")
                                   .arg(module_.color.name())
                                   .arg(ui::fonts::DATA_FAMILY));
            results_layout_->addWidget(sec);

            if (arr[0].isObject()) {
                auto* table = build_json_table(arr, accent, this);
                if (table)
                    results_layout_->addWidget(table);
            } else {
                // Simple array — display as comma-separated
                QStringList items;
                for (const auto& v : arr)
                    items.append(format_value(it.key(), v));
                auto* lbl = new QLabel(items.join(", "));
                lbl->setWordWrap(true);
                lbl->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;"
                                           "padding:4px; background:%4; border:1px solid %5;")
                                       .arg(ui::colors::TEXT_PRIMARY())
                                       .arg(ui::fonts::SMALL)
                                       .arg(ui::fonts::DATA_FAMILY)
                                       .arg(ui::colors::BG_RAISED())
                                       .arg(ui::colors::BORDER_DIM()));
                results_layout_->addWidget(lbl);
            }
        } else if (it.value().isObject()) {
            auto obj = it.value().toObject();
            if (obj.isEmpty())
                continue;

            QString sec_label = it.key();
            sec_label.replace('_', ' ');
            auto* sec = new QLabel(sec_label.toUpper());
            sec->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700; font-family:%2;"
                                       "letter-spacing:1px; padding:8px 0 4px 0;")
                                   .arg(module_.color.name())
                                   .arg(ui::fonts::DATA_FAMILY));
            results_layout_->addWidget(sec);

            auto* table = build_kv_table(obj, accent, this);
            if (table)
                results_layout_->addWidget(table);
        }
    }

    // 3. Raw JSON viewer (collapsed)
    auto* raw_btn = new QPushButton("Show Raw JSON", this);
    raw_btn->setCursor(Qt::PointingHandCursor);
    raw_btn->setStyleSheet(QString("QPushButton { color:%1; font-size:%2px; font-family:%3;"
                                   "background:transparent; border:1px solid %4; padding:4px 12px; }"
                                   "QPushButton:hover { background:%5; }")
                               .arg(ui::colors::TEXT_SECONDARY())
                               .arg(ui::fonts::TINY)
                               .arg(ui::fonts::DATA_FAMILY)
                               .arg(ui::colors::BORDER_DIM())
                               .arg(ui::colors::BG_HOVER()));

    auto* raw_text = new QTextEdit;
    raw_text->setReadOnly(true);
    raw_text->setVisible(false);
    raw_text->setMaximumHeight(300);
    raw_text->setPlainText(QJsonDocument(payload).toJson(QJsonDocument::Indented));
    raw_text->setStyleSheet(QString("QTextEdit { background:%1; color:%2; border:1px solid %3;"
                                    "font-family:%4; font-size:%5px; padding:8px; }")
                                .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM())
                                .arg(ui::fonts::DATA_FAMILY)
                                .arg(ui::fonts::SMALL));

    connect(raw_btn, &QPushButton::clicked, this, [raw_text, raw_btn]() {
        bool showing = raw_text->isVisible();
        raw_text->setVisible(!showing);
        raw_btn->setText(showing ? "Show Raw JSON" : "Hide Raw JSON");
    });

    results_layout_->addWidget(raw_btn);
    results_layout_->addWidget(raw_text);

    status_label_->setText("Done");
}

// ── Service signal handlers ──────────────────────────────────────────────────

static const QHash<ModuleId, QStringList>& get_context_map() {
    static const QHash<ModuleId, QStringList> map = {
        {ModuleId::Valuation,
         {"dcf", "dcf_sensitivity", "football_field", "lbo_returns", "lbo_model", "lbo_debt_schedule",
          "lbo_sensitivity", "trading_comps", "precedent_txns"}},
        {ModuleId::Merger,
         {"merger_model", "accretion_dilution", "pro_forma", "sources_uses", "contribution", "revenue_synergies",
          "cost_synergies", "synergy_dcf", "integration_costs", "payment_structure", "earnout", "exchange_ratio",
          "collar", "cvr"}},
        {ModuleId::Deals, {"scan_filings", "all_deals", "search_deals", "create_deal", "update_deal", "parse_filing"}},
        {ModuleId::Startup,
         {"berkus", "scorecard", "vc_method", "first_chicago", "risk_factor", "startup_comprehensive"}},
        {ModuleId::Fairness, {"fairness_opinion", "premium_analysis", "process_quality"}},
        {ModuleId::Industry, {"tech_metrics", "healthcare_metrics", "finserv_metrics"}},
        {ModuleId::Advanced, {"monte_carlo", "regression"}},
        {ModuleId::Comparison,
         {"compare_deals", "rank_deals", "benchmark_premium", "payment_structures", "industry_deals"}},
    };
    return map;
}

void MAModulePanel::on_result_ready(const QString& context, const QJsonObject& payload) {
    auto it = get_context_map().find(module_.id);
    if (it != get_context_map().end() && it->contains(context)) {
        display_result(payload);
    }
}

void MAModulePanel::on_error(const QString& context, const QString& message) {
    auto it = get_context_map().find(module_.id);
    if (it != get_context_map().end() && it->contains(context)) {
        display_error(QString("[%1] %2").arg(context, message));
    }
}

// ── Tab stylesheet helper ───────────────────────────────────────────────────
void MAModulePanel::apply_tab_stylesheet() {
    if (!sub_tabs_)
        return;
    sub_tabs_->setStyleSheet(QString("QTabWidget::pane { border:1px solid %1; background:%2; }"
                                     "QTabBar::tab { background:%3; color:%4; padding:6px 16px;"
                                     "font-family:%5; font-size:%6px; border:1px solid %1; border-bottom:none; }"
                                     "QTabBar::tab:selected { background:%2; color:%7; font-weight:700;"
                                     "border-bottom:2px solid %7; }")
                                 .arg(ui::colors::BORDER_DIM(), ui::colors::BG_SURFACE(), ui::colors::BG_RAISED())
                                 .arg(ui::colors::TEXT_SECONDARY())
                                 .arg(ui::fonts::DATA_FAMILY())
                                 .arg(ui::fonts::SMALL)
                                 .arg(module_.color.name()));
}

// ── Theme refresh ───────────────────────────────────────────────────────────
void MAModulePanel::refresh_theme() {
    // Header bar
    if (header_bar_)
        header_bar_->setStyleSheet(QString("background:%1; border-bottom:1px solid %2;")
                                       .arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));

    // Header title
    if (header_title_)
        header_title_->setStyleSheet(
            QString("color:%1; font-size:%2px; font-weight:700; font-family:%3; letter-spacing:1px;")
                .arg(module_.color.name())
                .arg(ui::fonts::TINY)
                .arg(ui::fonts::DATA_FAMILY()));

    // Header divider
    if (header_bar_) {
        auto* div = header_bar_->findChild<QWidget*>("maPanelDivider");
        if (div)
            div->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    }

    // Header category
    if (header_category_)
        header_category_->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                                            .arg(ui::colors::TEXT_SECONDARY())
                                            .arg(ui::fonts::TINY)
                                            .arg(ui::fonts::DATA_FAMILY()));

    // Status label
    if (status_label_)
        status_label_->setStyleSheet(QString("color:%1; font-size:%2px; font-family:%3;")
                                         .arg(ui::colors::TEXT_SECONDARY())
                                         .arg(ui::fonts::TINY)
                                         .arg(ui::fonts::DATA_FAMILY()));

    // Tab widget
    apply_tab_stylesheet();
}

} // namespace fincept::screens
