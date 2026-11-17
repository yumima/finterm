// tests/services/test_earnings_refresh.cpp
//
// When the ER Earnings data is refreshed. The two failures this pins:
//   - polling for three months between prints (a refresh every few minutes
//     of six Yahoo calls + three years of bars, for data that changes once
//     a day at most);
//   - showing the pre-print picture after the numbers are out (NVDA,
//     17 Nov 2026: the 8-K was on EDGAR at 13:20 PT and the tab still showed
//     August's release).

#include "services/equity/EarningsRefresh.h"

#include <QtTest/QtTest>

using namespace finterm::services::equity;

namespace {

const QTimeZone kEt("America/New_York");

QDateTime et(int y, int m, int d, int hh, int mm = 0) {
    return QDateTime(QDate(y, m, d), QTime(hh, mm), kEt);
}

/// An analysis whose next print is announced at `when` (an ET instant).
EarningsAnalysis next_at(const QDateTime& when) {
    EarningsAnalysis a;
    a.valid = true;
    a.next.timestamp = when.toSecsSinceEpoch();
    return a;
}

/// A reported quarter (newest first in history).
EarningsPoint reported(const QDateTime& when, bool reaction_done) {
    EarningsPoint p;
    p.timestamp = when.toSecsSinceEpoch();
    p.eps_actual = 1.0;
    if (reaction_done)
        p.reaction_pct = 2.0;
    return p;
}

} // namespace

class TestEarningsRefresh : public QObject {
    Q_OBJECT
  private slots:

    void close_is_next_weekday_after_1615() {
        // Tue 17 Nov 2026 10:00 → same day; 17:00 → Wed; Fri 17:00 → Mon.
        QCOMPARE(next_close_after(et(2026, 11, 17, 10)), et(2026, 11, 17, 16, 15));
        QCOMPARE(next_close_after(et(2026, 11, 17, 17)), et(2026, 11, 18, 16, 15));
        QCOMPARE(next_close_after(et(2026, 11, 20, 17)), et(2026, 11, 23, 16, 15));
        QCOMPARE(next_close_after(et(2026, 11, 21, 12)), et(2026, 11, 23, 16, 15));
    }

    void quiet_months_refresh_once_a_day() {
        // Next print 70 days out: one refresh after today's close, no polling.
        const auto a = next_at(et(2027, 1, 28, 16, 30));
        const auto fetched = et(2026, 11, 18, 10);
        const auto p = plan_earnings_refresh(a, fetched, fetched);
        QCOMPARE(p.mode, EarningsRefreshMode::Refresh);
        QCOMPARE(p.at, et(2026, 11, 18, 16, 15));
    }

    void no_scheduled_print_is_quiet() {
        EarningsAnalysis a;
        a.valid = true;
        const auto fetched = et(2026, 11, 18, 10);
        QCOMPARE(plan_earnings_refresh(a, fetched, fetched).at, et(2026, 11, 18, 16, 15));
    }

    void timed_print_watch_starts_15_min_before() {
        // 16:05 print, fetched in the morning: the close-refresh (16:15) comes
        // AFTER the window opens (15:50), so the watch takes over.
        const auto a = next_at(et(2026, 11, 17, 16, 5));
        const auto fetched = et(2026, 11, 17, 10);
        const auto p = plan_earnings_refresh(a, fetched, fetched);
        QCOMPARE(p.mode, EarningsRefreshMode::Watch);
        QCOMPARE(p.at, et(2026, 11, 17, 15, 50));
        QCOMPARE(p.print, QDate(2026, 11, 17));
    }

    void inside_the_window_checks_every_minute() {
        const auto a = next_at(et(2026, 11, 17, 16, 5));
        const auto now = et(2026, 11, 17, 16, 10);
        const auto p = plan_earnings_refresh(a, et(2026, 11, 17, 15, 50), now);
        QCOMPARE(p.mode, EarningsRefreshMode::Watch);
        QCOMPARE(p.at, now.addSecs(60));
    }

    void date_only_print_watches_both_slots() {
        // NVDA's case: Yahoo had only the DATE (pinned to midnight ET).
        const auto a = next_at(et(2026, 11, 17, 0));
        // Pre-market slot: every minute.
        auto now = et(2026, 11, 17, 7);
        QCOMPARE(plan_earnings_refresh(a, now, now).at, now.addSecs(60));
        // Midday: every 10 minutes…
        now = et(2026, 11, 17, 11);
        QCOMPARE(plan_earnings_refresh(a, now, now).at, now.addSecs(600));
        // …but never past the after-close slot's start.
        now = et(2026, 11, 17, 15, 50);
        QCOMPARE(plan_earnings_refresh(a, now, now).at, et(2026, 11, 17, 15, 55));
        // After-close slot (NVDA's 8-K landed 16:20 ET): every minute.
        now = et(2026, 11, 17, 16, 20);
        QCOMPARE(plan_earnings_refresh(a, now, now).at, now.addSecs(60));
        QCOMPARE(plan_earnings_refresh(a, now, now).mode, EarningsRefreshMode::Watch);
    }

