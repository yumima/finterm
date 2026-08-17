#pragma once
// Data model for the OWNERSHIP screen.
//
// Every field here is something a filing or a data provider actually stated.
// The optionality is load-bearing: a Form 4 grant reports no price, and a
// defaulted 0.0 would render as "acquired at $0.00" — a confident number the
// filing never made. std::optional lets the render show a placeholder for
// "not reported" and a real 0 for "reported as zero", which are different
// facts about the same company.

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace fincept::ownership {

/// How an insider's filing history looks over multiple years.
///
/// Cohen, Malloy and Pomorski separate insiders who trade the same calendar
/// month every year from everyone else, and find the predictive content sits
/// almost entirely with the latter. Unclassified is a first-class outcome, not
/// a failure: an insider with one year of filings cannot be tested for an
/// annual pattern, and labelling them anyway would be a guess presented as a
/// finding.
enum class Pattern { Unclassified, Routine, Opportunistic };

/// One line from a Form 4 table.
struct InsiderTransaction {
    QString   insider;      ///< reporting owner name as filed
    QStringList roles;      ///< Director, officer title, 10% owner
    QDate     date;         ///< transaction date, not the filing date
    QString   code;         ///< SEC transaction code (P, S, A, M, F, …)
    QString   code_label;   ///< human reading of the code
    QString   security;     ///< "Common Stock", "Stock Option", …
    bool      derivative = false;
    /// True only for P and S. Everything else is compensation mechanics or
    /// administrative — a grant vesting is not a director deciding to buy, and
    /// mixing them is what makes naive insider screens useless.
    bool      open_market = false;
    bool      acquired = false;   ///< direction as filed (A vs D)
    QString   source_url;         ///< the filing this row was read from
    QDate     filed_date;

    std::optional<double> shares;
    std::optional<double> price;
    std::optional<double> value;             ///< shares x price, when both filed
    std::optional<double> shares_held_after;
};

/// One insider, with the multi-year view used to classify them.
struct InsiderProfile {
    QString insider;
    Pattern pattern = Pattern::Unclassified;
    int     trades = 0;            ///< across their whole filing history
    int     trades_in_window = 0;  ///< within the window this screen fetched
    int     years_observed = 0;
    int     routine_month = 0;     ///< 1-12 when pattern is Routine
    QString reason;                ///< why unclassified, when it is
};

/// A window in which two or more insiders bought on the open market.
struct BuyCluster {
    QDate       start;
    QDate       end;
    QStringList insiders;
    double      total_value = 0.0;
};

/// A 5% beneficial-ownership filing.
///
/// The percentage owned lives in the filing body, which is free-form for most
/// filers, so it is deliberately not extracted — a regex over prose produces a
/// number that looks authoritative and is sometimes wrong. The document link
/// is offered instead.
struct BeneficialStake {
    QString form;          ///< SC 13D, SC 13G, with /A for amendments
    bool    activist = false;   ///< 13D declares intent to influence; 13G is passive
    bool    amendment = false;
    QDate   filed_date;
    QString url;
};

/// A 13F-derived institutional position, as reported by the data provider.
struct InstitutionalHolder {
    QString holder;
    QDate   as_of;                  ///< quarter end the position was reported for
    std::optional<double> pct;      ///< fraction of shares outstanding
    std::optional<double> shares;
    std::optional<double> value;
};

/// Exchange short-interest figures and the float they are measured against.
struct ShortInterest {
    std::optional<double> shares_short;
    std::optional<double> shares_short_prior;
    std::optional<double> short_ratio;        ///< days to cover
    std::optional<double> pct_float;
    std::optional<double> float_shares;
    std::optional<double> shares_outstanding;
    std::optional<double> held_pct_insiders;
    std::optional<double> held_pct_institutions;
    QDate as_of;
    QDate prior_as_of;
};

/// Everything the screen shows for one symbol, plus what it could not show.
struct OwnershipSnapshot {
    QString symbol;
    QString company;
    QString cik;

    QVector<InsiderTransaction> transactions;
    QVector<InsiderProfile>     insiders;
    QVector<BuyCluster>         clusters;
    QVector<BeneficialStake>    stakes;
    QVector<InstitutionalHolder> holders;
    ShortInterest               shorts;

    /// Coverage, so a truncated fetch can say so. A capped window that renders
    /// silently reads as a quiet period when it may be a busy one.
    int  filings_found = 0;
    int  filings_parsed = 0;
    int  filings_truncated = 0;
    int  window_months = 0;

    bool    edgar_ok = false;   ///< the Form 4 / 13D half returned
    bool    market_ok = false;  ///< the holders / short-interest half returned
    QString edgar_error;
    QString market_error;

    bool has_any() const {
        return !transactions.isEmpty() || !stakes.isEmpty() || !holders.isEmpty();
    }
};

} // namespace fincept::ownership
