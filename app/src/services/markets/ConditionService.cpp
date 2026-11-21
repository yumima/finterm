#include "services/markets/ConditionService.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/TopicPolicy.h"
#include "services/economics/EconomicsService.h"
#include "services/markets/MarketConditions.h"
#include "services/markets/MarketDataService.h"
#include "services/prediction/kalshi/KalshiRestClient.h"
#include "storage/cache/CacheManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

namespace finterm::services {

namespace cnd = conditions;

namespace {

constexpr const char* kPersistPrefix = "cond:v1:";

QJsonValue num_or_null(double v) {
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
}

QJsonObject stats_json(const cnd::Stats& s) {
    QJsonObject o;
    o["as_of"] = s.as_of.toString(Qt::ISODate);
    o["last"] = num_or_null(s.last);
    o["sma20"] = num_or_null(s.sma20);
    o["sma50"] = num_or_null(s.sma50);
    o["sma200"] = num_or_null(s.sma200);
    o["vs200"] = num_or_null(s.vs_sma200_pct);
    o["p1y"] = num_or_null(s.pctile_1y);
    o["p5y"] = num_or_null(s.pctile_5y);
    o["rsi"] = num_or_null(s.rsi14);
    o["adx"] = num_or_null(s.adx14);
    o["pdi"] = num_or_null(s.plus_di);
    o["mdi"] = num_or_null(s.minus_di);
    o["ret1m"] = num_or_null(s.ret_1m_pct);
    o["regime"] = cnd::regime_key(s.regime);
    QJsonArray closes;
    for (const auto& c : s.closes)
        closes.append(QJsonArray{c.date.toString(Qt::ISODate), c.value});
    o["closes"] = closes;
    return o;
}

QJsonArray points_json(const QVector<cnd::DatedValue>& xs) {
    QJsonArray a;
    for (const auto& x : xs)
        a.append(QJsonArray{x.date.toString(Qt::ISODate), x.value});
    return a;
}

QVector<cnd::DatedValue> fred_observations(const QJsonObject& data) {
    QVector<cnd::DatedValue> out;
    for (const auto& v : data.value("observations").toArray()) {
        const QJsonObject o = v.toObject();
        const QDate d = QDate::fromString(o.value("date").toString(), Qt::ISODate);
        const QJsonValue jv = o.value("value");
        // fred_data.py emits numbers; a null / "." is absent, never 0.
        double val = jv.isDouble() ? jv.toDouble() : cnd::kNaN;
        if (jv.isString()) {
            bool ok = false;
            val = jv.toString().toDouble(&ok);
            if (!ok)
                val = cnd::kNaN;
        }
        if (d.isValid() && std::isfinite(val))
            out.append({d, val});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.date < b.date; });
    return out;
}

/// The longest look-back a FRED reading needs: ten years for the percentile,
/// a year more for a YoY, a margin for quarterly series — and the chart.
QString fred_start() {
    return QDate::currentDate().addYears(-25).toString(Qt::ISODate);
}

qint64 now_ms() {
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

ConditionService& ConditionService::instance() {
    static ConditionService s;
    return s;
}

ConditionService::ConditionService() : QObject(nullptr) {}

void ConditionService::ensure_registered_with_hub() {
    if (registered_)
        return;
    registered_ = true;
    auto& hub = datahub::DataHub::instance();
    hub.register_producer(this);

    // Daily-bar readings move with today's bar; the bars cache is 10 minutes.
    datahub::TopicPolicy stats;
    stats.ttl_ms = 15 * 60'000;
    stats.min_interval_ms = 5 * 60'000;
    stats.refresh_timeout_ms = 10 * 60'000; // queued behind other symbols
    hub.set_policy_pattern(QStringLiteral("cond:stats:*"), stats);

    // FRED posts once a day at most (monthly for CPI, quarterly for Z.1).
    datahub::TopicPolicy fred;
    fred.ttl_ms = 60 * 60'000;
    fred.min_interval_ms = 10 * 60'000;
    fred.refresh_timeout_ms = 3 * 60'000;
    hub.set_policy_pattern(QStringLiteral("cond:fred:*"), fred);

    // The Treasury publishes the curve after the close, once a day.
    datahub::TopicPolicy ust;
    ust.ttl_ms = 60 * 60'000;
    ust.min_interval_ms = 10 * 60'000;
    ust.refresh_timeout_ms = 2 * 60'000;
    hub.set_policy(kUstTopic, ust);

    // Prediction-market odds move through the day.
    datahub::TopicPolicy odds;
    odds.ttl_ms = 2 * 60'000;
    odds.min_interval_ms = 60'000;
    odds.refresh_timeout_ms = 60'000;
    hub.set_policy_pattern(QStringLiteral("cond:kalshi:*"), odds);

    kalshi_ = new prediction::kalshi_ns::KalshiRestClient(this);
    connect(kalshi_, &prediction::kalshi_ns::KalshiRestClient::events_ready, this,
            [this](const QVector<prediction::PredictionEvent>& events, const QString&) {
                const QString series = kalshi_current_;
                kalshi_current_.clear();
                if (series.isEmpty())
                    return;
                struct Ev {
                    QJsonObject o;
                    QString close;
                };
                QVector<Ev> evs;
                for (const auto& e : events) {
                    QJsonArray outcomes;
                    QString close;
                    for (const auto& m : e.markets) {
                        if (m.closed || m.outcomes.isEmpty())
                            continue;
                        const double p = m.outcomes.first().price;
                        QString label = m.extras.value(QStringLiteral("yes_sub_title")).toString();
                        if (label.isEmpty())
                            label = m.question;
                        outcomes.append(QJsonObject{{"label", label}, {"p", num_or_null(p)}, {"ticker", m.key.market_id}});
                        if (close.isEmpty() || (!m.end_date_iso.isEmpty() && m.end_date_iso < close))
                            close = m.end_date_iso;
                    }
                    if (outcomes.isEmpty())
                        continue;
                    evs.append({QJsonObject{{"ticker", e.key.event_id},
                                            {"title", e.title},
                                            {"close", close},
                                            {"outcomes", outcomes}},
                                close});
                }
                // Soonest first: the next meeting / print is the one read.
                std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.close < b.close; });
                QJsonArray arr;
                for (const auto& e : evs)
                    arr.append(e.o);
                if (arr.isEmpty())
                    datahub::DataHub::instance().publish_error(kalshi_topic(series),
                                                               QStringLiteral("Kalshi has no open events for %1")
                                                                   .arg(series));
                else
                    publish(kalshi_topic(series), QJsonObject{{"events", arr}});
                pump_kalshi();
            });
    connect(kalshi_, &prediction::kalshi_ns::KalshiRestClient::request_error, this,
            [this](const QString& ctx, const QString& msg) {
                if (!ctx.contains(QLatin1String("fetch_events")) || kalshi_current_.isEmpty())
                    return;
                const QString series = kalshi_current_;
                kalshi_current_.clear();
                datahub::DataHub::instance().publish_error(kalshi_topic(series), msg);
                pump_kalshi();
            });
    LOG_INFO("Conditions", "ConditionService registered (cond:stats/fred/ust/kalshi)");
}

