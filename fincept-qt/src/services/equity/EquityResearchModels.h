// src/services/equity/EquityResearchModels.h
#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace fincept::services::equity {

// ── Symbol search ─────────────────────────────────────────────────────────────
struct SearchResult {
    QString symbol;
    QString name;
    QString exchange;
    QString type;
    QString currency;
    QString industry;
};

// ── Real-time quote ───────────────────────────────────────────────────────────
struct QuoteData {
    QString symbol;
    bool valid = true;
    double price = 0.0;
    double change = 0.0;
    double change_pct = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double prev_close = 0.0;
    double volume = 0.0;
    QString exchange;
    qint64 timestamp = 0;
};

// ── Company fundamentals ──────────────────────────────────────────────────────
struct StockInfo {
    QString symbol;
    bool valid = true;
    QString company_name;
    QString sector;
    QString industry;
    QString description;
    QString website;
    QString country;
    QString currency;
    QString exchange;
    int employees = 0;

    // Valuation
    double market_cap = 0.0;
    double enterprise_value = 0.0;
    double pe_ratio = 0.0;
    double forward_pe = 0.0;
    double peg_ratio = 0.0;
    double price_to_book = 0.0;
    double ev_to_revenue = 0.0;
    double ev_to_ebitda = 0.0;

    // Profitability
    double gross_margins = 0.0;
    double operating_margins = 0.0;
    double ebitda_margins = 0.0;
    double profit_margins = 0.0;
    double roe = 0.0;
    double roa = 0.0;
    double gross_profits = 0.0;

    // Per share / cash
    double book_value = 0.0;
    double revenue_per_share = 0.0;
    double free_cashflow = 0.0;
    double operating_cashflow = 0.0;
    double total_cash = 0.0;
    double total_debt = 0.0;
    double total_revenue = 0.0;

    // Growth
    double earnings_growth = 0.0;
    double revenue_growth = 0.0;

    // Share data
    double shares_outstanding = 0.0;
    double float_shares = 0.0;
    double held_insiders_pct = 0.0;
    double held_institutions_pct = 0.0;
    double short_ratio = 0.0;
    double short_pct_of_float = 0.0;

    // Price range / risk
    double week52_high = 0.0;
    double week52_low = 0.0;
    double avg_volume = 0.0;
    double beta = 0.0;
    double dividend_yield = 0.0;
    double current_price = 0.0;

    // Analyst targets
    double target_high = 0.0;
    double target_low = 0.0;
    double target_mean = 0.0;
    double recommendation_mean = 0.0;
    QString recommendation_key;
    int analyst_count = 0;
};

// ── Historical OHLCV candle ───────────────────────────────────────────────────
struct Candle {
    qint64 timestamp = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    qint64 volume = 0;
};

// ── Financial statements ──────────────────────────────────────────────────────
// period → line_item → value; stored as raw JSON since yfinance returns
// hundreds of heterogeneous line-item names that vary by company.
struct FinancialsData {
    QString symbol;
    // Each entry: (period_string, QJsonObject of line items).
    //
    // These are ANNUAL (fiscal-year) statements — Ticker.financials, not
    // quarterly_financials. The stream was documented as quarterly for a
    // long time and the UI labelled its columns to match, presenting fiscal
    // years as the latest quarter.
    QVector<QPair<QString, QJsonObject>> income_statement;
    QVector<QPair<QString, QJsonObject>> balance_sheet;
    QVector<QPair<QString, QJsonObject>> cash_flow;

    // Trailing twelve months, summed from the four most recent QUARTERS —
    // what an analyst means by "TTM revenue", and what a fiscal-year figure
    // cannot give you mid-year. Flow items only: summing four balance-sheet
    // snapshots would be meaningless. Zero when fewer than four quarters
    // were reported, so a 6- or 9-month total never wears the TTM label.
    double ttm_revenue = 0;
    double ttm_net_income = 0;
    double ttm_operating_income = 0;
    double ttm_ebitda = 0;
    QString ttm_period; // e.g. "2025-09-30 … 2024-12-31", empty when absent
};

// ── Technical indicator signal ────────────────────────────────────────────────
//
// Buy / sell wording, with a caveat worth carrying: scored against a 40-day
// trend label over 178 large caps and twelve years, the verdict behind these
// values agrees with the trend already in place ~96% of the time and with the
// direction of the next 40 days ~53% — barely off a coin flip. These
// names describe a move that has happened; they do not forecast one. Anything
// presenting them onward, the MCP surface especially, should carry that.
enum class TechSignal { StrongBuy, Buy, Neutral, Sell, StrongSell };

