#include "services/futures/FuturesHub.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/TopicPolicy.h"
#include "services/futures/FuturesService.h"
#include "services/markets/MarketDataService.h"

#include <QDateTime>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace finterm::services::futures {

namespace {
/// A curve not asked for this long has nothing on screen: stop its tape.
constexpr qint64 kActiveMs = 5 * 60'000;
}

FuturesHub& FuturesHub::instance() {
    static FuturesHub h;
    return h;
}

FuturesHub::FuturesHub() : QObject(nullptr) {}

void FuturesHub::ensure_registered_with_hub() {
    if (registered_)
        return;
    registered_ = true;
    qRegisterMetaType<Curve>();
    qRegisterMetaType<QVector<PastCurve>>();
    qRegisterMetaType<CotReport>();
    qRegisterMetaType<SpreadHistory>();
    auto& hub = datahub::DataHub::instance();
    hub.register_producer(this);

    // The batch quote runs ~10 minutes behind; a minute between reads is plenty.
    datahub::TopicPolicy curve;
    curve.ttl_ms = 60'000;
    curve.min_interval_ms = 30'000;
    curve.refresh_timeout_ms = 60'000;
    hub.set_policy_pattern(QStringLiteral("fut:curve:*"), curve);
    // Settlements a week / a month ago do not change during the day.
    datahub::TopicPolicy past;
    past.ttl_ms = 6 * 3600'000;
    past.min_interval_ms = 5 * 60'000;
    past.refresh_timeout_ms = 2 * 60'000;
    hub.set_policy_pattern(QStringLiteral("fut:past:*"), past);
    // The CFTC publishes once a week.
    datahub::TopicPolicy cot;
    cot.ttl_ms = 6 * 3600'000;
    cot.min_interval_ms = 5 * 60'000;
    cot.refresh_timeout_ms = 2 * 60'000;
    hub.set_policy_pattern(QStringLiteral("fut:cot:*"), cot);
    datahub::TopicPolicy spread;
    spread.ttl_ms = 30 * 60'000;
    spread.min_interval_ms = 2 * 60'000;
    spread.refresh_timeout_ms = 2 * 60'000;
    hub.set_policy_pattern(QStringLiteral("fut:spread:*"), spread);

    connect(&MarketDataService::instance(), &MarketDataService::stream_ticks, this, &FuturesHub::on_ticks);
    auto* sweep = new QTimer(this);
    sweep->setInterval(60'000);
    connect(sweep, &QTimer::timeout, this, &FuturesHub::sync_stream);
    sweep->start();
    LOG_INFO("Futures", "FuturesHub registered (fut:curve/past/cot/spread)");
}

QStringList FuturesHub::topic_patterns() const {
    return {QStringLiteral("fut:curve:*"), QStringLiteral("fut:past:*"), QStringLiteral("fut:cot:*"),
            QStringLiteral("fut:spread:*")};
}

void FuturesHub::refresh(const QStringList& topics) {
    for (const auto& t : topics) {
        if (t.startsWith(QLatin1String("fut:curve:")))
            refresh_curve(t.mid(10));
        else if (t.startsWith(QLatin1String("fut:past:")))
            refresh_past(t.mid(9));
        else if (t.startsWith(QLatin1String("fut:cot:")))
            refresh_cot(t.mid(8));
        else if (t.startsWith(QLatin1String("fut:spread:"))) {
            const QStringList legs = t.mid(11).split(QLatin1Char('|'));
            if (legs.size() == 2)
                refresh_spread(legs[0], legs[1]);
        }
    }
}

// ── curve ──────────────────────────────────────────────────────────────────

void FuturesHub::refresh_curve(const QString& root) {
    const QString key = QStringLiteral("curve:") + root;
    if (running_.contains(key))
        return;
    running_.insert(key);
    QPointer<FuturesHub> self = this;
    fetch_curve(root, this, [self, root, key](bool ok, Curve curve, QString err) {
        if (!self)
            return;
        self->running_.remove(key);
        const QString topic = curve_topic(root);
        if (!ok) {
            datahub::DataHub::instance().publish_error(topic, err);
            return;
        }
        // A live print of the front month newer than this read stands: the
        // read is ~10 minutes behind the tape, and the strip must not snap back.
        const auto lv = self->live_.constFind(root);
        if (const int fi = curve.index_of(curve.front);
            fi >= 0 && lv != self->live_.constEnd() && lv->symbol == curve.front && lv->ts > curve.contracts[fi].ts) {
            auto& c = curve.contracts[fi];
            c.price = lv->price;
            c.prev_close = lv->prev_close;
            c.change = lv->change;
            c.pct = lv->pct;
            c.ts = lv->ts;
            c.stale = false;
        }
        self->curves_.insert(root, curve);
        self->live_.remove(root);
        if (const int fi = curve.index_of(curve.front); fi >= 0)
            self->live_.insert(root, curve.contracts[fi]); // the tape is laid on this
        datahub::DataHub::instance().publish(topic, QVariant::fromValue(curve));
        self->sync_stream();
        // The settlements a week / a month ago need these months: a widget
        // that asked for them before the curve was held is answered now, not
        // at the past topic's next interval.
        const QString past = past_topic(root);
        for (const auto& s : datahub::DataHub::instance().stats())
            if (s.topic == past && s.subscriber_count > 0 && s.total_publishes == 0) {
                datahub::DataHub::instance().request(past, /*force=*/true);
                break;
            }
    });
}

void FuturesHub::on_ticks(const QVector<StreamTick>& ticks) {
    // Between reads the front month moves with the tape. A print carries its
    // change against the vendor's previous close, so price and change stay a
    // pair; a print without one moves neither.
    for (const auto& t : ticks) {
        for (auto it = curves_.begin(); it != curves_.end(); ++it) {
            Curve& curve = it.value();
            const int i = curve.index_of(curve.front);
            if (i < 0 || curve.contracts[i].symbol != t.symbol || !std::isfinite(t.change))
                continue;
            const double prev = t.price - t.change;
            auto& c = curve.contracts[i];
            if (!(prev > 0) || c.price == t.price)
                continue;
            c.price = t.price;
            c.prev_close = prev;
            c.change = t.change;
            c.pct = t.change / prev * 100.0;
            if (t.time_ms > 0)
                c.ts = t.time_ms / 1000;
            c.stale = false;
            live_.insert(it.key(), c);
            datahub::DataHub::instance().publish_live(curve_topic(it.key()), QVariant::fromValue(curve));
        }
    }
}

void FuturesHub::sync_stream() {
    // Stream the front month of every curve still on screen (asked for lately).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList fronts;
    QSet<QString> watched_topics;
    for (const auto& s : datahub::DataHub::instance().stats())
        if (s.topic.startsWith(QLatin1String("fut:curve:")) &&
            (s.subscriber_count > 0 || now - s.last_refresh_request_ms < kActiveMs))
            watched_topics.insert(s.topic);
    for (auto it = curves_.begin(); it != curves_.end();) {
        const bool watched = watched_topics.contains(curve_topic(it.key()));
        if (!watched) {
            live_.remove(it.key());
            it = curves_.erase(it);
            continue;
        }
        if (!it->front.isEmpty())
            fronts << it->front;
        ++it;
    }
    MarketDataService::instance().set_stream_extras(this, fronts);
}

// ── past settlements ───────────────────────────────────────────────────────

void FuturesHub::refresh_past(const QString& root) {
    const QString topic = past_topic(root);
    const auto cit = curves_.constFind(root);
    if (cit == curves_.constEnd() || cit->contracts.isEmpty()) {
        // Needs the curve's months: read the curve; asked again on its TTL.
        datahub::DataHub::instance().publish_error(topic, QStringLiteral("waiting for the %1 curve").arg(root));
        datahub::DataHub::instance().request(curve_topic(root));
        return;
    }
    // After a refused download, not again for five minutes: it is a
    // thirty-symbol request that holds the app-wide download gate.
    if (QDateTime::currentSecsSinceEpoch() - past_failed_at_.value(root, 0) < 300) {
        datahub::DataHub::instance().publish_error(topic, QStringLiteral("backing off after a refused read"));
        return;
    }
    const QString key = QStringLiteral("past:") + root;
    if (running_.contains(key))
        return;
    running_.insert(key);
    QStringList symbols;
    const int anchor = std::max(0, cit->index_of(cit->front));
    symbols << cit->contracts[anchor].symbol; // the front month first: the past sessions are picked off its bars
    for (const auto& c : cit->contracts)
        if (c.symbol != symbols.first())
            symbols << c.symbol;
    QPointer<FuturesHub> self = this;
    fetch_curve_history(symbols, this, [self, root, key, topic](bool ok, QVector<PastCurve> past, QString err) {
        if (!self)
            return;
        self->running_.remove(key);
        if (!ok) {
            self->past_failed_at_.insert(root, QDateTime::currentSecsSinceEpoch());
            datahub::DataHub::instance().publish_error(topic, err);
            return;
        }
        datahub::DataHub::instance().publish(topic, QVariant::fromValue(past));
    });
}

// ── positioning / spreads ──────────────────────────────────────────────────

void FuturesHub::refresh_cot(const QString& cftc) {
    const QString key = QStringLiteral("cot:") + cftc;
    if (running_.contains(key))
        return;
    running_.insert(key);
    QPointer<FuturesHub> self = this;
    fetch_cot(cftc, this, [self, cftc, key](bool ok, CotReport report, QString err) {
        if (!self)
            return;
        self->running_.remove(key);
        if (!ok)
            datahub::DataHub::instance().publish_error(cot_topic(cftc), err);
        else
            datahub::DataHub::instance().publish(cot_topic(cftc), QVariant::fromValue(report));
    });
}

void FuturesHub::refresh_spread(const QString& near_sym, const QString& far_sym) {
    const QString key = QStringLiteral("spread:") + near_sym + QLatin1Char('|') + far_sym;
    if (running_.contains(key))
        return;
    running_.insert(key);
    QPointer<FuturesHub> self = this;
    fetch_spread_history(near_sym, far_sym, this, [self, near_sym, far_sym, key](bool ok, SpreadHistory h, QString err) {
        if (!self)
            return;
        self->running_.remove(key);
        if (!ok)
            datahub::DataHub::instance().publish_error(spread_topic(near_sym, far_sym), err);
        else
            datahub::DataHub::instance().publish(spread_topic(near_sym, far_sym), QVariant::fromValue(h));
    });
}

} // namespace finterm::services::futures
