// src/screens/power_trader/PowerTraderTypes.h
#pragma once
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::power_trader {

// ── Enumerations ──────────────────────────────────────────────────────────────

enum class TradeDirection { Buy, Sell, Exchange, Other };
enum class AssetType { Stock, Option, Bond, ETF, MutualFund, Crypto, Other };
enum class MemberChamber { Senate, House };
enum class BodyFilter    { All, Senate, House, Cabinet };

enum class RankingDimension {
    Alpha,         // alpha vs SPY YTD (portfolio_return - spy_return)
    Return,        // absolute YTD return %
    NetWorth,      // estimated net worth
    NetBuyer,      // net $ purchased in last 90 days (buys - sells)
    Frequency,     // trade count YTD
    AvgSignal,     // average signal score across all trades (committee overlap etc.)
    DisclosureLag, // average days between trade and disclosure filing
    BestPick,      // highest single-position estimated gain %
};

// ── Core entities ─────────────────────────────────────────────────────────────

struct CongressMember {
    QString id;           // slug, e.g. "nancy-pelosi"
    QString full_name;
    QString party;        // "D", "R", "I"
    MemberChamber chamber;
    QString state;
    QString district;     // House only — e.g. "CA-12"
    QStringList committees;
    QDate term_start;
    double estimated_net_worth    = 0;
    int    trade_count_ytd        = 0;
    double portfolio_return_ytd   = 0; // as percent, e.g. 12.4 = +12.4%
    double spy_return_ytd         = 0;
    double alpha_ytd              = 0; // portfolio_return_ytd - spy_return_ytd
    bool   watched                = false;
};

struct PoliticalTrade {
    QString id;
    QString member_id;
    QString member_name;
    QString party;
    MemberChamber chamber;
    QDate transaction_date;
    QDate disclosure_date;
    int   disclosure_lag_days     = 0;
    QString ticker;
    QString asset_name;
    AssetType asset_type;
    TradeDirection direction;
    double amount_low             = 0;
    double amount_high            = 0;
    QString amount_range_label;
    QString committee_relevance;   // overlapping committee name, if any
    double signal_score           = 0;  // 0–100
    QString source_url;
};

// ── Portfolio reconstruction ──────────────────────────────────────────────────
// Congress members disclose amount *ranges* ($15,001–$50,000), not exact shares.
// All portfolio values are *estimated* using the midpoint of each range.

struct MemberHolding {
    QString ticker;
    QString asset_name;
    double  est_cost_basis    = 0;  // sum of (buy midpoints) – (sell midpoints)
    double  current_price     = 0;  // fetched from MarketDataService
    double  est_market_value  = 0;  // current_price × estimated_shares
    double  est_pnl           = 0;  // est_market_value – est_cost_basis
    double  est_pnl_pct       = 0;  // est_pnl / est_cost_basis * 100
    double  est_weight        = 0;  // % of member's total estimated portfolio
    int     buy_count         = 0;
    int     sell_count        = 0;
    bool    committee_overlap = false;
    QString committee_name;         // which committee makes this an insider trade
    QString sector;
};

struct NavPoint {
    QDate  date;
    double est_nav = 0; // cumulative estimated portfolio value at this date
};

struct MemberPortfolio {
    QString member_id;
    QVector<MemberHolding> holdings;      // current estimated positions (net long)
    QVector<NavPoint>       nav_series;   // portfolio value over time (from trade history)
    double est_total_value   = 0;
    double est_total_cost    = 0;
    double est_total_pnl     = 0;
    double est_total_pnl_pct = 0;
    // Pre-computed rankings (set by service, 1 = best)
    int rank_alpha     = 0;
    int rank_return    = 0;
    int rank_net_worth = 0;
    int rank_frequency = 0;
    int rank_signal    = 0;
    int rank_lag       = 0;
    // Derived stats
    double net_buyer_90d     = 0;  // net $ purchased last 90 days
    double avg_signal_score  = 0;
    double avg_lag_days      = 0;
    double best_pick_pnl_pct = 0;  // best single-position gain %
    QString best_pick_ticker;
};

// ── Aggregate / big-picture types ─────────────────────────────────────────────

struct SectorExposure {
    QString sector;
    double  total_est_amount = 0;  // sum of all trade midpoints in this sector
    int     trade_count      = 0;
    int     member_count     = 0;
    QStringList top_tickers;
    QStringList members;           // member IDs trading in this sector
    double  pct_of_all       = 0;  // % of all disclosed trading activity
};

struct CommitteeInsiderSignal {
    QString member_id;
    QString member_name;
    QString party;
    QString committee;
    QString ticker;
    QString sector;
    double  amount_midpoint = 0;
    QDate   transaction_date;
    double  signal_score    = 0;
    int     overlap_trades  = 0;  // trades in committee's sector by this member
    int     total_trades    = 0;  // all trades by this member
    double  overlap_pct     = 0;  // overlap_trades / total_trades * 100
};

struct RankedMember {
    CongressMember member;
    double rank_value = 0;   // the value for the chosen RankingDimension
    QString rank_label;      // formatted display string
    int     rank        = 0; // 1 = best
};

// ── Insider watch entry ───────────────────────────────────────────────────────

struct InsiderWatchEntry {
    QString member_id;
    QString member_name;
    QString party;
    MemberChamber chamber;
    QString state;
    QStringList committees;

    // ── Composite insider score (0–100) ───────────────────────────────────────
    double insider_score     = 0;   // weighted aggregate

    // ── Score components (each 0–100, weighted into insider_score) ────────────
    double cmte_overlap_score  = 0; // trades in committee-relevant sectors
    double timing_score        = 0; // speed of disclosure after trade
    double concentration_score = 0; // % of portfolio in one sector
    double size_score          = 0; // average trade size vs peers
    double pattern_score       = 0; // cluster: multiple members same ticker/window
    double return_score        = 0; // estimated YTD alpha

    // ── Human-readable evidence ────────────────────────────────────────────────
    QString top_ticker;             // highest-signal ticker for this member
    QString top_committee;          // committee driving the overlap
    QString top_sector;             // sector most concentrated in
    int     committee_trades    = 0; // trades with committee relevance
    int     total_trades        = 0;
    double  cmte_overlap_pct    = 0; // committee_trades / total_trades * 100
    double  avg_disclosure_lag  = 0; // average days to disclose
    double  biggest_trade_amt   = 0; // midpoint of largest single trade
    QStringList evidence_bullets;   // 3–5 plain-English red flags
};

// ── Committee group ───────────────────────────────────────────────────────────

struct CommitteeGroup {
    QString     committee;
    int         member_count     = 0;
    int         trade_count      = 0;
    double      total_est_amount = 0;
    double      avg_signal_score = 0;
    double      correlation_pct  = 0;  // % of trades touching committee-relevant tickers
    QStringList member_ids;
    QStringList top_tickers;           // up to 5
    QString     top_sector;
};

// ── Party aggregate stats ─────────────────────────────────────────────────────

struct PartyStats {
    QString party;                     // "D", "R", "I"
    int     member_count      = 0;
    int     trade_count_90d   = 0;
    double  total_disc_90d    = 0;     // estimated $ value of all trades (90d)
    double  avg_signal        = 0;
    double  avg_lag           = 0;
    double  net_bought_90d    = 0;     // net buys – sells ($)
    int     net_buyer_count   = 0;     // members net-buying last 90d
    int     net_seller_count  = 0;
    QVector<SectorExposure> top_sectors;
    QStringList top_tickers;
};

// ── Cabinet (Executive Branch OGE Form 278) ───────────────────────────────────

struct CabinetHolding {
    QString asset_name;
    QString ticker;
    QString asset_type;          // "Stock","Bond","ETF","Fund","Real Estate","Business Interest","Crypto","Other"
    QString sector;
    double  value_min          = 0;
    double  value_max          = 0;
    QString value_range_label;
    bool    is_conflict        = false;  // overlaps regulatory domain
    QString conflict_note;               // plain English e.g. "Treasury Sec oversees banks"
};

struct CabinetMember {
    QString id;
    QString full_name;
    QString title;               // "Secretary of Defense"
    QString department;
    QString party;               // "R" or "D"
    int     disclosure_year    = 0;
    double  est_total_min      = 0;
    double  est_total_max      = 0;
    double  conflict_score     = 0;  // 0–100
    int     conflict_count     = 0;
    QStringList regulated_sectors;
    QVector<CabinetHolding> holdings;
    QStringList conflict_flags;
    QVector<SectorExposure> sector_breakdown;
    QString source_url;
    QString data_source;
};

struct CabinetSummary {
    QVector<CabinetMember> members;
    QDateTime last_updated;
    bool      loaded          = false;
    int       disclosure_year = 0;
    double    total_est_min   = 0;
    double    total_est_max   = 0;
    QString   data_note;
};

// ── Summary ───────────────────────────────────────────────────────────────────

struct PowerTraderSummary {
    QVector<CongressMember> members;
    QVector<PoliticalTrade> recent_trades;
    QDateTime last_updated;
    bool loaded  = false;
};

// ── Signal Builder ────────────────────────────────────────────────────────────

/// Per-trade decomposed factor scores (0–100 each).
/// Committee/size/lag/herd computed from available data.
/// Timing/bill/lobbying/history are reserved for future data integration.
struct TradeFactorScores {
    QString trade_id;
    double  committee = 0;  // trade in committee-regulated sector
    double  size      = 0;  // dollar amount of the trade
    double  lag       = 0;  // disclosure timing (shorter or over-deadline = higher)
    double  herd      = 0;  // coordinated cluster of members same ticker
    double  timing    = 0;  // proximity to legislation (pending Congress.gov)
    double  bill      = 0;  // member sponsored/voted related bill (pending)
    double  lobbying  = 0;  // company lobbied member's committee (pending LDA)
    double  history   = 0;  // member's historical committee-trade record
};

/// User-configurable signal scoring preset.
/// Weights are multipliers: 0 = ignore factor, 1.0 = standard, 2.0 = double.
struct SignalPreset {
    QString id;               // slug (e.g. "default", "cmte_heavy") or uuid for user presets
    QString name;
    bool    builtin  = false; // built-in presets cannot be deleted

    double  w_committee = 1.0;
    double  w_size      = 1.0;
    double  w_lag       = 1.0;
    double  w_herd      = 1.0;
    double  w_timing    = 0.0;   // 0 until Congress.gov integrated
    double  w_bill      = 0.0;
    double  w_lobbying  = 0.0;
    double  w_history   = 0.5;

    /// Compute a 0–100 score from base factors and this preset's weights.
    double apply(const TradeFactorScores& f) const {
        const double weighted =
              w_committee * f.committee
            + w_size      * f.size
            + w_lag       * f.lag
            + w_herd      * f.herd
            + w_timing    * f.timing
            + w_bill      * f.bill
            + w_lobbying  * f.lobbying
            + w_history   * f.history;
        const double total_w =
              w_committee + w_size + w_lag + w_herd
            + w_timing    + w_bill + w_lobbying + w_history;
        return total_w > 0.0 ? qMin(100.0, weighted / total_w) : 0.0;
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

inline QString direction_label(TradeDirection d) {
    switch (d) {
        case TradeDirection::Buy:      return QStringLiteral("BUY");
        case TradeDirection::Sell:     return QStringLiteral("SELL");
        case TradeDirection::Exchange: return QStringLiteral("EXCH");
        case TradeDirection::Other:    return QStringLiteral("OTHER");
    }
    return QStringLiteral("?");
}

inline QString asset_type_label(AssetType t) {
    switch (t) {
        case AssetType::Stock:      return QStringLiteral("Stock");
        case AssetType::Option:     return QStringLiteral("Option");
        case AssetType::Bond:       return QStringLiteral("Bond");
        case AssetType::ETF:        return QStringLiteral("ETF");
        case AssetType::MutualFund: return QStringLiteral("MutFund");
        case AssetType::Crypto:     return QStringLiteral("Crypto");
        case AssetType::Other:      return QStringLiteral("Other");
    }
    return QStringLiteral("?");
}

inline QString chamber_label(MemberChamber c) {
    return c == MemberChamber::Senate ? QStringLiteral("Senate") : QStringLiteral("House");
}

inline QString ranking_label(RankingDimension d) {
    switch (d) {
        case RankingDimension::Alpha:         return QStringLiteral("Alpha vs SPY");
        case RankingDimension::Return:        return QStringLiteral("YTD Return");
        case RankingDimension::NetWorth:      return QStringLiteral("Net Worth");
        case RankingDimension::NetBuyer:      return QStringLiteral("Net Buyer (90d)");
        case RankingDimension::Frequency:     return QStringLiteral("Trade Frequency");
        case RankingDimension::AvgSignal:     return QStringLiteral("Avg Signal Score");
        case RankingDimension::DisclosureLag: return QStringLiteral("Disclosure Lag");
        case RankingDimension::BestPick:      return QStringLiteral("Best Single Pick");
    }
    return {};
}

} // namespace fincept::power_trader