struct TechIndicator {
    QString name;
    double value = 0.0;
    TechSignal signal = TechSignal::Neutral;
    QString category; // "trend" | "momentum" | "volatility" | "volume"
    /// Whether this indicator's signal counts toward the composite rating.
    /// False for readings with no directional content of their own (ATR, band
    /// widths) and for the second line of a two-line indicator (MACD signal,
    /// Stoch %D, Aroon Down) — they are displayed, but counting them would let
    /// one indicator vote twice. See TechnicalRating.h.
    bool votes = false;
    /// Weighted bucket this vote lands in ("trend" | "momentum" | "volume").
    /// Not the same as `category`, which is the panel it is displayed under —
    /// CCI is shown with the trend indicators but scored as an oscillator.
    QString rating_bucket;
};

struct TechnicalsData {
    QString symbol;
    QString period;
    /// Bar interval the indicators were computed on ("1d" | "1wk"). Part of
    /// the result's identity: the broadcast watchers match on it, because a
    /// weekly and a daily request can share the same floored period ("10y")
    /// and would otherwise resolve each other's subscriptions.
    QString interval;
    QVector<TechIndicator> trend;
    QVector<TechIndicator> momentum;
    QVector<TechIndicator> volatility;
    QVector<TechIndicator> volume;
    TechSignal overall_signal = TechSignal::Neutral;
    /// Weighted composite in [-1, +1] behind `overall_signal`; drives the gauge.
    double net_score = 0.0;
    /// Per-bucket contributions, or why no rating was produced.
    QString rating_basis;
    /// Set when the Python side lost an indicator stage — the panel is thinner
    /// than it looks and the user should be told rather than shown a confident
    /// rating computed from the survivors.
    QString data_warning;
    int voting_count = 0;
    int strong_buy = 0;
    int buy = 0;
    int neutral = 0;
    int sell = 0;
    int strong_sell = 0;
    /// Unix seconds of the last bar the indicators were read from, straight
    /// off the row's `timestamp` column (exchange-midnight stamp for daily
    /// bars, week-start for weekly). 0 when the rows carried none. Lets the
    /// tab say which bar the verdict describes — including that the bar may
    /// still be forming.
    qint64 last_bar_ts = 0;
};

// ── Peer comparison ───────────────────────────────────────────────────────────
struct PeerData {
    QString symbol;
    QString name;
    QString sector;
    double market_cap = 0.0;
    double pe_ratio = 0.0;
    double forward_pe = 0.0;
    double price_to_book = 0.0;
    double price_to_sales = 0.0;
    double peg_ratio = 0.0;
    double roe = 0.0;
    double roa = 0.0;
    double profit_margin = 0.0;
    double operating_margin = 0.0;
    double gross_margin = 0.0;
    double revenue_growth = 0.0;
    double earnings_growth = 0.0;
    double debt_to_equity = 0.0;
    double current_ratio = 0.0;
    double quick_ratio = 0.0;
    double dividend_yield = 0.0;
    double beta = 0.0;
    double price = 0.0;
    double change_pct = 0.0;
};

// ── News article ──────────────────────────────────────────────────────────────
struct NewsArticle {
    QString title;
    QString description;
    QString url;
    QString publisher;
    QString published_date;
};

// ── Optional market sentiment snapshot ──────────────────────────────────────
struct SentimentSourceSnapshot {
    QString source_id;
    QString label;
    bool available = false;
    double buzz_score = 0.0;
    double bullish_pct = 0.0;
    double sentiment_score = 0.0;
    double activity_count = 0.0;
};

struct MarketSentimentSnapshot {
    QString symbol;
    bool configured = false;
    bool available = false;
    QString status;
    QString message;
    double average_buzz = 0.0;
    double average_bullish_pct = 0.0;
    int coverage = 0;
    QString source_alignment;
    QVector<SentimentSourceSnapshot> sources;
    QString fetched_at;
};

// ── Earnings event ───────────────────────────────────────────────────────────
// Single earnings announcement — used to draw markers on the price chart.
// Surprise percentage is signed: positive = beat, negative = miss.
struct EarningsEvent {
    qint64 timestamp = 0;          // unix seconds at announcement
    double eps_estimate = 0.0;     // analyst consensus
    double eps_actual = 0.0;       // reported (0 for upcoming)
    double surprise_pct = 0.0;     // (actual - estimate) / |estimate| × 100
    bool   has_estimate = false;
    bool   has_actual = false;
    bool   has_surprise = false;
};

