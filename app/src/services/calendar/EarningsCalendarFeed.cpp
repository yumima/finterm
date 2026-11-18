#include "services/calendar/EarningsCalendarFeed.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <utility>

namespace finterm::earnings_calendar {

namespace {
constexpr qint64 kFreshSec = 30 * 60; // reports land a few days ahead; actuals at the open/close
constexpr int kRunTimeoutMs = 180'000; // cold: one Nasdaq call per weekday, 20 s each at worst
// whats_on_calendar.py MAX_DAYS: the script reads at most this many weekdays
// per call. A longer range is split, never silently truncated.
constexpr int kMaxRunWeekdays = 70;

struct Day {
    QJsonArray rows;
    qint64 at = 0;       // when last answered (secs)
    bool failed = false; // the latest run could not answer it (rows, if any, are older)
};

struct Waiter {
    QPointer<QObject> ctx;
    QDate start, end;
    std::function<void(const DaysResult&)> cb;
    bool force = false;
    bool after_run = false; // forced while a run was out: that run's answer predates it
};

struct State {
    QHash<QDate, Day> days;
    QVector<Waiter> waiters;
    bool in_flight = false;
    QDate run_start, run_end;
};
State& st() {
    static State s;
    return s;
}

QVector<QDate> weekdays(const QDate& start, const QDate& end) {
    QVector<QDate> out;
    for (QDate d = start; d.isValid() && d <= end; d = d.addDays(1))
        if (d.dayOfWeek() <= 5)
            out.append(d);
    return out;
}

bool fresh(const QDate& d, qint64 now) {
    const auto it = st().days.constFind(d);
    return it != st().days.constEnd() && !it->failed && now - it->at < kFreshSec;
}

DaysResult assemble(const QDate& start, const QDate& end, DaysResult::Failure why, const QString& error) {
    DaysResult r;
    const auto days = weekdays(start, end);
    r.days_total = static_cast<int>(days.size());
    for (const QDate& d : days) {
        const auto it = st().days.constFind(d);
        // A day the latest run could not answer is named as such, even when
        // an older answer's rows are still shown for it.
        if (it == st().days.constEnd() || it->failed)
            r.failed_days << d.toString(Qt::ISODate);
        if (it != st().days.constEnd())
            for (const auto& v : it->rows)
                r.rows.append(v);
    }
    r.ok = r.days_total == 0 || r.failed_days.size() < r.days_total;
    if (!r.ok) {
        r.failure = why == DaysResult::Failure::None ? DaysResult::Failure::Refused : why;
        r.error = !error.isEmpty() ? error : QStringLiteral("Nasdaq earnings calendar did not answer");
    }
    return r;
}

void pump();

void on_answer(const python::PythonResult& res) {
    auto& s = st();
    const QJsonObject root = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8()).object();
    auto why = DaysResult::Failure::None;
    QString error;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QDate> answered;
    if (!res.success || root.isEmpty() || !root.contains(QLatin1String("days_answered"))) {
        why = DaysResult::Failure::ScriptFailed;
        error = root.value("error").toString(QStringLiteral("couldn't read the earnings calendar"));
        LOG_WARN("EarningsCalendar",
                 "whats_on_calendar.py earnings failed: " + (res.error.isEmpty() ? error : res.error).left(200));
    } else {
        for (const auto& v : root.value("days_answered").toArray())
            answered.insert(QDate::fromString(v.toString(), Qt::ISODate));
        QHash<QDate, QJsonArray> by_day;
        for (const auto& v : root.value("earnings").toArray()) {
            const QDate d = QDate::fromString(v.toObject().value("date").toString(), Qt::ISODate);
            if (answered.contains(d))
                by_day[d].append(v);
        }
        // Only what the answer says it covers. An answered day with no rows
        // is a quiet day (a holiday); a day it does not name was not answered.
        for (const QDate& d : std::as_const(answered))
            s.days.insert(d, Day{by_day.value(d), now, false});
        if (root.contains("error"))
            error = root.value("error").toString();
    }
    // Every day of the run was asked, so one the answer does not name is a
    // failure — named by assemble() even when older rows are still held.
    for (const QDate& d : weekdays(s.run_start, s.run_end))
        if (!answered.contains(d)) {
            auto it = s.days.find(d);
            if (it != s.days.end())
                it->failed = true;
            else
                s.days.insert(d, Day{{}, 0, true});
        }
    // Waiters inside the run are answered now (failed days named); a waiter
    // outside it, or forced after the run began, is served by the next run.
    const auto list = std::exchange(s.waiters, {});
    QVector<Waiter> answer_now;
    for (const auto& w : list) {
        if (!w.ctx)
            continue;
        if (!w.after_run && w.start >= s.run_start && w.end <= s.run_end)
            answer_now.append(w);
        else
            s.waiters.append(Waiter{w.ctx, w.start, w.end, w.cb, w.force, false});
    }
    s.in_flight = false;
    for (const auto& w : answer_now)
        if (w.ctx)
            w.cb(assemble(w.start, w.end, why, error));
    pump();
}

void pump() {
    auto& s = st();
    if (s.in_flight || s.waiters.isEmpty())
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QDate lo, hi;
    QVector<Waiter> ready, waiting;
    for (const auto& w : std::exchange(s.waiters, {})) {
        if (!w.ctx)
            continue;
        bool need = false;
        for (const QDate& d : weekdays(w.start, w.end))
            if (w.force || !fresh(d, now)) {
                need = true;
                break;
            }
        if (!need) {
            ready.append(w);
            continue;
        }
        waiting.append(w);
        lo = lo.isValid() ? std::min(lo, w.start) : w.start;
        hi = hi.isValid() ? std::max(hi, w.end) : w.end;
    }
    s.waiters = waiting;
    if (!s.waiters.isEmpty()) {
        // At most the script's cap per run; waiters past it (each at most the
        // cap long, see fetch_days) are read by the next one.
        const auto run_days = weekdays(lo, hi);
        if (run_days.size() > kMaxRunWeekdays)
            hi = run_days.at(kMaxRunWeekdays - 1);
        s.in_flight = true; // before any callback below can re-enter pump()
        s.run_start = lo;
        s.run_end = hi;
        python::PythonRunner::instance().run_interactive(
            QStringLiteral("whats_on_calendar.py"),
            {QStringLiteral("earnings"), lo.toString(Qt::ISODate), hi.toString(Qt::ISODate)},
            [](python::PythonResult r) { on_answer(r); }, kRunTimeoutMs);
    }
    for (const auto& w : ready)
        if (w.ctx)
            w.cb(assemble(w.start, w.end, DaysResult::Failure::None, {}));
}
} // namespace

void fetch_days(QObject* ctx, const QDate& start, const QDate& end, std::function<void(const DaysResult&)> cb,
                bool force) {
    if (!start.isValid() || !end.isValid() || end < start) {
        cb(DaysResult{true, {}, {}, 0, {}, DaysResult::Failure::None});
        return;
    }
    // One run reads at most kMaxRunWeekdays; a longer ask is clamped to it,
    // so every waiter fits inside some run.
    QDate last = end;
    if (const auto days = weekdays(start, end); days.size() > kMaxRunWeekdays)
        last = days.at(kMaxRunWeekdays - 1);
    st().waiters.append(Waiter{ctx, start, last, std::move(cb), force, force && st().in_flight});
    pump();
}

} // namespace finterm::earnings_calendar
