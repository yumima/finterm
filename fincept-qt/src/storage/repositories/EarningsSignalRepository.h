// src/storage/repositories/EarningsSignalRepository.h
#pragma once
#include "storage/repositories/BaseRepository.h"

#include <optional>

namespace fincept {

/// One reading of the pre-earnings scorecard, taken on one day, about one
/// upcoming print — and, once that print has happened, what it actually did.
///
/// The observation fields are write-once. Only `resolve()` ever mutates a row,
/// and only the outcome columns.
struct EarningsSignalRecord {
    qint64  id = 0;
    QString symbol;
    qint64  report_ts = 0;        // the print this reading is about
    QString observed_on;          // YYYY-MM-DD in market time
    QString captured_at;          // ISO-8601 UTC
    int     days_to_report = 0;

    QString verdict;              // BUY | HOLD | SELL
    double  score = 0.0;          // -100 … +100
    double  confidence = 0.0;     // 0 … 1
    std::optional<double> setup_score;
    std::optional<double> bar_score;
    // The typical SIZE of this name's print reaction, unsigned — the band, not
    // the call. `predicted_move_pct` below is the signed estimate; keeping the
    // two apart is what stops "this name usually moves 7%" being read back as
    // "we expected it to rise 7%".
    std::optional<double> expected_move_pct;
    std::optional<double> consensus_eps;
    std::optional<double> dispersion_pct;
    std::optional<double> runup_5d_pct;
    std::optional<double> price_at_capture;
    /// The engine's signed point estimate for the next-session move. NULL on
    /// rows written before it existed, and on setups too thin to justify one.
    std::optional<double> predicted_move_pct;

    bool    resolved = false;
    std::optional<double> actual_eps;
    std::optional<double> surprise_pct;
    std::optional<double> actual_move_pct;
    std::optional<bool>   direction_hit;
    QString resolved_at;
};

/// Append-only ledger of what the earnings scorecard said before each print,
/// and what the print then did.
///
/// It can only ever describe reports observed after it shipped: Yahoo's
/// estimates and revisions are a point-in-time snapshot with no history, so
/// the reading a week before a past report is not recoverable. The record
/// starts empty and fills as prints go by.
class EarningsSignalRepository : public BaseRepository<EarningsSignalRecord> {
  public:
    static EarningsSignalRepository& instance();

    /// Write today's reading. A no-op when this symbol/report already has one
    /// for `observed_on` — the tab may be opened many times a day and each
    /// visit must not count as another observation.
    /// Returns the new row id, or 0 when nothing was written.
    qint64 observe(const EarningsSignalRecord& r);

    /// Every reading for a symbol, most recent report first.
    QVector<EarningsSignalRecord> for_symbol(const QString& symbol) const;

    /// Readings about prints that have happened but have no outcome yet.
    QVector<EarningsSignalRecord> unresolved(const QString& symbol) const;

    /// Record what a print did. Fills every unresolved reading about that
    /// report at once — they were all observations of the same event — and
    /// touches only the outcome columns. Returns rows updated.
    ///
    /// `print_day` (ISO yyyy-MM-dd, market time) is the day the print landed,
    /// and it is REQUIRED: only a reading observed strictly before it was a
    /// prediction. Yahoo's schedule routinely lags the real announcement, so
    /// a reading written under a stale "upcoming" date can have been taken
    /// after the numbers were public — settling one of those would launder
    /// hindsight into the hit rate. Rows on or after `print_day` are left
    /// unresolved, and therefore out of every statistic drawn from this
    /// table. The filter lives HERE rather than in the caller because
    /// resolution is per-report: a caller that screens one row cannot stop
    /// this from settling its siblings.
    int resolve(const QString& symbol, qint64 report_ts, std::optional<double> actual_eps,
                std::optional<double> surprise_pct, double actual_move_pct,
                const QString& print_day);

  private:
    static EarningsSignalRecord map_row(QSqlQuery& q);
};

} // namespace fincept