QStringList ConditionService::topic_patterns() const {
    return {QStringLiteral("cond:stats:*"), QStringLiteral("cond:fred:*"), kUstTopic, QStringLiteral("cond:kalshi:*")};
}

void ConditionService::refresh(const QStringList& topics) {
    for (const auto& t : topics) {
        if (t.startsWith(QLatin1String("cond:stats:"))) {
            const QString sym = t.mid(11);
            if (!stats_queue_.contains(sym) && !stats_running_.contains(sym))
                stats_queue_.append(sym);
        } else if (t.startsWith(QLatin1String("cond:fred:"))) {
            refresh_fred(t.mid(10));
        } else if (t == kUstTopic) {
            refresh_ust();
        } else if (t.startsWith(QLatin1String("cond:kalshi:"))) {
            const QString ser = t.mid(12);
            if (!kalshi_queue_.contains(ser) && kalshi_current_ != ser)
                kalshi_queue_.append(ser);
        }
    }
    pump_stats();
    pump_kalshi();
}

void ConditionService::publish(const QString& topic, QJsonObject payload) {
    payload["t"] = static_cast<double>(now_ms());
    CacheManager::instance().put(kPersistPrefix + topic,
                                 QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)),
                                 kPersistTtlSec, QStringLiteral("market_conditions"));
    datahub::DataHub::instance().publish(topic, QVariant::fromValue(payload));
}

QJsonObject ConditionService::last_known(const QString& topic) const {
    const QVariant v = datahub::DataHub::instance().peek_raw(topic);
    if (v.canConvert<QJsonObject>()) {
        const QJsonObject o = v.toJsonObject();
        if (!o.isEmpty())
            return o;
    }
    if (const auto hit = CacheManager::instance().try_get(kPersistPrefix + topic))
        return QJsonDocument::fromJson(hit->toUtf8()).object();
    return {};
}

