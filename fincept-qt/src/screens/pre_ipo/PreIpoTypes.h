// src/screens/pre_ipo/PreIpoTypes.h
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::pre_ipo {

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class IpoStatus { Unknown, Rumored, Filed, Priced, Listed, Acquired };

inline QString ipo_status_label(IpoStatus s) {
    switch (s) {
        case IpoStatus::Rumored:  return "Rumored";
        case IpoStatus::Filed:    return "Filed";
        case IpoStatus::Priced:   return "Priced";
        case IpoStatus::Listed:   return "Listed";
        case IpoStatus::Acquired: return "Acquired";
        default:                  return "Unknown";
    }
}

// ── Layered data model ───────────────────────────────────────────────────────
// Each struct is sourced from a single tier so the UI can render provenance
// honestly and analytics know which inputs are present.

struct RelatedPerson {
    QString name;
    QStringList roles; // "executiveOfficer", "director", "promoter"
};

/// One Form D filing. A company typically has multiple over its life.
struct PrimaryRound {
    QString accession;
    QDate   filed_date;
    QDate   first_sale_date;
    double  amount_sold_m   = 0;  // $M actually sold so far
    double  amount_offered_m = 0; // $M target
    QString exemption;            // "06b, 06c"
    QStringList securities_types; // "Equity", "Debt", "Option", "Warrant"
    double  minimum_investment_usd = 0;
    QString round_name_inferred;  // "Series E (est.)"
    QVector<RelatedPerson> related_persons;
    QString edgar_url;
};

/// One mutual fund's quarterly fair-value mark for a private security.
struct FundMark {
    QString fund_name;
    QString fund_cik;
    QString issuer_raw;       // raw issuer string in N-PORT (varies)
    QDate   as_of;            // reporting period end
    QDate   filed_date;
    double  shares_held = 0;
    double  fair_value_usd = 0;
    double  mark_pps = 0;     // fair_value / shares
};

/// Public secondary-market observation (Hiive50, news, manual).
struct SecondaryQuote {
    QString source;           // "Hiive50", "News"
    QDate   as_of;
    double  bid_pps = 0;
    double  ask_pps = 0;
    double  last_pps = 0;
    double  change_90d_pct = 0;
};

/// IPO pipeline status (S-1 / F-1 family).
struct S1Status {
    QString accession;
    QDate   first_filed;
    QDate   latest_amended;
    int     amendment_count = 0;
    QStringList form_types;
    int     days_since_first_filed = 0;
    double  offering_size_m = 0;
    double  est_price_low = 0;
    double  est_price_high = 0;
    long long shares_outstanding = 0;
    QStringList underwriters;
    QString status_label;     // "Filed", "Amended", "Priced", "Listed"
    QDate   priced_date;
    QString edgar_url;
};

/// XBRL-derived annual financials.
struct Financials {
    double revenue_m = 0;
    double revenue_growth_yoy_pct = 0;
    double net_income_m = 0;
    double gross_margin_pct = 0;
    double cash_m = 0;
    QDate  as_of;             // FY end
};

/// Computed analytics layer (derived only).
struct Analytics {
    double consensus_mark_pps = 0;
    double mark_dispersion_pct = 0;
    double mark_drift_vs_last_round_pct = 0;
    double hiive_premium_pct = 0;
    int    ipo_readiness_score = 0;     // 0..100
    int    days_to_price_est = 0;       // 0 if no S-1
    double comp_implied_valuation_b = 0;
    double valuation_gap_pct = 0;
    int    smart_money_index = 0;       // count of crossover funds holding name
    double composite_picks_score = 0;   // ranks Picks tab
};

/// Full per-company aggregate the UI consumes.
struct PrivateCompany {
    QString id;               // slug
    QString cik;              // SEC CIK (padded 10)
    QString name;
    QStringList aliases;
    QString sector;           // industry group from Form D
    QString sub_sector;
    QString hq_city;
    QString hq_state;
    QString hq_country;
    QDate   founded;
    int     employee_count_est = 0;
    QStringList tags;         // free-text: "ai", "fintech", "defense"
    QString description;
    bool    watched = false;

    // Status & legacy back-compat fields populated by analytics ----------------
    IpoStatus ipo_status = IpoStatus::Unknown;
    QString   ipo_expected_window;
    double    last_valuation_usd = 0;   // $B, derived from latest mark or round
    QDate     last_round_date;
    QString   last_round_name;
    double    revenue_est_usd = 0;      // $M
    double    cumulative_raised_m = 0;
    QStringList key_investors;
    QStringList public_comps;
    QDate     s1_filed_date;

    // Layered data -----------------------------------------------------------
    QVector<PrimaryRound>   rounds;
    QVector<FundMark>       fund_marks;
    QVector<SecondaryQuote> secondary;
    S1Status   s1;
    Financials fin;
    Analytics  analytics;

    // Legacy back-compat fields used by the old detail panel (kept until the
    // UI is fully migrated; populated by the service from the layered data).
    double  secondary_market_price = 0;
    QString secondary_market_source;
    QDate   secondary_market_date;
    double  implied_share_price = 0;
    int     shares_outstanding_k = 0;
    double  form_d_implied_price = 0;
};

struct FormDFiling {
    QString company_name;
    QString cik;
    QDate   filed_date;
    double  amount_raised = 0;  // $M
    QString exemption;
    QString offering_type;
    QString state;
    QString edgar_url;
};

struct S1Filing {
    QString company_name;
    QString cik;
    QDate   filed_date;
    int     amendment_count = 0;
    double  offering_size_usd = 0;
    QStringList underwriters;
    QString edgar_url;
    bool    is_amendment = false;
    // Confirmed IPO date from Nasdaq calendar (overrides the 90-day estimate).
    QDate   actual_ipo_date;
    bool    has_actual_date = false;
    QString ticker;         // proposed ticker symbol (from Nasdaq when available)
    QString price_range;    // proposed price range (from Nasdaq when available)
};

/// Computed market-level signal for the right-rail "Signals" pane.
enum class SignalKind { MarkUp, MarkDown, NewFiling, AmendmentBurst, PremiumHigh, ReadinessJump, RoundFiled };

struct Signal {
    QString    company_id;
    QString    company_name;
    SignalKind kind = SignalKind::NewFiling;
    QString    description;
    QDateTime  at;
};

struct FundEntry {
    QString label;
    QString cik;
};

struct PreIpoSummary {
    QVector<PrivateCompany> companies;
    QVector<FormDFiling>    recent_form_d;
    QVector<S1Filing>       ipo_pipeline;
    QVector<Signal>         signal_list;  // 'signals' is a Qt MOC keyword
    QVector<FundEntry>      funds;
    QDateTime               last_updated;
    bool loaded = false;
};

} // namespace fincept::pre_ipo
