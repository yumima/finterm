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

// ── Summary ───────────────────────────────────────────────────────────────────

struct PowerTraderSummary {
    QVector<CongressMember> members;
    QVector<PoliticalTrade> recent_trades;
    QDateTime last_updated;
    bool loaded = false;
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
