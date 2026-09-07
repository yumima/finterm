#pragma once
// Data model for Ownership — the per-stock register and the market-wide scans.
//
// Every field here is something a filing or a data provider actually stated.
// The optionality is load-bearing: a Form 4 grant reports no price, and a
// defaulted 0.0 would render as "acquired at $0.00" — a confident number the
// filing never made. std::optional lets the render show a placeholder for
// "not reported" and a real 0 for "reported as zero", which are different
// facts about the same company.
//
// Three sources, three clocks. A 13F position is true as of a QUARTER END and
// public up to 45 days later; a FINRA short-interest reading is true as of a
// SETTLEMENT DATE and public about ten business days later; a Form 4 has a
// TRADE date and a FILED date two business days apart. Every struct carries
// its own dates so that nothing on screen has to be aged by guesswork.

#include <QDate>
#include <QHash>
#include <QJsonArray>
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
    QDate     filed_date;
    QString   code;         ///< SEC transaction code (P, S, A, M, F, …)
    QString   code_label;   ///< human reading of the code
    QString   security;     ///< "Common Stock", "Stock Option", …
    bool      derivative = false;
    /// True only for P and S. Everything else is compensation mechanics or
    /// administrative — a grant vesting is not a director deciding to buy, and
    /// mixing them is what makes naive insider screens useless.
    bool      open_market = false;
    bool      acquired = false;   ///< direction as filed (A vs D)
    /// A 10% beneficial owner is a holder, not an insider in the sense the
    /// evidence is about: their purchase ranking inverts (Lakonishok & Lee),
    /// so they are shown and never scored.
    bool      ten_percent_owner = false;
    /// The filing's 10b5-1 checkbox. A plan trade was decided months before it
    /// printed. Absent on pre-2023 filings — unknown, which is not false.
    std::optional<bool> plan_10b5_1;
    QString   source_url;         ///< the filing this row was read from

    std::optional<double> shares;
    std::optional<double> price;
    std::optional<double> value;             ///< shares x price, when both filed
    std::optional<double> shares_held_after;

    /// What the stock did AFTER the trade, from the daily close on the trade
    /// date. Shown after the fact, never projected: absent until the window
    /// has elapsed and the prices are in hand.
    std::optional<double> ret_1w;
    std::optional<double> ret_1m;
    std::optional<double> ret_3m;

    /// A decision about price by someone whose decisions carry information.
    bool scorable_buy() const {
        return open_market && acquired && !derivative && !ten_percent_owner &&
               !plan_10b5_1.value_or(false);
    }
};

/// One insider, with the multi-year view used to classify them.
struct InsiderProfile {
    QString insider;
    Pattern pattern = Pattern::Unclassified;
    int     trades = 0;
    int     trades_in_window = 0;
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
    QString form;               ///< SC 13D, SC 13G, with /A for amendments
    bool    activist = false;   ///< 13D declares intent to influence; 13G is passive
    bool    amendment = false;
    QDate   filed_date;
    QString filer;              ///< who took the stake, from the submission header
    bool    subject_verified = true;
    QString url;
};

/// One 13F filer's position in the security, from the local index.
struct Holder {
    QString manager;
    QString cik;
    /// "index" for a named passive complex, "broad" for a book of a thousand
    /// or more names, "focused" otherwise. 13F cannot tell a hedge fund from a
    /// pension fund, so the model never claims to.
    QString tier;
    std::optional<double> shares;
    std::optional<double> value;
    std::optional<double> weight;       ///< of the filer's disclosed equity book
    std::optional<double> book_total;
    int position_count = 0;
    /// "new", "added", "trimmed", "held", "first seen", or empty with one quarter.
    QString action;
    std::optional<double> shares_delta;
    std::optional<double> pct_change;
    QString put_call;                   ///< "" for stock; PUT/CALL never counted as long
    bool    is_derivative = false;
    QString note;

    bool is_focused() const { return tier == QLatin1String("focused"); }
};

