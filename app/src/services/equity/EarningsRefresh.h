// src/services/equity/EarningsRefresh.h
#pragma once
#include "services/equity/EquityResearchModels.h"

#include <QDate>
#include <QDateTime>
#include <QString>

namespace finterm::services::equity {

/// When an earnings payload is next expected to CHANGE — the whole refresh
/// policy for the ER Earnings data, as one pure function.
///
/// Earnings data is static for ~90 days and then changes completely within
/// minutes. A fixed TTL is wrong at both ends: a short one re-fetches six
/// Yahoo calls + three years of bars every few minutes for three months, a
/// long one shows the pre-print picture for hours after the numbers are out.
/// So the data is refreshed at the moments it changes, and only then:
///
///   QUIET     once a day, after the close (run-ups, implied move, consensus
///             revisions move daily, nothing faster);
///   WATCH     in the print's release window — from 15 min before a timed
///             print, or across the two US release slots (pre-market,
///             after-close) of a date-only one — a cheap one-request "is the
///             earnings 8-K on EDGAR yet?" check each minute, slower between
///             the slots; the full refresh runs the moment it says yes;
///   LANDED    the release is out but Yahoo hasn't carried the actual yet:
///             a full refresh every few minutes until it does;
///   REACTION  the actual is in; the one-session price reaction completes at
///             the next close, so refresh just after it.
///
/// Times are US market time (ET). Weekends are skipped; exchange holidays
/// are not modelled — the cost is one harmless refresh on a holiday.
enum class EarningsRefreshMode {
    Refresh, ///< refetch the analysis and the links (fresh, past every cache)
    Watch,   ///< ask the cheap release-filed check; refresh only if it says yes
};

struct EarningsRefreshPlan {
    QDateTime at; ///< when (an instant; compare in UTC)
    EarningsRefreshMode mode = EarningsRefreshMode::Refresh;
    QDate print;  ///< the print this is about (ET date); invalid when quiet
    QString why;  ///< for the log and the refresh tooltip
};

/// The print the tab and the schedule are about — ONE definition, shared:
/// the newest non-projected print of the last few days, whether Yahoo has
/// its actual yet or not (a print whose date has passed with no actual sits
/// in history as "pending" — Yahoo lags the release by minutes to a day, and
/// `next` has already moved on to the following quarter); otherwise the next
/// scheduled one.
struct CurrentPrint {
    QDate date;            // ET; invalid when there is no print to be about
    qint64 timestamp = 0;  // the announcement instant (midnight ET = date only)
    bool from_history = false;
    bool reported = false;      // the actual is in
    bool reaction_done = false; // and its one-session reaction is final
};
CurrentPrint current_print(const EarningsAnalysis& a, const QDateTime& now);

/// `fetched_at`: when the analysis was fetched UPSTREAM (its as_of), not
/// when it was read from a cache. `release_seen`: the cheap check has already
/// found this print's release on EDGAR. Never returns a time before
/// `fetched_at` + 1 minute or (for WATCH) before `now`.
EarningsRefreshPlan plan_earnings_refresh(const EarningsAnalysis& a, const QDateTime& fetched_at,
                                          const QDateTime& now, const QDate& release_seen = QDate());

/// The next weekday 16:15 ET strictly after `t`: by then the session's close
/// is in, and a refresh sees the day's final numbers.
QDateTime next_close_after(const QDateTime& t);

} // namespace finterm::services::equity