// ── Earnings analysis (ER "Earnings" tab) ───────────────────────────────────
// Backed by the daemon's `earnings_analysis` action. Unlike the older models
// above, these use std::optional rather than a 0.0 sentinel: a 0% surprise, a
// 0.00 EPS estimate and a flat price reaction are all real, meaningful values
// here, and collapsing them into "missing" would silently corrupt the
// scorecard that reads them.

/// One reported quarter: what was expected, what landed, and how the stock
/// actually traded on the print.
struct EarningsPoint {
    qint64 timestamp = 0;                 // unix seconds at announcement
    // The coming quarter, carried in the same shape with consensus standing
    // in for the actual, so a week before the print the row is already there
    // with its expected QoQ/YoY. Never scored — a forecast is not a track
    // record — and rendered as provisional wherever it appears.
    bool is_estimate = false;
    std::optional<double> eps_estimate;
    std::optional<double> eps_actual;
    std::optional<double> surprise_pct;   // signed: + = beat
    /// True when the surprise is an accounting artefact rather than a beat.
    ///
    /// Yahoo's reported EPS is GAAP; the estimate it is subtracted from is the
    /// street's adjusted consensus. For a company with material one-offs the
    /// two are not the same measure, and the difference describes an accounting
    /// event: GOOG's July 2026 print reads +213%, META's October 2025 tax
    /// charge reads -84%. Across 3,857 quarters the surprise figure does track
    /// the next session's move (Spearman +0.114, p~1e-12), but above 100% that
    /// relationship is +0.077 at p=0.32 — noise, as a basis mismatch predicts.
    /// Such quarters are still displayed and still marked; they are dropped
    /// from the legs that average surprises, so one accounting quarter cannot
    /// set a company's whole track record.
    bool surprise_suspect = false;
    // Sequential and year-ago change in reported EPS. QoQ carries the
    // company's seasonality (a March quarter is "down" against December every
    // year); YoY is the seasonality-free version. Neither feeds the score —
    // the UI plots them against reaction_pct and reports the measured
    // correlation so the reader can see which one tracks for this name.
    std::optional<double> eps_qoq_pct;
    std::optional<double> eps_yoy_pct;
    std::optional<double> reaction_pct;   // close-to-close move over the print
    std::optional<double> runup_pct;      // 5 sessions into the print
    // Stdev of daily returns over the 20 sessions before this print, in
    // percent. The forecastable half of a reaction is its SIZE, and a name's
    // own earnings history offers only a dozen observations months apart;
    // this describes the regime the print actually landed in.
    std::optional<double> pre_vol_pct;
    std::optional<double> price_before;
    std::optional<double> price_after;
    // Trailing (is_estimate) row only: where the price sits now against the
    // close after the last completed print, and the price itself. Separate
    // from reaction_pct on purpose — a live, still-moving number must never
    // be counted as a finished observation by the correlations.
    std::optional<double> move_since_last_pct;
    std::optional<double> price_now;
    // True when the trailing row carries a real forward consensus (so it
    // draws a bar); false when it exists only to carry the price to today.
    bool has_forward_estimate = false;
};

/// Consensus for one horizon — "0q" current quarter, "+1q", "0y", "+1y".
struct EarningsEstimateRow {
    QString period;
    QString label;
    std::optional<double> eps_avg, eps_low, eps_high, analysts, year_ago_eps, eps_growth;
    std::optional<double> rev_avg, rev_low, rev_high, year_ago_rev, rev_growth;
};

/// Where consensus sat 7/30/60/90 days ago — the revision trend.
struct EarningsTrendRow {
    QString period;
    QString label;
    std::optional<double> current, d7, d30, d60, d90;
};

/// How many analysts moved their number, and which way.
struct EarningsRevisionRow {
    QString period;
    QString label;
    std::optional<double> up_7d, up_30d, down_7d, down_30d;
};

/// Expected growth for the symbol vs its index, per horizon.
struct EarningsGrowthRow {
    QString period;
    QString label;
    std::optional<double> stock, index;
};

