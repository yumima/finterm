// src/services/markets/ExtendedHoursService.cpp
#include "services/markets/ExtendedHoursService.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonWorker.h"
#include "services/markets/MarketDataService.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>

namespace finterm::services {

namespace exthours = finterm::screens::widgets::exthours;

namespace {

// One batch quote for the US names (~0.1 s), bars only for futures/FX/non-US.
// Between fetches the live stream moves each reading, so the poll only has to
// catch what the stream can't (a symbol with no print yet, a new close).
constexpr int kPollMs = 60'000;
// A cold Yahoo session (crumb handshake, one stalled symbol) has been measured
// at 40 s for an 18-symbol book.
constexpr int kFetchTimeoutMs = 45'000;
// One quick retry after a failed fetch: the first call of a session pays the
// handshake and can time out on that alone.
constexpr int kRetryMs = 5'000;
// Outside an extended session, refused symbols (or a failed fetch) are asked
// again on a back-off: 5 min, doubling to 30 min. Hammering a source that is
// rate-limiting us is what keeps it rate-limiting us.
constexpr qint64 kOwedBackoffSecs = 5 * 60;
constexpr qint64 kOwedBackoffMaxSecs = 30 * 60;
// How long an answer covers a symbol that leaves the screen and comes back.
constexpr qint64 kCoveredSecs = 10 * 60;
// A fetch slower than this is logged at INFO (with the first answer and any
// refusal); the rest at DEBUG, so the log shows a slow start, not every poll.
constexpr qint64 kSlowAnswerMs = 1500;

/// Whether an extended-hours print can still arrive in the fetched window
/// (exthours::fetch_window_open_at: the market calendar's bell, 13:00 on an
/// early close). Outside it a fetch can only repeat the last answer; overnight
/// prints come through the stream.
bool fetch_window_open() {
    return exthours::fetch_window_open_at(QDateTime::currentDateTime());
}

QString snapshot_path() {
    const QString dir = AppPaths::cache() + "/portfolio_ext";
    QDir().mkpath(dir);
    return dir + "/quotes.json";
}

} // namespace

ExtendedHoursService& ExtendedHoursService::instance() {
    static ExtendedHoursService s;
    return s;
}

ExtendedHoursService::ExtendedHoursService() {
    timer_.setInterval(kPollMs);
    connect(&timer_, &QTimer::timeout, this, [this] { fetch(); });
    connect(&MarketDataService::instance(), &MarketDataService::stream_ticks, this,
            &ExtendedHoursService::on_ticks);
    load_snapshot();
}

void ExtendedHoursService::watch(QObject* owner, const QStringList& symbols) {
    if (!owner)
        return;
    if (symbols.isEmpty()) {
        if (owners_.remove(owner) == 0)
            return;
    } else {
        const auto it = owners_.constFind(owner);
        if (it != owners_.constEnd() && *it == symbols)
            return;
        owners_.insert(owner, symbols);
        if (!hooked_.contains(owner)) {
            hooked_.insert(owner);
            connect(owner, &QObject::destroyed, this, [this, owner] {
                hooked_.remove(owner);
                if (owners_.remove(owner))
                    recompute_watched();
            });
        }
    }
    recompute_watched();
}

void ExtendedHoursService::recompute_watched() {
    QSet<QString> all;
    for (const auto& list : std::as_const(owners_))
        for (const auto& s : list)
            all.insert(s);
    QStringList sorted(all.cbegin(), all.cend());
    std::sort(sorted.begin(), sorted.end());
    if (sorted == watched_)
        return;
    watched_ = sorted;
    watched_set_ = all;
    owed_ &= all;
    MarketDataService::instance().set_stream_extras(this, watched_);
    if (watched_.isEmpty()) {
        timer_.stop();
        return;
    }
    if (!timer_.isActive())
        timer_.start();
    // A symbol nobody has had an answer for yet is fetched now; otherwise the
    // shown values (and the stream) stand until the next poll.
    if (!uncovered(all).isEmpty())
        schedule_fetch();
}

QSet<QString> ExtendedHoursService::uncovered(const QSet<QString>& symbols) const {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QString> out;
    for (const auto& s : symbols) {
        const auto it = answered_at_.constFind(s);
        if (it == answered_at_.constEnd() || now - *it > kCoveredSecs)
            out.insert(s);
    }
    return out;
}

void ExtendedHoursService::schedule_fetch() {
    // Coalesce: several surfaces register in the same event-loop pass at
    // start-up, and they should cost one download, not three.
    if (kick_pending_)
        return;
    kick_pending_ = true;
    QTimer::singleShot(0, this, [this] {
        kick_pending_ = false;
        fetch();
    });
}

void ExtendedHoursService::refresh() {
    answered_at_.clear();
    fetch(/*force=*/true);
}

std::optional<ExtendedHoursService::Quote> ExtendedHoursService::quote(const QString& symbol) const {
    const auto it = ext_.constFind(symbol);
    if (it == ext_.constEnd())
        return std::nullopt;
    return exthours::latest(*it);
}

void ExtendedHoursService::set_state(State s, const QString& error) {
    if (s == state_ && error == error_)
        return;
    state_ = s;
    error_ = error;
    emit state_changed();
}

bool ExtendedHoursService::fetch_could_help() const {
    if (fetch_window_open())
        return true; // prints are landing in the fetched window
    const bool unanswered = !uncovered(watched_set_).isEmpty() || !owed_.isEmpty() || state_ == State::Failed;
    if (!unanswered)
        return false; // every answer is in and nothing new can print
    if (refusal_streak_ == 0)
        return true;
    // The source has been refusing: wait it out instead of feeding the limit.
    const qint64 backoff =
        std::min(kOwedBackoffMaxSecs, kOwedBackoffSecs << std::min(refusal_streak_ - 1, 3));
    return !last_attempt_.isValid() || last_attempt_.secsTo(QDateTime::currentDateTime()) >= backoff;
}

void ExtendedHoursService::fetch(bool force) {
    if (watched_.isEmpty() || in_flight_)
        return;
    if (!force && !fetch_could_help())
        return;

    const QStringList symbols = watched_;
    QJsonArray syms;
    for (const auto& s : symbols)
        syms.append(s);
    QJsonObject payload;
    payload.insert(QStringLiteral("symbols"), syms);

    in_flight_ = true;
    last_attempt_ = QDateTime::currentDateTime();
    set_state(State::Loading, error_);
    QPointer<ExtendedHoursService> guard(this);
    // Submit → answer, queue wait included: the number a user feels as "the
    // EXT column fills last". In the log so a slow start can be told apart
    // from a slow source.
    QElapsedTimer clock;
    clock.start();
    python::PythonWorker::instance().submit(
        QStringLiteral("extended_hours"), payload,
        [guard, symbols, clock](bool ok, QJsonObject result, QString err) {
            if (!guard)
                return;
            auto* self = guard.data();
            self->in_flight_ = false;
            if (!ok) {
                // Keep what is shown: a failed refresh is no reason to blank
                // a number someone may be reading.
                LOG_WARN("ExtendedHours", QString("fetch of %1 symbols failed after %2 ms: %3")
                                              .arg(symbols.size())
                                              .arg(clock.elapsed())
                                              .arg(err.left(120)));
                ++self->refusal_streak_;
                self->set_state(State::Failed, err.left(120));
                // One quick retry per streak; after that the back-off rules.
                // It waits its turn behind any fetch already out.
                if (self->refusal_streak_ == 1)
                    QTimer::singleShot(kRetryMs, self, [self] { self->fetch(/*force=*/true); });
                return;
            }
            const QJsonArray rows = result.contains(QStringLiteral("_value"))
                                        ? result.value(QStringLiteral("_value")).toArray()
                                        : result.value(QStringLiteral("data")).toArray();
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            QSet<QString> changed;
            QSet<QString> owed;
            for (const auto& v : rows) {
                const auto o = v.toObject();
                const QString sym = o.value(QStringLiteral("symbol")).toString();
                if (sym.isEmpty())
                    continue;
                const auto effect = exthours::apply_row(self->ext_, o, now);
                if (effect == exthours::RowEffect::Unavailable) {
                    owed.insert(sym);
                    continue;
                }
                if (effect != exthours::RowEffect::Kept)
                    changed.insert(sym);
            }
            // Covered = answered. A refused symbol is owed another ask on the
            // next poll, even in a window where nothing else would be fetched.
            for (const auto& s : symbols)
                if (!owed.contains(s))
                    self->answered_at_.insert(s, now);
            self->owed_ = owed;
            const qint64 ms = clock.elapsed();
            const QString line = QString("%1 symbols answered in %2 ms (%3 refused, %4 changed)")
                                     .arg(symbols.size())
                                     .arg(ms)
                                     .arg(owed.size())
                                     .arg(changed.size());
            if (!self->logged_first_answer_ || ms > kSlowAnswerMs || !owed.isEmpty())
                LOG_INFO("ExtendedHours", line);
            else
                LOG_DEBUG("ExtendedHours", line);
            self->logged_first_answer_ = true;
            self->refusal_streak_ = owed.isEmpty() ? 0 : self->refusal_streak_ + 1;
            self->last_answer_ = QDateTime::currentDateTime();
            self->set_state(State::Idle);
            if (!rows.isEmpty())
                self->save_snapshot();
            if (!changed.isEmpty())
                emit self->quotes_changed(changed);
            // Symbols that arrived while this was out.
            if (!(self->uncovered(self->watched_set_) - owed).isEmpty())
                self->schedule_fetch();
        },
        kFetchTimeoutMs);
}

void ExtendedHoursService::on_ticks(const QVector<StreamTick>& ticks) {
    if (watched_set_.isEmpty())
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSet<QString> changed;
    for (const auto& t : ticks) {
        if (!watched_set_.contains(t.symbol))
            continue;
        if (exthours::apply_print(ext_, t.symbol, t.market_hours, t.price, t.change, t.time_ms, now))
            changed.insert(t.symbol);
    }
    if (!changed.isEmpty())
        emit quotes_changed(changed);
}

// ── Snapshot ────────────────────────────────────────────────────────────────
// So the first paint after a restart shows the last known readings instead of
// dashes for the moment the fetch takes. Only within 12 hours: that spans the
// overnight gap (a 16:30 ET save is still good at the 04:00 ET pre-open)
// without reaching back past a regular session; is_current() then keeps any
// reading older than the last close off the portfolio.


void ExtendedHoursService::save_snapshot() const {
    QJsonArray arr;
    for (auto it = ext_.cbegin(); it != ext_.cend(); ++it) {
        if (it->declined)
            continue; // a decline is re-learnt from the next fetch
        QJsonObject o{{"symbol", it.key()}, {"regular", it->regular}, {"regular_ts", it->regular_ts}};
        for (const auto side : exthours::kSides)
            if (const auto& r = it->at(side))
                o.insert(exthours::ext_side_key(side), exthours::reading_to_json(*r));
        arr.append(o);
    }
    QJsonObject root;
    root.insert("version", 4);
    root.insert("saved_at", QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert("symbols", arr);
    QSaveFile f(snapshot_path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void ExtendedHoursService::load_snapshot() {
    QFile f(snapshot_path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    // Before v4: one value per symbol with no session, not enough to say
    // which session a number is — not shown at all.
    if (!doc.isObject() || doc.object().value("version").toInt() != 4)
        return;
    const auto saved_at = QDateTime::fromString(doc.object().value("saved_at").toString(), Qt::ISODate);
    if (!saved_at.isValid() || saved_at.secsTo(QDateTime::currentDateTime()) > 12 * 3600)
        return;
    for (const auto& v : doc.object().value("symbols").toArray()) {
        const auto o = v.toObject();
        const QString sym = o.value("symbol").toString();
        if (sym.isEmpty())
            continue;
        Sessions x;
        x.regular = o.value("regular").toDouble();
        x.regular_ts = static_cast<qint64>(o.value("regular_ts").toDouble());
        for (const auto side : exthours::kSides)
            x.at(side) = exthours::reading_from_json(o.value(exthours::ext_side_key(side)), 0);
        ext_.insert(sym, x);
    }
}

} // namespace finterm::services
