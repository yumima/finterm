// src/services/portfolio/PortfolioService.h
#pragma once
#include "python/PythonRunner.h"
#include "screens/portfolio/PortfolioTypes.h"
#include "services/markets/MarketDataService.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace fincept::services {

/// Singleton service managing portfolio data, live quotes, and computed metrics.
/// Screens connect to signals — never call DB or market data directly.
class PortfolioService : public QObject {
    Q_OBJECT
  public:
    static PortfolioService& instance();

    // ── Portfolio CRUD ───────────────────────────────────────────────────────
    void load_portfolios();
    void create_portfolio(const QString& name, const QString& owner, const QString& currency,
                          const QString& description = {});
    void delete_portfolio(const QString& id);

    // ── Summary (assets + live quotes) ───────────────────────────────────────
    void load_summary(const QString& portfolio_id);
    void refresh_summary(const QString& portfolio_id); // invalidates cache first

    // ── Asset operations ─────────────────────────────────────────────────────
    void add_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                   const QString& date = {});
    void sell_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                    const QString& date = {});

    // ── Transactions ─────────────────────────────────────────────────────────
    void load_transactions(const QString& portfolio_id, int limit = 50);
    /// Edit an existing holding in place. The blotter shows the stored asset
    /// row (build_summary reads get_assets, NOT a sum of transactions), so
    /// rewriting only the transaction left the displayed position unchanged.
    /// This rewrites the edited transaction, then re-derives the asset row's
    /// quantity and weighted-average cost from ALL of the symbol's transactions
    /// (so multi-lot positions stay intact), invalidates the cache, and
    /// refreshes the summary.
    void edit_position(const QString& portfolio_id, const QString& symbol, const QString& txn_id, double qty,
                       double price, const QString& date, const QString& notes = {});
    void delete_transaction(const QString& id, const QString& portfolio_id);

    // ── Dividend ──────────────────────────────────────────────────────────────
    void record_dividend(const QString& portfolio_id, const QString& symbol, double qty, double amount_per_share,
                         double total, const QString& date = {}, const QString& notes = {});

    // ── Historical correlation ─────────────────────────────────────────────────
    /// Fetch 30-day daily closes for @p symbols and compute pairwise Pearson
    /// correlation matrix. Result emitted via correlation_computed().
    void fetch_correlation(const QStringList& symbols);

    // ── Benchmark data ───────────────────────────────────────────────────────
    /// Fetch daily closes for an arbitrary benchmark ticker (defaults to SPY
    /// for backward compatibility). Emits benchmark_history_loaded(symbol, ...)
    /// AND the legacy spy_history_loaded(...) so older consumers keep working.
    void fetch_benchmark_history(const QString& symbol = "SPY", const QString& period = "1y");
    /// Convenience wrapper: pick a default benchmark from a portfolio currency
    /// (CAD → ^GSPTSE, USD → SPY, GBP → ^FTSE, EUR → ^STOXX50E, AUD → ^AXJO,
    /// INR → ^NSEI). Falls back to SPY for unknown currencies.
    static QString default_benchmark_for_currency(const QString& currency);
    /// Legacy shim — calls fetch_benchmark_history("SPY", period).
    void fetch_spy_history(const QString& period = "1y");

    // ── 1D intraday ──────────────────────────────────────────────────────────
    /// Fetch intraday OHLC bars for a single symbol from yfinance.
    /// Default (1d / 1m) is the classic 1D today's session at 1-minute
    /// resolution. Pass e.g. ("5d", "30m") to drive the 1W focused-symbol
    /// chart at half-hour granularity over the trading week.
    /// Emits symbol_intraday_loaded() on success.
    void fetch_symbol_intraday(const QString& symbol,
                               const QString& period = QStringLiteral("1d"),
                               const QString& interval = QStringLiteral("1m"));

    /// Build today's aggregate portfolio-NAV curve by fetching 1-minute bars
    /// for every holding and summing (qty × close) at each shared timestamp.
    /// Emits portfolio_intraday_loaded() once all per-symbol fetches return.
    void fetch_portfolio_intraday(const QString& portfolio_id);

    /// Fetch analyst targets, P/E, dividend yield and consensus for every
    /// holding (one yfinance "info" call per symbol), compute MV-weighted
    /// aggregates, and emit portfolio_fundamentals_loaded().
    void fetch_portfolio_fundamentals(const QString& portfolio_id);

    // ── Risk-free rate ────────────────────────────────────────────────────────
    /// Fetch the current 10-year Treasury yield (DGS10) from FRED.
    /// Result is cached 24h in SettingsRepository. Emits risk_free_rate_loaded(rate).
    void fetch_risk_free_rate();

    // ── Metrics (async computation) ──────────────────────────────────────────
    void compute_metrics(const portfolio::PortfolioSummary& summary);

    // ── Import / Export ──────────────────────────────────────────────────────
    void export_csv(const QString& portfolio_id, const QString& file_path);
    void export_json(const QString& portfolio_id, const QString& file_path);
    void import_json(const QString& file_path, portfolio::ImportMode mode, const QString& merge_target_id = {});

    // ── Snapshots (performance history) ─────────────────────────────────────
    void load_snapshots(const QString& portfolio_id, int days = 365);

    /// Reconstruct daily NAV from yfinance OHLC for the current holdings and
    /// upsert one row per trading day into portfolio_snapshots. This is what
    /// gives Beta and MDD a real time series on a freshly imported portfolio
    /// — without it both metrics show "—" until the user keeps the app open
    /// across enough days for daily snapshots to accumulate.
    /// Period: e.g. "1mo" / "6mo" / "1y" / "5y" (yfinance period strings).
    /// Emits history_backfilled(portfolio_id, point_count) on success.
    void backfill_history(const QString& portfolio_id, const QString& period = "1y");

    // ── Cache control ────────────────────────────────────────────────────────
    void invalidate_cache(const QString& portfolio_id);

  signals:
    void portfolios_loaded(QVector<portfolio::Portfolio> portfolios);
    void portfolio_created(portfolio::Portfolio portfolio);
    void portfolio_deleted(QString id);

    void summary_loaded(portfolio::PortfolioSummary summary);
    void summary_error(QString portfolio_id, QString error);

    void transactions_loaded(QVector<portfolio::Transaction> transactions);

    void metrics_computed(portfolio::ComputedMetrics metrics);
    void snapshots_loaded(QString portfolio_id, QVector<portfolio::PortfolioSnapshot> snapshots);

    void asset_added(QString portfolio_id);
    void asset_sold(QString portfolio_id);

    void export_complete(QString file_path);
    void import_complete(portfolio::ImportResult result);

    /// Pairwise Pearson correlation matrix keyed by "SYM1|SYM2".
    /// Values in [-1, 1]. Diagonal (self-correlation) = 1.0.
    void correlation_computed(QHash<QString, double> matrix);

    /// SPY daily close history: parallel vectors of ISO date strings and prices.
    /// Kept for back-compat — also fired whenever benchmark_history_loaded fires
    /// with symbol == "SPY" so existing consumers don't break.
    void spy_history_loaded(QStringList dates, QVector<double> closes);

    /// Generalised benchmark history: includes the symbol so chart consumers
    /// can label the overlay correctly when a non-SPY benchmark is requested.
    void benchmark_history_loaded(QString symbol, QStringList dates, QVector<double> closes);

    /// Current 10-year risk-free rate as annual decimal (e.g. 0.043 = 4.3%).
    void risk_free_rate_loaded(double rate);

    /// Fired when backfill_history finishes. point_count is the number of
    /// trading days written (0 on failure).
    void history_backfilled(QString portfolio_id, int point_count);

    /// 1-minute intraday close series for a single symbol. Timestamps are
    /// epoch-ms (so the chart's QDateTimeAxis can plot sub-day resolution
    /// without lossy date-only string conversion).
    void symbol_intraday_loaded(QString symbol, QVector<qint64> timestamps_ms,
                                QVector<double> closes);
    /// 1-minute aggregate-NAV intraday series for a portfolio. Built by
    /// summing (qty × close) across all holdings at each shared timestamp.
    void portfolio_intraday_loaded(QString portfolio_id, QVector<qint64> timestamps_ms,
                                   QVector<double> navs);

    /// MV-weighted analyst fundamentals for the whole portfolio.
    void portfolio_fundamentals_loaded(QString portfolio_id,
                                       portfolio::PortfolioFundamentals fundamentals);

  private:
    PortfolioService();

    void build_summary(const QString& portfolio_id, const QVector<portfolio::PortfolioAsset>& assets,
                       const portfolio::Portfolio& portfolio);

    /// Replace the price-dependent fields in `summary` with the freshest
    /// values from the CacheManager's `market_last:*` entries, then re-derive
    /// every aggregate (per-holding market_value/PnL, totals, weights,
    /// gainers/losers). Used so the disk-cache emit in load_summary() paints
    /// totals computed against current-known prices instead of the
    /// previous session's snapshot — otherwise the user sees a briefly
    /// wrong ribbon while DataHub's per-symbol hydration races to overwrite
    /// the per-holding rows.
    static void refresh_summary_prices_from_market_last(portfolio::PortfolioSummary& summary);

    // ── Summary cache (P11) ──────────────────────────────────────────────────
    // In-memory (not CacheManager) intentionally: PortfolioSummary holds nested
    // Portfolio + QVector<HoldingWithQuote>, which would require a large JSON
    // (de)serializer to persist. The summary is cheap to recompute from quotes
    // on restart, so durability gains nothing. Kept tight with a 5-min TTL.
    struct CachedSummary {
        portfolio::PortfolioSummary summary;
        qint64 timestamp = 0;
    };
    QHash<QString, CachedSummary> summary_cache_;
    QMutex cache_mutex_;
    static constexpr int kCacheTtlSec = 300; // 5 minutes

    // ── SPY cache (for OLS beta in compute_metrics) ──────────────────────────
    // Beta is always computed against SPY regardless of which benchmark is
    // shown on the chart, so the cache keys to "SPY" specifically.
    QStringList spy_dates_cache_;
    QVector<double> spy_closes_cache_;

    // ── Risk-free rate cache (annual decimal, e.g. 0.043) ────────────────────
    double rf_rate_ = 0.04; // default 4% until FRED responds

    // ── Backfill state ───────────────────────────────────────────────────────
    // Per-portfolio guard so compute_metrics doesn't kick off backfill on
    // every refresh tick. Cleared on app restart — that's the explicit retry.
    QSet<QString> backfill_attempted_;

    // Per-portfolio guard for fetch_portfolio_fundamentals. Analyst targets
    // change at most daily; re-fetching on every 20 s summary tick would
    // waste N yfinance calls. Cleared when the portfolio's holdings change
    // (invalidate_cache) so a position add/remove re-fetches.
    QSet<QString> fundamentals_fetched_;

    // Last successfully computed fundamentals, keyed by portfolio_id. When
    // fetch_portfolio_fundamentals short-circuits on the guard above, we
    // re-emit the cached value so a portfolio switch back to a previously
    // loaded portfolio re-populates the heatmap analyst panel instead of
    // leaving it showing whichever portfolio was loaded most recently.
    // Guarded by cache_mutex_ (same as summary_cache_).
    QHash<QString, portfolio::PortfolioFundamentals> fundamentals_cache_;

    // 1D fan-out epoch: each fetch_*_intraday() call increments its stream's
    // counter and the callback captures the value at submit time. If the
    // user switches periods/tickers before the in-flight request lands, the
    // captured epoch no longer matches and the callback drops silently
    // instead of emitting stale data over a newer request. Per-stream so a
    // symbol fetch can't cancel a portfolio fetch and vice versa.
    qint64 symbol_intraday_epoch_    = 0;
    qint64 portfolio_intraday_epoch_ = 0;
};

} // namespace fincept::services
