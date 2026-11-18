#pragma once
// EarningsCalendarFeed.h — the one reader of the Nasdaq earnings calendar.
//
// The dashboard's EARNINGS CALENDAR (this week, every company, ranked by size)
// and News › WHAT'S ON (large caps and the user's names, a few weeks ahead)
// both read this feed. They used to fetch on their own — the tile straight
// from Nasdaq with its own parser and no cache — so the two could disagree
// about the same week. Now: one whats_on_calendar.py `earnings` run at a time,
// callers whose days it covers coalesced onto it, answered days held for
// kFreshSec, and a day Nasdaq did not answer named as such (never read as a
// day without reports). Each caller filters the full rows its own way.

#include <QDate>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace finterm::earnings_calendar {

struct DaysResult {
    /// Why nothing could be read — a local failure must never be reported as
    /// the source refusing (and the other way round).
    enum class Failure { None, Refused, ScriptFailed };
    bool ok = false;         ///< at least one asked-for weekday was answered
    QJsonArray rows;         ///< every company reporting on the answered days
    QStringList failed_days; ///< ISO weekdays Nasdaq did not answer (rows may be missing)
    int days_total = 0;      ///< weekdays in the asked range
    QString error;           ///< why nothing could be read (empty when ok)
    Failure failure = Failure::None;
};

/// Delivers the rows for weekdays in [start, end] to `cb` on the GUI thread;
/// dropped if `ctx` is destroyed first. `force` (a user's refresh) re-reads
/// days held in memory, never answered by a run that started before it; the
/// script's own per-day cache still applies. A range is clamped to the
/// script's per-call cap (70 weekdays, whats_on_calendar.py MAX_DAYS).
void fetch_days(QObject* ctx, const QDate& start, const QDate& end, std::function<void(const DaysResult&)> cb,
                bool force = false);

} // namespace finterm::earnings_calendar
