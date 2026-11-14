#pragma once
// MacroCalendarFeed.h — the one fetch of ForexFactory's weekly calendar.
//
// The dashboard's ECONOMIC CALENDAR and News › WHAT'S ON both read this feed.
// It rate-limits hard (HTTP 429 after a few requests in minutes), and each
// widget used to fetch on its own, every refresh: two app launches were
// enough to get the calendar refused. One shared fetch, cached on disk for
// kFreshSec, concurrent callers coalesced onto the in-flight request, and the
// last good copy served (marked stale) when the feed refuses. Next week's
// file is appended when published (cached, including its frequent 404).

#include <QJsonArray>
#include <QObject>
#include <QString>

#include <functional>

namespace finterm::macro_calendar {

struct FeedResult {
    bool ok = false;      ///< rows are usable (fresh or last-good)
    bool stale = false;   ///< served from the last good copy after a refusal
    QJsonArray rows;
    QString error;        ///< why the live fetch failed (empty when it did not)
};

/// Delivers the week's rows to `cb` (on the GUI thread). `ctx` scopes the
/// callback: it is dropped if `ctx` is destroyed first. `force` skips the
/// fresh-cache shortcut (a user-pressed refresh) but still coalesces.
void fetch_week(QObject* ctx, std::function<void(const FeedResult&)> cb, bool force = false);

} // namespace finterm::macro_calendar
