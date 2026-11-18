#pragma once
// CompanyEventsFeed.h — the one reader of the user's company events: ex-dividend
// dates (Nasdaq's dividend calendar) and open-market insider cluster buys (the
// Ownership scan's local Form 4 store), for the user's own symbols over a date
// window — whats_on_calendar.py `company_events`. News › WHAT'S ON reads it.
//
// Same rules as EarningsCalendarFeed next to it: one run per (window, symbol
// set) however many ask, answers on the GUI thread after the call returns,
// and a refusal is never an empty calendar. An answer is held on disk
// (CacheManager) for kFreshSec; one with weekdays the dividend calendar did not
// answer (div_days_failed) for kPartialSec only, so those days are asked again.

#include <QDate>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace finterm::company_events {

struct EventsResult {
    bool ok = false;
    /// {dividends: [...], insiders: [...], insider_last_scanned, days_total,
    /// div_days_failed: [ISO weekdays Nasdaq did not answer]}
    QJsonObject root;
    QString error; ///< why there is no answer (the script's words), when !ok
};

/// The events for `symbols` (empty: insider clusters only — no dividends are
/// read without names) over [start, end]. `cb` runs on the GUI thread after
/// this returns; dropped if `ctx` is destroyed first. `force` (a user's
/// refresh) skips the held answer and is never answered by a run that started
/// before it.
void fetch(QObject* ctx, const QDate& start, const QDate& end, const QStringList& symbols,
           std::function<void(const EventsResult&)> cb, bool force = false);

} // namespace finterm::company_events
