// src/screens/equity_research/EquityOverviewTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

// ── Custom QPainter candlestick canvas ───────────────────────────────────────
class ResearchCandleCanvas : public QWidget {
    Q_OBJECT
  public:
    enum class PlaceholderState { Loading, NoData, Error };

    explicit ResearchCandleCanvas(QWidget* parent = nullptr);
    void set_candles(const QVector<services::equity::Candle>& candles, const QString& currency_sym);
    void clear();
    /// Set the empty-state message shown when there are no candles to draw.
    /// Loading: covered by the LoadingOverlay above the canvas, so the
    /// placeholder text is empty (the overlay's animation tells the story).
    /// NoData: "No data available for this period" — valid symbol, no series.
    /// Error: "Failed to load chart — try again" — fetch errored out.
    void set_placeholder_state(PlaceholderState state);

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    void rebuild_cache();
    void draw_hover_overlay(QPainter& p);

    QVector<services::equity::Candle> candles_;
    QPixmap cache_;
    bool dirty_ = true;
    QString currency_sym_ = "$";
    PlaceholderState placeholder_state_ = PlaceholderState::Loading;

    // Hover crosshair state. hover_idx_ is an absolute candles_ index
    // (NOT visible-window-relative). The cached geometry below is filled
    // in by rebuild_cache() so mouseMoveEvent and draw_hover_overlay()
    // can map cursor x → candle without re-deriving the layout.
    int    hover_idx_     = -1;
    int    last_plot_w_   = 0;
    int    last_plot_h_   = 0;
    int    last_start_    = 0;   // first visible candle (window slide)
    int    last_count_    = 0;   // visible candle count
    double last_slot_w_   = 0.0; // pixels per candle slot

    // Upper bound on candles rendered in a single paint. Used as a safety
    // clamp only — the service shapes the dataset by period (1M/3M/6M/1Y/5Y)
    // so this should never actually clip in normal use. 1500 covers 5Y daily
    // (~1260 trading days) with margin. The previous value (260, ≈ one year
    // of trading days) silently truncated every 5Y view down to its most
    // recent 1Y window.
    static constexpr int MAX_VISIBLE = 1500;
    static constexpr int PRICE_AXIS_W = 80;
    static constexpr int TIME_AXIS_H = 24;
    static constexpr int LABEL_STEP = 20;
};

// ── Overview tab ─────────────────────────────────────────────────────────────
class EquityOverviewTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityOverviewTab(QWidget* parent = nullptr);
    /// @param force  Bypass the same-symbol early-return so the caller can
    ///               re-trigger a load on the currently-displayed symbol
    ///               (manual-refresh flow).
    void set_symbol(const QString& symbol, bool force = false);

    /// Read-only accessor for the currently selected period button.
    /// Used by external refresh paths that want to re-fetch historical
    /// without overriding the user's period choice.
    QString current_period() const { return current_period_; }

    static QString currency_symbol(const QString& currency_code);

  private:
    /// Called by the three QueryStore subscription callbacks set up in
    /// set_symbol(). Each consumes the State tuple for its category and
    /// updates the relevant UI surfaces. Kept as private (non-slot) methods
    /// because they're invoked from std::function callbacks, not Qt
    /// signal/slot machinery.
    void apply_quote_state(const services::query::QueryStore::State& s);
    void apply_info_state(const services::query::QueryStore::State& s);
    void apply_historical_state(const services::query::QueryStore::State& s);

  private:
    void build_ui();

    QWidget* build_col1();
    QWidget* build_chart_panel();
    QWidget* build_col4();

    QWidget* build_trading_panel();
    QWidget* build_valuation_panel();
    QWidget* build_share_stats_panel();
    QWidget* build_analyst_panel();
    QWidget* build_52w_panel();
    QWidget* build_profitability_panel();
    QWidget* build_growth_panel();

    QWidget* build_bottom_row();
    QWidget* build_company_desc_panel();
    QWidget* build_company_info_panel();
    QWidget* build_financial_health_panel();

    void rebuild_chart(const QVector<services::equity::Candle>& candles);
    void switch_period(QPushButton* btn, const QString& period);

    static QString fmt_large(double v);
    static QString fmt_pct(double v);
    QString fmt_price(double v) const;

    QString current_symbol_;
    QString current_period_ = "1y";
    QString current_currency_;
    // The QueryStore key the historical subscription is currently bound to.
    // Tracked so switch_period() / set_symbol() can call unsubscribe(key)
    // before rebinding — without this, a late resolve for the previous
    // key would still deliver to apply_historical_state with stale candles.
    QString current_historical_key_;

    // Trading panel
    QLabel* open_val_ = nullptr;
    QLabel* high_val_ = nullptr;
    QLabel* low_val_ = nullptr;
    QLabel* prev_close_val_ = nullptr;
    QLabel* vol_val_ = nullptr;

    // Valuation panel
    QLabel* mktcap_val_ = nullptr;
    QLabel* pe_val_ = nullptr;
    QLabel* fwd_pe_val_ = nullptr;
    QLabel* peg_val_ = nullptr;
    QLabel* pb_val_ = nullptr;
    QLabel* div_val_ = nullptr;
    QLabel* beta_val_ = nullptr;

    // Share stats panel
    QLabel* shares_out_val_ = nullptr;
    QLabel* float_val_ = nullptr;
    QLabel* insiders_val_ = nullptr;
    QLabel* institutions_val_ = nullptr;
    QLabel* short_pct_val_ = nullptr;

    // Chart
    QFrame*               chart_panel_   = nullptr;
    ResearchCandleCanvas* candle_canvas_ = nullptr;
    QPushButton* btn_1m_ = nullptr;
    QPushButton* btn_3m_ = nullptr;
    QPushButton* btn_6m_ = nullptr;
    QPushButton* btn_1y_ = nullptr;
    QPushButton* btn_5y_ = nullptr;
    QPushButton* active_period_btn_ = nullptr;

    // Analyst panel
    QLabel* target_high_val_ = nullptr;
    QLabel* target_mean_val_ = nullptr;
    QLabel* target_low_val_ = nullptr;
    QLabel* analyst_count_val_ = nullptr;
    QLabel* rec_key_label_ = nullptr;

    // 52 Week Range
    QLabel* w52h_val_ = nullptr;
    QLabel* w52l_val_ = nullptr;
    QLabel* avg_vol_val_ = nullptr;

    // Profitability
    QLabel* gross_margin_val_ = nullptr;
    QLabel* op_margin_val_ = nullptr;
    QLabel* profit_margin_val_ = nullptr;
    QLabel* roa_val_ = nullptr;
    QLabel* roe_val_ = nullptr;

    // Growth
    QLabel* rev_growth_val_ = nullptr;
    QLabel* earnings_growth_val_ = nullptr;

    // Company description
    QLabel* company_desc_ = nullptr;

    // Company info
    QLabel* company_emp_ = nullptr;
    QLabel* company_web_ = nullptr;
    QLabel* company_currency_ = nullptr;

    // Financial health
    QLabel* cash_val_ = nullptr;
    QLabel* debt_val_ = nullptr;
    QLabel* free_cf_val_ = nullptr;

    // Cached data
    services::equity::StockInfo cached_info_;
    services::equity::QuoteData cached_quote_;
    QVector<services::equity::Candle> cached_candles_;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
    // Loading state is owned by QueryStore now — no more per-category booleans.
    // Overlay show/hide is driven from apply_historical_state's State.loading
    // tuple; quote/info panels populate independently as their subscriptions
    // resolve.
};

} // namespace fincept::screens
