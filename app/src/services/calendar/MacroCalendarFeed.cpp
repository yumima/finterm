#include "services/calendar/MacroCalendarFeed.h"

#include "network/http/HttpClient.h"
#include "storage/cache/CacheManager.h"

#include <QJsonDocument>
#include <QPointer>
#include <QVector>

#include <optional>
#include <utility>

namespace finterm::macro_calendar {

namespace {
constexpr const char* kThisUrl = "https://nfs.faireconomy.media/ff_calendar_thisweek.json";
constexpr const char* kNextUrl = "https://nfs.faireconomy.media/ff_calendar_nextweek.json";
constexpr const char* kFreshKey = "macro:ff:thisweek:v1";
constexpr const char* kLastGoodKey = "macro:ff:thisweek:lastgood:v1";
// Next week's file is published only part of the week (404 otherwise). It is
// what fills the list on a Friday evening or a weekend, when every row of the
// Sunday–Saturday file is past — but a 404 is cached too, so it costs at most
// one request per kNextSec against the feed's rate limit.
constexpr const char* kNextKey = "macro:ff:nextweek:v1";
constexpr int kFreshSec = 30 * 60;          // the feed changes a few times a day
constexpr int kLastGoodSec = 8 * 24 * 3600; // a week's file, plus slack
constexpr int kNextSec = 6 * 3600;

struct Waiter {
    QPointer<QObject> ctx;
    std::function<void(const FeedResult&)> cb;
};
QVector<Waiter>& waiters() {
    static QVector<Waiter> w;
    return w;
}
bool& in_flight() {
    static bool f = false;
    return f;
}
bool& force_pending() {
    static bool f = false;
    return f;
}

QJsonArray parse_rows(const QString& s) {
    const QJsonDocument d = QJsonDocument::fromJson(s.toUtf8());
    return d.isArray() ? d.array() : QJsonArray{};
}
QString to_raw(const QJsonArray& rows) {
    return QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}
std::optional<QJsonArray> cached(const char* key) {
    if (const auto hit = CacheManager::instance().try_get(QString::fromLatin1(key)))
        return parse_rows(*hit);
    return std::nullopt;
}

void deliver(FeedResult r, const QJsonArray& next_rows) {
    for (const auto& v : next_rows)
        r.rows.append(v);
    const auto list = std::exchange(waiters(), {});
    for (const auto& w : list)
        if (w.ctx)
            w.cb(r);
}

// Step 2: append next week (cached, or fetched once), then deliver.
void with_next_week(FeedResult this_week) {
    if (const auto next = cached(kNextKey)) {
        in_flight() = false;
        deliver(std::move(this_week), *next);
        return;
    }
    HttpClient::instance().get(QString::fromLatin1(kNextUrl), [this_week](Result<QJsonDocument> res) {
        in_flight() = false;
        const QJsonArray rows =
            res.is_ok() && res.value().isArray() ? res.value().array() : QJsonArray{}; // 404 → none this window
        CacheManager::instance().put(QString::fromLatin1(kNextKey), to_raw(rows), kNextSec, QStringLiteral("macro"));
        deliver(this_week, rows);
    });
}

// Step 1: this week, from the fresh cache unless forced, else the network
// (last good copy on refusal).
void run() {
    const bool force = std::exchange(force_pending(), false);
    if (!force) {
        if (const auto fresh = cached(kFreshKey); fresh && !fresh->isEmpty()) {
            with_next_week(FeedResult{true, false, *fresh, {}});
            return;
        }
    }
    HttpClient::instance().get(QString::fromLatin1(kThisUrl), [](Result<QJsonDocument> res) {
        auto& cache = CacheManager::instance();
        if (res.is_ok() && res.value().isArray() && !res.value().array().isEmpty()) {
            const QJsonArray rows = res.value().array();
            const QString raw = to_raw(rows);
            cache.put(QString::fromLatin1(kFreshKey), raw, kFreshSec, QStringLiteral("macro"));
            cache.put(QString::fromLatin1(kLastGoodKey), raw, kLastGoodSec, QStringLiteral("macro"));
            with_next_week(FeedResult{true, false, rows, {}});
            return;
        }
        const QString why =
            res.is_ok() ? QStringLiteral("unreadable reply") : QString::fromStdString(res.error()).left(160);
        if (const auto last = cached(kLastGoodKey); last && !last->isEmpty()) {
            with_next_week(FeedResult{true, true, *last, why});
            return;
        }
        in_flight() = false;
        deliver(FeedResult{false, false, {}, why}, {});
    });
}
} // namespace

void fetch_week(QObject* ctx, std::function<void(const FeedResult&)> cb, bool force) {
    if (!force) {
        const auto fresh = cached(kFreshKey);
        const auto next = cached(kNextKey);
        if (fresh && !fresh->isEmpty() && next) {
            FeedResult r{true, false, *fresh, {}};
            for (const auto& v : *next)
                r.rows.append(v);
            cb(r);
            return;
        }
    }
    waiters().append({ctx, std::move(cb)});
    if (force)
        force_pending() = true;
    if (in_flight())
        return; // coalesced onto the request already out
    in_flight() = true;
    run();
}

} // namespace finterm::macro_calendar