/// Totals over EVERY filer, computed before the display limit was applied.
struct HoldersSummary {
    QDate quarter;
    QDate prior_quarter;
    int   holder_count = 0;
    int   option_holders = 0;
    double total_shares = 0.0;
    double total_value = 0.0;
    int   buyers = 0;      ///< added shares vs the prior quarter
    int   new_holders = 0; ///< filed last quarter, did not hold
    int   sellers = 0;
    int   exited = 0;      ///< held last quarter, filed this one, do not hold
    double exited_shares = 0.0;
    /// Value share of the ten largest positions, over all filers.
    std::optional<double> top10_share;
    /// Value share held by index and broad books — the part of the register
    /// that rebalances rather than decides.
    std::optional<double> broad_share;
    QString sort;          ///< "value" or "weight" — which ranking the rows are in
    double min_book_value = 0.0;
    QDate partial_quarter; ///< a newer, incomplete quarter exists in the index
    int   partial_filers = 0;

    /// 13F is due 45 days after the quarter it describes.
    QDate filed_by() const { return quarter.isValid() ? quarter.addDays(45) : QDate(); }
};

/// What the market-data vendor reports about the share count and who holds it.
/// Its institutional percentage is shown beside the one computed from the
/// filings, never instead of it.
struct VendorFloat {
    std::optional<double> shares_outstanding;
    std::optional<double> float_shares;
    std::optional<double> held_pct_insiders;
    std::optional<double> held_pct_institutions;
    /// The vendor's short figures, kept only as a fallback for when FINRA is
    /// unreachable.
    std::optional<double> shares_short;
    std::optional<double> short_pct_float;
    std::optional<double> short_ratio;
    QDate short_as_of;
};

/// One FINRA consolidated short-interest reading.
struct ShortReading {
    QDate  settlement;
    QDate  published_after;   ///< roughly when FINRA made it public
    std::optional<double> shares_short;
    std::optional<double> prior;
    std::optional<double> avg_daily_volume;
    std::optional<double> days_to_cover;
    std::optional<double> change_pct;   ///< vs the prior settlement
};

/// A symbol's readings, newest first.
struct ShortHistory {
    QVector<ShortReading> rows;
    QString source;   ///< "finra" or "local cache"
    QString error;
    bool has_data() const { return !rows.isEmpty(); }
    const ShortReading* latest() const { return rows.isEmpty() ? nullptr : &rows.first(); }
};

/// Daily short-sale volume from FINRA — flow, not a position. Kept as one
/// line: the trend of a symbol against its own recent range.
struct ShortVolume {
    QDate  as_of;
    double latest = 0.0;
    double avg_20 = 0.0;
    int    days = 0;
    QString error;
    bool has_data() const { return days > 0; }
};

/// One position line inside a single manager's book (the filer drill).
struct BookPosition {
    QString issuer;
    QString cusip;
    QString security_class;
    QString ticker;    ///< from the index's CUSIP map; empty when unmapped
    std::optional<double> shares;
    std::optional<double> value;
    std::optional<double> weight;
    QString action;
    std::optional<double> shares_delta;
    std::optional<double> pct_change;
    /// Price performance measured from the QUARTER END the filing describes —
    /// not from the filing date. 13F is due 45 days after quarter end, so part
    /// of this window predates disclosure: it is what the disclosed shares did,
    /// never a return a reader could have earned.
    std::optional<double> ret_since_quarter_end;
    std::optional<double> ret_3m;
    std::optional<double> ret_6m;
    bool priced = false;
};

/// The last close ON OR BEFORE @p on, from a [["YYYY-MM-DD", close], ...]
/// series. Quarter ends and anniversaries land on weekends and holidays
/// often enough that an exact-date lookup would silently drop a return.
inline std::optional<double> close_on_or_before(const QJsonArray& series, const QDate& on) {
    std::optional<double> best;
    QDate best_date;
    for (const auto& v : series) {
        const auto row = v.toArray();
        if (row.size() < 2)
            continue;
        const QDate d = QDate::fromString(row.at(0).toString(), Qt::ISODate);
        if (!d.isValid() || d > on)
            continue;
        if (!best || d > best_date) {
            best = row.at(1).toDouble();
            best_date = d;
        }
    }
    return best;
}

