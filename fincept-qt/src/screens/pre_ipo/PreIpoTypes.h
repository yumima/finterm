// src/screens/pre_ipo/PreIpoTypes.h
#pragma once

#include <QDate>
#include <QDateTime>
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

// ── Core structs ──────────────────────────────────────────────────────────────

struct FundingRound {
    QString round_name;            // "Series A", "Series I"
    QDate   date;
    double  amount_usd        = 0;  // millions
    double  implied_valuation  = 0;  // post-money, billions
    double  price_per_share    = 0;  // USD per share at this round (0 if unknown)
    int     shares_thousands   = 0;  // shares sold in thousands; Form D doesn't
                                     // disclose this — always 0 from SEC data
    QStringList lead_investors;
};

struct PrivateCompany {
    QString id;              // slug e.g. "stripe"
    QString name;
    QString sector;
    QString sub_sector;
    QString hq_city;
    QString hq_country;
    QDate   founded;
    IpoStatus ipo_status         = IpoStatus::Unknown;
    double    last_valuation_usd = 0; // billions
    QDate     last_round_date;
    QString   last_round_name;
    QVector<FundingRound> rounds;
    double  revenue_est_usd = 0;  // millions
    int     employee_count  = 0;
    QStringList key_investors;
    QDate   s1_filed_date;
    QString ipo_expected_window;   // "H2 2025", "2026"
    QStringList public_comps;      // ticker symbols
    QStringList tags;              // "ai", "fintech", "defense"
    bool    watched = false;
    QString description;

    // ── Share price data ──────────────────────────────────────────────────────
    double  secondary_market_price   = 0;  // $/share on Forge/EquityZen/CartaX
    QString secondary_market_source;        // "Forge Global", "EquityZen", etc.
    QDate   secondary_market_date;          // date of secondary price observation
    double  implied_share_price      = 0;  // last_valuation ÷ shares_outstanding
    int     shares_outstanding_k     = 0;  // estimated shares outstanding, thousands
    double  form_d_implied_price     = 0;  // Form D: amount_raised ÷ shares_sold
};

struct FormDFiling {
    QString company_name;
    QDate   filed_date;
    double  amount_raised = 0; // millions
    QString exemption;         // "506(b)", "506(c)"
    QString offering_type;
    QString state;
    QString edgar_url;
};

struct S1Filing {
    QString company_name;
    QDate   filed_date;
    double  offering_size_usd = 0;
    QStringList underwriters;
    QString edgar_url;
    bool    is_amendment = false;
};

struct PreIpoSummary {
    QVector<PrivateCompany> companies;
    QVector<FormDFiling>    recent_form_d;
    QVector<S1Filing>       ipo_pipeline;
    QDateTime last_updated;
    bool loaded = false;
};

} // namespace fincept::pre_ipo
