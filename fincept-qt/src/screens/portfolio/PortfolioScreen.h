// src/screens/portfolio/PortfolioScreen.h
#pragma once
#include "core/symbol/IGroupLinked.h"
#include "screens/IStatefulScreen.h"
#include "screens/portfolio/PortfolioTypes.h"

#include <QHideEvent>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSet>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <optional>

namespace fincept::screens {

class PortfolioCommandBar;
class PortfolioStatsRibbon;
class PortfolioStatusBar;
class PortfolioHeatmap;
class PortfolioPerfChart;
class PortfolioSectorPanel;
class PortfolioBlotter;
class PortfolioTxnPanel;
class PortfolioOrderPanel;
class PortfolioDetailWrapper;
class PortfolioFFNView;
class PortfolioInsightsPanel;

class PortfolioScreen : public QWidget, public IStatefulScreen, public IGroupLinked {
    Q_OBJECT
    Q_INTERFACES(fincept::IGroupLinked)
  public:
    explicit PortfolioScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "portfolio"; }

    // IGroupLinked — active symbol = currently selected holding. Receiving
    // a group symbol scrolls/selects that holding if it exists in the
    // current portfolio; symbols not held are silently ignored (no synthetic
    // "phantom" selection).
    void set_group(SymbolGroup g) override { link_group_ = g; }
    SymbolGroup group() const override { return link_group_; }
    void on_group_symbol_changed(const SymbolRef& ref) override;
    SymbolRef current_symbol() const override;

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private slots:
    void on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios);
    void on_portfolio_selected(const QString& id);
    void on_summary_loaded(portfolio::PortfolioSummary summary);
    void on_summary_error(QString portfolio_id, QString error);
    void on_metrics_computed(portfolio::ComputedMetrics metrics);
    void on_snapshots_loaded(QString portfolio_id, QVector<portfolio::PortfolioSnapshot> snapshots);
    void on_portfolio_created(portfolio::Portfolio portfolio);
    void on_portfolio_deleted(QString id);
    void on_asset_changed(QString portfolio_id);
    void on_create_requested();
    void on_delete_requested(const QString& id);
    void on_detail_view_selected(portfolio::DetailView view);
    void on_refresh_interval_changed(int ms);
    void on_symbol_selected(const QString& symbol);
    void on_buy_requested();
    void on_sell_requested();
    void on_order_panel_close();

  private:
    void build_ui();
    void refresh_theme();
    QWidget* build_empty_state();
    QWidget* build_loading_state();
    QWidget* build_main_view();
    void update_content_state();
    void update_main_view_data();
    /// @param force_fresh true on user-initiated refresh (tab show, portfolio
    ///        change, refresh button) — drops the quote cache so the next
    ///        fetch hits the data source. Timer-driven ticks pass false so
    ///        background polling can still benefit from the cache TTL.
    void request_refresh(bool force_fresh = false);
    void load_demo_portfolio();
    void reposition_order_panel();
    void animate_order_panel_in();
    const portfolio::HoldingWithQuote* find_holding(const QString& symbol) const;

    /// Live-quote subscription. Resubscribes to `market:quote:<sym>` for the
    /// current holdings if the symbol set has changed since last call.
    /// Brings the Portfolio screen onto the same DataHub stream the dashboard
    /// uses, so today's chg% / price stay fresh between the slower 20s
    /// PortfolioService refreshes — and stay fresh even when the user has
    /// the Portfolio tab visible overnight (hideEvent never fires in that
    /// case, so the showEvent-driven force refresh wouldn't run).
    void hub_resubscribe_holdings();
    void hub_unsubscribe_all();
    /// Re-derive totals, gainers/losers and weights from the per-holding
    /// fields, then push the patched summary to command/stats/status bars
    /// and the main view. Called from a debounce timer so a burst of quote
    /// publishes (one per held symbol) triggers exactly one UI pass.
    void rebuild_summary_aggregates_and_refresh();

    // Sub-widgets
    PortfolioCommandBar* command_bar_ = nullptr;
    PortfolioStatsRibbon* stats_ribbon_ = nullptr;
    PortfolioStatusBar* status_bar_ = nullptr;
    QStackedWidget* content_stack_ = nullptr;

    // Content pages
    QWidget* empty_state_ = nullptr;
    QWidget* loading_state_ = nullptr;
    QWidget* main_view_ = nullptr;

    // Main view sub-widgets
    PortfolioHeatmap* heatmap_ = nullptr;
    PortfolioPerfChart* perf_chart_ = nullptr;
    PortfolioSectorPanel* sector_panel_ = nullptr;
    PortfolioBlotter* blotter_ = nullptr;
    PortfolioTxnPanel* txn_panel_ = nullptr;
    PortfolioOrderPanel* order_panel_ = nullptr;
    PortfolioDetailWrapper* detail_wrapper_ = nullptr;
    PortfolioFFNView* ffn_view_ = nullptr;
    PortfolioInsightsPanel* insights_panel_ = nullptr;
    QWidget* insights_scrim_ = nullptr;

    // State
    QVector<portfolio::Portfolio> portfolios_;
    QString selected_id_;
    QString selected_symbol_;
    portfolio::PortfolioSummary current_summary_;
    portfolio::ComputedMetrics current_metrics_;
    bool summary_loaded_ = false;
    bool order_panel_visible_ = false;
    bool show_ffn_ = false;
    std::optional<portfolio::DetailView> active_detail_;

    // Debounce keys for on_summary_loaded side-fetches. Without these, the
    // 20-second refresh tick re-fires fetch_correlation (spawns a Python
    // process) + two fetch_benchmark_history calls + fetch_risk_free_rate
    // every cycle, even when nothing changed.
    QStringList last_correlation_syms_;       // symbol set last sent to fetch_correlation
    QSet<QString> fetched_benchmarks_;        // benchmarks already pulled this session
    bool risk_free_fetched_ = false;          // DGS10 — server caches 24h, once per session is enough

    // Refresh timer (P3)
    QTimer* refresh_timer_ = nullptr;
    int refresh_interval_ms_ = 20000;

    // DataHub live-quote subscription state.
    QTimer* hub_refresh_timer_ = nullptr; // debounces aggregate rebuild
    QStringList hub_subscribed_syms_;     // sorted; used to skip resub on no-op
    bool hub_active_ = false;

    // Order panel slide-in animation
    QPropertyAnimation* order_panel_anim_ = nullptr;

    // Positions section header — count badge updates with holdings
    QLabel* positions_count_label_ = nullptr;

    // Symbol-group link (None when unlinked).
    SymbolGroup link_group_ = SymbolGroup::None;
};

} // namespace fincept::screens
