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

enum class IpoStatus { Unknown, Rumored, Filed, Priced, Listed, Acquired, Withdrawn };

inline QString ipo_status_label(IpoStatus s) {
    switch (s) {
        case IpoStatus::Rumored:  return "Rumored";
        case IpoStatus::Filed:    return "Filed";
        case IpoStatus::Priced:   return "Priced";
        case IpoStatus::Listed:   return "Listed";
        case IpoStatus::Acquired: return "Acquired";
        case IpoStatus::Withdrawn: return "Withdrawn";
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
    /// as_of was NOT disclosed (repPdEnd absent) and has been substituted with
    /// filed_date. N-PORT is filed up to ~60 days after the period it covers,
    /// so an unflagged substitution overstates the mark's freshness by up to
    /// two months. Consumers must render it as an approximation.
    bool    as_of_estimated = false;
    double  shares_held = 0;
    double  fair_value_usd = 0;
    double  mark_pps = 0;     // fair_value / shares
};

/// One special-purpose-vehicle (SPV) Form D filing that raises capital to buy
/// into a single underlying private company on the secondary market. The
/// entity is named after the target (e.g. "HII Anthropic-01",
/// "Hiive ScaleAI Series I"), so the SPV's own Form D becomes a free public
/// proxy for secondary-market interest in that company — the closest an
/// independent app can get to the proprietary order-flow signals that
/// Hiive/Forge/NPM sell. Source: SEC EDGAR Form D (pooled-fund filers).
struct SpvActivity {
    QString underlying_id;       // slug of the target company (join key)
    QString underlying_name;     // "Anthropic", "Scale AI", …
    QString sponsor;             // "Hiive", "Forge", "Destiny", … or "" if unknown
    QString spv_name;            // raw entity name on the filing
    QString cik;                 // SPV's own CIK (not the target's)
    QDate   filed_date;
    double  amount_sold_m   = 0; // $M raised by the SPV so far
    double  amount_offered_m = 0;// $M target
    double  minimum_investment_usd = 0;
    int     num_investors = 0;   // totalNumberAlreadyInvested
    QString edgar_url;
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
    QString status_label;     // "Filed", "Amended", "Priced", "Withdrawn"
    QDate   priced_date;
    QString edgar_url;
    /// True when an RW/AW is the most recent filing on the registration —
    /// the deal was pulled. Not "an RW appears in the history": companies
    /// withdraw and re-file, and a re-filed deal is live again.
    ///
    /// Load-bearing rather than cosmetic. A withdrawing company files an
    /// S-1/A on the way out, so it GAINS amendment count as it dies — and
    /// the amendment-burst signal reads that as "pricing imminent". Without
    /// this flag, cancellation and imminent pricing are indistinguishable.
    bool    withdrawn = false;
    QDate   withdrawn_date;
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
    int    ipo_readiness_score = 0;     // 0..100; only meaningful if available
    /// False when readiness could not be computed from anything other than the
    /// filing record, in which case the score carries NO information and
    /// consumers must render a dash.
    ///
    /// Not defensive plumbing — without it the score is actively misleading.
    /// Of its five terms, the financial one can never fire (nothing populates
    /// Financials) and the maturity/raise terms come only from Form D, which
    /// pipeline-only stubs lack. So for an S-1 stub the only evaluable terms
    /// are "has an S-1" and "has an amendment" — both true — and any scheme
    /// that scores over just those hands every such company an identical top
    /// mark. The earlier absolute version silently capped at 80; rescaling
    /// over available terms made it exactly 100. Both are noise dressed as a
    /// ranking. Absent is the honest answer.
    bool   ipo_readiness_available = false;
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
    double    last_valuation_usd = 0;   // USD; rendered headline. Form D/N-PORT
                                        // can't derive this, so it is set from
                                        // the curated valuation seed (see below).
    // Curated valuation-seed provenance. Overlaid by PreIpoService AFTER
    // recompute_analytics() (which zeroes last_valuation_usd). When seed_as_of
    // is valid the UI shows "as reported · <as_of> · <source>". Never sourced
    // from SEC filings — kept separate so SEC-derived fields stay authoritative.
    QString   seed_source;              // e.g. "Company announcement"
    QString   seed_source_url;
    QDate     seed_as_of;
    QString   seed_round;               // e.g. "Series F", "tender offer"
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
    QVector<SpvActivity>    spv_activity;  // secondary-interest SPVs targeting this name
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
// Withdrawn is its own kind, not an AmendmentBurst variant: the whole point
// is that a pulled deal must not look like an imminent pricing, and sharing
// a kind means sharing the chip label, the colour and the KPI counter — so
// only the sentence would have differed.
enum class SignalKind { MarkUp, MarkDown, NewFiling, AmendmentBurst, PremiumHigh,
                        ReadinessJump, RoundFiled, Withdrawn };

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
