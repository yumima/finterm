// src/screens/portfolio/PortfolioPerfChart.h
#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QChartView>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

/// QChartView subclass that draws a vertical crosshair and floating tooltip.
class CrosshairChartView : public QChartView {
    Q_OBJECT
  public:
    explicit CrosshairChartView(QChart* chart, QWidget* parent = nullptr);

    /// Feed the raw series points so we can snap to the nearest one.
    void set_series_data(const QVector<QPointF>& pts, const QString& currency);

  protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    void update_crosshair(const QPoint& widget_pos);
    void hide_crosshair();

    QVector<QPointF> pts_;
    QString currency_;
    QGraphicsLineItem* v_line_ = nullptr;
    QLabel* tooltip_ = nullptr;
};

/// NAV performance chart with period selector and info bar.
class PortfolioPerfChart : public QWidget {
    Q_OBJECT
  public:
    explicit PortfolioPerfChart(QWidget* parent = nullptr);

    void set_summary(const portfolio::PortfolioSummary& summary);
    void set_snapshots(const QVector<portfolio::PortfolioSnapshot>& snapshots);
    void set_currency(const QString& currency);
    /// Feed real benchmark closes for the overlay. The symbol is shown on the
    /// toggle button and used to disambiguate currencies in indexed mode.
    void set_benchmark_history(const QString& symbol, const QStringList& dates,
                               const QVector<double>& closes);
    /// Legacy shim — same as set_benchmark_history("SPY", dates, closes).
    void set_spy_history(const QStringList& dates, const QVector<double>& closes);
    void refresh_theme();

    // ── Per-symbol focus mode ─────────────────────────────────────────────
    /// Switch to per-symbol focus: chart renders this symbol's daily close
    /// history instead of portfolio NAV. Triggers focus_symbol_period_requested
    /// so the owner re-fetches data for the current period.
    void set_focus_symbol(const QString& symbol);
    /// Exit focus mode and revert to portfolio NAV view.
    void clear_focus_symbol();
    /// Feed daily closes for the currently focused symbol. Mismatched symbols
    /// are ignored (race-safe against stale fetches).
    void set_focus_history(const QString& symbol, const QStringList& dates,
                           const QVector<double>& closes);
    QString focus_symbol() const { return focus_symbol_; }

    // ── 1D intraday ─────────────────────────────────────────────────────────
    /// Feed today's 1-minute close series for the currently focused symbol.
    /// Mismatched symbol → ignored (race-safe against stale fetches).
    void set_symbol_intraday(const QString& symbol,
                             const QVector<qint64>& timestamps_ms,
                             const QVector<double>& closes);
    /// Feed today's 1-minute aggregate-NAV series for portfolio view.
    void set_portfolio_intraday(const QVector<qint64>& timestamps_ms,
                                const QVector<double>& navs);

  signals:
    /// Emitted when the user clicks a period button that requires backfilling
    /// (e.g. 5Y when only 1Y is cached). Owner can call
    /// PortfolioService::backfill_history(portfolio_id, period) in response.
    void backfill_period_requested(QString period);
    /// Focus mode: owner should fetch daily closes for @p symbol over the
    /// yfinance @p period (e.g. "1mo", "1y", "max").
    void focus_symbol_period_requested(QString symbol, QString period);
    /// User clicked 1D. Owner calls PortfolioService::fetch_symbol_intraday
    /// (focus mode, symbol non-empty) or fetch_portfolio_intraday (aggregate,
    /// symbol empty). The chart paints a placeholder until set_*_intraday()
    /// lands.
    void intraday_requested(QString symbol_or_empty);

  private:
    void build_ui();
    void update_chart();
    void update_chart_focus(); ///< Render focused-symbol daily-close series.
    /// Render the 1D intraday series stored in intraday_ts_ms_/intraday_values_.
    /// is_aggregate=true → portfolio NAV mode (uses currency formatting and
    /// resets info-bar labels accordingly). Returns true if data was drawn,
    /// false if the series is empty and a loading placeholder rendered.
    bool render_intraday(bool is_aggregate);
    void set_period(const QString& period);
    void update_period_buttons_enabled();
    QColor chart_color() const;
    /// Convert a UTC ISO date string to ms since epoch (UTC midnight).
    /// Centralised so portfolio and benchmark series share the same time axis.
    static qint64 iso_date_to_ms_utc(const QString& iso_date);
    /// Map the chart's internal period token to a yfinance period string.
    QString period_for_yfinance() const;

    // Period selector buttons
    QVector<QPushButton*> period_btns_;
    QString current_period_ = "1M";

    // Benchmark toggle + indexed-mode toggle
    QPushButton* benchmark_btn_ = nullptr;
    QPushButton* indexed_btn_ = nullptr;
    bool show_benchmark_ = false;
    bool indexed_mode_ = false; // false = currency value, true = base-100 indexed

    // Info bar — single row carrying two semantic groups:
    //   left  group: period change %, total return %, MV, COST (overall)
    //   right group: LAST, BID x ASK, DAY range, VOL (today, focus only)
    // A fixed-width spacer separates the two so the user can read them as
    // "overall | today". The live group hides at portfolio level.
    QLabel* period_change_label_ = nullptr;
    QLabel* total_return_label_ = nullptr;
    QLabel* nav_label_ = nullptr;
    QLabel* cost_basis_label_ = nullptr;
    QLabel* live_last_label_ = nullptr;
    QLabel* live_bidask_label_ = nullptr;
    QLabel* live_range_label_ = nullptr;
    QLabel* live_vol_label_ = nullptr;
    void set_live_group_visible(bool visible);

    // Chart
    CrosshairChartView* chart_view_ = nullptr;

    // Data
    portfolio::PortfolioSummary summary_;
    QVector<portfolio::PortfolioSnapshot> snapshots_;
    QString currency_ = "USD";

    // Benchmark history — symbol may differ from "SPY" depending on currency
    QString benchmark_symbol_ = "SPY";
    QStringList spy_dates_;
    QVector<double> spy_closes_;

    // Per-symbol focus mode (empty focus_symbol_ → portfolio NAV view).
    QString focus_symbol_;
    QStringList focus_dates_;
    // 1D intraday series. Populated by set_symbol_intraday() (focus mode)
    // and set_portfolio_intraday() (aggregate). Timestamps are epoch-ms so
    // the chart's QDateTimeAxis can plot sub-day resolution. Render path
    // routes here whenever current_period_ == "1D" and the matching series
    // is non-empty.
    QVector<qint64> intraday_ts_ms_;
    QVector<double> intraday_values_;
    QString          intraday_for_symbol_;  // empty = portfolio aggregate
    bool             intraday_requested_   = false;  // 1D selected, waiting for data
    // True once the fetch callback has fired for the current request. Lets
    // render_intraday distinguish "still loading" from "loaded but empty"
    // (e.g. weekend, holiday, pre-open) — the latter should show a terminal
    // message, not "Loading…" forever.
    bool             intraday_resolved_    = false;
    QVector<double> focus_closes_;
    bool focus_data_loaded_ = false; // true once set_focus_history fires (even with empty data)
    QLabel* title_label_ = nullptr;
};

} // namespace fincept::screens