// ── Daily-bar readings ──────────────────────────────────────────────────────

void ConditionService::pump_stats() {
    // A few at a time, not all at once: each is a daemon call, and a burst of
    // forty would queue every other screen's request behind it.
    while (stats_running_.size() < kStatsConcurrency && !stats_queue_.isEmpty())
        refresh_stats(stats_queue_.takeFirst());
}

void ConditionService::refresh_stats(const QString& sym) {
    stats_running_.insert(sym);
    QPointer<ConditionService> self = this;
    // The chart dialog's 5Y view reads this exact series (same cache key).
    MarketDataService::instance().fetch_bars(sym, QStringLiteral("5y"), QStringLiteral("1d"),
                                             [self, sym](const MarketDataService::BarsReply& r) {
                                                 if (!self)
                                                     return;
                                                 self->stats_running_.remove(sym);
                                                 const QString topic = stats_topic(sym);
                                                 if (!r.ok || r.bars.isEmpty()) {
                                                     datahub::DataHub::instance().publish_error(
                                                         topic, r.ok ? QStringLiteral("no price history")
                                                                     : r.error);
                                                 } else {
                                                     QVector<cnd::Bar> bars;
                                                     bars.reserve(r.bars.size());
                                                     for (const auto& p : r.bars)
                                                         bars.append({p.date(), p.high, p.low, p.close});
                                                     // A >3x one-day jump: readings are taken from the
                                                     // break on, never across it. A spliced vendor series
                                                     // (Yahoo's QO=F) is then left with no history worth
                                                     // reading; a real crash or buyout keeps its readings
                                                     // over the history since — and says so.
                                                     const cnd::Break brk = cnd::find_break(bars);
                                                     if (brk.found()) {
                                                         QVector<cnd::Bar> since;
                                                         for (const auto& b : bars)
                                                             if (b.date >= brk.date)
                                                                 since.append(b);
                                                         bars = since;
                                                     }
                                                     const cnd::Stats s = cnd::compute(bars);
                                                     if (s.valid()) {
                                                         QJsonObject o = stats_json(s);
                                                         if (brk.found()) {
                                                             o["break_date"] = brk.date.toString(Qt::ISODate);
                                                             o["break_ratio"] = brk.ratio;
                                                         }
                                                         self->publish(topic, o);
                                                     } else
                                                         datahub::DataHub::instance().publish_error(
                                                         topic, QStringLiteral("no usable daily bars"));
                                                 }
                                                 self->pump_stats();
                                             });
}

// ── FRED ────────────────────────────────────────────────────────────────────

void ConditionService::refresh_fred(const QString& key) {
    if (fred_running_.contains(key))
        return;
    const cnd::FredKey fk = cnd::parse_fred_key(QStringLiteral("FRED:") + key);
    const QString topic = fred_topic(key);
    if (!fk.valid()) {
        datahub::DataHub::instance().publish_error(topic, QStringLiteral("not a FRED series key: %1").arg(key));
        return;
    }
    fred_running_.insert(key);
    struct Acc {
        QHash<QString, QVector<cnd::DatedValue>> got;
        QString error;
        int pending = 0;
    };
    auto acc = std::make_shared<Acc>();
    const QStringList ids = fk.series();
    acc->pending = ids.size();
    QPointer<ConditionService> self = this;
    for (const auto& id : ids) {
        // The same script, cache and in-flight run every FRED reader uses.
        EconomicsService::instance().fetch(
            QStringLiteral("fred_data.py"), QStringLiteral("series"), {id, fred_start()},
            [self, acc, id, fk, key, topic](const EconomicsResult& r) {
                if (!self)
                    return;
                if (r.success)
                    acc->got.insert(id, fred_observations(r.data));
                else if (acc->error.isEmpty())
                    acc->error = QStringLiteral("%1: %2").arg(id, r.error);
                if (--acc->pending > 0)
                    return;
                self->fred_running_.remove(key);
                if (!acc->error.isEmpty()) {
                    datahub::DataHub::instance().publish_error(topic, acc->error);
                    return;
                }
                const auto values = cnd::fred_values(fk, acc->got.value(fk.a), acc->got.value(fk.b));
                if (values.isEmpty()) {
                    datahub::DataHub::instance().publish_error(topic, QStringLiteral("FRED returned no observations"));
                    return;
                }
                self->publish(topic, QJsonObject{{"points", points_json(values)}});
            });
    }
}

