// src/services/equity/EarningsRefresh.cpp
#include "services/equity/EarningsRefresh.h"

#include "core/util/BarTime.h"

#include <QTimeZone>

#include <algorithm>

namespace finterm::services::equity {

namespace {

const QTimeZone& et_zone() {
    static const QTimeZone kEt("America/New_York");
    return kEt;
}

QDateTime et(const QDate& d, const QTime& t) {
    return QDateTime(d, t, et_zone());
}

bool weekday(const QDate& d) {
    return d.dayOfWeek() <= 5;
}

constexpr int kWatchFastSec = 60;        // in a release slot
constexpr int kWatchSlowSec = 10 * 60;   // between a date-only print's two slots
constexpr int kLandedSec = 3 * 60;       // release out, Yahoo's actual not yet
constexpr int kMinGapSec = 60;           // never sooner than this after a fetch
constexpr int kRecentPrintDays = 3;      // a print this recent is still "the" print

struct Window {
    QDateTime start;     // first moment the release can be out
    QDateTime end;       // give up watching (Yahoo or EDGAR never showed it)
    bool timed = false;  // Yahoo carried a time of day
};

/// The release window of a print announced at `when` (an event instant).
/// A midnight-ET stamp is a bare DATE (the daemon pins calendar dates there).
Window release_window(const QDateTime& when) {
    const QDateTime w = when.toTimeZone(et_zone());
    Window win;
    win.timed = !(w.time().hour() == 0 && w.time().minute() == 0);
    if (win.timed) {
        win.start = w.addSecs(-15 * 60);
    } else {
        win.start = et(w.date(), QTime(6, 30)); // pre-market releases start ~6:30-7:00
    }
    // Two days' grace: a release filed the next morning, a vendor a day late.
    win.end = et(w.date().addDays(2), QTime(20, 0));
    return win;
}

/// Is `t` inside a slot where a release is likely any minute?
bool in_fast_slot(const Window& win, const QDateTime& when, const QDateTime& t) {
    if (win.timed)
        return t >= win.start && t <= when.addSecs(3 * 3600);
    const QDate d = when.toTimeZone(et_zone()).date();
    const QDateTime tt = t.toTimeZone(et_zone());
    if (tt.date() != d)
        return false;
    return (tt >= et(d, QTime(6, 30)) && tt <= et(d, QTime(9, 45))) ||
           (tt >= et(d, QTime(15, 55)) && tt <= et(d, QTime(19, 0)));
}

/// The next fast slot starting after `t`, or invalid.
QDateTime next_fast_slot(const Window& win, const QDateTime& when, const QDateTime& t) {
    if (win.timed)
        return t < win.start ? win.start : QDateTime();
    const QDate d = when.toTimeZone(et_zone()).date();
    for (const QDateTime s : {et(d, QTime(6, 30)), et(d, QTime(15, 55))})
        if (t < s)
            return s;
    return {};
}

} // namespace

QDateTime next_close_after(const QDateTime& t) {
    const QDateTime tt = t.toTimeZone(et_zone());
    QDate d = tt.date();
    if (!(weekday(d) && tt < et(d, QTime(16, 15))))
        d = d.addDays(1);
    while (!weekday(d))
        d = d.addDays(1);
    return et(d, QTime(16, 15));
}

CurrentPrint current_print(const EarningsAnalysis& a, const QDateTime& now) {
    CurrentPrint c;
    const QDate today = core::bartime::market_date_et(now);
    for (const auto& p : a.history) {
        if (p.is_estimate || p.timestamp <= 0)
            continue;
        const QDate d = core::bartime::market_date_et(p.timestamp);
        const qint64 age = d.daysTo(today);
        if (age >= 0 && age <= kRecentPrintDays) {
            c.date = d;
            c.timestamp = p.timestamp;
            c.from_history = true;
            c.reported = p.eps_actual.has_value();
            c.reaction_done = p.reaction_pct.has_value();
            return c;
        }
        break; // newest real print only
    }
    if (a.next.timestamp.has_value()) {
        c.date = core::bartime::market_date_et(*a.next.timestamp);
        c.timestamp = *a.next.timestamp;
    }
    return c;
}

EarningsRefreshPlan plan_earnings_refresh(const EarningsAnalysis& a, const QDateTime& fetched_at,
                                          const QDateTime& now, const QDate& release_seen) {
    const QDateTime floor = fetched_at.addSecs(kMinGapSec);
    const auto refresh = [&](QDateTime at, const QDate& print, const QString& why) {
        return EarningsRefreshPlan{std::max(at, floor), EarningsRefreshMode::Refresh, print, why};
    };
    const auto quiet = [&]() { return refresh(next_close_after(fetched_at), {}, QStringLiteral("daily, after the close")); };

    CurrentPrint cur = current_print(a, now);
    if (cur.from_history && cur.reported) {
        if (!cur.reaction_done)
            return refresh(next_close_after(fetched_at), cur.date,
                           QStringLiteral("the price reaction completes at the close"));
        // Settled — the next print is what is coming.
        cur = CurrentPrint{};
        if (a.next.timestamp.has_value()) {
            cur.date = core::bartime::market_date_et(*a.next.timestamp);
            cur.timestamp = *a.next.timestamp;
        }
    }
    if (!cur.date.isValid())
        return quiet();

    // The print is due or pending (its date passed, no actual yet).
    // EVENT-STAMP: earnings announcement instant (midnight ET = date only).
    const QDateTime when = QDateTime::fromSecsSinceEpoch(cur.timestamp, QTimeZone::UTC);
    const QDate print = cur.date;
    const Window win = release_window(when);

    if (now > win.end)
        return quiet(); // never showed up — the daily refresh carries on

    if (release_seen.isValid() && release_seen == print)
        return refresh(fetched_at.addSecs(kLandedSec), print,
                       QStringLiteral("released — waiting for the reported figures"));

    if (now < win.start) {
        // Before the window: the daily refresh, unless the window opens first.
        const QDateTime close = next_close_after(fetched_at);
        if (close < win.start)
            return refresh(close, print, QStringLiteral("daily, after the close"));
        return EarningsRefreshPlan{win.start, EarningsRefreshMode::Watch, print,
                                   QStringLiteral("release window opens")};
    }

    // Inside the window, still unreported: watch.
    QDateTime at;
    if (in_fast_slot(win, when, now)) {
        at = now.addSecs(kWatchFastSec);
    } else {
        at = now.addSecs(kWatchSlowSec);
        const QDateTime slot = next_fast_slot(win, when, now);
        if (slot.isValid() && slot < at)
            at = slot;
    }
    return EarningsRefreshPlan{at, EarningsRefreshMode::Watch, print, QStringLiteral("watching for the release")};
}

} // namespace finterm::services::equity
