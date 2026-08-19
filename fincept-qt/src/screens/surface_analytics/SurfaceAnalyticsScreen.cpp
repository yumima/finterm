#include "SurfaceAnalyticsScreen.h"

#include "Surface3DWidget.h"
#include "SurfaceCapabilities.h"
#include "SurfaceControlPanel.h"
#include "SurfaceCsvImporter.h"
#include "SurfaceDataInspector.h"
#include "SurfaceDefaults.h"
#include "SurfaceEquityMath.h"
#include "SurfaceLineWidget.h"
#include "SurfaceTableWidget.h"
#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "services/markets/MarketDataService.h"
#include "ui/theme/Theme.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonValue>
#include <QMap>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>

#include <cstdlib>
#include <ctime>

namespace fincept::surface {

using namespace fincept::ui;

static const char* MONO = "'Consolas','Courier New',monospace";

// ── Category accent colors (muted, functional only) ─────────────────────────
// R,G,B kept but used as muted accent — no bright blobs
static constexpr int CAT_ACCENT[][3] = {
    {88, 166, 255},  // EQUITY DERIV
    {63, 185, 80},   // FIXED INCOME
    {217, 164, 6},   // FX
    {220, 80, 80},   // CREDIT
    {155, 114, 255}, // COMMODITIES
    {217, 119, 6},   // RISK  → amber
    {89, 196, 217},  // MACRO
};

// ── Helpers ──────────────────────────────────────────────────────────────────
static QString cat_hex(int i) {
    return QString("rgb(%1,%2,%3)").arg(CAT_ACCENT[i][0]).arg(CAT_ACCENT[i][1]).arg(CAT_ACCENT[i][2]);
}

static QLabel* make_sep(QWidget* parent) {
    auto* s = new QLabel("|", parent);
    s->setStyleSheet(QString("color:%1; font-size:12px; background:transparent;"
                             " font-family:%2;")
                         .arg(colors::BORDER_MED())
                         .arg(MONO));
    return s;
}

static QString btn_inactive() {
    return QString("QPushButton { background:%1; border:1px solid %2; color:%3;"
                   " font-size:12px; font-weight:bold; font-family:%4;"
                   " padding:0 10px; }"
                   "QPushButton:hover { background:%5; color:%6; border-color:%7; }")
        .arg(colors::BG_RAISED())
        .arg(colors::BORDER_DIM())
        .arg(colors::TEXT_SECONDARY())
        .arg(MONO)
        .arg(colors::BG_HOVER())
        .arg(colors::TEXT_PRIMARY())
        .arg(colors::BORDER_BRIGHT());
}

// ── Constructor ──────────────────────────────────────────────────────────────
SurfaceAnalyticsScreen::SurfaceAnalyticsScreen(QWidget* parent) : QWidget(parent) {
    srand((unsigned)time(nullptr));
    setup_ui();
    // NOTE: the surfaces below are SYNTHETIC until a real source is wired —
    // load_demo_data() generates them analytically with rand() noise. The
    // control panel disables FETCH for the DEMO tier and shows an amber
    // "SYNTHETIC DATA" badge (see tier_name/tier_color).
    // Default to the seeded equity underlyings on first open so demo data fills.
    if (!control_panel_->state().basket.isEmpty())
        control_panel_->set_capability(active_chart_);
    load_demo_data();
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

QString SurfaceAnalyticsScreen::current_symbol_or_default() const {
    QString s = control_panel_ ? control_panel_->state().symbol : QString();
    if (s.isEmpty())
        s = QString::fromUtf8(defaults::EQUITY_UNDERLYINGS[0]);
    return s;
}

// 0 means "no quote" — NOT a price. It used to return 100.0, which was then
// stamped onto real fetched surfaces as spot_price and handed to MCP clients
// as a number. A made-up round figure is indistinguishable from a real one
// once it leaves here, so the unknown has to stay unknown.
float SurfaceAnalyticsScreen::spot_for(const QString& sym) const {
    if (sym.isEmpty())
        return 0.0f;
    auto it = spot_cache_.constFind(sym);
    if (it != spot_cache_.constEnd())
        return it.value();
    // Best-effort hub lookup; if no quote published yet, returns invalid QVariant.
    auto& hub = fincept::datahub::DataHub::instance();
    QVariant v = hub.peek(QString("market:quote:%1").arg(sym));
    if (v.isValid()) {
        if (v.canConvert<fincept::services::QuoteData>()) {
            auto q = v.value<fincept::services::QuoteData>();
            if (q.price > 0.0)
                return (float)q.price;
        }
        bool ok = false;
        double d = v.toDouble(&ok);
        if (ok && d > 0.0)
            return (float)d;
    }
    return 0.0f;
}

// ── Layout ───────────────────────────────────────────────────────────────────
void SurfaceAnalyticsScreen::setup_ui() {
    setStyleSheet(QString("QWidget { background:%1; color:%2; font-family:%3; }")
                      .arg(colors::BG_BASE())
                      .arg(colors::TEXT_PRIMARY())
                      .arg(MONO));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Row 1 — category bar
    category_bar_ = build_category_bar();
    root->addWidget(category_bar_);

    // Row 2 — surface chip bar
    surface_bar_ = build_surface_bar();
    root->addWidget(surface_bar_);

    // Vertical split: top = horizontal split (control panel | view stack), bottom = data inspector
    auto* outer = new QSplitter(Qt::Vertical, this);
    outer->setHandleWidth(1);
    outer->setStyleSheet(QString("QSplitter::handle { background:%1; }").arg(colors::BORDER_DIM()));

    auto* hsplit = new QSplitter(Qt::Horizontal, outer);
    hsplit->setHandleWidth(1);
    hsplit->setStyleSheet(QString("QSplitter::handle { background:%1; }").arg(colors::BORDER_DIM()));

    control_panel_ = new SurfaceControlPanel(hsplit);
    hsplit->addWidget(control_panel_);

    auto* right = new QWidget(hsplit);
    right->setStyleSheet(QString("background:%1;").arg(colors::BG_BASE()));
    auto* rvl = new QVBoxLayout(right);
    rvl->setContentsMargins(0, 0, 0, 0);
    rvl->setSpacing(0);

    view_stack_ = new QStackedWidget(right);
    surface_3d_ = new Surface3DWidget(view_stack_);
    surface_table_ = new SurfaceTableWidget(view_stack_);
    surface_line_ = new SurfaceLineWidget(view_stack_);
    view_stack_->addWidget(surface_3d_);    // 0
    view_stack_->addWidget(surface_table_); // 1
    view_stack_->addWidget(surface_line_);  // 2
    rvl->addWidget(view_stack_, 1);

    hsplit->addWidget(right);
    hsplit->setStretchFactor(0, 0);
    hsplit->setStretchFactor(1, 1);
    outer->addWidget(hsplit);

    data_inspector_ = new SurfaceDataInspector(outer);
    outer->addWidget(data_inspector_);
    outer->setStretchFactor(0, 1);
    outer->setStretchFactor(1, 0);
    outer->setSizes({600, 220});

    root->addWidget(outer, 1);

    // Wire control panel signals
    connect(control_panel_, &SurfaceControlPanel::controls_changed, this,
            &SurfaceAnalyticsScreen::on_controls_changed);
    connect(control_panel_, &SurfaceControlPanel::symbol_changed, this,
            &SurfaceAnalyticsScreen::on_control_symbol_changed);
    connect(control_panel_, &SurfaceControlPanel::fetch_requested, this,
            &SurfaceAnalyticsScreen::on_fetch_requested);

    // Default visibility / capability for active surface
    control_panel_->set_capability(active_chart_);
}

// ── Category bar (32px, Obsidian tab style) ──────────────────────────────────
QWidget* SurfaceAnalyticsScreen::build_category_bar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(32);
    bar->setStyleSheet(QString("QWidget { background:%1; border-bottom:1px solid %2; }")
                           .arg(colors::BG_SURFACE())
                           .arg(colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(0);

    const auto categories = get_surface_categories();
    for (int i = 0; i < (int)categories.size(); i++) {
        bool active = (i == active_category_);
        auto* btn = new QPushButton(categories[i].name, bar);
        btn->setFixedHeight(32);

        if (active) {
            btn->setStyleSheet(QString("QPushButton { background:#b45309; color:%1;"
                                       " border:none; border-bottom:2px solid %2;"
                                       " padding:0 14px; font-size:12px; font-weight:bold;"
                                       " font-family:%3; }"
                                       "QPushButton:hover { background:#b45309; }")
                                   .arg(colors::TEXT_PRIMARY())
                                   .arg(cat_hex(i))
                                   .arg(MONO));
        } else {
            btn->setStyleSheet(QString("QPushButton { background:transparent; color:%1;"
                                       " border:none; padding:0 14px; font-size:12px;"
                                       " font-family:%2; }"
                                       "QPushButton:hover { background:%3; color:%4; }")
                                   .arg(colors::TEXT_SECONDARY())
                                   .arg(MONO)
                                   .arg(colors::BG_RAISED())
                                   .arg(colors::TEXT_SECONDARY()));
        }

        btn->setProperty("cat_index", i);
        connect(btn, &QPushButton::clicked, this, [this, i]() { on_category_clicked(i); });
        hl->addWidget(btn);
    }

    hl->addStretch();

    // Right controls — flat Obsidian buttons
    auto* import_btn = new QPushButton("IMPORT CSV", bar);
    import_btn->setFixedHeight(20);
    import_btn->setStyleSheet(btn_inactive());
    connect(import_btn, &QPushButton::clicked, this, &SurfaceAnalyticsScreen::on_import_csv);
    hl->addWidget(import_btn);

    hl->addSpacing(4);
    hl->addWidget(make_sep(bar));
    hl->addSpacing(4);

    btn_3d_ = new QPushButton("3D", bar);
    btn_table_ = new QPushButton("TABLE", bar);
    btn_line_ = new QPushButton("LINE", bar);
    btn_3d_->setFixedHeight(20);
    btn_table_->setFixedHeight(20);
    btn_line_->setFixedHeight(20);
    btn_3d_->setCheckable(true);
    btn_table_->setCheckable(true);
    btn_line_->setCheckable(true);
    btn_3d_->setChecked(view_mode_ == ViewMode::Surface3D);
    btn_table_->setChecked(view_mode_ == ViewMode::Table);
    btn_line_->setChecked(view_mode_ == ViewMode::Line);

    apply_view_mode_buttons();

    connect(btn_3d_, &QPushButton::clicked, this, &SurfaceAnalyticsScreen::on_view_3d);
    connect(btn_table_, &QPushButton::clicked, this, &SurfaceAnalyticsScreen::on_view_table);
    connect(btn_line_, &QPushButton::clicked, this, &SurfaceAnalyticsScreen::on_view_line);
    hl->addWidget(btn_3d_);
    hl->addWidget(btn_table_);
    hl->addWidget(btn_line_);

    hl->addSpacing(4);
    hl->addWidget(make_sep(bar));
    hl->addSpacing(4);

    auto* ref_btn = new QPushButton("REFRESH", bar);
    ref_btn->setFixedHeight(20);
    ref_btn->setStyleSheet(btn_inactive());
    connect(ref_btn, &QPushButton::clicked, this, &SurfaceAnalyticsScreen::on_refresh);
    hl->addWidget(ref_btn);

    return bar;
}

// ── Surface chip bar (26px, hairline) ────────────────────────────────────────
QWidget* SurfaceAnalyticsScreen::build_surface_bar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(26);
    bar->setStyleSheet(QString("QWidget { background:%1; border-bottom:1px solid %2; }")
                           .arg(colors::BG_SURFACE())
                           .arg(colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(8, 0, 8, 0);
    hl->setSpacing(4);

    const auto categories = get_surface_categories();
    if (active_category_ >= (int)categories.size())
        return bar;

    const auto& cat = categories[active_category_];
    const QString acol = cat_hex(active_category_);

    // Category label prefix
    auto* cat_lbl = new QLabel(QString("■ %1").arg(categories[active_category_].name), bar);
    cat_lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold; background:transparent;"
                                   " font-family:%2;")
                               .arg(acol)
                               .arg(MONO));
    hl->addWidget(cat_lbl);
    hl->addWidget(make_sep(bar));

    for (int i = 0; i < (int)cat.types.size(); i++) {
        bool active = (cat.types[i] == active_chart_);
        const char* name = chart_type_name(cat.types[i]);

        auto* btn = new QPushButton(name, bar);
        btn->setFixedHeight(18);

        if (active) {
            btn->setStyleSheet(QString("QPushButton { background:rgba(217,119,6,0.12); border:1px solid %1; color:%2;"
                                       " font-size:12px; font-weight:bold; font-family:%3; padding:0 8px; }"
                                       "QPushButton:hover { background:%2; color:%4; }")
                                   .arg(colors::AMBER_DIM())
                                   .arg(colors::AMBER())
                                   .arg(MONO)
                                   .arg(colors::BG_BASE()));
        } else {
            btn->setStyleSheet(QString("QPushButton { background:transparent; border:none; color:%1;"
                                       " font-size:12px; font-family:%2; padding:0 8px; }"
                                       "QPushButton:hover { color:%3; border-bottom:1px solid %4; }")
                                   .arg(colors::TEXT_SECONDARY())
                                   .arg(MONO)
                                   .arg(colors::TEXT_PRIMARY())
                                   .arg(acol));
        }

        connect(btn, &QPushButton::clicked, this, [this, i]() { on_surface_clicked(active_category_, i); });
        hl->addWidget(btn);
    }

    hl->addStretch();
    return bar;
}

void SurfaceAnalyticsScreen::refresh_surface_bar() {
    auto* main_layout = qobject_cast<QVBoxLayout*>(layout());
    if (!main_layout)
        return;
    int idx = main_layout->indexOf(surface_bar_);
    if (idx < 0)
        return;
    main_layout->removeWidget(surface_bar_);
    surface_bar_->deleteLater();
    surface_bar_ = build_surface_bar();
    main_layout->insertWidget(idx, surface_bar_);
}

// ── Data loading ─────────────────────────────────────────────────────────────
void SurfaceAnalyticsScreen::load_demo_data() {
    // Every surface REGENERATED below loses its provenance — it has just been
    // overwritten, and leaving the mark would keep the badge claiming OPRA
    // over a generated curve.
    //
    // The rates surfaces are not regenerated and are not symbol-dependent: the
    // Treasury curve does not change because the user typed another ticker.
    // The bar-derived risk surfaces are not regenerated either. Clearing their
    // marks would re-badge real data SYNTHETIC and report its lineage as
    // "generated, not fetched" — the same false claim, pointed the other way.
    static const QSet<ChartType> kSurvivesRegeneration = {
        ChartType::YieldCurve, ChartType::RealYield, ChartType::InflationExpectations,
        ChartType::ForwardRate, ChartType::Correlation, ChartType::PCA,
        ChartType::VaR, ChartType::Drawdown, ChartType::BetaSurface,
    };
    for (auto it = fetched_.begin(); it != fetched_.end();)
        it = kSurvivesRegeneration.contains(*it) ? std::next(it) : fetched_.erase(it);
    imported_from_.clear();
    QString qsym = current_symbol_or_default();
    std::string sym = qsym.toStdString();
    // A generated surface needs a scale to be drawn around. With no live quote
    // this is a drawing constant, not a price — which is why everything below
    // is badged SYNTHETIC DATA and its lineage says "generated, not fetched".
    constexpr float kNominalSpot = 100.0f;
    float spot = spot_for(qsym);
    if (spot <= 0.0f)
        spot = kNominalSpot;

    // Build a basket vector<string> from the control-panel state for risk surfaces.
    std::vector<std::string> basket;
    if (control_panel_) {
        for (const QString& s : control_panel_->state().basket)
            basket.push_back(s.toStdString());
    }
    if (basket.empty()) {
        for (auto* s : defaults::RISK_BASKET)
            basket.emplace_back(s);
    }

    vol_data_ = generate_vol_surface(sym.c_str(), spot);
    delta_data_ = generate_delta_surface(sym.c_str(), spot);
    gamma_data_ = generate_gamma_surface(sym.c_str(), spot);
    vega_data_ = generate_vega_surface(sym.c_str(), spot);
    theta_data_ = generate_theta_surface(sym.c_str(), spot);
    skew_data_ = generate_skew_surface(sym.c_str());
    local_vol_data_ = generate_local_vol(sym.c_str(), spot);

    // Swaption / cap-floor vol, the OIS basis and the rating x maturity bond
    // spread grid are gated for the same reason. The curve, real yields,
    // breakevens and the implied forwards derived from the curve are filled
    // from FRED by load_rates_from_fred().

    // FX vol / forward points / cross-currency basis, CDS, rating transitions
    // and recovery rates are gated — see required_feed(). They stay empty, and
    // the chart says which feed it would take rather than drawing a model.

    cmdty_fwd_data_ = generate_commodity_forward();
    cmdty_vol_data_ = generate_commodity_vol();
    crack_data_ = generate_crack_spread();
    contango_data_ = generate_contango();

    // Stress-test P&L and factor exposure are gated: both need definitions the
    // app does not have. VaR, correlation, PCA, drawdown and beta are computed
    // from real bars by compute_equity_surfaces().
    liquidity_data_ = generate_liquidity(sym.c_str(), spot);
    impl_div_data_ = generate_implied_dividend(sym.c_str(), spot);

}

// ── Chart routing ─────────────────────────────────────────────────────────────
void SurfaceAnalyticsScreen::update_chart() {
    // A gated surface draws nothing and says what it would take. Set before
    // the dispatch below so it applies whichever view mode is active.
    if (surface_3d_) {
        const char* feed = required_feed(active_chart_);
        surface_3d_->set_empty_text(
            feed ? QStringLiteral("NO DATA — this surface needs %1.\n\n"
                                  "Nothing is drawn rather than a modelled shape:\n"
                                  "an invented surface is worse than an empty one.")
                       .arg(QString::fromUtf8(feed))
                 : QString());
    }

    auto minmax = [](const std::vector<std::vector<float>>& z, float& mn, float& mx) {
        mn = 9999;
        mx = -9999;
        for (const auto& row : z)
            for (float v : row) {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
    };

    auto fmt_strikes = [](const std::vector<float>& s) {
        std::vector<std::string> v;
        for (float f : s) {
            char b[16];
            std::snprintf(b, 16, "$%.0f", f);
            v.push_back(b);
        }
        return v;
    };
    auto fmt_dtes = [](const std::vector<int>& s) {
        std::vector<std::string> v;
        for (int i : s) {
            char b[16];
            std::snprintf(b, 16, "%dD", i);
            v.push_back(b);
        }
        return v;
    };
    auto fmt_months = [](const std::vector<int>& s) {
        std::vector<std::string> v;
        for (int i : s) {
            char b[16];
            std::snprintf(b, 16, "%dM", i);
            v.push_back(b);
        }
        return v;
    };

    float mn, mx;

    // ── Capability-driven view-mode coercion ───────────────────────────────
    // A surface declares which views it supports. If the active view_mode_ is
    // not supported, fall back to the first one that is.
    const auto& cap = capability_for(active_chart_);
    ViewMode mode = view_mode_;
    if (mode == ViewMode::Surface3D && !cap.supports_3d)
        mode = cap.supports_table ? ViewMode::Table : ViewMode::Line;
    if (mode == ViewMode::Line && !cap.supports_line)
        mode = cap.supports_table ? ViewMode::Table : ViewMode::Surface3D;
    if (mode == ViewMode::Table && !cap.supports_table)
        mode = cap.supports_3d ? ViewMode::Surface3D : ViewMode::Line;

    surface_3d_->set_supported(cap.supports_3d);

    if (mode == ViewMode::Line) {
        view_stack_->setCurrentIndex(2);
        update_line_view();
        return;
    }

    if (mode == ViewMode::Table) {
        view_stack_->setCurrentIndex(1);

        switch (active_chart_) {
            case ChartType::Volatility:
                surface_table_->show_vol(vol_data_);
                return;
            case ChartType::DeltaSurface:
                surface_table_->show_greeks(delta_data_);
                return;
            case ChartType::GammaSurface:
                surface_table_->show_greeks(gamma_data_);
                return;
            case ChartType::VegaSurface:
                surface_table_->show_greeks(vega_data_);
                return;
            case ChartType::ThetaSurface:
                surface_table_->show_greeks(theta_data_);
                return;
            case ChartType::Correlation:
                surface_table_->show_correlation(corr_data_);
                return;
            case ChartType::YieldCurve:
                surface_table_->show_yield(yield_data_);
                return;
            case ChartType::PCA:
                surface_table_->show_pca(pca_data_);
                return;
            default:
                break;
        }

        // Generic table fallback
        std::vector<std::string> r_labels, c_labels;
        const std::vector<std::vector<float>>* zptr = nullptr;
        bool div = false;

        switch (active_chart_) {
            case ChartType::SkewSurface:
                for (int e : skew_data_.expirations)
                    r_labels.push_back(std::to_string(e) + "D");
                for (float d : skew_data_.deltas)
                    c_labels.push_back(std::to_string((int)d) + "D");
                zptr = &skew_data_.z;
                div = true;
                break;
            case ChartType::SwaptionVol:
                for (int e : swaption_data_.option_expiries)
                    r_labels.push_back(std::to_string(e) + "M");
                for (int t : swaption_data_.swap_tenors)
                    c_labels.push_back(std::to_string(t) + "M");
                zptr = &swaption_data_.z;
                break;
            case ChartType::BondSpread:
                r_labels = bond_spread_data_.ratings;
                for (int m : bond_spread_data_.maturities)
                    c_labels.push_back(std::to_string(m) + "M");
                zptr = &bond_spread_data_.z;
                break;
            case ChartType::CDSSpread:
                r_labels = cds_data_.entities;
                for (int t : cds_data_.tenors)
                    c_labels.push_back(std::to_string(t) + "M");
                zptr = &cds_data_.z;
                break;
            case ChartType::CreditTransition:
                r_labels = credit_trans_data_.ratings;
                c_labels = credit_trans_data_.to_ratings;
                zptr = &credit_trans_data_.z;
                div = true;
                break;
            case ChartType::StressTestPnL:
                r_labels = stress_data_.scenarios;
                c_labels = stress_data_.portfolios;
                zptr = &stress_data_.z;
                div = true;
                break;
            case ChartType::FactorExposure:
                r_labels = factor_data_.assets;
                c_labels = factor_data_.factors;
                zptr = &factor_data_.z;
                div = true;
                break;
            case ChartType::Drawdown:
                r_labels = drawdown_data_.assets;
                for (int w : drawdown_data_.windows)
                    c_labels.push_back(std::to_string(w) + "D");
                zptr = &drawdown_data_.z;
                break;
            case ChartType::BetaSurface:
                r_labels = beta_data_.assets;
                for (int h : beta_data_.horizons)
                    c_labels.push_back(std::to_string(h) + "D");
                zptr = &beta_data_.z;
                div = true;
                break;
            case ChartType::CommodityForward:
                r_labels = cmdty_fwd_data_.commodities;
                for (int m : cmdty_fwd_data_.contract_months)
                    c_labels.push_back("M" + std::to_string(m));
                zptr = &cmdty_fwd_data_.z;
                break;
            case ChartType::ContangoBackwardation:
                r_labels = contango_data_.commodities;
                for (int m : contango_data_.contract_months)
                    c_labels.push_back("M" + std::to_string(m));
                zptr = &contango_data_.z;
                div = true;
                break;
            case ChartType::MonetaryPolicyPath:
                r_labels = monetary_data_.central_banks;
                for (int m : monetary_data_.meetings_ahead)
                    c_labels.push_back("Mtg" + std::to_string(m));
                zptr = &monetary_data_.z;
                break;
            default:
                break;
        }

        if (zptr && !zptr->empty()) {
            minmax(*zptr, mn, mx);
            surface_table_->show_generic_matrix(r_labels, c_labels, *zptr, mn, mx, div);
        }
        return;
    }

    // ── 3D mode ───────────────────────────────────────────────────────────────
    view_stack_->setCurrentIndex(0);

    switch (active_chart_) {
        case ChartType::Volatility: {
            minmax(vol_data_.z, mn, mx);
            auto sl = fmt_strikes(vol_data_.strikes), dl = fmt_dtes(vol_data_.expirations);
            surface_3d_->set_surface(vol_data_.z, "STRIKE", "IV %", "DTE", mn, mx, false, &sl, &dl);
            break;
        }
        case ChartType::DeltaSurface: {
            minmax(delta_data_.z, mn, mx);
            auto sl = fmt_strikes(delta_data_.strikes), dl = fmt_dtes(delta_data_.expirations);
            surface_3d_->set_surface(delta_data_.z, "STRIKE", "DELTA", "DTE", mn, mx, false, &sl, &dl);
            break;
        }
        case ChartType::GammaSurface: {
            minmax(gamma_data_.z, mn, mx);
            auto sl = fmt_strikes(gamma_data_.strikes), dl = fmt_dtes(gamma_data_.expirations);
            surface_3d_->set_surface(gamma_data_.z, "STRIKE", "GAMMA", "DTE", mn, mx, false, &sl, &dl);
            break;
        }
        case ChartType::VegaSurface: {
            minmax(vega_data_.z, mn, mx);
            auto sl = fmt_strikes(vega_data_.strikes), dl = fmt_dtes(vega_data_.expirations);
            surface_3d_->set_surface(vega_data_.z, "STRIKE", "VEGA", "DTE", mn, mx, false, &sl, &dl);
            break;
        }
        case ChartType::ThetaSurface: {
            minmax(theta_data_.z, mn, mx);
            auto sl = fmt_strikes(theta_data_.strikes), dl = fmt_dtes(theta_data_.expirations);
            surface_3d_->set_surface(theta_data_.z, "STRIKE", "THETA", "DTE", mn, mx, true, &sl, &dl);
            break;
        }
        case ChartType::SkewSurface: {
            minmax(skew_data_.z, mn, mx);
            std::vector<std::string> dl, sl;
            for (float d : skew_data_.deltas) {
                char b[16];
                std::snprintf(b, 16, "%dD", (int)d);
                dl.push_back(b);
            }
            for (int e : skew_data_.expirations) {
                char b[16];
                std::snprintf(b, 16, "%dD", e);
                sl.push_back(b);
            }
            surface_3d_->set_surface(skew_data_.z, "DELTA", "SKEW %", "DTE", mn, mx, true, &dl, &sl);
            break;
        }
        case ChartType::LocalVolSurface: {
            minmax(local_vol_data_.z, mn, mx);
            auto sl = fmt_strikes(local_vol_data_.strikes), dl = fmt_dtes(local_vol_data_.expirations);
            surface_3d_->set_surface(local_vol_data_.z, "STRIKE", "LV %", "DTE", mn, mx, false, &sl, &dl);
            break;
        }
        case ChartType::YieldCurve: {
            minmax(yield_data_.z, mn, mx);
            std::vector<std::string> ml;
            for (int m : yield_data_.maturities) {
                char b[16];
                std::snprintf(b, 16, "%dM", m);
                ml.push_back(b);
            }
            surface_3d_->set_surface(yield_data_.z, "MATURITY", "YIELD %", "TIME", mn, mx, false, &ml, nullptr);
            break;
        }
        case ChartType::SwaptionVol: {
            minmax(swaption_data_.z, mn, mx);
            auto tl = fmt_months(swaption_data_.swap_tenors), el = fmt_months(swaption_data_.option_expiries);
            surface_3d_->set_surface(swaption_data_.z, "TENOR", "VOL bp", "EXPIRY", mn, mx, false, &tl, &el);
            break;
        }
        case ChartType::CapFloorVol: {
            minmax(capfloor_data_.z, mn, mx);
            auto tl = fmt_months(capfloor_data_.maturities);
            auto sl = fmt_strikes(capfloor_data_.strikes);
            surface_3d_->set_surface(capfloor_data_.z, "MATURITY", "VOL bp", "STRIKE", mn, mx, false, &tl, &sl);
            break;
        }
        case ChartType::BondSpread: {
            minmax(bond_spread_data_.z, mn, mx);
            auto ml = fmt_months(bond_spread_data_.maturities);
            surface_3d_->set_surface(bond_spread_data_.z, "MATURITY", "SPREAD bp", "RATING", mn, mx, false, &ml,
                                     nullptr);
            break;
        }
        case ChartType::OISBasis: {
            minmax(ois_data_.z, mn, mx);
            auto tl = fmt_months(ois_data_.tenors);
            surface_3d_->set_surface(ois_data_.z, "TENOR", "BASIS bp", "TIME", mn, mx, true, &tl, nullptr);
            break;
        }
        case ChartType::RealYield: {
            minmax(real_yield_data_.z, mn, mx);
            auto ml = fmt_months(real_yield_data_.maturities);
            surface_3d_->set_surface(real_yield_data_.z, "MATURITY", "REAL YLD %", "TIME", mn, mx, true, &ml, nullptr);
            break;
        }
        case ChartType::ForwardRate: {
            minmax(fwd_rate_data_.z, mn, mx);
            auto tl = fmt_months(fwd_rate_data_.start_tenors);
            auto fl = fmt_months(fwd_rate_data_.forward_periods);
            surface_3d_->set_surface(fwd_rate_data_.z, "START TENOR", "FWD RATE %", "FWD PERIOD", mn, mx, false, &tl,
                                     &fl);
            break;
        }
        case ChartType::FXVol: {
            minmax(fx_vol_data_.z, mn, mx);
            std::vector<std::string> dl, tl;
            for (float d : fx_vol_data_.deltas) {
                char b[16];
                std::snprintf(b, 16, "%dD", (int)d);
                dl.push_back(b);
            }
            for (int t : fx_vol_data_.tenors) {
                char b[16];
                std::snprintf(b, 16, "%dD", t);
                tl.push_back(b);
            }
            surface_3d_->set_surface(fx_vol_data_.z, "TENOR", "VOL %", "DELTA", mn, mx, false, &dl, &tl);
            break;
        }
        case ChartType::FXForwardPoints: {
            minmax(fx_fwd_data_.z, mn, mx);
            auto tl = fmt_months(fx_fwd_data_.tenors);
            surface_3d_->set_surface(fx_fwd_data_.z, "TENOR", "FWD PTS", "PAIR", mn, mx, true, &tl, nullptr);
            break;
        }
        case ChartType::CrossCurrencyBasis: {
            minmax(xccy_data_.z, mn, mx);
            auto tl = fmt_months(xccy_data_.tenors);
            surface_3d_->set_surface(xccy_data_.z, "TENOR", "BASIS bp", "PAIR", mn, mx, true, &tl, nullptr);
            break;
        }
        case ChartType::CDSSpread: {
            minmax(cds_data_.z, mn, mx);
            auto tl = fmt_months(cds_data_.tenors);
            surface_3d_->set_surface(cds_data_.z, "TENOR", "SPREAD bp", "ENTITY", mn, mx, false, &tl, nullptr);
            break;
        }
        case ChartType::CreditTransition: {
            minmax(credit_trans_data_.z, mn, mx);
            surface_3d_->set_surface(credit_trans_data_.z, "TO RATING", "PROB %", "FROM RATING", mn, mx, false);
            break;
        }
        case ChartType::RecoveryRate: {
            minmax(recovery_data_.z, mn, mx);
            surface_3d_->set_surface(recovery_data_.z, "SECTOR", "RECOVERY %", "SENIORITY", mn, mx, false);
            break;
        }
        case ChartType::CommodityForward: {
            minmax(cmdty_fwd_data_.z, mn, mx);
            std::vector<std::string> ml;
            for (int m : cmdty_fwd_data_.contract_months) {
                char b[8];
                std::snprintf(b, 8, "M%d", m);
                ml.push_back(b);
            }
            surface_3d_->set_surface(cmdty_fwd_data_.z, "CONTRACT", "PRICE", "COMMODITY", mn, mx, false, &ml, nullptr);
            break;
        }
        case ChartType::CommodityVol: {
            minmax(cmdty_vol_data_.z, mn, mx);
            auto sl = fmt_strikes(cmdty_vol_data_.strikes);
            auto el = fmt_dtes(cmdty_vol_data_.expirations);
            surface_3d_->set_surface(cmdty_vol_data_.z, "STRIKE", "VOL %", "EXPIRY", mn, mx, false, &sl, &el);
            break;
        }
        case ChartType::CrackSpread: {
            minmax(crack_data_.z, mn, mx);
            std::vector<std::string> ml;
            for (int m : crack_data_.contract_months) {
                char b[8];
                std::snprintf(b, 8, "M%d", m);
                ml.push_back(b);
            }
            surface_3d_->set_surface(crack_data_.z, "CONTRACT", "SPREAD $/bbl", "PRODUCT", mn, mx, true, &ml, nullptr);
            break;
        }
        case ChartType::ContangoBackwardation: {
            minmax(contango_data_.z, mn, mx);
            std::vector<std::string> ml;
            for (int m : contango_data_.contract_months) {
                char b[8];
                std::snprintf(b, 8, "M%d", m);
                ml.push_back(b);
            }
            surface_3d_->set_surface(contango_data_.z, "CONTRACT", "ROLL %", "COMMODITY", mn, mx, true, &ml, nullptr);
            break;
        }
        case ChartType::Correlation: {
            int n = (int)corr_data_.assets.size();
            if (corr_data_.z.empty() || (int)corr_data_.z.size() < n)
                break;
            std::vector<std::vector<float>> slice(n, std::vector<float>(n));
            for (int r = 0; r < n; r++)
                for (int c = 0; c < n; c++)
                    slice[r][c] = corr_data_.z[r][c];
            surface_3d_->set_surface(slice, "ASSET", "CORR", "ASSET", -1.f, 1.f, true);
            break;
        }
        case ChartType::PCA: {
            minmax(pca_data_.z, mn, mx);
            surface_3d_->set_surface(pca_data_.z, "ASSET", "LOADING", "PC", mn, mx, true);
            break;
        }
        case ChartType::VaR: {
            minmax(var_data_.z, mn, mx);
            surface_3d_->set_surface(var_data_.z, "HORIZON", "VaR %", "CONFIDENCE", mn, mx, false);
            break;
        }
        case ChartType::StressTestPnL: {
            minmax(stress_data_.z, mn, mx);
            surface_3d_->set_surface(stress_data_.z, "PORTFOLIO", "P&L %", "SCENARIO", mn, mx, true);
            break;
        }
        case ChartType::FactorExposure: {
            minmax(factor_data_.z, mn, mx);
            surface_3d_->set_surface(factor_data_.z, "FACTOR", "EXPOSURE", "ASSET", mn, mx, true);
            break;
        }
        case ChartType::LiquidityHeatmap: {
            minmax(liquidity_data_.z, mn, mx);
            surface_3d_->set_surface(liquidity_data_.z, "TIME", "BID-ASK bp", "STRIKE", mn, mx, false);
            break;
        }
        case ChartType::Drawdown: {
            minmax(drawdown_data_.z, mn, mx);
            std::vector<std::string> wl;
            for (int w : drawdown_data_.windows) {
                char b[8];
                std::snprintf(b, 8, "%dD", w);
                wl.push_back(b);
            }
            surface_3d_->set_surface(drawdown_data_.z, "WINDOW", "DRAWDOWN %", "ASSET", mn, mx, false, &wl, nullptr);
            break;
        }
        case ChartType::BetaSurface: {
            minmax(beta_data_.z, mn, mx);
            std::vector<std::string> hl2;
            for (int h : beta_data_.horizons) {
                char b[8];
                std::snprintf(b, 8, "%dD", h);
                hl2.push_back(b);
            }
            // Name the benchmark on the axis. "BETA" alone is not a number
            // anyone can act on — beta to what?
            const std::string blabel =
                beta_benchmark_.isEmpty()
                    ? std::string("BETA")
                    : "BETA vs " + beta_benchmark_.toUpper().toStdString();
            std::vector<std::string> al(beta_data_.assets);
            surface_3d_->set_surface(beta_data_.z, "HORIZON", blabel, "ASSET", mn, mx, true, &hl2,
                                     al.empty() ? nullptr : &al);
            break;
        }
        case ChartType::ImpliedDividend: {
            minmax(impl_div_data_.z, mn, mx);
            auto el = fmt_dtes(impl_div_data_.expirations);
            surface_3d_->set_surface(impl_div_data_.z, "EXPIRY", "DIV $", "SERIES", mn, mx, false, &el, nullptr);
            break;
        }
        case ChartType::InflationExpectations: {
            minmax(inflation_data_.z, mn, mx);
            std::vector<std::string> hl;
            for (int h : inflation_data_.horizons) {
                char b[8];
                std::snprintf(b, 8, "%dY", h);
                hl.push_back(b);
            }
            surface_3d_->set_surface(inflation_data_.z, "HORIZON", "BREAKEVEN %", "TIME", mn, mx, false, &hl, nullptr);
            break;
        }
        case ChartType::MonetaryPolicyPath: {
            minmax(monetary_data_.z, mn, mx);
            std::vector<std::string> ml;
            for (int m : monetary_data_.meetings_ahead) {
                char b[8];
                std::snprintf(b, 8, "Mtg%d", m);
                ml.push_back(b);
            }
            surface_3d_->set_surface(monetary_data_.z, "MEETING", "RATE %", "CENTRAL BANK", mn, mx, false, &ml,
                                     nullptr);
            break;
        }
        default:
            surface_3d_->clear();
            break;
    }
}

// ── Metrics routing — now hands the active surface's z grid to the control panel
// which computes summary stats locally (count/min/max/mean/median/std/skew/kurt).
void SurfaceAnalyticsScreen::update_metrics() {
    if (!control_panel_)
        return;
    if (const auto* z = active_z_grid())
        control_panel_->update_metrics(*z);
    else
        control_panel_->update_metrics({});
}

const std::vector<std::vector<float>>* SurfaceAnalyticsScreen::active_z_grid() const {
    switch (active_chart_) {
        case ChartType::Volatility: return &vol_data_.z;
        case ChartType::DeltaSurface: return &delta_data_.z;
        case ChartType::GammaSurface: return &gamma_data_.z;
        case ChartType::VegaSurface: return &vega_data_.z;
        case ChartType::ThetaSurface: return &theta_data_.z;
        case ChartType::SkewSurface: return &skew_data_.z;
        case ChartType::LocalVolSurface: return &local_vol_data_.z;
        case ChartType::YieldCurve: return &yield_data_.z;
        case ChartType::SwaptionVol: return &swaption_data_.z;
        case ChartType::CapFloorVol: return &capfloor_data_.z;
        case ChartType::BondSpread: return &bond_spread_data_.z;
        case ChartType::OISBasis: return &ois_data_.z;
        case ChartType::RealYield: return &real_yield_data_.z;
        case ChartType::ForwardRate: return &fwd_rate_data_.z;
        case ChartType::FXVol: return &fx_vol_data_.z;
        case ChartType::FXForwardPoints: return &fx_fwd_data_.z;
        case ChartType::CrossCurrencyBasis: return &xccy_data_.z;
        case ChartType::CDSSpread: return &cds_data_.z;
        case ChartType::CreditTransition: return &credit_trans_data_.z;
        case ChartType::RecoveryRate: return &recovery_data_.z;
        case ChartType::CommodityForward: return &cmdty_fwd_data_.z;
        case ChartType::CommodityVol: return &cmdty_vol_data_.z;
        case ChartType::CrackSpread: return &crack_data_.z;
        case ChartType::ContangoBackwardation: return &contango_data_.z;
        case ChartType::Correlation: return &corr_data_.z;
        case ChartType::PCA: return &pca_data_.z;
        case ChartType::VaR: return &var_data_.z;
        case ChartType::StressTestPnL: return &stress_data_.z;
        case ChartType::FactorExposure: return &factor_data_.z;
        case ChartType::LiquidityHeatmap: return &liquidity_data_.z;
        case ChartType::Drawdown: return &drawdown_data_.z;
        case ChartType::BetaSurface: return &beta_data_.z;
        case ChartType::ImpliedDividend: return &impl_div_data_.z;
        case ChartType::InflationExpectations: return &inflation_data_.z;
        case ChartType::MonetaryPolicyPath: return &monetary_data_.z;
    }
    return nullptr;
}

void SurfaceAnalyticsScreen::update_inspector_lineage() {
    if (!data_inspector_)
        return;
    const auto& cap = capability_for(active_chart_);
    QString date_range;
    if (control_panel_) {
        const auto& s = control_panel_->state();
        if (s.start_date.isValid() && s.end_date.isValid())
            date_range = QString("%1 → %2")
                             .arg(s.start_date.toString("yyyy-MM-dd"))
                             .arg(s.end_date.toString("yyyy-MM-dd"));
    }
    QString sym = current_symbol_or_default();
    if (cap.tier == SurfaceTier::EQUITIES && control_panel_)
        sym = control_panel_->state().basket.join(",");
    qint64 count = 0;
    if (const auto* z = active_z_grid())
        for (const auto& row : *z)
            count += (qint64)row.size();
    // Provenance is about the numbers on screen, not about the chart's tier.
    // Naming cap.dataset for a generated surface asserts that these values
    // came out of OPRA.PILLAR, which is exactly the claim that must never be
    // made without it being true.
    const bool synthetic = active_is_synthetic();
    const auto imported = imported_from_.constFind(active_chart_);
    const bool is_import = imported != imported_from_.constEnd();
    if (control_panel_)
        control_panel_->set_provenance(is_import ? SurfaceProvenance::Imported
                                       : synthetic ? SurfaceProvenance::Synthetic
                                                   : SurfaceProvenance::Fetched);
    if (is_import) {
        // The file IS the source. No dataset, no schema, and no cost query —
        // nothing here went near Databento.
        data_inspector_->set_lineage(imported.value(), QStringLiteral("imported CSV"),
                                     QStringLiteral("—"), sym, QString(), count, 0.0);
        return;
    }
    // Name the window the rates surfaces actually cover.
    if (!rates_dates_.isEmpty() && !synthetic &&
        (active_chart_ == ChartType::YieldCurve || active_chart_ == ChartType::RealYield ||
         active_chart_ == ChartType::InflationExpectations ||
         active_chart_ == ChartType::ForwardRate)) {
        data_inspector_->set_lineage(QStringLiteral("FRED (St. Louis Fed)"),
                                     QStringLiteral("constant-maturity series"),
                                     QStringLiteral("series id"), sym,
                                     QStringLiteral("%1 → %2").arg(rates_dates_.first(),
                                                                   rates_dates_.last()),
                                     count, 0.0);
        return;
    }
    if (synthetic) {
        data_inspector_->set_lineage(QStringLiteral("— generated, not fetched —"),
                                     QStringLiteral("analytic model"),
                                     QStringLiteral("—"),
                                     sym, QString(), count, 0.0);
        return;
    }
    data_inspector_->set_lineage(QString::fromUtf8(cap.dataset),
                                 QString::fromUtf8(cap.schema),
                                 QString::fromUtf8(cap.symbology),
                                 sym, date_range, count, 0.0);

    // Fire-and-forget cost lookup. Skip for DEMO (no dataset) and for
    // capabilities whose schema is a composite ("definition+cbbo-1s") since
    // metadata.get_cost takes a single schema. Use the first listed.
    if (cap.tier == SurfaceTier::DEMO || !control_panel_)
        return;
    auto& svc = DatabentoService::instance();
    if (!svc.has_api_key())
        return;
    const auto& s = control_panel_->state();
    if (!s.start_date.isValid() || !s.end_date.isValid())
        return;
    QString schema = QString::fromUtf8(cap.schema);
    int plus = schema.indexOf('+');
    if (plus >= 0)
        schema = schema.left(plus);
    DbCostQuery q;
    q.dataset = QString::fromUtf8(cap.dataset);
    q.schema = schema;
    q.start = s.start_date;
    q.end = s.end_date;
    q.stype_in = QString::fromUtf8(cap.symbology);
    if (cap.tier == SurfaceTier::EQUITIES)
        q.symbols = s.basket.isEmpty() ? QStringList{s.symbol} : s.basket;
    else if (QString::fromUtf8(cap.symbology) == "parent")
        q.symbols = QStringList{s.symbol + ".OPT"};
    else
        q.symbols = QStringList{s.symbol};

    QPointer<SurfaceAnalyticsScreen> self = this;
    QString ds = q.dataset;
    QString sch = QString::fromUtf8(cap.schema);
    QString symb = QString::fromUtf8(cap.symbology);
    QString sym_text = sym;
    QString dr = date_range;
    qint64 row_ct = count;
    svc.get_cost(q, [self, ds, sch, symb, sym_text, dr, row_ct](DbCostResult r) {
        if (!self || !self->data_inspector_)
            return;
        if (!r.success)
            return;
        self->data_inspector_->set_lineage(ds, sch, symb, sym_text, dr,
                                           row_ct > 0 ? row_ct : r.record_count,
                                           r.cost_usd);
    });
}

void SurfaceAnalyticsScreen::update_line_view() {
    if (!surface_line_)
        return;
    auto fmt_months_str = [](const std::vector<int>& v) {
        QStringList out;
        for (int i : v) out << QString("%1M").arg(i);
        return out;
    };

    switch (active_chart_) {
        case ChartType::YieldCurve: {
            // Newest row: fill_grid() writes oldest-first of the yield matrix as a single curve
            if (yield_data_.z.empty() || yield_data_.maturities.empty())
                break;
            std::vector<float> xs, ys;
            for (size_t i = 0; i < yield_data_.maturities.size(); ++i) {
                xs.push_back((float)yield_data_.maturities[i]);
                // z.back(): fill_grid() writes oldest-first, so [0] is up to
                // sixty business days stale while the 3D and forward views
                // both use today's curve.
                ys.push_back(yield_data_.z.back().size() > i ? yield_data_.z.back()[i] : 0.0f);
            }
            surface_line_->set_curve("YIELD CURVE", xs, ys, fmt_months_str(yield_data_.maturities),
                                     "Maturity (months)", "Yield %", QColor(63, 185, 80));
            return;
        }
        case ChartType::ContangoBackwardation: {
            std::vector<SurfaceLineWidget::Series> series;
            for (size_t i = 0; i < contango_data_.commodities.size() && i < contango_data_.z.size(); ++i) {
                SurfaceLineWidget::Series s;
                s.name = QString::fromStdString(contango_data_.commodities[i]);
                for (size_t k = 0; k < contango_data_.contract_months.size(); ++k)
                    s.x_values.push_back((float)contango_data_.contract_months[k]);
                s.y_values.assign(contango_data_.z[i].begin(), contango_data_.z[i].end());
                static const QColor palette[] = {
                    QColor(217, 119, 6), QColor(88, 166, 255), QColor(63, 185, 80),
                    QColor(220, 80, 80), QColor(155, 114, 255), QColor(89, 196, 217)};
                s.color = palette[i % 6];
                series.push_back(s);
            }
            surface_line_->set_series("CONTANGO / BACKWARDATION", series, "Contract month", "Roll %");
            return;
        }
        case ChartType::CommodityForward: {
            std::vector<SurfaceLineWidget::Series> series;
            for (size_t i = 0; i < cmdty_fwd_data_.commodities.size() && i < cmdty_fwd_data_.z.size(); ++i) {
                SurfaceLineWidget::Series s;
                s.name = QString::fromStdString(cmdty_fwd_data_.commodities[i]);
                for (size_t k = 0; k < cmdty_fwd_data_.contract_months.size(); ++k)
                    s.x_values.push_back((float)cmdty_fwd_data_.contract_months[k]);
                s.y_values.assign(cmdty_fwd_data_.z[i].begin(), cmdty_fwd_data_.z[i].end());
                static const QColor palette[] = {
                    QColor(217, 119, 6), QColor(88, 166, 255), QColor(63, 185, 80),
                    QColor(220, 80, 80), QColor(155, 114, 255), QColor(89, 196, 217)};
                s.color = palette[i % 6];
                series.push_back(s);
            }
            surface_line_->set_series("FUTURES TERM STRUCTURE", series, "Contract month", "Price");
            return;
        }
        case ChartType::CrackSpread: {
            std::vector<SurfaceLineWidget::Series> series;
            for (size_t i = 0; i < crack_data_.spread_types.size() && i < crack_data_.z.size(); ++i) {
                SurfaceLineWidget::Series s;
                s.name = QString::fromStdString(crack_data_.spread_types[i]);
                for (size_t k = 0; k < crack_data_.contract_months.size(); ++k)
                    s.x_values.push_back((float)crack_data_.contract_months[k]);
                s.y_values.assign(crack_data_.z[i].begin(), crack_data_.z[i].end());
                static const QColor palette[] = {QColor(217, 119, 6), QColor(88, 166, 255),
                                                 QColor(63, 185, 80), QColor(220, 80, 80)};
                s.color = palette[i % 4];
                series.push_back(s);
            }
            surface_line_->set_series("CRACK / CRUSH SPREAD", series, "Contract month", "Spread $/bbl");
            return;
        }
        case ChartType::FXForwardPoints: {
            std::vector<SurfaceLineWidget::Series> series;
            for (size_t i = 0; i < fx_fwd_data_.pairs.size() && i < fx_fwd_data_.z.size(); ++i) {
                SurfaceLineWidget::Series s;
                s.name = QString::fromStdString(fx_fwd_data_.pairs[i]);
                for (size_t k = 0; k < fx_fwd_data_.tenors.size(); ++k)
                    s.x_values.push_back((float)fx_fwd_data_.tenors[k]);
                s.y_values.assign(fx_fwd_data_.z[i].begin(), fx_fwd_data_.z[i].end());
                static const QColor palette[] = {QColor(217, 119, 6), QColor(88, 166, 255),
                                                 QColor(63, 185, 80), QColor(220, 80, 80)};
                s.color = palette[i % 4];
                series.push_back(s);
            }
            surface_line_->set_series("FX FORWARD POINTS", series, "Tenor (months)", "Fwd points");
            return;
        }
        case ChartType::InflationExpectations: {
            if (inflation_data_.z.empty() || inflation_data_.horizons.empty())
                break;
            std::vector<float> xs, ys;
            for (size_t i = 0; i < inflation_data_.horizons.size(); ++i) {
                xs.push_back((float)inflation_data_.horizons[i]);
                ys.push_back(inflation_data_.z.back().size() > i ? inflation_data_.z.back()[i]
                                                                  : 0.0f);
            }
            QStringList xl;
            for (int h : inflation_data_.horizons) xl << QString("%1Y").arg(h);
            surface_line_->set_curve("INFLATION EXPECTATIONS", xs, ys, xl,
                                     "Horizon (years)", "Breakeven %",
                                     QColor(89, 196, 217));
            return;
        }
        case ChartType::MonetaryPolicyPath: {
            std::vector<SurfaceLineWidget::Series> series;
            for (size_t i = 0; i < monetary_data_.central_banks.size() && i < monetary_data_.z.size(); ++i) {
                SurfaceLineWidget::Series s;
                s.name = QString::fromStdString(monetary_data_.central_banks[i]);
                for (size_t k = 0; k < monetary_data_.meetings_ahead.size(); ++k)
                    s.x_values.push_back((float)monetary_data_.meetings_ahead[k]);
                s.y_values.assign(monetary_data_.z[i].begin(), monetary_data_.z[i].end());
                static const QColor palette[] = {QColor(217, 119, 6), QColor(88, 166, 255),
                                                 QColor(63, 185, 80), QColor(220, 80, 80)};
                s.color = palette[i % 4];
                series.push_back(s);
            }
            surface_line_->set_series("RATE PATH", series, "Meetings ahead", "Rate %");
            return;
        }
        default:
            break;
    }
    // Fallback — clear if the current chart has no line representation.
    surface_line_->clear();
}

void SurfaceAnalyticsScreen::apply_view_mode_buttons() {
    auto active = [&]() {
        return QString("QPushButton { background:rgba(217,119,6,0.18); color:%1; "
                       "border:1px solid %2; padding:0 10px; font-size:12px; "
                       "font-weight:bold; font-family:%3; }")
            .arg(colors::AMBER())
            .arg(colors::AMBER_DIM())
            .arg(MONO);
    };
    if (btn_3d_) {
        btn_3d_->setChecked(view_mode_ == ViewMode::Surface3D);
        btn_3d_->setStyleSheet(view_mode_ == ViewMode::Surface3D ? active() : btn_inactive());
    }
    if (btn_table_) {
        btn_table_->setChecked(view_mode_ == ViewMode::Table);
        btn_table_->setStyleSheet(view_mode_ == ViewMode::Table ? active() : btn_inactive());
    }
    if (btn_line_) {
        btn_line_->setChecked(view_mode_ == ViewMode::Line);
        btn_line_->setStyleSheet(view_mode_ == ViewMode::Line ? active() : btn_inactive());
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────
void SurfaceAnalyticsScreen::on_category_clicked(int index) {
    active_category_ = index;
    const auto cats = get_surface_categories();
    if (!cats.empty() && index < (int)cats.size())
        active_chart_ = cats[index].types[0];

    // Rebuild both bars
    auto* main_layout = qobject_cast<QVBoxLayout*>(layout());
    if (main_layout) {
        int ci = main_layout->indexOf(category_bar_);
        int si = main_layout->indexOf(surface_bar_);
        if (ci >= 0) {
            main_layout->removeWidget(category_bar_);
            category_bar_->deleteLater();
            category_bar_ = build_category_bar();
            main_layout->insertWidget(ci, category_bar_);
        }
        if (si >= 0) {
            main_layout->removeWidget(surface_bar_);
            surface_bar_->deleteLater();
            surface_bar_ = build_surface_bar();
            main_layout->insertWidget(si, surface_bar_);
        }
    }

    if (control_panel_)
        control_panel_->set_capability(active_chart_);
    load_dataset_range_for_active_capability();
    update_chart();
    update_metrics();
    update_inspector_lineage();
    fincept::ScreenStateManager::instance().notify_changed(this);
}

void SurfaceAnalyticsScreen::on_surface_clicked(int cat, int surf_index) {
    const auto cats = get_surface_categories();
    if (cat < (int)cats.size() && surf_index < (int)cats[cat].types.size())
        active_chart_ = cats[cat].types[surf_index];
    refresh_surface_bar();
    if (control_panel_)
        control_panel_->set_capability(active_chart_);
    load_dataset_range_for_active_capability();
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_view_3d() {
    view_mode_ = ViewMode::Surface3D;
    apply_view_mode_buttons();
    update_chart();
}

void SurfaceAnalyticsScreen::on_view_table() {
    view_mode_ = ViewMode::Table;
    apply_view_mode_buttons();
    update_chart();
}

void SurfaceAnalyticsScreen::on_view_line() {
    view_mode_ = ViewMode::Line;
    apply_view_mode_buttons();
    update_chart();
}

void SurfaceAnalyticsScreen::on_import_csv() {
    QString path = QFileDialog::getOpenFileName(this, "Import Surface CSV", {}, "CSV Files (*.csv)");
    if (!path.isEmpty())
        dispatch_csv(path);
}

void SurfaceAnalyticsScreen::on_refresh() {
    load_demo_data();
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_controls_changed() {
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_control_symbol_changed(const QString& /*sym*/) {
    // Rebuild demo surfaces with the new underlying so the chart isn't stale.
    load_demo_data();
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_fetch_requested() {
    if (!control_panel_)
        return;
    const auto& cap = capability_for(active_chart_);
    if (cap.tier == SurfaceTier::DEMO)
        return; // Button should be disabled, but guard anyway

    auto& svc = DatabentoService::instance();
    if (!svc.has_api_key()) {
        if (data_inspector_) {
            data_inspector_->set_status("No Databento API key configured", false);
            data_inspector_->set_error("Add a key in Settings → Credentials → Databento.");
        }
        return;
    }

    DatabentoFetchParams p;
    static const char* CT_NAMES[] = {
        "Volatility", "DeltaSurface", "GammaSurface", "VegaSurface", "ThetaSurface",
        "SkewSurface", "LocalVolSurface", "YieldCurve", "SwaptionVol", "CapFloorVol",
        "BondSpread", "OISBasis", "RealYield", "ForwardRate", "FXVol",
        "FXForwardPoints", "CrossCurrencyBasis", "CDSSpread", "CreditTransition",
        "RecoveryRate", "CommodityForward", "CommodityVol", "CrackSpread",
        "ContangoBackwardation", "Correlation", "PCA", "VaR", "StressTestPnL",
        "FactorExposure", "LiquidityHeatmap", "Drawdown", "BetaSurface",
        "ImpliedDividend", "InflationExpectations", "MonetaryPolicyPath",
    };
    int idx = (int)active_chart_;
    if (idx >= 0 && idx < (int)(sizeof(CT_NAMES) / sizeof(CT_NAMES[0])))
        p.chart_type = QString::fromUtf8(CT_NAMES[idx]);

    const auto& s = control_panel_->state();
    p.symbol = s.symbol;
    p.basket = s.basket;
    p.dataset = s.dataset.isEmpty() ? QString::fromUtf8(cap.dataset) : s.dataset;
    p.start_date = s.start_date;
    p.end_date = s.end_date;
    p.strike_window_pct = s.strike_window_pct;
    p.dte_min = s.dte_min;
    p.dte_max = s.dte_max;
    p.iv_method = s.iv_method;
    p.spot_override = spot_for(p.symbol);

    // These fetches PRICE against spot — the provider needs a real one, and
    // shipping the unknown 0 through would return quotes with no IV, leave
    // every grid empty, and report "loaded" over the unchanged synthetic
    // chart. Refusing with a reason is the honest failure; substituting a
    // round number was the dishonest one this stopped doing.
    switch (active_chart_) {
        case ChartType::Volatility:
        case ChartType::DeltaSurface:
        case ChartType::GammaSurface:
        case ChartType::VegaSurface:
        case ChartType::ThetaSurface:
        case ChartType::SkewSurface:
        case ChartType::LocalVolSurface:
        case ChartType::ImpliedDividend:
        case ChartType::LiquidityHeatmap:
            if (p.spot_override <= 0.0f) {
                if (data_inspector_) {
                    data_inspector_->set_status(
                        QStringLiteral("No live quote for %1").arg(p.symbol), false);
                    data_inspector_->set_error(
                        QStringLiteral("This surface is priced against spot, and no quote for %1 "
                                       "has been published yet. Open the symbol on a screen that "
                                       "subscribes to its quote, then fetch again.").arg(p.symbol));
                }
                return;
            }
            break;
        default:
            break;
    }

    if (data_inspector_)
        data_inspector_->set_status(
            QString("Fetching %1 …").arg(QString::fromUtf8(chart_type_name(active_chart_))), true);
    svc.fetch_with_params(p);
}

void SurfaceAnalyticsScreen::dispatch_csv(const QString& path) {
    std::string err;
    auto rows = parse_csv_file(path, err);
    if (rows.empty())
        return;
    // The user's own file is real data. Without marking it, the provenance
    // rules would badge it SYNTHETIC DATA and report its lineage as
    // "generated, not fetched" — the same false claim as before, pointed the
    // other way.
    const auto mark_imported = [this, &path]() {
        imported_from_.insert(active_chart_, QFileInfo(path).fileName());
        fetched_.remove(active_chart_);   // a file replaces whatever was fetched
        update_inspector_lineage();
    };
    switch (active_chart_) {
        case ChartType::Volatility:
            if (load_vol_surface(rows, vol_data_, err)) {
                update_chart();
                update_metrics();
                mark_imported();
            }
            break;
        case ChartType::DeltaSurface:
            if (load_greeks_surface(rows, delta_data_, err, "Delta")) {
                update_chart();
                update_metrics();
                mark_imported();
            }
            break;
        case ChartType::GammaSurface:
            if (load_greeks_surface(rows, gamma_data_, err, "Gamma")) {
                update_chart();
                update_metrics();
                mark_imported();
            }
            break;
        case ChartType::VegaSurface:
            if (load_greeks_surface(rows, vega_data_, err, "Vega")) {
                update_chart();
                update_metrics();
                mark_imported();
            }
            break;
        case ChartType::ThetaSurface:
            if (load_greeks_surface(rows, theta_data_, err, "Theta")) {
                update_chart();
                update_metrics();
                mark_imported();
            }
            break;
        default:
            break;
    }
}

// ── Databento slots ───────────────────────────────────────────────────────────
void SurfaceAnalyticsScreen::on_vol_surface_received(const fincept::DatabentoVolSurfaceResult& r) {
    if (data_inspector_)
        data_inspector_->set_status(r.success ? "Vol surface loaded" : "Vol fetch failed", r.success);
    if (!r.success) {
        if (data_inspector_)
            data_inspector_->set_error(r.error);
        update_chart();
        return;
    }

    QString sym = current_symbol_or_default();
    std::string sym_std = sym.toStdString();
    float spot = spot_for(sym);

    if (!r.vol.z.empty()) {
        vol_data_ = r.vol;
        vol_data_.underlying = sym_std;
        vol_data_.spot_price = spot;
        fetched_.insert(ChartType::Volatility);
    }
    if (!r.delta.z.empty()) {
        delta_data_ = r.delta;
        delta_data_.underlying = sym_std;
        delta_data_.spot_price = spot;
        fetched_.insert(ChartType::DeltaSurface);
    }
    if (!r.gamma.z.empty()) {
        gamma_data_ = r.gamma;
        gamma_data_.underlying = sym_std;
        gamma_data_.spot_price = spot;
        fetched_.insert(ChartType::GammaSurface);
    }
    if (!r.vega.z.empty()) {
        vega_data_ = r.vega;
        vega_data_.underlying = sym_std;
        vega_data_.spot_price = spot;
        fetched_.insert(ChartType::VegaSurface);
    }
    if (!r.theta.z.empty()) {
        theta_data_ = r.theta;
        theta_data_.underlying = sym_std;
        theta_data_.spot_price = spot;
        fetched_.insert(ChartType::ThetaSurface);
    }
    if (!r.skew.z.empty()) {
        skew_data_ = r.skew;
        skew_data_.underlying = sym_std;
        fetched_.insert(ChartType::SkewSurface);
    }

    if (data_inspector_) {
        QStringList headers = {"strike", "expiration", "iv"};
        QVector<QStringList> rows;
        for (size_t i = 0; i < vol_data_.strikes.size(); ++i) {
            for (size_t j = 0; j < vol_data_.expirations.size(); ++j) {
                if (i < vol_data_.z.size() && j < vol_data_.z[i].size()) {
                    rows.push_back({QString::number(vol_data_.strikes[i], 'f', 2),
                                    QString::number(vol_data_.expirations[j]),
                                    QString::number(vol_data_.z[i][j], 'f', 4)});
                }
            }
        }
        data_inspector_->show_table("vol_surface", headers, rows);
    }

    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_ohlcv_received(const fincept::DatabentoOhlcvResult& r) {
    if (data_inspector_) {
        data_inspector_->set_status(r.success ? "OHLCV loaded" : "OHLCV fetch failed", r.success);
        if (!r.success)
            data_inspector_->set_error(r.error);
        else {
            QStringList headers = {"symbol", "date", "open", "high", "low", "close", "volume"};
            QVector<QStringList> rows;
            for (auto it = r.data.constBegin(); it != r.data.constEnd(); ++it) {
                for (const QJsonObject& bar : it.value()) {
                    rows.push_back({it.key(),
                                    bar.value("date").toString(),
                                    QString::number(bar.value("open").toDouble(), 'f', 2),
                                    QString::number(bar.value("high").toDouble(), 'f', 2),
                                    QString::number(bar.value("low").toDouble(), 'f', 2),
                                    QString::number(bar.value("close").toDouble(), 'f', 2),
                                    QString::number(bar.value("volume").toDouble(), 'f', 0)});
                }
            }
            data_inspector_->show_table("ohlcv-1d", headers, rows);
            compute_equity_surfaces(r);
        }
    }
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

// ── Rates and inflation, from FRED ──────────────────────────────────────────
//
// The Treasury curve, real yields and breakevens are published daily by the
// St. Louis Fed and fred_data.py already knows how to fetch them. They were
// being drawn by generate_yield_curve() and friends instead — smooth invented
// shapes on a screen a reader could reasonably price against.
//
// Each surface is maturity x date: the last N observations of each maturity's
// series, so the picture is the curve moving through time rather than one
// snapshot. A maturity with no observation on a date is a hole (NaN), never
// interpolated — an interpolated yield is a number nobody published.
namespace {

struct FredSeriesSpec {
    const char* series;   ///< FRED series id
    int         months;   ///< maturity/horizon this series represents
};

// Constant-maturity nominal Treasury yields.
const std::vector<FredSeriesSpec> kCurveSeries = {
    {"DGS1MO", 1},  {"DGS3MO", 3},  {"DGS6MO", 6},   {"DGS1", 12},  {"DGS2", 24},
    {"DGS3", 36},   {"DGS5", 60},   {"DGS7", 84},    {"DGS10", 120},
    {"DGS20", 240}, {"DGS30", 360},
};
// TIPS constant-maturity real yields.
const std::vector<FredSeriesSpec> kRealSeries = {
    {"DFII5", 60}, {"DFII7", 84}, {"DFII10", 120}, {"DFII20", 240}, {"DFII30", 360},
};
// Breakeven inflation. Daily series only, so the axis is a true horizon.
// NOTE the unit: every consumer of InflationExpData::horizons formats it as
// "%dY", so these are YEARS. The maturity vectors above are months, which is
// what their consumers format — the two axes genuinely differ, and storing 60
// here drew the 5-year breakeven labelled "60Y".
const std::vector<FredSeriesSpec> kBreakevenSeries = {
    {"T5YIE", 5}, {"T10YIE", 10},
};

QStringList series_ids(const std::vector<FredSeriesSpec>& specs) {
    QStringList out;
    for (const auto& s : specs) out << QString::fromUtf8(s.series);
    return out;
}

/// The first per-series error in a `multiple` payload, or empty when the
/// response carries data. See on_fred_result() for why this is needed.
QString first_series_error(const QJsonObject& payload) {
    const QJsonArray arr = payload.value(QStringLiteral("results")).isArray()
                               ? payload.value(QStringLiteral("results")).toArray()
                               : payload.value(QStringLiteral("data")).toArray();
    if (arr.isEmpty())
        return {};
    QString first;
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("observations")).toArray().isEmpty())
            return {};   // at least one series came back — not a blanket failure
        const QString e = o.value(QStringLiteral("error")).toString();
        if (first.isEmpty() && !e.isEmpty())
            first = e;
    }
    return first;
}

/// date -> series_id -> value, from fred_data.py's `multiple` payload.
QMap<QString, QHash<QString, double>> by_date(const QJsonObject& payload) {
    QMap<QString, QHash<QString, double>> out;
    const QJsonArray arr = payload.value(QStringLiteral("results")).isArray()
                               ? payload.value(QStringLiteral("results")).toArray()
                               : payload.value(QStringLiteral("data")).toArray();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("series_id")).toString();
        if (id.isEmpty())
            continue;
        for (const auto& ov : o.value(QStringLiteral("observations")).toArray()) {
            const QJsonObject obs = ov.toObject();
            const QString date = obs.value(QStringLiteral("date")).toString();
            const QJsonValue vv = obs.value(QStringLiteral("value"));
            // get_series() converts to float and drops FRED's "." placeholder,
            // so the value arrives as a NUMBER. Accept a string too rather
            // than depend on that: reading a number with toString() yields an
            // empty string and would silently empty every one of these
            // surfaces.
            bool ok = false;
            double val = 0.0;
            if (vv.isDouble()) {
                val = vv.toDouble();
                ok = true;
            } else if (vv.isString()) {
                val = vv.toString().toDouble(&ok);   // "." parses as not-ok
            }
            // A day a series has no observation is a hole, and it stays one.
            if (ok && !date.isEmpty())
                out[date][id] = val;
        }
    }
    return out;
}

/// Fill a maturity x date grid, newest `max_dates` dates, oldest first.
/// Returns false when nothing usable came back, so the caller can leave the
/// surface empty rather than draw a grid of holes.
bool fill_grid(const QJsonObject& payload, const std::vector<FredSeriesSpec>& specs,
               int max_dates, std::vector<int>& maturities,
               std::vector<int>& time_points, std::vector<std::vector<float>>& z,
               QStringList* out_dates = nullptr) {
    const auto rows = by_date(payload);
    if (rows.isEmpty())
        return false;
    QStringList dates = rows.keys();          // QMap keys are sorted: oldest first
    if (dates.size() > max_dates)
        dates = dates.mid(dates.size() - max_dates);

    maturities.clear();
    for (const auto& sp : specs) maturities.push_back(sp.months);
    time_points.clear();
    z.clear();
    int filled = 0;
    for (int d = 0; d < dates.size(); ++d) {
        const auto& row = rows[dates[d]];
        std::vector<float> line;
        for (const auto& sp : specs) {
            const auto it = row.constFind(QString::fromUtf8(sp.series));
            if (it == row.constEnd()) {
                line.push_back(std::numeric_limits<float>::quiet_NaN());
            } else {
                line.push_back(float(it.value()));
                ++filled;
            }
        }
        time_points.push_back(d);
        z.push_back(std::move(line));
    }
    // Keep the real dates. The row index is what the axis needs, but a table
    // labelled "D0…D59" over published FRED observations hides WHEN each row
    // is — and that is half of what a curve-through-time is for.
    if (out_dates)
        *out_dates = dates;
    return filled > 0;
}

} // namespace

void SurfaceAnalyticsScreen::load_rates_from_fred() {
    auto& econ = fincept::services::EconomicsService::instance();
    const QString start = QDate::currentDate().addDays(-180).toString(Qt::ISODate);
    const QString end   = QDate::currentDate().toString(Qt::ISODate);

    const struct { const char* rid; const std::vector<FredSeriesSpec>* specs; } jobs[] = {
        {"surface_curve",     &kCurveSeries},
        {"surface_real",      &kRealSeries},
        {"surface_breakeven", &kBreakevenSeries},
    };
    for (const auto& j : jobs) {
        QStringList args = series_ids(*j.specs);
        args << start << end;
        econ.execute(QStringLiteral("surface_analytics"), QStringLiteral("fred_data.py"),
                     QStringLiteral("multiple"), args, QString::fromUtf8(j.rid));
    }
}

void SurfaceAnalyticsScreen::on_fred_result(const QString& request_id,
                                            const fincept::services::EconomicsResult& r) {
    if (!r.success) {
        // FRED needs a key. Say which surfaces are empty because of it rather
        // than leaving three panels blank with no reason.
        if (request_id.startsWith(QStringLiteral("surface_")) && data_inspector_)
            data_inspector_->set_error(
                QStringLiteral("FRED fetch failed (%1). Set FRED_API_KEY to fill the curve, "
                               "real-yield and breakeven surfaces.").arg(r.error.left(120)));
        return;
    }
    // fred_data.py's `multiple` returns an ARRAY, which EconomicsService wraps
    // as {"data": [...]} — and its failure test only fires when the root has
    // an `error` and no `data`. So a missing API key arrives here as SUCCESS
    // with every element carrying its own error, and the branch above never
    // runs: three panels stay blank with nothing said. Look inside.
    if (const QString per_series = first_series_error(r.data); !per_series.isEmpty()) {
        if (data_inspector_)
            data_inspector_->set_error(
                QStringLiteral("FRED returned no data: %1").arg(per_series.left(160)));
        return;
    }
    if (request_id == QStringLiteral("surface_curve")) {
        if (fill_grid(r.data, kCurveSeries, 60, yield_data_.maturities,
                      yield_data_.time_points, yield_data_.z, &rates_dates_)) {
            fetched_.insert(ChartType::YieldCurve);
            build_forward_rates();
        }
    } else if (request_id == QStringLiteral("surface_real")) {
        if (fill_grid(r.data, kRealSeries, 60, real_yield_data_.maturities,
                      real_yield_data_.time_points, real_yield_data_.z))
            fetched_.insert(ChartType::RealYield);
    } else if (request_id == QStringLiteral("surface_breakeven")) {
        if (fill_grid(r.data, kBreakevenSeries, 60, inflation_data_.horizons,
                      inflation_data_.time_points, inflation_data_.z))
            fetched_.insert(ChartType::InflationExpectations);
    } else {
        return;
    }
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

// Implied forward rates from the most recent published curve.
//
// f(a,b) is the rate the curve implies for the period from a to b, under
// continuous compounding: (y_b*b - y_a*a) / (b - a). It is a DERIVATION of
// numbers FRED published, not a quote — which is why it is only built when a
// real curve is present, and never on its own.
void SurfaceAnalyticsScreen::build_forward_rates() {
    if (yield_data_.z.empty() || yield_data_.maturities.size() < 2)
        return;
    const std::vector<float>& latest = yield_data_.z.back();
    const auto& mats = yield_data_.maturities;

    fwd_rate_data_.start_tenors.clear();
    fwd_rate_data_.forward_periods.clear();
    fwd_rate_data_.z.clear();
    for (int m : mats) fwd_rate_data_.start_tenors.push_back(m);
    const std::vector<int> periods = {12, 24, 60, 120};
    fwd_rate_data_.forward_periods = periods;

    auto yield_at = [&](int months) -> double {
        for (size_t i = 0; i < mats.size(); ++i)
            if (mats[i] == months)
                return double(latest[i]);
        return std::numeric_limits<double>::quiet_NaN();
    };

    bool any = false;
    for (size_t i = 0; i < mats.size(); ++i) {
        const double a = double(mats[i]) / 12.0;
        const double ya = double(latest[i]);
        std::vector<float> row;
        for (int p : periods) {
            const double b = a + double(p) / 12.0;
            const double yb = yield_at(mats[i] + p);
            // Only where BOTH legs are published maturities. Interpolating the
            // far leg would invent the very number the chart is reporting.
            if (std::isnan(ya) || std::isnan(yb) || b <= a) {
                row.push_back(std::numeric_limits<float>::quiet_NaN());
                continue;
            }
            row.push_back(float((yb * b - ya * a) / (b - a)));
            any = true;
        }
        fwd_rate_data_.z.push_back(std::move(row));
    }
    if (any) {
        fetched_.insert(ChartType::ForwardRate);
    } else {
        // load_rates_from_fred() runs on every showEvent, so a later curve
        // that yields no exact-maturity pairs must drop the mark too —
        // otherwise the badge claims FRED provenance for a blank surface.
        fwd_rate_data_ = {};
        fetched_.remove(ChartType::ForwardRate);
    }
}

// Build the risk surfaces from the bars themselves.
//
// These five were drawn by SurfaceDemoData's generators while the bars needed
// to compute them were being fetched and dropped into the inspector table. The
// data was there; nothing joined it to the chart.
void SurfaceAnalyticsScreen::compute_equity_surfaces(const fincept::DatabentoOhlcvResult& r) {
    using namespace fincept::surface;
    PriceTable prices;
    for (auto it = r.data.constBegin(); it != r.data.constEnd(); ++it) {
        // Bars arrive newest-last from the provider, but sort on the date the
        // bar carries rather than trusting arrival order — every statistic
        // below is order-dependent and a silent reversal inverts all of them.
        QVector<QJsonObject> bars = it.value();
        std::sort(bars.begin(), bars.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("date").toString() < b.value("date").toString();
        });
        PriceSeries closes;
        closes.reserve(size_t(bars.size()));
        for (const QJsonObject& b : bars) {
            const double c = b.value("close").toDouble();
            if (c > 0.0)
                closes.push_back(c);
        }
        if (closes.size() >= 2)
            prices[it.key().toStdString()] = std::move(closes);
    }
    if (prices.size() < 2)
        return;   // a correlation of one asset with itself is not a surface

    // ── Correlation ─────────────────────────────────────────────────────
    const CorrelationResult corr = compute_correlation(prices);
    if (!corr.z.empty()) {
        corr_data_.assets = corr.assets;
        corr_data_.z = corr.z;
        corr_data_.window = corr.observations;
        fetched_.insert(ChartType::Correlation);
    }

    // ── PCA ─────────────────────────────────────────────────────────────
    const PcaResult pca = compute_pca(prices);
    if (!pca.z.empty()) {
        pca_data_.factors = pca.factors;
        pca_data_.assets = pca.assets;
        pca_data_.z = pca.z;
        pca_data_.variance_explained = pca.variance_explained;
        fetched_.insert(ChartType::PCA);
    }

    // ── Drawdown: asset x lookback window ───────────────────────────────
    // NaN where a window is longer than the history fetched, so a 90-day
    // drawdown is never a 40-day drawdown wearing the wrong label.
    {
        const std::vector<int> windows = {20, 60, 120, 250};
        drawdown_data_.assets.clear();
        drawdown_data_.windows = windows;
        drawdown_data_.z.clear();
        bool any = false;
        for (const auto& [sym, closes] : prices) {
            drawdown_data_.assets.push_back(sym);
            std::vector<float> row;
            for (int w : windows) {
                const double d = max_drawdown_pct(closes, w);
                row.push_back(float(d));
                any = any || !std::isnan(d);
            }
            drawdown_data_.z.push_back(std::move(row));
        }
        if (any)
            fetched_.insert(ChartType::Drawdown);
        else
            drawdown_data_ = {};
    }

    // ── Beta: asset x lookback ──────────────────────────────────────────
    // Against a broad-market ETF when the basket has one, otherwise against
    // the equal-weighted basket. Which one is stated in the axis label — a
    // beta means nothing without saying beta to WHAT.
    {
        static const std::vector<std::string> kBroad = {"SPY", "IVV", "VOO", "VTI", "QQQ"};
        std::string bench;
        for (const auto& cand : kBroad)
            if (prices.count(cand)) { bench = cand; break; }

        std::vector<double> bench_rets;
        if (!bench.empty()) {
            bench_rets = daily_returns(prices.at(bench));
        } else {
            // Equal-weighted basket: average the per-day returns across names,
            // over the window they all share.
            size_t common = std::numeric_limits<size_t>::max();
            std::vector<std::vector<double>> all;
            for (const auto& [sym, closes] : prices) {
                all.push_back(daily_returns(closes));
                common = std::min(common, all.back().size());
            }
            if (common != std::numeric_limits<size_t>::max() && common > 0) {
                bench_rets.assign(common, 0.0);
                for (auto& rr : all) {
                    const size_t off = rr.size() - common;
                    for (size_t i = 0; i < common; ++i)
                        bench_rets[i] += rr[off + i] / double(all.size());
                }
            }
        }

        if (bench_rets.size() >= 20) {
            const std::vector<int> horizons = {20, 60, 120, 250};
            beta_data_.assets.clear();
            beta_data_.horizons = horizons;
            beta_data_.z.clear();
            for (const auto& [sym, closes] : prices) {
                beta_data_.assets.push_back(sym);
                const std::vector<double> ar = daily_returns(closes);
                std::vector<float> row;
                for (int h : horizons) {
                    if (int(ar.size()) < h || int(bench_rets.size()) < h) {
                        row.push_back(std::numeric_limits<float>::quiet_NaN());
                        continue;
                    }
                    const std::vector<double> a(ar.end() - h, ar.end());
                    const std::vector<double> b(bench_rets.end() - h, bench_rets.end());
                    row.push_back(float(beta(a, b)));
                }
                beta_data_.z.push_back(std::move(row));
            }
            beta_benchmark_ = QString::fromStdString(
                bench.empty() ? std::string("equal-weighted basket") : bench);
            fetched_.insert(ChartType::BetaSurface);
        }
    }

    // ── VaR: confidence x horizon, on the equal-weighted basket ─────────
    {
        size_t common = std::numeric_limits<size_t>::max();
        std::vector<std::vector<double>> all;
        for (const auto& [sym, closes] : prices) {
            all.push_back(daily_returns(closes));
            common = std::min(common, all.back().size());
        }
        if (common != std::numeric_limits<size_t>::max() && common >= 30) {
            std::vector<double> port(common, 0.0);
            for (auto& rr : all) {
                const size_t off = rr.size() - common;
                for (size_t i = 0; i < common; ++i)
                    port[i] += rr[off + i] / double(all.size());
            }
            const std::vector<float> confs = {0.90f, 0.95f, 0.99f};
            const std::vector<int> horizons = {1, 5, 10, 20};
            var_data_.confidence_levels = confs;
            var_data_.horizons = horizons;
            var_data_.z.clear();
            for (float c : confs) {
                std::vector<float> row;
                for (int h : horizons)
                    row.push_back(float(historical_var_pct(port, double(c), h)));
                var_data_.z.push_back(std::move(row));
            }
            fetched_.insert(ChartType::VaR);
        }
    }
}

void SurfaceAnalyticsScreen::on_futures_received(const fincept::DatabentoFuturesResult& r) {
    if (data_inspector_) {
        data_inspector_->set_status(r.success ? "Futures curve loaded" : "Futures fetch failed", r.success);
        if (!r.success)
            data_inspector_->set_error(r.error);
    }
    if (r.success) {
        if (!r.forward.z.empty()) {
            cmdty_fwd_data_ = r.forward;
            fetched_.insert(ChartType::CommodityForward);
        }
        if (!r.contango.z.empty()) {
            contango_data_ = r.contango;
            fetched_.insert(ChartType::ContangoBackwardation);
        }
    }
    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_surface_received(const fincept::DatabentoSurfaceResult& r) {
    if (data_inspector_) {
        data_inspector_->set_status(r.success ? "Surface loaded" : "Surface fetch failed", r.success);
        if (!r.success)
            data_inspector_->set_error(r.error);
    }
    if (!r.success) {
        update_chart();
        return;
    }
    const auto& type = r.type;
    QString sym = current_symbol_or_default();
    std::string sym_std = sym.toStdString();
    float spot = spot_for(sym);

    if (type == "local_vol" && !r.z.empty()) {
        local_vol_data_.strikes.assign(r.x_axis.begin(), r.x_axis.end());
        local_vol_data_.expirations.assign(r.y_axis.begin(), r.y_axis.end());
        local_vol_data_.z = r.z;
        local_vol_data_.underlying = sym_std;
        local_vol_data_.spot_price = spot;
        fetched_.insert(ChartType::LocalVolSurface);
    } else if (type == "implied_dividend" && !r.z.empty()) {
        impl_div_data_.strikes.assign(r.x_axis.begin(), r.x_axis.end());
        impl_div_data_.expirations.assign(r.y_axis.begin(), r.y_axis.end());
        impl_div_data_.z = r.z;
        impl_div_data_.underlying = sym_std;
        fetched_.insert(ChartType::ImpliedDividend);
    } else if (type == "liquidity" && !r.z.empty()) {
        liquidity_data_.strikes.assign(r.x_axis.begin(), r.x_axis.end());
        liquidity_data_.expirations.assign(r.y_axis.begin(), r.y_axis.end());
        liquidity_data_.z = r.z;
        liquidity_data_.underlying = sym_std;
        fetched_.insert(ChartType::LiquidityHeatmap);
    } else if (type == "commodity_vol" && !r.z.empty()) {
        cmdty_vol_data_.strikes.assign(r.x_axis.begin(), r.x_axis.end());
        cmdty_vol_data_.expirations.assign(r.y_axis.begin(), r.y_axis.end());
        cmdty_vol_data_.z = r.z;
        cmdty_vol_data_.commodity = sym_std;
        fetched_.insert(ChartType::CommodityVol);
    } else if (type == "crack_spread" && !r.z.empty()) {
        crack_data_.spread_types = r.x_labels;
        crack_data_.contract_months.assign(r.y_axis.begin(), r.y_axis.end());
        crack_data_.z = r.z;
        fetched_.insert(ChartType::CrackSpread);
    } else if (type == "stress_test" && !r.z.empty()) {
        stress_data_.scenarios = r.x_labels;
        stress_data_.portfolios = r.y_labels;
        stress_data_.z = r.z;
        fetched_.insert(ChartType::StressTestPnL);
    }

    update_chart();
    update_metrics();
    update_inspector_lineage();
}

void SurfaceAnalyticsScreen::on_db_fetch_started(const QString& desc) {
    if (data_inspector_)
        data_inspector_->set_status(desc, true);
}

void SurfaceAnalyticsScreen::on_db_fetch_failed(const QString& err) {
    if (data_inspector_) {
        data_inspector_->set_status("Fetch failed", false);
        data_inspector_->set_error(err);
    }
}

void SurfaceAnalyticsScreen::on_db_connection_tested(bool ok, const QString& msg) {
    if (!control_panel_)
        return;
    control_panel_->set_provider_status("databento",
                                        ok ? "connected" : "error",
                                        ok ? QString() : msg);
}

void SurfaceAnalyticsScreen::on_db_raw_response(const QString& cmd, const QString& raw_stdout) {
    if (!data_inspector_)
        return;
    QString header = QString("=== %1 ===\n").arg(cmd);
    data_inspector_->set_raw_output(header + raw_stdout);
}

void SurfaceAnalyticsScreen::refresh_provider_status() {
    if (!control_panel_)
        return;
    auto& svc = DatabentoService::instance();
    control_panel_->set_provider_status(
        "databento",
        svc.has_api_key() ? "configured" : "not configured",
        svc.has_api_key() ? "key set" : "Settings → Credentials");
}

void SurfaceAnalyticsScreen::load_dataset_range_for_active_capability() {
    if (!control_panel_)
        return;
    const auto& cap = capability_for(active_chart_);
    QString ds = QString::fromUtf8(cap.dataset);
    if (ds.isEmpty())
        return; // DEMO surface — no Databento dataset to query
    auto& svc = DatabentoService::instance();
    if (!svc.has_api_key())
        return;
    QPointer<SurfaceAnalyticsScreen> self = this;
    svc.get_dataset_range(ds, [self](DbDatasetRange r) {
        if (!self || !self->control_panel_)
            return;
        self->control_panel_->apply_dataset_range(r.start, r.end);
    });
}

// ── Show/hide event — P3 compliance ──────────────────────────────────────────
void SurfaceAnalyticsScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    auto& svc = DatabentoService::instance();
    connect(&svc, &DatabentoService::vol_surface_ready, this,
            &SurfaceAnalyticsScreen::on_vol_surface_received, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::ohlcv_ready, this,
            &SurfaceAnalyticsScreen::on_ohlcv_received, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::futures_ready, this,
            &SurfaceAnalyticsScreen::on_futures_received, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::surface_ready, this,
            &SurfaceAnalyticsScreen::on_surface_received, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::fetch_started, this,
            &SurfaceAnalyticsScreen::on_db_fetch_started, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::fetch_failed, this,
            &SurfaceAnalyticsScreen::on_db_fetch_failed, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::connection_tested, this,
            &SurfaceAnalyticsScreen::on_db_connection_tested, Qt::UniqueConnection);
    connect(&svc, &DatabentoService::raw_response, this,
            &SurfaceAnalyticsScreen::on_db_raw_response, Qt::UniqueConnection);
    connect(&fincept::services::EconomicsService::instance(),
            &fincept::services::EconomicsService::result_ready, this,
            &SurfaceAnalyticsScreen::on_fred_result, Qt::UniqueConnection);
    load_rates_from_fred();
    refresh_provider_status();
    load_dataset_range_for_active_capability();
}

void SurfaceAnalyticsScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    auto& svc = DatabentoService::instance();
    disconnect(&svc, nullptr, this, nullptr);
}

// ── IStatefulScreen ───────────────────────────────────────────────────────────

QVariantMap SurfaceAnalyticsScreen::save_state() const {
    QVariantMap s{
        {"category", active_category_},
        {"chart", static_cast<int>(active_chart_)},
        {"view_mode", static_cast<int>(view_mode_)},
    };
    if (control_panel_) {
        const auto& cs = control_panel_->state();
        s["symbol"] = cs.symbol;
        s["dataset"] = cs.dataset;
        s["start_date"] = cs.start_date.toString(Qt::ISODate);
        s["end_date"] = cs.end_date.toString(Qt::ISODate);
        s["strike_window_pct"] = cs.strike_window_pct;
        s["dte_min"] = cs.dte_min;
        s["dte_max"] = cs.dte_max;
        s["iv_method"] = cs.iv_method;
        s["basket"] = cs.basket;
    }
    return s;
}

void SurfaceAnalyticsScreen::restore_state(const QVariantMap& state) {
    const int cat = state.value("category", 0).toInt();
    if (cat != active_category_)
        on_category_clicked(cat);
    if (state.contains("view_mode")) {
        view_mode_ = static_cast<ViewMode>(state.value("view_mode", 0).toInt());
        apply_view_mode_buttons();
    }
    if (control_panel_) {
        SurfaceControlsState cs = control_panel_->state();
        cs.symbol = state.value("symbol", cs.symbol).toString();
        cs.dataset = state.value("dataset", cs.dataset).toString();
        QString sd = state.value("start_date").toString();
        QString ed = state.value("end_date").toString();
        if (!sd.isEmpty()) cs.start_date = QDate::fromString(sd, Qt::ISODate);
        if (!ed.isEmpty()) cs.end_date = QDate::fromString(ed, Qt::ISODate);
        cs.strike_window_pct = state.value("strike_window_pct", cs.strike_window_pct).toInt();
        cs.dte_min = state.value("dte_min", cs.dte_min).toInt();
        cs.dte_max = state.value("dte_max", cs.dte_max).toInt();
        cs.iv_method = state.value("iv_method", cs.iv_method).toString();
        cs.basket = state.value("basket", cs.basket).toStringList();
        control_panel_->apply_state(cs);
        load_demo_data();
        update_chart();
        update_metrics();
        update_inspector_lineage();   // load_demo_data() cleared fetched_
    }
}

// ── IGroupLinked ─────────────────────────────────────────────────────────────

void SurfaceAnalyticsScreen::on_group_symbol_changed(const fincept::SymbolRef& ref) {
    if (!ref.is_valid() || !control_panel_)
        return;
    // Push the linked symbol into the control panel; demo data + chart rebuild
    // happen via on_control_symbol_changed.
    SurfaceControlsState cs = control_panel_->state();
    if (cs.symbol.compare(ref.symbol, Qt::CaseInsensitive) == 0)
        return;
    cs.symbol = ref.symbol.toUpper();
    control_panel_->apply_state(cs);
    on_control_symbol_changed(cs.symbol);
}

fincept::SymbolRef SurfaceAnalyticsScreen::current_symbol() const {
    if (!control_panel_)
        return {};
    QString s = control_panel_->state().symbol;
    if (s.isEmpty())
        return {};
    return fincept::SymbolRef::equity(s);
}

} // namespace fincept::surface
