// src/screens/portfolio/PortfolioTypes.h
#pragma once
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace fincept::portfolio {

// ── Core entities ────────────────────────────────────────────────────────────

struct Portfolio {
    QString id;
    QString name;
    QString owner;
    QString currency = "USD";
    QString description;
    QString created_at;
    QString updated_at;
};

struct PortfolioAsset {
    int id = 0;
    QString portfolio_id;
    QString symbol;
    double quantity = 0;
    double avg_buy_price = 0;
    QString first_purchase_date;
    QString last_updated;
    QString sector; // empty = not yet resolved; filled from import JSON or SectorResolver
};

struct Transaction {
    QString id;
    QString portfolio_id;
    QString symbol;
    QString transaction_type; // BUY, SELL, DIVIDEND, SPLIT
    double quantity = 0;
    double price = 0;
    double total_value = 0;
    QString transaction_date;
    QString notes;
    QString created_at;
};

// ── Live-enriched data ───────────────────────────────────────────────────────

struct HoldingWithQuote {
    // From PortfolioAsset
    QString symbol;
    double quantity = 0;
    double avg_buy_price = 0;
    QString sector;
    QString first_purchase_date; // entry date — anchors the peak-high lookup

    // Live market data
    double current_price = 0;
    double market_value = 0;
    double cost_basis = 0;
    double unrealized_pnl = 0;
    double unrealized_pnl_percent = 0;
    double day_change = 0;
    double day_change_percent = 0;
    double weight = 0; // % of total portfolio
    // Real-time order-book snapshot when the data source reports it.
    // yfinance free-tier returns zeros outside RTH / for illiquid tickers
    // — consumers treat 0 as "unavailable" rather than a live quote.
    double day_high = 0;
    double day_low = 0;
    double day_volume = 0;
    double bid = 0;
    double ask = 0;
    double bid_size = 0;
    double ask_size = 0;

    // Trailing-stop tracking. peak_price is the highest daily high seen since
    // first_purchase_date, max'd against the live price; 0 means "not known
    // yet" (the history fetch hasn't landed, or yfinance has no data for the
    // symbol) and consumers must render a dash rather than 0%.
    // drawdown_from_peak_percent is the drop from that peak to current_price
    // — the blotter's L% column. Always <= 0 by construction.
    double peak_price = 0;
    double drawdown_from_peak_percent = 0;

    // From the transaction-log replay (PortfolioLedger): profit locked in by
    // past sells of this symbol, and cash dividends received. Neither is in
    // unrealized_pnl — a position that took profit is not a position that
    // never had it.
    double realized_pnl = 0;
    double dividend_income = 0;

    // FX: the instrument's trading currency and the rate applied to fold it
    // into the portfolio currency. Per-share display fields (price, avg cost,
    // day change, peak) stay in the INSTRUMENT currency — that is what the
    // exchange prints and what keeps the trailing-stop math coherent — while
    // market_value / cost_basis / P&L aggregates are converted. Before this,
    // AAPL (USD) and RY.TO (CAD) were summed as bare numbers and the result
    // labelled with the portfolio currency.
    QString currency;      // empty = unknown (treated as the portfolio currency)
    double fx_rate = 1.0;  // instrument → portfolio currency multiplier
};

// ── Trailing-stop maths ──────────────────────────────────────────────────────

/// Re-derive drawdown after current_price moved, keeping the stored peak.
/// A price above the stored peak IS the new peak — waiting for tomorrow's
/// daily bar would report a phantom drawdown while the position prints highs.
inline void refresh_drawdown(HoldingWithQuote& h) {
    if (h.peak_price <= 0 || h.current_price <= 0) {
        h.drawdown_from_peak_percent = 0;
        return;
    }
    if (h.current_price > h.peak_price)
        h.peak_price = h.current_price;
    h.drawdown_from_peak_percent = (h.current_price - h.peak_price) / h.peak_price * 100.0;
}

/// Seed the peak from a fetched highest-daily-high (0 = unknown/failed fetch)
/// and derive the drawdown against the holding's current price.
inline void set_peak_high(HoldingWithQuote& h, double peak_high) {
    h.peak_price = peak_high > 0 ? peak_high : 0;
    refresh_drawdown(h);
}

struct PortfolioSummary {
    Portfolio portfolio;
    QVector<HoldingWithQuote> holdings;

    double total_market_value = 0;
    double total_cost_basis = 0;
    double total_unrealized_pnl = 0;
    double total_unrealized_pnl_percent = 0;
    double total_day_change = 0;
    double total_day_change_percent = 0;
    // Portfolio-wide realized P&L and dividend income from the transaction
    // log — INCLUDING fully closed positions, which have no holding row.
    // Before the ledger existed, selling a winner deleted it and its entire
    // gain vanished from every total.
    double total_realized_pnl = 0;
    double total_dividend_income = 0;
    int total_positions = 0;
    int gainers = 0;
    int losers = 0;
    QString last_updated;
    // True when this summary was hydrated from the on-disk cache rather
    // than a fresh live computation. Used by the UI to surface a small
    // "CACHED" badge so the user knows the numbers may be stale until
    // the in-flight quote refetch lands and emits a fresh summary.
    bool from_cache = false;
    // True when a cross-currency holding could not be converted (its trading
    // currency is still unknown, or no FX rate was available) and entered the
    // totals at face value. The totals are then approximate and the UI must
    // say so rather than present them as exact.
    bool fx_incomplete = false;
    // Instrument→portfolio-currency multiplier for EVERY symbol in the
    // transaction log, not just open holdings — closed positions still
    // contribute cash flows that the return math must convert. Absent means
    // "no conversion known"; consumers treat that as 1.0 and should already
    // have set fx_incomplete.
    QHash<QString, double> fx_rates;
};

// ── Computed analytics ───────────────────────────────────────────────────────

// The one metrics engine's output (PortfolioService::compute_metrics). Every
// value is derived from the flow-adjusted daily NAV return series; a metric
// that cannot be computed honestly is absent, and consumers must render a
// dash — the app used to fill these gaps with proxies (cross-holding
// dispersion annualized as "volatility", mean-return-over-a-constant as
// "beta") that were systematically wrong.
struct ComputedMetrics {
    std::optional<double> sharpe;
    std::optional<double> sortino;            // downside deviation vs the live risk-free MAR
    std::optional<double> beta;               // OLS slope vs SPY daily returns
    std::optional<double> alpha;              // annualized OLS intercept, % (only with beta)
    std::optional<double> volatility;         // annualized %
    std::optional<double> max_drawdown;       // %
    std::optional<double> var_95;             // 1-day VaR in currency (historical simulation)
    std::optional<double> cvar_95;            // 1-day CVaR (expected shortfall) in currency
    std::optional<double> risk_score;         // 0-100 composite
    std::optional<double> concentration_top3; // sum of top 3 weights %
    int return_days = 0;                      // observations behind the series-based metrics
};

// ── Snapshot for performance history ─────────────────────────────────────────

struct PortfolioSnapshot {
    int id = 0;
    QString portfolio_id;
    double total_value = 0;
    double total_cost_basis = 0;
    double total_pnl = 0;
    double total_pnl_percent = 0;
    QString snapshot_date;
    // Provenance: "live" rows are real end-of-day valuations from quotes the
    // app actually saw; "backfill" rows are synthetic back-projections built
    // from TODAY'S quantities and cost basis, so they misstate any date
    // before the most recent trade. Backfill may never overwrite "live".
    QString source = QStringLiteral("live");
};

// ── Portfolio-level aggregated analyst / fundamental data ─────────────────────
//
// Computed by PortfolioService::fetch_portfolio_fundamentals. Each numeric
// field is the market-value-weighted average across holdings that reported a
// valid (non-zero) value. Consensus maps each holding's recommendation_key to
// a score (strong_buy=1 … strong_sell=5), takes the MV-weighted average, and
// maps back to a display string.
struct PortfolioFundamentals {
    double tgt_low   = 0;  // weighted analyst low-target NAV
    double tgt_mean  = 0;  // weighted analyst mean-target NAV
    double tgt_high  = 0;  // weighted analyst high-target NAV
    double pe_ratio  = 0;  // weighted trailing P/E
    double div_yield = 0;  // weighted dividend yield (fraction, e.g. 0.014)
    QString consensus;     // "Strong Buy" | "Buy" | "Hold" | "Sell" | "Strong Sell"
    bool has_analyst_data = false;  // false until at least one fetch returns
};

// ── Enums ────────────────────────────────────────────────────────────────────

enum class HeatmapMode { Pnl, Weight, DayChange, Aft };

enum class SortColumn { Symbol, Price, Change, Pnl, PnlPct, Drawdown, Weight, MarketValue };

enum class SortDirection { Asc, Desc };

enum class DetailView {
    AnalyticsSectors,
    PerfRisk,
    Optimization,
    QuantStats,
    ReportsPme,
    Indices,
    RiskMgmt,
    Planning,
    Economics,
    FuturesExtHours
};

// ── Import/Export types ──────────────────────────────────────────────────────

struct PortfolioExportTransaction {
    QString date;
    QString symbol;
    QString type; // BUY, SELL, DIVIDEND, SPLIT
    double quantity = 0;
    double price = 0;
    double total_value = 0;
    QString notes;
};

struct PortfolioExportData {
    QString format_version = "1.0";
    QString portfolio_name;
    QString owner;
    QString currency;
    QString export_date;
    QVector<PortfolioExportTransaction> transactions;
};

enum class ImportMode { New, Merge };

struct ImportResult {
    QString portfolio_id;
    QString portfolio_name;
    int transactions_replayed = 0;
    QStringList errors;
};

} // namespace fincept::portfolio
