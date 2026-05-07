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
    int   disclosure_lag_days     = 0;  // disclosure_date - transaction_date
    QString ticker;
    QString asset_name;
    AssetType asset_type;
    TradeDirection direction;
    double amount_low             = 0;
    double amount_high            = 0;
    QString amount_range_label;         // e.g. "$15,001 – $50,000"
    QString committee_relevance;        // overlapping committee name, if any
    double signal_score           = 0;  // 0–100 composite score
    QString source_url;
};

// ── Aggregate view ────────────────────────────────────────────────────────────

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

} // namespace fincept::power_trader
