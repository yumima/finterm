#pragma once
#include "ui/RenderCoalescer.h"
#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/portfolio/PortfolioTypes.h"
#include "services/markets/MarketDataService.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace finterm::screens::widgets {

/// Portfolio Summary Widget — mirrors a real portfolio from PortfolioService.
/// A dropdown selects which portfolio to view when several are configured;
/// the choice is remembered across sessions. Holdings come straight from the
/// same backend the Portfolio screen uses, so a buy/sell there shows up here.
///
/// Once a portfolio's holdings are known the widget subscribes to
/// `market:quote:<sym>` on the DataHub for each holding, so prices stay live.
/// Adds/sells in the selected portfolio (PortfolioService::asset_added/sold)
/// trigger a re-fetch and rewire the subscriptions.
///
/// The AFT% column is fed from a different source than the rest of the row:
/// the quote feed stops at the closing bell, so extended-hours moves come from
/// the yfinance daemon on a slow timer. During the regular session it keeps
/// showing the LAST extended move rather than blanking — that is what the
/// Portfolio screen's heatmap has always done, and a column that empties
/// itself for six and a half hours a day reads as broken. It is frozen then,
/// so it is fetched once and not re-polled until the symbol set changes.
class PortfolioSummaryWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit PortfolioSummaryWidget(QWidget* parent = nullptr);

    struct Holding {
        QString symbol;
        double shares = 0;
        double avg_cost = 0; // instrument currency, as the exchange prints it
        // Instrument→portfolio-currency multiplier. This widget recomputes
        // value/cost/day-P&L from live quotes rather than reusing the
        // summary's totals, so it needs the same rate the portfolio screen
        // used — otherwise the two screens show different totals for the
        // same book.
        double fx_rate = 1.0;
    };

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void apply_styles();

  public:
    void load_holdings();
    void fetch_prices(const QVector<Holding>& holdings);

  private:
    // Populate the dropdown from the loaded portfolios and (re)select one.
    void on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios);
    // Render the summary for the selected portfolio; ignores other portfolios.
    void on_summary_loaded(const portfolio::PortfolioSummary& summary);
    // Switch the active portfolio: persist the choice and load its summary.
    void select_portfolio(const QString& id);

  public:
    void render(const QVector<Holding>& holdings, const QVector<services::QuoteData>& quotes);

    /// Re-subscribe to `market:quote:<sym>` for every holding. Drops old
    /// subscriptions first — holdings set may have changed since last call.
    void hub_resubscribe(const QVector<Holding>& holdings);
    void hub_unsubscribe_all();
    /// Rebuild quotes vector from `row_cache_` in `last_holdings_` order
    /// and re-render.
    void rebuild_from_cache();

    // Summary labels
    QLabel* total_value_lbl_ = nullptr;
    QLabel* day_pnl_lbl_ = nullptr;
    QLabel* day_chg_pct_lbl_ = nullptr;
    QLabel* total_pnl_lbl_ = nullptr;
    QLabel* total_chg_pct_lbl_ = nullptr;
    QLabel* num_holdings_lbl_ = nullptr;

    // Holdings list layout
    QVBoxLayout* list_layout_ = nullptr;

    // Widgets needing theme refresh
    QWidget* summary_card_ = nullptr;
    QWidget* header_row_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVector<QLabel*> metric_labels_;
    QVector<QLabel*> metric_values_;
    QVector<QLabel*> header_labels_;

    // Cached for theme-change re-render
    QVector<Holding> last_holdings_;
    // Holdings left out because their FX conversion into the portfolio
    // currency is unknown (no assumed 1.0 rate). Named in the totals' tooltip.
    QStringList fx_excluded_;
    QVector<services::QuoteData> last_quotes_;

    QHash<QString, services::QuoteData> row_cache_;
    bool hub_active_ = false;

    // ── After-hours column ───────────────────────────────────────────────────
    // Fed by services::ExtendedHoursService, the source every AFT surface
    // shares, so the same symbol can't read differently here and on the
    // Portfolio screen.
    /// Tell the shared extended-hours source what this widget shows.
    void watch_aft();
    /// AFT% header suffix from the shared source's state: an empty column
    /// with no explanation reads as a bug, so "loading" and "failed" show.
    void update_aft_header();
    QLabel* aft_header_ = nullptr;

    // ── Portfolio selection ──────────────────────────────────────────────────
    QComboBox* portfolio_combo_ = nullptr;
    QVector<portfolio::Portfolio> portfolios_;
    QString selected_id_;                // id of the portfolio currently shown
    bool suppress_combo_signal_ = false; // guard against programmatic combo edits
    bool portfolios_loaded_ = false;     // false until first portfolios_loaded()

  private:
    /// Coalesces the per-quote re-render: one rebuild_from_cache() per tick batch.
    ui::RenderCoalescer quote_render_{this, [this]() { rebuild_from_cache(); }};
};

} // namespace finterm::screens::widgets
