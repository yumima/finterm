#include "services/calendar/CompanyEventsFeed.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include <utility>

namespace finterm::company_events {

namespace {
// + window + symbol-set hash (v3: the key carries the window's end too).
constexpr const char* kKeyPrefix = "whatson:company_events:v3:";
// The script caches each day itself (4 h / 7 d).
constexpr int kFreshSec = 60 * 60;
// Some days unanswered: ask for them again soon.
constexpr int kPartialSec = 10 * 60;
// Cold: ~1 Nasdaq request per weekday of the window, paced — well over the
// default 30 s process deadline the first time.
constexpr int kRunTimeoutMs = 180'000;

struct Waiter {
    QPointer<QObject> ctx;
    std::function<void(const EventsResult&)> cb;
};

struct Run {
    QStringList args;
    bool in_flight = false;
    QVector<Waiter> waiting; ///< answered by the run in flight
    QVector<Waiter> after;   ///< forced while it was out: answered by the next run
};

QHash<QString, Run>& runs() {
    static QHash<QString, Run> r;
    return r;
}

void answer(const Waiter& w, const EventsResult& r) {
    if (!w.ctx)
        return;
    // Queued on the caller's context: after the call returns, and dropped
    // with it.
    QTimer::singleShot(0, w.ctx.data(), [w, r]() { w.cb(r); });
}

void start(const QString& key);

void on_answer(const QString& key, const python::PythonResult& res) {
    EventsResult r;
    const QJsonObject root = QJsonDocument::fromJson(python::extract_json(res.output).toUtf8()).object();
    if (!res.success || root.isEmpty() || root.contains(QLatin1String("error"))) {
        // Dividends AND insider clusters come from this one call.
        r.error = root.value("error").toString(res.success ? QStringLiteral("calendar unreadable")
                                                           : QStringLiteral("script failed"));
        LOG_WARN("CompanyEvents", "whats_on_calendar.py company_events failed: " +
                                      (res.error.isEmpty() ? r.error : res.error).left(200));
    } else {
        r.ok = true;
        r.root = root;
        const bool partial = !root.value("div_days_failed").toArray().isEmpty();
        finterm::CacheManager::instance().put(key,
                                              QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)),
                                              partial ? kPartialSec : kFreshSec, QStringLiteral("whatson"));
    }
    auto it = runs().find(key);
    if (it == runs().end())
        return;
    const auto waiting = std::exchange(it->waiting, {});
    it->in_flight = false;
    if (!it->after.isEmpty()) {
        it->waiting = std::exchange(it->after, {});
        start(key);
    } else {
        runs().erase(it);
    }
    for (const auto& w : waiting)
        answer(w, r);
}

void start(const QString& key) {
    auto& run = runs()[key];
    run.in_flight = true;
    python::PythonRunner::instance().run(
        QStringLiteral("whats_on_calendar.py"), run.args,
        // Deferred: PythonRunner answers a missing script inside run(), while
        // the caller is still inside start().
        [key](python::PythonResult res) {
            QTimer::singleShot(0, QCoreApplication::instance(), [key, res]() { on_answer(key, res); });
        },
        {}, kRunTimeoutMs);
}
} // namespace

void fetch(QObject* ctx, const QDate& start_day, const QDate& end_day, const QStringList& symbols,
           std::function<void(const EventsResult&)> cb, bool force) {
    QStringList syms = symbols;
    syms.removeAll(QString());
    syms.sort();
    syms.removeDuplicates();
    const QString sym_arg = syms.join(QLatin1Char(','));
    const QString key =
        QString::fromLatin1(kKeyPrefix) + start_day.toString(Qt::ISODate) + QLatin1Char(':') +
        end_day.toString(Qt::ISODate) + QLatin1Char(':') +
        QString::fromLatin1(QCryptographicHash::hash(sym_arg.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const Waiter w{ctx, std::move(cb)};

    if (!force) {
        if (const auto hit = finterm::CacheManager::instance().try_get(key)) {
            const QJsonDocument d = QJsonDocument::fromJson(hit->toUtf8());
            if (d.isObject()) {
                answer(w, EventsResult{true, d.object(), {}});
                return;
            }
        }
    }
    auto& run = runs()[key];
    if (run.in_flight) {
        // A forced ask must not be answered by a run that started before it.
        (force ? run.after : run.waiting).append(w);
        return;
    }
    run.args = {QStringLiteral("company_events"), start_day.toString(Qt::ISODate), end_day.toString(Qt::ISODate)};
    if (!sym_arg.isEmpty())
        run.args << sym_arg;
    run.waiting.append(w);
    start(key);
}

} // namespace finterm::company_events