/// The same lookup for several dates in ONE pass over the series.
inline QVector<std::optional<double>> closes_on_or_before(const QJsonArray& series,
                                                         const QVector<QDate>& on) {
    QVector<std::optional<double>> best(on.size());
    QVector<QDate> best_date(on.size());
    for (const auto& v : series) {
        const auto row = v.toArray();
        if (row.size() < 2)
            continue;
        const QDate d = QDate::fromString(row.at(0).toString(), Qt::ISODate);
        if (!d.isValid())
            continue;
        const double close = row.at(1).toDouble();
        for (int i = 0; i < on.size(); ++i) {
            if (d > on[i])
                continue;
            if (!best[i] || d > best_date[i]) {
                best[i] = close;
                best_date[i] = d;
            }
        }
    }
    return best;
}

/// Below this share of a filer's book, a value-weighted return describes the
/// sample rather than the book, so it is withheld.
inline constexpr double kMinReturnCoverage = 0.5;

/// A manager's disclosed equity book for one quarter, with the moves that got
/// them there.
struct ManagerBook {
    QString manager;
    QString cik;
    QDate   period;
    QDate   prior_period;
    double  total_value = 0.0;
    int     position_count = 0;
    std::optional<double> book_return_since_quarter_end;
    std::optional<double> book_return_3m;
    double  return_coverage = 0.0;
    QString return_error;
    QVector<BookPosition> positions;
    QVector<BookPosition> exits;
    QString error;
};

/// A 13F filer as a search result.
struct Manager {
    QString name;
    QString cik;
    double  book_value = 0.0;
    int     position_count = 0;
};

/// Everything the per-stock tab shows for one symbol, plus what it could not.
struct OwnershipSnapshot {
    QString symbol;
    QString company;
    QString cik;

    // ── Form 4 and 13D/G, from EDGAR ────────────────────────────────────────
    QVector<InsiderTransaction> transactions;
    QVector<InsiderProfile>     insiders;
    QVector<BeneficialStake>    stakes;
    int  filings_found = 0;
    int  filings_parsed = 0;
    int  filings_truncated = 0;
    int  insider_rows_filed_as_owner = 0;
    QStringList insider_other_issuers;
    int  stakes_filed_by_this_cik = 0;
    int  stakes_unverified = 0;
    int  stakes_truncated = 0;
    int  window_months = 0;
    bool    edgar_ok = false;
    QString edgar_error;
    bool    returns_ok = false;   ///< forward returns on the Form 4 rows landed

    // ── 13F holders, from the local index ───────────────────────────────────
    QVector<Holder> holders;      ///< the display slice, in summary.sort order
    HoldersSummary  summary;
    bool    holders_ok = false;
    QString holders_error;

    // ── Vendor share count and FINRA short interest ─────────────────────────
    VendorFloat  vendor;
    bool    market_ok = false;
    QString market_error;
    ShortHistory short_history;
    ShortVolume  short_volume;
    /// Where SI ÷ 13F shares sits across the ranked universe, when the
    /// market-wide ranking has been computed for the same settlement date.
    std::optional<double> sirio_percentile;
    int sirio_universe = 0;

    bool has_any() const {
        return !transactions.isEmpty() || !stakes.isEmpty() || !holders.isEmpty() ||
               short_history.has_data();
    }
};

// ── Market-wide scans ────────────────────────────────────────────────────────

/// One issuer with open-market insider purchases in the window.
struct InsiderBuyRow {
    QString symbol;
    QString issuer;
    int     insiders = 0;   ///< distinct buyers — the cluster, stated as a count
    int     trades = 0;
    double  value = 0.0;
    double  shares = 0.0;
    std::optional<double> avg_price;
    QDate   first_trade;
    QDate   last_trade;
    QDate   last_filed;
    QStringList roles;
    std::optional<double> stake_increase;   ///< shares bought / shares held before
    bool    any_plan = false;
    bool    any_ten_pct = false;
    int     plan_unknown = 0;   ///< rows read before the 10b5-1 column existed
    std::optional<double> ret_1w;
    std::optional<double> ret_1m;
};