    void release_seen_refreshes_until_yahoo_has_it() {
        const auto a = next_at(et(2026, 11, 17, 0));
        const auto fetched = et(2026, 11, 17, 16, 21);
        const auto p = plan_earnings_refresh(a, fetched, fetched, QDate(2026, 11, 17));
        QCOMPARE(p.mode, EarningsRefreshMode::Refresh);
        QCOMPARE(p.at, fetched.addSecs(180));
    }

    void reported_print_waits_for_the_reaction_close() {
        // Reported Tue after the close; the reaction session is Wed.
        EarningsAnalysis a = next_at(et(2027, 2, 25, 16, 5));
        a.history.append(reported(et(2026, 11, 17, 16, 20), false));
        const auto fetched = et(2026, 11, 17, 16, 40);
        const auto p = plan_earnings_refresh(a, fetched, fetched);
        QCOMPARE(p.mode, EarningsRefreshMode::Refresh);
        QCOMPARE(p.at, et(2026, 11, 18, 16, 15));
        QCOMPARE(p.print, QDate(2026, 11, 17));
        // Once the reaction is in, the next print is 100 days out: quiet.
        a.history[0].reaction_pct = 3.0;
        const auto later = et(2026, 11, 18, 16, 20);
        QCOMPARE(plan_earnings_refresh(a, later, later).at, et(2026, 11, 19, 16, 15));
    }

    void pending_print_is_still_the_print() {
        // Review case: a 16:20 print whose date has passed with no actual on
        // Yahoo yet. Yahoo files it in history as "pending" and `next` jumps
        // to the following quarter — the schedule must stay on THIS print.
        EarningsAnalysis a = next_at(et(2027, 2, 25, 16, 20));
        EarningsPoint pending;
        pending.timestamp = et(2026, 11, 17, 16, 20).toSecsSinceEpoch();
        a.history.append(pending);
        const auto now = et(2026, 11, 17, 16, 23);
        const auto cur = current_print(a, now);
        QCOMPARE(cur.date, QDate(2026, 11, 17));
        QVERIFY(cur.from_history && !cur.reported);
        // Release already seen on EDGAR: refresh every 3 minutes, not tomorrow.
        auto p = plan_earnings_refresh(a, now, now, QDate(2026, 11, 17));
        QCOMPARE(p.mode, EarningsRefreshMode::Refresh);
        QCOMPARE(p.at, now.addSecs(180));
        // Not yet seen: keep watching each minute.
        p = plan_earnings_refresh(a, now, now);
        QCOMPARE(p.mode, EarningsRefreshMode::Watch);
        QCOMPARE(p.at, now.addSecs(60));
    }

    void gives_up_watching_after_two_days() {
        const auto a = next_at(et(2026, 11, 17, 16, 5));
        const auto now = et(2026, 11, 19, 21);
        const auto p = plan_earnings_refresh(a, now, now);
        QCOMPARE(p.mode, EarningsRefreshMode::Refresh);
        QCOMPARE(p.at, et(2026, 11, 20, 16, 15));
    }

    void never_sooner_than_a_minute_after_a_fetch() {
        // A refresh that comes back with the same old data must not loop.
        const auto a = next_at(et(2026, 11, 17, 0));
        const auto fetched = et(2026, 11, 17, 16, 21);
        const auto p = plan_earnings_refresh(a, fetched, fetched.addSecs(-3600), QDate(2026, 11, 17));
        QVERIFY(p.at >= fetched.addSecs(60));
    }

    void stale_cache_is_due_at_once() {
        // Opened Wed morning on a copy cached Mon: Monday's close has passed,
        // so the plan is already due — the scheduler refreshes immediately.
        const auto a = next_at(et(2027, 1, 28, 16, 30));
        const auto fetched = et(2026, 11, 16, 9);
        const auto now = et(2026, 11, 18, 9);
        QVERIFY(plan_earnings_refresh(a, fetched, now).at <= now);
    }
};

QTEST_MAIN(TestEarningsRefresh)
#include "test_earnings_refresh.moc"