// ── Treasury par curve ──────────────────────────────────────────────────────

void ConditionService::refresh_ust() {
    if (ust_running_)
        return;
    ust_running_ = true;
    QPointer<ConditionService> self = this;
    // Markets ▸ WORLD's 2Y and 2s10s come from this same call and cache entry.
    EconomicsService::instance().fetch_treasury_par_curve([self](const EconomicsResult& r) {
        if (!self)
            return;
        self->ust_running_ = false;
        const QJsonArray rows = r.data.value("rows").toArray();
        if (!r.success || rows.isEmpty()) {
            datahub::DataHub::instance().publish_error(
                kUstTopic, r.success ? QStringLiteral("the Treasury curve had no rows") : r.error);
            return;
        }
        self->publish(kUstTopic, QJsonObject{{"rows", rows}});
    });
}

// ── Kalshi ──────────────────────────────────────────────────────────────────

void ConditionService::pump_kalshi() {
    if (!kalshi_ || !kalshi_current_.isEmpty() || kalshi_queue_.isEmpty())
        return;
    // One request at a time: the client's events_ready does not say which
    // series it answers, so the one in flight is the one it is for.
    kalshi_current_ = kalshi_queue_.takeFirst();
    const int seq = ++kalshi_seq_;
    kalshi_->fetch_events(QStringLiteral("open"), kalshi_current_, true, 20);
    // The client's requests have no transfer timeout: a stalled connection
    // must not hold the one-at-a-time queue (and every series) forever.
    QTimer::singleShot(30'000, this, [this, seq]() {
        if (seq != kalshi_seq_ || kalshi_current_.isEmpty())
            return;
        const QString series = kalshi_current_;
        kalshi_current_.clear();
        ++kalshi_seq_; // a late answer for it is dropped
        datahub::DataHub::instance().publish_error(kalshi_topic(series), QStringLiteral("Kalshi did not answer in 30 s"));
        pump_kalshi();
    });
}

// ── Chartable derived series ────────────────────────────────────────────────

bool ConditionService::is_derived(const QString& key) {
    return cnd::is_fred_key(key) || key.startsWith(QLatin1String("UST:")) || key.startsWith(QLatin1String("CORR:"));
}

QString ConditionService::derived_label(const QString& key) {
    static const QHash<QString, QString> k = {
        {"FRED:DFF", "Fed funds (effective)"},
        {"FRED:DFII10", "10Y real yield (TIPS)"},
        {"FRED:T10YIE", "10Y breakeven inflation"},
        {"FRED:CPIAUCSL@YOY", "CPI YoY"},
        {"FRED:CPILFESL@YOY", "Core CPI YoY"},
        {"FRED:PCEPILFE@YOY", "Core PCE YoY"},
        {"FRED:GFDEGDQ188S", "Federal debt / GDP"},
        {"FRED:NCBEILQ027S/GDP*0.1", "Buffett indicator"},
        {"FRED:BAMLH0A0HYM2", "High-yield OAS"},
        {"FRED:BAMLC0A0CM", "Investment-grade OAS"},
    };
    return k.value(key.toUpper());
}

namespace {
QVector<QPair<QDate, double>> to_pairs(const QVector<cnd::DatedValue>& xs) {
    QVector<QPair<QDate, double>> out;
    out.reserve(xs.size());
    for (const auto& x : xs)
        out.append({x.date, x.value});
    return out;
}

QVector<cnd::DatedValue> closes_from(const QJsonObject& stats) {
    QVector<cnd::DatedValue> out;
    for (const auto& v : stats.value("closes").toArray()) {
        const QJsonArray a = v.toArray();
        const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
        if (d.isValid() && a.at(1).isDouble())
            out.append({d, a.at(1).toDouble()});
    }
    return out;
}
} // namespace

void ConditionService::answer_once(const QString& topic,
                                   std::function<QVector<QPair<QDate, double>>(const QJsonObject&)> build,
                                   SeriesCallback cb) {
    // The topic's next answer or error, whichever comes first, exactly once.
    auto* ctx = new QObject(this);
    auto done = std::make_shared<bool>(false);
    auto& hub = datahub::DataHub::instance();
    hub.subscribe(ctx, topic, [ctx, cb, build, done](const QVariant& v) {
        if (*done || !v.canConvert<QJsonObject>())
            return;
        *done = true;
        cb(true, build(v.toJsonObject()), {});
        ctx->deleteLater();
    });
    hub.subscribe_errors(ctx, topic, [ctx, cb, done](const QString& err) {
        if (*done)
            return;
        *done = true;
        cb(false, {}, err);
        ctx->deleteLater();
    });
    hub.request(topic, true);
}

