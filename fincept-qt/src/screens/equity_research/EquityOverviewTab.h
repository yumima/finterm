// src/screens/equity_research/EquityOverviewTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
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

    // ── Pass 1 chart-overlay controls ────────────────────────────────────────
    /// Toggle log scaling on the price axis. Linear is the default; log is
    /// dramatically more accurate visually for long periods where the price
    /// has compounded across multiple multiples.
    void set_log_scale(bool on);
    /// Toggle the volume subchart underneath the candles. Costs ~25% of the
    /// vertical plot area when on.
    void set_show_volume(bool on);
    /// Toggle individual moving-average overlays. SMAs are computed on the
    /// fly from candles_ (cheap — O(n) one pass per SMA).
    void set_show_sma(int period, bool on);  // period ∈ {20, 50, 200}
    /// Earnings markers — drawn as dashed vertical lines at each event
    /// whose timestamp lies within the visible candle window.
    void set_earnings_events(const QVector<services::equity::EarningsEvent>& events);
    /// 52-week high for the crosshair "vs 52w-high" comparator. Set by the
    /// tab when StockInfo arrives; 0 ⇒ comparator suppressed.
    void set_week52_high(double v);

  signals:
    /// Fired whenever the crosshair lands on a different candle (mouseMove)
    /// or the cursor leaves the plot area (-1). Lets the hosting tab keep
    /// an always-visible comparison legend in sync with the hovered date
    /// without polling. Idx is an absolute candles_ index, or -1 for "no
    /// hover" (show the most-recent close instead).
    void hover_changed(int hover_idx);

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    void rebuild_cache();
    void draw_hover_overlay(QPainter& p);
    /// Helper for mouseMoveEvent / leaveEvent — sets hover_idx_ and emits
    /// the signal if the value actually changed. Centralizes the dedupe so
    /// rapid mouse moves over the same candle don't fire repeated signals.
    void set_hover_idx(int idx);

    QVector<services::equity::Candle> candles_;
    QPixmap cache_;
    bool dirty_ = true;
    QString currency_sym_ = "$";
    PlaceholderState placeholder_state_ = PlaceholderState::Loading;

    // Pass 1 overlay state — every toggle invalidates the pixmap cache so
    // the next paintEvent rebuilds with the new options.
    bool log_scale_   = false;
    bool show_volume_ = false;
    bool show_sma20_  = false;
    bool show_sma50_  = false;
    bool show_sma200_ = false;
    QVector<services::equity::EarningsEvent> earnings_events_;
    double week52_high_ = 0.0;

    // Comparison overlay state. Each series is rendered as a single line
    // normalized to the primary symbol's first visible close — so the user
    // reads divergence as relative outperformance, not absolute price.
  public:
    struct Comparison {
        QString symbol;
        QColor  color;
        QVector<services::equity::Candle> candles;
    };
    void set_comparisons(const QVector<Comparison>& list);

  private:
    QVector<Comparison> comparisons_;

    // Volume strip geometry — when show_volume_ is on, the price plot
    // shrinks by VOLUME_FRAC of its height to make room. The volume strip
    // sits between the price plot and the time axis.
    static constexpr double VOLUME_FRAC = 0.22;

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

    // ── Keyboard-shortcut entry points ──────────────────────────────────────
    // Each method maps to one user-visible chart action — clicking the
    // relevant toggle / period button programmatically. Public so the
    // hosting screen can wire QShortcuts on the screen root without
    // poking into private button members.
    void shortcut_set_period(int slot);   // 1..5 → 1mo/3mo/6mo/1y/5y
    void shortcut_toggle_log();
    void shortcut_toggle_volume();
    void shortcut_toggle_sma50();
    void shortcut_toggle_earnings();
    void shortcut_open_comparison();
    void shortcut_open_range_picker();

    /// Add a ticker to the comparison overlay. Public entry point so other
    /// surfaces (Peers tab click-to-add) can route through the same code
    /// path the inline input uses. Silently no-ops on empty/duplicate/
    /// self-comparison/max-reached — chip strip and overlay are updated as
    /// a side effect. Symbol is upper-cased before compare/store.
    void add_comparison(const QString& symbol);

  private:
    /// Called by the three QueryStore subscription callbacks set up in
    /// set_symbol(). Each consumes the State tuple for its category and
    /// updates the relevant UI surfaces. Kept as private (non-slot) methods
    /// because they're invoked from std::function callbacks, not Qt
    /// signal/slot machinery.
    void apply_quote_state(const services::query::QueryStore::State& s);
    void apply_info_state(const services::query::QueryStore::State& s);
    void apply_historical_state(const services::query::QueryStore::State& s);
    void apply_earnings_state(const services::query::QueryStore::State& s);
    /// Bind or drop the earnings subscription depending on the EARN toggle
    /// state and the current symbol. Called when the toggle flips or the
    /// symbol changes.
    void refresh_earnings_subscription();

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
    /// Mirror of the EARN toggle button state — gates whether we hold an
    /// earnings subscription. When false, set_symbol() skips earnings work
    /// entirely to avoid an unused Finnhub-or-yfinance call per symbol.
    bool show_earnings_ = false;
    QString current_earnings_key_;

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
    QPushButton* btn_custom_ = nullptr;   // opens date-range picker
    QPushButton* active_period_btn_ = nullptr;
    void open_custom_range_picker();

    // Overlay toggle buttons. Held as members so saved chart views can
    // re-check them programmatically on symbol switch — without these the
    // saved state could be applied to the canvas but the buttons would lie.
    QPushButton* btn_log_   = nullptr;
    QPushButton* btn_vol_   = nullptr;
    QPushButton* btn_sma20_ = nullptr;
    QPushButton* btn_sma50_ = nullptr;
    QPushButton* btn_sma200_= nullptr;
    QPushButton* btn_earn_  = nullptr;
    QPushButton* btn_comp_  = nullptr;   // label + click-to-focus the inline input

    // Inline COMP controls — split across two rows:
    //   • Button row (btn_row): COMP focus-button + inline ticker input.
    //   • Legend row (comp_row_): one chip per active comparison, always
    //     visible, sitting between the primary OHLC strip and the canvas.
    //     Each chip = [✕][color swatch] TICKER: chg% $price. The chg% and
    //     price update dynamically as the user hovers the crosshair (via
    //     ResearchCandleCanvas::hover_changed); when not hovering they show
    //     the full-window return and the last close.
    QHBoxLayout* comp_chips_   = nullptr;   ///< layout for chip widgets in comp_row_
    QLineEdit*   comp_input_   = nullptr;   ///< inline ticker input (Enter to add)

    // Permanent primary-ticker stats strip — date + OHLC + Δ + vol + 52w-hi
    // + SMA50. Sits between btn_row and comp_row, always visible. When the
    // user isn't hovering it shows the most-recent candle; while hovering
    // it tracks the crosshair via the same hover_changed signal that drives
    // the comp chip labels. Each pointer is held so update_primary_stats_row
    // can rewrite text without rebuilding the row.
    QLabel* primary_date_lbl_  = nullptr;
    QLabel* primary_ohlc_lbl_  = nullptr;
    QLabel* primary_delta_lbl_ = nullptr;
    QLabel* primary_vol_lbl_   = nullptr;
    QLabel* primary_52w_lbl_   = nullptr;
    QLabel* primary_sma50_lbl_ = nullptr;
    /// Per-comp label widget pointers, keyed by ticker symbol, so the hover
    /// slot can update chip text in O(1) without rebuilding the row. Cleared
    /// + repopulated each rebuild_comp_strip() call.
    QHash<QString, QLabel*> comp_chip_labels_;
    /// Tear down and rebuild the chip widgets from comp_state_. Cheap (≤3
    /// chips), called whenever a comparison is added/removed or its candles
    /// arrive from the service.
    void rebuild_comp_strip();
    /// Update comp_input_'s placeholder and enabled state to reflect whether
    /// adding another comparison is allowed (cap is kMaxComparisons in the
    /// .cpp). Called from rebuild_comp_strip and on construction.
    void refresh_comp_input_state();
    /// Recompute each chip's label text for the given hover candle index
    /// (or -1 for no-hover, in which case the label shows the full-window
    /// return and the last close). Walks comp_state_ × comp_chip_labels_;
    /// no allocations beyond QString formatting.
    void update_comp_chip_labels(int hover_idx);
    /// Rewrite the permanent primary stats strip from cached_candles_ at
    /// the given index (or the latest candle when hover_idx == -1 or out
    /// of range). Mirrors the formatting the canvas hover overlay used —
    /// date · O H L C · Δ ± · Vol · 52w-hi · SMA50.
    void update_primary_stats_row(int hover_idx);

    // Comparison series the user has added. We hold the canvas-bound state
    // here too so re-subscribing on symbol/period change can re-fetch each
    // comp without losing the colour assignment. `candles` is the running
    // copy that survives partial callbacks — if comp#1 resolves before
    // comp#2, we re-push the full list to the canvas so #1 doesn't flash
    // out while #2 is still in flight.
    struct CompState {
        QString symbol;
        QColor  color;
        QVector<services::equity::Candle> candles;
    };
    QVector<CompState> comp_state_;
    void refresh_comparisons();

    // Per-ticker view persistence (period + overlays + custom range).
    void save_chart_view() const;
    void load_chart_view(const QString& symbol);
    /// Set to true while load_chart_view is applying — gates save_chart_view
    /// against the toggled signals each setChecked emits, so we don't
    /// thrash storage with redundant writes during restoration.
    bool restoring_view_ = false;

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