/// The upcoming report.
/// What the option market is pricing for the coming print — the number every
/// professional earnings screen leads with, and the panel's only genuinely
/// forward-looking input (everything else is derived from what the company
/// already did).
///
/// Absent whenever it cannot be computed honestly: no listed options, or no
/// expiry within ten days after the print. That guard matters — an ATM
/// straddle prices every session to expiry, so quoting AAPL's nearest
/// post-earnings expiry (102 days out, 11.3%) as "the earnings move" would be
/// confidently wrong. `event_move_pct` further strips the ordinary days
/// between now and expiry out in quadrature, and is itself optional because
/// the decomposition degenerates on roughly half of real chains; the total is
/// the trustworthy figure.
///
/// Unlike the rest of the tab this cannot be backtested here — there is no
/// historical option-chain source in this stack — so it is presented as the
/// market's pricing, never folded into the scorecard.
struct EarningsImpliedMove {
    QString expiry;
    int days_after_print = 0;
    std::optional<double> total_move_pct;  // ATM straddle ÷ spot
    std::optional<double> event_move_pct;  // total minus ordinary days, in quadrature
    std::optional<double> straddle, spot, strike;
};

struct EarningsNext {
    std::optional<qint64> timestamp;
    // Yahoo publishes a date *range* while the company hasn't confirmed —
    // shown as "estimated" so the countdown isn't read as a fixed date.
    bool is_estimated = true;
    std::optional<double> eps_avg, eps_low, eps_high, analysts;
    std::optional<double> rev_avg, rev_low, rev_high;
    std::optional<double> year_ago_eps, year_ago_rev, eps_growth, rev_growth;
    std::optional<EarningsImpliedMove> implied;
};

/// Valuation context the scorecard weighs the estimates against.
struct EarningsValuation {
    std::optional<double> price, trailing_eps, forward_eps, trailing_pe, forward_pe;
    std::optional<double> target_mean, target_high, target_low;
    std::optional<double> recommendation_mean, analyst_count;
    std::optional<double> earnings_growth, revenue_growth;
    QString recommendation;
};

struct EarningsAnalysis {
    QString symbol;
    QString currency;
    qint64  as_of = 0;
    bool    valid = false;
    EarningsNext next;
    EarningsValuation valuation;
    QVector<EarningsPoint> history;        // newest first
    QVector<EarningsEstimateRow> estimates;
    QVector<EarningsTrendRow> trend;
    QVector<EarningsRevisionRow> revisions;
    QVector<EarningsGrowthRow> growth;
    // Price run-up into the current setup, same close-to-close basis as
    // EarningsPoint::runup_pct so the two compare directly.
    std::optional<double> runup_5d_pct;
    std::optional<double> runup_20d_pct;
    // Longer windows, for racing the price against the consensus number over
    // the same period — the difference is how much of the move was earnings
    // and how much was the multiple.
    std::optional<double> runup_60d_pct;
    std::optional<double> runup_90d_pct;
    // The same run-ups with the index's move removed. A stock up 8% into a
    // print in a week the market rose 7% has not been bid up for its earnings.
    std::optional<double> rel_runup_20d_pct;
    std::optional<double> rel_runup_90d_pct;
    // Distance from the 52-week high; 0 = sitting on it, negative = below.
    std::optional<double> pct_from_52w_high;
    // Same 20-session realised volatility, as it stands going into the coming
    // print.
    std::optional<double> pre_vol_pct;

    /// True when there is nothing earnings-shaped to show (ETF, index, fund).
    bool has_content() const {
        return !history.isEmpty() || !estimates.isEmpty() || next.timestamp.has_value();
    }
};

} // namespace fincept::services::equity

// ── QVariant interop for QueryStore ─────────────────────────────────────────
// QueryStore is type-erased through QVariant. Each parsed-payload type needs
// to be registered with Qt's meta-type system so it can round-trip through a
// QVariant. Outside the namespace because Q_DECLARE_METATYPE injects template
// specializations into the global namespace.
#include <QMetaType>
Q_DECLARE_METATYPE(fincept::services::equity::QuoteData)
Q_DECLARE_METATYPE(fincept::services::equity::StockInfo)
Q_DECLARE_METATYPE(QVector<fincept::services::equity::Candle>)
Q_DECLARE_METATYPE(fincept::services::equity::TechnicalsData)
Q_DECLARE_METATYPE(fincept::services::equity::FinancialsData)
Q_DECLARE_METATYPE(QVector<fincept::services::equity::NewsArticle>)
Q_DECLARE_METATYPE(QVector<fincept::services::equity::PeerData>)
Q_DECLARE_METATYPE(QVector<fincept::services::equity::EarningsEvent>)
Q_DECLARE_METATYPE(fincept::services::equity::EarningsAnalysis)
