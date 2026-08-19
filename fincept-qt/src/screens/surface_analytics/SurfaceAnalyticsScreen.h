#pragma once
// SurfaceAnalyticsScreen — 35 financial surface visualizations
// Equity derivatives, fixed income, FX, credit, commodities, risk, macro

#include "SurfaceCapabilities.h"
#include "SurfaceDemoData.h"

#include <QSet>
#include "SurfaceTypes.h"
#include "core/symbol/IGroupLinked.h"
#include "screens/IStatefulScreen.h"
#include "services/databento/DatabentoService.h"

#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::surface {

class Surface3DWidget;
class SurfaceTableWidget;
class SurfaceLineWidget;
class SurfaceControlPanel;
class SurfaceDataInspector;

class SurfaceAnalyticsScreen : public QWidget,
                               public fincept::screens::IStatefulScreen,
                               public fincept::IGroupLinked {
    Q_OBJECT
    Q_INTERFACES(fincept::IGroupLinked)
  public:
    explicit SurfaceAnalyticsScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "surface_analytics"; }
    int state_version() const override { return 1; }

    // IGroupLinked — subscribe-only. Equity group changes are forwarded to
    // the SurfaceControlPanel which becomes the new symbol of record; demo data
    // and chart re-render via on_control_symbol_changed.
    void set_group(fincept::SymbolGroup g) override { link_group_ = g; }
    fincept::SymbolGroup group() const override { return link_group_; }
    void on_group_symbol_changed(const fincept::SymbolRef& ref) override;
    fincept::SymbolRef current_symbol() const override;

  private slots:
    void on_category_clicked(int index);
    void on_surface_clicked(int cat, int surf_index);
    void on_view_3d();
    void on_view_table();
    void on_view_line();
    void on_import_csv();
    void on_refresh();
    void on_controls_changed();
    void on_control_symbol_changed(const QString& sym);
    void on_fetch_requested();
    // Databento data slots
    void on_vol_surface_received(const fincept::DatabentoVolSurfaceResult& r);
    void on_ohlcv_received(const fincept::DatabentoOhlcvResult& r);
    void on_futures_received(const fincept::DatabentoFuturesResult& r);
    void on_surface_received(const fincept::DatabentoSurfaceResult& r);
    void on_db_fetch_started(const QString& desc);
    void on_db_fetch_failed(const QString& err);
    void on_db_connection_tested(bool ok, const QString& msg);
    void on_db_raw_response(const QString& cmd, const QString& raw_stdout);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    enum class ViewMode { Surface3D = 0, Table = 1, Line = 2 };

    void setup_ui();
    QWidget* build_category_bar();
    QWidget* build_surface_bar();
    void refresh_surface_bar();
    void load_demo_data();
    /// Build Correlation, PCA, VaR, Drawdown and Beta from the daily bars a
    /// fetch returned. Everything it fills is marked fetched_, so the badge
    /// and the lineage report real provenance for them.
    void compute_equity_surfaces(const fincept::DatabentoOhlcvResult& r);
    /// What the beta surface is measured AGAINST — a broad-market ETF from
    /// the basket when there is one, otherwise the equal-weighted basket.
    /// Shown on the axis, because a beta without its benchmark is not a
    /// number anyone can use.
    QString beta_benchmark_;
    /// Chart types whose drawn surface came from a real fetch. Everything not
    /// in here is analytically generated — load_demo_data() fills EVERY
    /// surface, including the ones whose tier names a Databento dataset, so
    /// "which tier is this chart" was never an answer to "where did these
    /// numbers come from".
    QSet<ChartType> fetched_;
    /// Surfaces loaded from a file the user chose, and where each came from.
    /// Kept apart from fetched_: an import is real data, but it did not come
    /// out of the tier's Databento dataset, and saying it did is the same
    /// false provenance claim as calling generated data COMPUTED.
    QHash<ChartType, QString> imported_from_;
    /// True when the surface currently on screen was generated, not fetched
    /// and not imported.
    bool active_is_synthetic() const {
        return !fetched_.contains(active_chart_) && !imported_from_.contains(active_chart_);
    }
    void update_chart();
    void update_metrics();
    void update_inspector_lineage();
    void dispatch_csv(const QString& path);
    void apply_view_mode_buttons();
    void update_line_view();
    void refresh_provider_status();
    void load_dataset_range_for_active_capability();

    QString current_symbol_or_default() const;
    float spot_for(const QString& sym) const;
    const std::vector<std::vector<float>>* active_z_grid() const;

    // Control bar widgets (kept for dynamic rebuild)
    QWidget* category_bar_ = nullptr;
    QWidget* surface_bar_ = nullptr;
    QPushButton* btn_3d_ = nullptr;
    QPushButton* btn_table_ = nullptr;
    QPushButton* btn_line_ = nullptr;

    // Main panels
    SurfaceControlPanel* control_panel_ = nullptr;
    SurfaceDataInspector* data_inspector_ = nullptr;
    Surface3DWidget* surface_3d_ = nullptr;
    SurfaceTableWidget* surface_table_ = nullptr;
    SurfaceLineWidget* surface_line_ = nullptr;
    QStackedWidget* view_stack_ = nullptr;

    // State
    int active_category_ = 0;
    ChartType active_chart_ = ChartType::Volatility;
    ViewMode view_mode_ = ViewMode::Surface3D;

    // ---- Data for all 35 surfaces ----
    VolatilitySurfaceData vol_data_;
    GreeksSurfaceData delta_data_, gamma_data_, vega_data_, theta_data_;
    SkewSurfaceData skew_data_;
    LocalVolData local_vol_data_;
    YieldCurveData yield_data_;
    SwaptionVolData swaption_data_;
    CapFloorVolData capfloor_data_;
    BondSpreadData bond_spread_data_;
    OISBasisData ois_data_;
    RealYieldData real_yield_data_;
    ForwardRateData fwd_rate_data_;
    FXVolData fx_vol_data_;
    FXForwardPointsData fx_fwd_data_;
    CrossCurrencyBasisData xccy_data_;
    CDSSpreadData cds_data_;
    CreditTransitionData credit_trans_data_;
    RecoveryRateData recovery_data_;
    CommodityForwardData cmdty_fwd_data_;
    CommodityVolData cmdty_vol_data_;
    CrackSpreadData crack_data_;
    ContangoData contango_data_;
    CorrelationMatrixData corr_data_;
    PCAData pca_data_;
    VaRData var_data_;
    StressTestData stress_data_;
    FactorExposureData factor_data_;
    LiquidityData liquidity_data_;
    DrawdownData drawdown_data_;
    BetaData beta_data_;
    ImpliedDividendData impl_div_data_;
    InflationExpData inflation_data_;
    MonetaryPolicyData monetary_data_;

    // Symbol group link — SymbolGroup::None when unlinked.
    fincept::SymbolGroup link_group_ = fincept::SymbolGroup::None;

    // Spot cache, populated from DataHub::peek("market:quote:<sym>") on demand.
    // Falls back to a constant if the hub has no quote for the symbol.
    QHash<QString, float> spot_cache_;

    // Category accent colors (R,G,B)
    static constexpr int CAT_COLORS[][3] = {
        {51, 153, 255},  // EQUITY DERIV — blue
        {102, 217, 140}, // FIXED INCOME — green
        {255, 191, 51},  // FX           — gold
        {230, 89, 89},   // CREDIT       — red
        {191, 140, 255}, // COMMODITIES  — purple
        {255, 140, 38},  // RISK         — orange
        {89, 217, 242},  // MACRO        — cyan
    };
};

} // namespace fincept::surface