struct InsiderBuyQuery {
    int    days = 30;
    int    min_insiders = 2;
    double min_value = 25'000.0;
    bool   exclude_ten_pct = true;
    bool   exclude_plan = true;
};

struct InsiderBuys {
    InsiderBuyQuery query;
    QVector<InsiderBuyRow> rows;
    int   days_scanned = 0;
    int   days_wanted = 0;
    QDate last_scanned;
    bool  returns_ok = false;
    QString error;
    bool  loaded = false;
};

/// One symbol on one FINRA settlement date, joined to its 13F shares.
struct ShortRankRow {
    QString symbol;
    QString name;
    std::optional<double> shares_short;
    std::optional<double> prior;
    std::optional<double> avg_daily_volume;
    std::optional<double> days_to_cover;
    std::optional<double> change_pct;
    std::optional<double> inst_shares;
    int holders = 0;
    std::optional<double> sirio;   ///< short interest ÷ 13F institutional shares
};

struct ShortRank {
    QDate  settlement;
    QDate  published_after;
    QDate  quarter;          ///< the 13F quarter the denominator came from
    int    symbols = 0;
    int    joined = 0;
    int    funds_dropped = 0;
    QString sort;
    QVector<ShortRankRow> rows;
    QVector<double> sirio_deciles;   ///< 10th..90th percentile cut points
    QString error;
    bool   loaded = false;
};

/// One security's holder-base change between the two indexed quarters.
struct MoverRow {
    QString ticker;
    QString name;
    bool    fund = false;
    int     holders = 0;
    int     holders_prior = 0;
    int     delta_holders = 0;
    int     new_holders = 0;
    int     closed = 0;
    int     added = 0;
    int     reduced = 0;
    double  shares = 0.0;
    double  value = 0.0;
    int     focused_holders = 0;
    std::optional<double> focused_net_shares;
    std::optional<double> top10_share;
};

struct Movers {
    QDate   quarter;
    QDate   prior_quarter;
    QString sort;
    QVector<MoverRow> rows;
    QString error;
    bool    loaded = false;
};

// ── Where ownership reaches the reader: watchlist rows, alerts, a calendar ──

/// One watchlist line, from the three local stores. Absent fields mean the
/// store has never seen the symbol — never zero.
struct WatchRow {
    std::optional<int>    holders;
    std::optional<int>    delta_holders;
    std::optional<double> top10_share;
    std::optional<double> shares_short;
    std::optional<double> days_to_cover;
    std::optional<double> si_change_pct;
    std::optional<double> sirio;
    int    insider_buys = 0;      ///< scorable open-market buys in the window
    int    insider_buyers = 0;
    double insider_buy_value = 0.0;
    QDate  last_insider_buy;
};

struct WatchRows {
    QHash<QString, WatchRow> rows;
    QDate quarter;            ///< 13F quarter the holder figures describe
    QDate settlement;         ///< FINRA settlement the short figures describe
    QDate published_after;
    QDate form4_scanned_to;
    int   days = 30;
    QString error;
    bool  loaded = false;
};

/// A dated event that moves the numbers on the ownership screens.
struct CalendarEntry {
    enum class Kind { Form13FDeadline, FinraPublication, LockupExpiry };
    Kind    kind = Kind::Form13FDeadline;
    QDate   date;             ///< the day the reader is waiting for
    QDate   basis;            ///< quarter end / settlement date / pricing date
    QString symbol;           ///< lock-ups only
    QString company;
};

struct OwnershipCalendar {
    QDate as_of;
    int   days = 90;
    QVector<CalendarEntry> entries;   ///< by date, soonest first
    QString error;
    bool  loaded = false;
};

} // namespace fincept::ownership