void ConditionService::fetch_series(const QString& key, SeriesCallback cb) {
    ensure_registered_with_hub();
    QPointer<ConditionService> self = this;
    auto answer_later = [self, cb](bool ok, QVector<QPair<QDate, double>> pts, QString err) {
        QTimer::singleShot(0, self ? static_cast<QObject*>(self.data()) : qApp,
                           [cb, ok, pts = std::move(pts), err] { cb(ok, pts, err); });
    };
    if (cnd::is_fred_key(key)) {
        const QString body = key.mid(5).toUpper();
        const QString topic = fred_topic(body);
        // The widget's own answer when it is held: the chart and the row
        // then read the same observations.
        const QJsonObject held = last_known(topic);
        auto from_points = [](const QJsonObject& o) {
            QVector<QPair<QDate, double>> pts;
            for (const auto& v : o.value("points").toArray()) {
                const QJsonArray a = v.toArray();
                const QDate d = QDate::fromString(a.at(0).toString(), Qt::ISODate);
                if (d.isValid() && a.at(1).isDouble())
                    pts.append({d, a.at(1).toDouble()});
            }
            return pts;
        };
        // The last answer at once; then, unless the hub's copy is fresh, the
        // fresh one when it lands (the chart redraws — cache first).
        if (!held.isEmpty())
            answer_later(true, from_points(held), {});
        if (held.isEmpty() || !datahub::DataHub::instance().peek(topic).isValid())
            answer_once(topic, [from_points](const QJsonObject& o) { return from_points(o); },
                        held.isEmpty() ? cb : SeriesCallback([cb](bool ok, auto pts, QString) {
                            if (ok) // a failed refresh keeps the drawn copy
                                cb(true, pts, {});
                        }));
        return;
    }
    if (key.startsWith(QLatin1String("UST:"))) {
        const QString what = key.mid(4).toLower();
        const QStringList legs = what.split(QLatin1Char('-'));
        auto build = [legs](const QJsonObject& o) {
            QVector<QPair<QDate, double>> pts;
            for (const auto& v : o.value("rows").toArray()) {
                const QJsonObject r = v.toObject();
                const QDate d = QDate::fromString(r.value("date").toString(), Qt::ISODate);
                const QJsonValue a = r.value(legs.value(0));
                if (!d.isValid() || !a.isDouble())
                    continue;
                if (legs.size() == 2) {
                    const QJsonValue b = r.value(legs.value(1));
                    if (b.isDouble())
                        pts.append({d, (a.toDouble() - b.toDouble()) * 100.0}); // spread in bp
                } else {
                    pts.append({d, a.toDouble()});
                }
            }
            std::reverse(pts.begin(), pts.end()); // rows are newest first
            return pts;
        };
        const QJsonObject held = last_known(kUstTopic);
        if (!held.isEmpty())
            answer_later(true, build(held), {});
        if (held.isEmpty() || !datahub::DataHub::instance().peek(kUstTopic).isValid())
            answer_once(kUstTopic, build, held.isEmpty() ? cb : SeriesCallback([cb](bool ok, auto pts, QString) {
                if (ok)
                    cb(true, pts, {});
            }));
        return;
    }
    if (key.startsWith(QLatin1String("CORR:"))) {
        const QStringList p = key.mid(5).split(QLatin1Char('|'));
        const int window = p.value(2).toInt();
        if (p.size() != 3 || window < 5) {
            answer_later(false, {}, QStringLiteral("bad correlation key"));
            return;
        }
        const QString a = p[0], b = p[1];
        const QJsonObject sa = last_known(stats_topic(a)), sb = last_known(stats_topic(b));
        if (sa.isEmpty() || sb.isEmpty()) {
            answer_later(false, {}, QStringLiteral("no history held for %1 / %2 yet").arg(a, b));
            return;
        }
        answer_later(true,
                     to_pairs(cnd::rolling_correlation(closes_from(sa), cnd::is_yield_symbol(a), closes_from(sb),
                                                       cnd::is_yield_symbol(b), window)),
                     {});
        return;
    }
    answer_later(false, {}, QStringLiteral("unknown series %1").arg(key));
}

} // namespace finterm::services
