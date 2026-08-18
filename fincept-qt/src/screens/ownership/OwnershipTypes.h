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

/// A tracked 13F manager.
///
/// `style` is not decoration. A position weight in a concentrated long-only
/// book is a conviction statement; the same weight in a hedged multi-strategy
/// book is one leg of a position and means almost nothing on its own. The UI
/// shows the style next to the weight so the reader can tell which they are
/// looking at, rather than the screen implying a confidence the filing cannot
/// support.
struct Manager {
    QString name;
    QString cik;
    QString style;      ///< "concentrated long", "activist", "index complex", …
    bool    user_added = false;
};

/// What one tracked manager did with one security, from their own 13F.
///
/// `weight` is the position as a share of the manager's DISCLOSED EQUITY BOOK
/// — not of their fund. 13F covers long US equities only: no shorts, no bonds,
/// no cash, no leverage. That is the number a flat holder table cannot give,
/// and the caveat that has to travel with it.
struct ManagerPosition {
    QString manager;
    QString cik;
    QString style;
    QString issuer;         ///< issuer name as the manager filed it
    QString cusip;
    QDate   period;         ///< quarter end the position was reported for
    QDate   filed_date;

    std::optional<double> shares;
    std::optional<double> value;
    std::optional<double> weight;       ///< of the manager's disclosed book
    std::optional<double> book_total;
    int position_count = 0;             ///< how many names in their book

    /// "new", "added", "trimmed", "exited", "held", or empty when only one
    /// quarter was available and no comparison could be made.
    QString action;
    std::optional<double> shares_delta;
    std::optional<double> pct_change;

    /// "" for stock, "PUT" or "CALL" for an option line. 13F reports options in
    /// the same table, and a put is a BEARISH position — it must never be
    /// folded into a share count or a weight.
    QString put_call;
    bool    is_derivative = false;
    /// Why a move could not be read as a decision — a filer with no prior
    /// filing has not opened a position, we simply could not see them.
    QString note;
};

/// One position line inside a single manager's book (the BY FIRM view).
struct BookPosition {
    QString issuer;
    QString cusip;
    QString security_class;
    /// Resolved from the index's CUSIP map, so a drill-through is exact rather
    /// than a guess made from the issuer name. Empty when unmapped.
    QString ticker;
    std::optional<double> shares;
    std::optional<double> value;
    std::optional<double> weight;
    /// "new", "added", "trimmed", "held" or "first seen" versus the prior
    /// indexed quarter; empty when only one quarter is indexed.
    QString action;
    std::optional<double> shares_delta;
    std::optional<double> pct_change;
};

/// A manager's disclosed equity book for one quarter, with the moves that got
/// them there.
struct ManagerBook {
    QString manager;
    QString cik;
    QString style;
    QDate   period;
    QDate   filed_date;
    double  total_value = 0.0;
    int     position_count = 0;
    /// How the filing's values were interpreted — SEC moved 13F from thousands
    /// to whole dollars in 2023 and a silent misread is a 1000x error.
    QString value_basis;
    QDate   prior_period;             ///< the quarter this book is diffed against
    QVector<BookPosition> positions;
    /// Names held last quarter and gone this one. They carry no weight and so
    /// have no place in a weight-ordered list, but "what did they get out of"
    /// is half the question the by-firm view answers.
    QVector<BookPosition> exits;
    QString error;
};

/// One holder on the demand scatter.
struct DemandPoint {
    QString manager;
    double  weight = 0.0;        ///< share of THEIR book
    std::optional<double> shares;
    std::optional<double> value;
    std::optional<double> delta; ///< share change vs the prior quarter
    std::optional<double> pct;   ///< absent for a new position — nothing to divide by
    QString action;
    bool    top = false;         ///< among the ten largest, so it is named
    int     rank = 0;
};

/// The two-axis read, as a distribution rather than a verdict.
///
/// Quadrant counts are taken against the MEDIAN holder's weight, because
/// conviction only means something relative to the other holders of the same
/// security. Kept as four counts rather than collapsed into a label: the whole
/// reason this replaced a badge is that the aggregate and the
/// conviction-weighted read can disagree, and both are true.
struct InstitutionalDemand {
    QString symbol;
    QString company;
    QDate   quarter;
    QDate   prior_quarter;

    int holders = 0;
    int buyers = 0;
    int sellers = 0;
    int exited = 0;
    int unchanged = 0;

    int high_add = 0;   ///< above-median weight, added shares
    int high_cut = 0;
    int low_add = 0;
    int low_cut = 0;

    double median_weight = 0.0;
    int    points_truncated = 0;
    /// The widest book still counted as discretionary. An index book's
    /// "change" is a rebalance, not a view, so they are excluded and the
    /// threshold is shown rather than assumed.
    int    max_book_positions = 0;

    QVector<DemandPoint> points;
    QString error;

    bool has_data() const { return holders > 0 && !points.isEmpty(); }
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
    /// Which of the tracked discretionary managers hold this, at what weight in
    /// their own book. Sorted by weight — conviction first, not size first.
    QVector<ManagerPosition>     smart_money;
    bool    smart_money_ok = false;
    QString smart_money_error;
    /// Total filers holding this, before the display limit — so the panel can
    /// say "showing 40 of 7,979" instead of implying 40 is all of them.
    int     holder_universe = 0;
    int     option_holders = 0;
    QDate   index_quarter;
    /// Aggregate across EVERY 13F filer, so institutional ownership can be
    /// computed from the filings instead of taken from a vendor aggregate.
    double  index_shares_held = 0.0;
    /// Quarter the vendor's holder table reports. Compared against
    /// index_quarter: the SEC bulk data sets publish only after the filing
    /// window closes, so the complete source can be a full quarter behind the
    /// shallow one, and the screen has to say which it is showing.
    QDate   vendor_quarter;
    /// The quarter the index diffs against, when two are present.
    QDate   prior_quarter;
    /// What the register did last quarter, counted across every filer rather
    /// than across the rows the panel happens to show.
    int     buyers = 0;
    int     sellers = 0;
    int     exited = 0;
    /// A newer quarter exists in the index but holds only the filers pulled
    /// directly from EDGAR ahead of SEC's bulk data set. Reported, never
    /// substituted: answering "who owns this" from a few hundred of 10,647
    /// filers would be a complete-looking answer that is wrong by orders of
    /// magnitude.
    InstitutionalDemand demand;
    QDate   partial_quarter;
    int     partial_filers = 0;

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
