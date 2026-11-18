#include "services/markets/MarketDataService.h"

#include "services/markets/LivePrice.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"
#include "datahub/TopicPolicy.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "services/markets/SecIdentity.h"
#include "storage/cache/CacheManager.h"
#include "storage/secure/SecureStorage.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QtNumeric>

#include <cmath>
#include <memory>

namespace finterm::services {

namespace {

/// A numeric field, or NaN when the vendor did not supply one.
///
/// The daemon's _sanitize_for_json sends every absent value as JSON null, and
/// QJsonValue::toDouble() reads null as 0.0 without complaint — so "Yahoo has
/// no profit margin for this company" and "this company's margin is 0.00%"
/// arrived identically. InfoData's fields default to kUnknownNum for the same
/// reason; this is what keeps them that way after a fetch.
double num(const QJsonObject& o, const char* key) {
    return o.value(QLatin1String(key)).toDouble(kUnknownNum);
}

} // namespace

MarketDataService& MarketDataService::instance() {
    static MarketDataService s;
    return s;
}

MarketDataService::MarketDataService() {}

// ── One mapping for a quote, wherever it comes from ─────────────────────────
// A daemon row (change_percent), the cache entry (change_pct) and back. These
// used to be five hand-copied blocks that had already drifted (one read no
// order book, one fell back to the symbol for the name and one did not).
namespace {
QuoteData quote_from_daemon(const QJsonObject& q) {
    QuoteData qd{};
    qd.symbol = q["symbol"].toString();
    qd.name = q["name"].toString(qd.symbol);
    qd.price = q["price"].toDouble();
    qd.change = q["change"].toDouble(qQNaN());
    qd.change_pct = q["change_percent"].toDouble(qQNaN());
    qd.high = q["high"].toDouble();
    qd.low = q["low"].toDouble();
    qd.volume = q["volume"].toDouble(qQNaN());
    qd.open = q["open"].toDouble(qQNaN());
    qd.prev_close = q["previous_close"].toDouble(qQNaN());
    qd.bid = q["bid"].toDouble();
    qd.ask = q["ask"].toDouble();
    qd.bid_size = q["bid_size"].toDouble();
    qd.ask_size = q["ask_size"].toDouble();
    return qd;
}

QJsonObject quote_to_cache(const QuoteData& q) {
    QJsonObject o;
    o["symbol"] = q.symbol;
    o["name"] = q.name;
    o["price"] = q.price;
    o["change"] = q.change;
    o["change_pct"] = q.change_pct;
    o["high"] = q.high;
    o["low"] = q.low;
    o["volume"] = q.volume;
    o["open"] = q.open; // NaN is written as null and read back as NaN
    o["prev_close"] = q.prev_close;
    o["bid"] = q.bid;
    o["ask"] = q.ask;
    o["bid_size"] = q.bid_size;
    o["ask_size"] = q.ask_size;
    return o;
}

QuoteData quote_from_cache(const QJsonObject& o, const QString& fallback_symbol = {}) {
    QuoteData qd{};
    qd.symbol = o.value("symbol").toString(fallback_symbol);
    qd.name = o.value("name").toString(qd.symbol);
    qd.price = o.value("price").toDouble();
    qd.change = o.value("change").toDouble(qQNaN());
    qd.change_pct = o.value("change_pct").toDouble(qQNaN());
    qd.high = o.value("high").toDouble();
    qd.low = o.value("low").toDouble();
    qd.volume = o.value("volume").toDouble(qQNaN());
    qd.open = o.value("open").toDouble(qQNaN());
    qd.prev_close = o.value("prev_close").toDouble(qQNaN());
    qd.bid = o.value("bid").toDouble();
    qd.ask = o.value("ask").toDouble();
    qd.bid_size = o.value("bid_size").toDouble();
    qd.ask_size = o.value("ask_size").toDouble();
    return qd;
}
/// The one cache key for a series. v1: the bars:: rules (priced rows only).
QString bars_key(const QString& symbol, const QString& period, const QString& interval, bool adjusted) {
    return QStringLiteral("bars:v1:%1:%2:%3:%4").arg(symbol, period, interval, adjusted ? "adj" : "raw");
}
/// Intraday series move by the minute; daily ones are stable within the
/// session's quarter-hour.
int bars_ttl_sec(const QString& interval) {
    return bars::is_intraday(interval) ? 60 : 600;
}
} // namespace

// ── DataHub Producer integration ────────────────────────────────────────────

QStringList MarketDataService::topic_patterns() const {
    return {QStringLiteral("market:quote:*"), QStringLiteral("market:sparkline:*"), QStringLiteral("market:history:*"),
            kMoversTopic};
}

int MarketDataService::max_requests_per_sec() const {
    // Raised from 2 to 10: with the merged batch-all refresh path and the
    // persistent yfinance worker, refreshes complete in <200ms, so gating at
    // 2 req/s starved cold-start (quote + spark + history arrived 500ms apart).
    // 10 req/s still backs off enough for upstream yfinance rate limits while
    // letting dashboard + markets refresh converge in one scheduler tick.
    return 10;
}

void MarketDataService::refresh(const QStringList& topics) {
    // Hub guarantees `topics` all match one of our patterns. We bundle ALL
    // three families into a single `yfinance_data.py batch_all` invocation so
    // one refresh tick spawns one Python process instead of three. Individual
    // fetch_quotes/fetch_sparklines/fetch_history callback APIs remain untouched
    // — they're still used by report builder and other one-shot paths.
    static const QString kQuote = QStringLiteral("market:quote:");
    static const QString kSpark = QStringLiteral("market:sparkline:");
    static const QString kHist = QStringLiteral("market:history:");

    const qint64 refresh_t0 = QDateTime::currentMSecsSinceEpoch();

    QStringList quote_syms;
    QStringList spark_syms;
    struct HistReq {
        QString topic;
        QString symbol;
        QString period;
        QString interval;
    };
    QVector<HistReq> hist_reqs;
    if (topics.contains(kMoversTopic)) {
        // The day's movers: their own screener call, one list for every screen.
        fetch_movers_all(
            [](bool ok, const TopMovers& tm, const QString& why) {
                auto& hub = datahub::DataHub::instance();
                if (ok)
                    hub.publish(kMoversTopic, QVariant::fromValue(tm));
                else
                    hub.publish_error(kMoversTopic, why.isEmpty() ? QStringLiteral("screener unavailable") : why);
            },
            /*fresh=*/true);
    }
    for (const auto& t : topics) {
        if (t.startsWith(kQuote)) {
            quote_syms.append(t.mid(kQuote.size()));
        } else if (t.startsWith(kSpark)) {
            spark_syms.append(t.mid(kSpark.size()));
        } else if (t.startsWith(kHist)) {
            const QString tail = t.mid(kHist.size());
            const QStringList parts = tail.split(QLatin1Char(':'));
            if (parts.size() == 3)
                hist_reqs.append({t, parts.at(0), parts.at(1), parts.at(2)});
        }
    }

    LOG_INFO("DataHub", QString("refresh() quotes=%1 sparks=%2 histories=%3 (1 python spawn)")
                            .arg(quote_syms.size())
                            .arg(spark_syms.size())
                            .arg(hist_reqs.size()));

    // Build the unified batch_all payload.
    QJsonObject payload;
    if (!quote_syms.isEmpty()) {
        QJsonArray arr;
        for (const auto& s : quote_syms)
            arr.append(s);
        payload["quotes"] = arr;
    }
    if (!spark_syms.isEmpty()) {
        QJsonArray arr;
        for (const auto& s : spark_syms)
            arr.append(s);
        payload["sparklines"] = arr;
    }
    if (!hist_reqs.isEmpty()) {
        QJsonArray arr;
        for (const auto& h : hist_reqs) {
            QJsonObject o;
            o["symbol"] = h.symbol;
            o["period"] = h.period;
            o["interval"] = h.interval;
            arr.append(o);
        }
        payload["histories"] = arr;
    }

    if (payload.isEmpty()) {
        return; // nothing beyond the movers (fetched above)
    }

    QPointer<MarketDataService> self = this;

    // Route via PythonWorker (persistent daemon) instead of PythonRunner so
    // we don't pay the 2–3s yfinance/pandas import cost per refresh tick. See
    // PythonWorker docs — scope is yfinance_data.py only.
    python::PythonWorker::instance().submit(
        "batch_all", payload,
        [self, quote_syms, spark_syms, hist_reqs, refresh_t0](bool ok, QJsonObject root, QString err) {
            if (!self)
                return;

            const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - refresh_t0;

            auto& hub = datahub::DataHub::instance();
            if (!ok) {
                LOG_WARN("MarketData", QString("batch_all failed in %1ms: %2").arg(elapsed).arg(err.left(200)));
                // Say so on every topic asked for: a subscriber waiting on a
                // refresh must hear a refusal, not silence (the last value,
                // if any, stays on screen).
                const QString why = err.left(200);
                for (const auto& s : quote_syms)
                    hub.publish_error(QStringLiteral("market:quote:") + s, why);
                for (const auto& s : spark_syms)
                    hub.publish_error(QStringLiteral("market:sparkline:") + s, why);
                for (const auto& h : hist_reqs)
                    hub.publish_error(h.topic, why);
                return;
            }

            // PythonWorker passes through the `result` JSON as-is (or wraps
            // a scalar under "_value"). For batch_all the daemon returns an
            // object so root is already the {quotes,sparklines,histories} map.
            //
            // Parse-only here: append every decoded item to the refresh_pending_*
            // queues, then schedule drain_refresh_chunk(). This callback's slice
            // stays small (JSON parsing only — no SQLite writes, no hub fan-out)
            // so we don't block input/paint while a 100-quote burst lands. The
            // actual cache writes + hub publishes happen kRefreshChunkSize items
            // at a time via singleShot(0). Wall-clock total is unchanged, but
            // the event loop gets to interleave keystrokes and video frames
            // between chunks instead of waiting behind the whole burst.
            // Only what THIS refresh asked for: PythonWorker merges a waiting
            // request with later ones and answers every caller with the
            // union — each publishes its own topics, not the others' again.
            const QSet<QString> want_quotes(quote_syms.begin(), quote_syms.end());
            const QSet<QString> want_sparks(spark_syms.begin(), spark_syms.end());
            QSet<QString> want_hists;
            for (const auto& h : hist_reqs)
                want_hists.insert(h.symbol + QLatin1Char('|') + h.period + QLatin1Char('|') + h.interval);
            const QJsonArray quotes_arr = root.value("quotes").toArray();
            int quotes_parsed = 0;
            QSet<QString> answered_quotes;
            for (const auto& v : quotes_arr) {
                const QJsonObject q = v.toObject();
                if (q.isEmpty() || q.contains("error") || !want_quotes.contains(q["symbol"].toString()))
                    continue;
                const QuoteData qd = quote_from_daemon(q);
                answered_quotes.insert(qd.symbol);
                // A symbol already waiting to be stored/published (merged
                // refreshes answer every caller with the same union, or an
                // older reply not yet drained) is replaced by this one — the
                // newer print — not queued twice.
                if (auto at = self->refresh_pending_quote_at_.constFind(qd.symbol);
                    at != self->refresh_pending_quote_at_.constEnd()) {
                    self->refresh_pending_quotes_[at.value() - self->refresh_pending_quote_base_] = qd;
                    continue;
                }
                self->refresh_pending_quote_at_.insert(qd.symbol, self->refresh_pending_quote_base_ +
                                                                      self->refresh_pending_quotes_.size());
                self->refresh_pending_quotes_.append(qd);
                ++quotes_parsed;
            }

            // A symbol asked for and not answered (refused, or an {error}
            // row) is an error on its topic — never an empty success.
            for (const auto& sym : quote_syms)
                if (!answered_quotes.contains(sym))
                    hub.publish_error(QStringLiteral("market:quote:") + sym, QStringLiteral("no quote returned"));

            // Sparklines — {sym: [closes]}
            const QJsonObject sparks = root.value("sparklines").toObject();
            int sparks_parsed = 0;
            for (auto it = sparks.begin(); it != sparks.end(); ++it) {
                const QJsonArray closes = it.value().toArray();
                if (closes.isEmpty() || !want_sparks.contains(it.key()))
                    continue;
                PendingSparkline ps;
                ps.symbol = it.key();
                ps.points.reserve(closes.size());
                for (const auto& c : closes)
                    ps.points.append(c.toDouble());
                // Same rule as quotes: one pending entry per symbol, the newest
                // (merged refreshes answer every caller with the union). The
                // pending lists are short (tens), so a scan is fine.
                bool replaced = false;
                for (auto& pending : self->refresh_pending_sparks_)
                    if (pending.symbol == ps.symbol) {
                        pending = std::move(ps);
                        replaced = true;
                        break;
                    }
                if (!replaced)
                    self->refresh_pending_sparks_.append(std::move(ps));
                ++sparks_parsed;
            }

            // Histories — array of {symbol, period, interval, points[]}
            const QJsonArray hists = root.value("histories").toArray();
            int hists_parsed = 0;
            for (const auto& hv : hists) {
                const QJsonObject h = hv.toObject();
                if (!want_hists.contains(h.value("symbol").toString() + QLatin1Char('|') +
                                         h.value("period").toString() + QLatin1Char('|') +
                                         h.value("interval").toString()))
                    continue;
                if (h.contains("error")) {
                    hub.publish_error(QStringLiteral("market:history:") + h.value("symbol").toString() +
                                          QLatin1Char(':') + h.value("period").toString() + QLatin1Char(':') +
                                          h.value("interval").toString(),
                                      h.value("error").toString());
                    continue;
                }
                PendingHistory ph;
                ph.symbol = h.value("symbol").toString();
                ph.period = h.value("period").toString();
                ph.interval = h.value("interval").toString();
                // Same rules as fetch_bars: priced rows only, one parser.
                ph.rows = bars::priced_rows(h.value("points").toArray());
                ph.points = bars::parse(ph.rows);
                bool replaced = false;
                for (auto& pending : self->refresh_pending_hists_)
                    if (pending.symbol == ph.symbol && pending.period == ph.period && pending.interval == ph.interval) {
                        pending = std::move(ph);
                        replaced = true;
                        break;
                    }
                if (!replaced)
                    self->refresh_pending_hists_.append(std::move(ph));
                ++hists_parsed;
            }

            self->refresh_quotes_total_ += quotes_parsed;
            self->refresh_sparks_total_ += sparks_parsed;
            self->refresh_hists_total_ += hists_parsed;
            if (self->refresh_started_ms_ == 0)
                self->refresh_started_ms_ = refresh_t0;

            LOG_INFO("MarketData", QString("batch_all parsed in %1ms: queued quotes=%2/%3 sparks=%4/%5 hists=%6/%7 "
                                           "(draining %8/chunk)")
                                       .arg(elapsed)
                                       .arg(quotes_parsed)
                                       .arg(quote_syms.size())
                                       .arg(sparks_parsed)
                                       .arg(spark_syms.size())
                                       .arg(hists_parsed)
                                       .arg(hist_reqs.size())
                                       .arg(kRefreshChunkSize));

            if (!self->refresh_drain_scheduled_ &&
                (!self->refresh_pending_quotes_.isEmpty() || !self->refresh_pending_sparks_.isEmpty() ||
                 !self->refresh_pending_hists_.isEmpty())) {
                self->refresh_drain_scheduled_ = true;
                QTimer::singleShot(0, self.data(), [self]() {
                    if (self)
                        self->drain_refresh_chunk();
                });
            }
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

void MarketDataService::drain_refresh_chunk() {
    // Process up to kRefreshChunkSize items this slice. Quotes are
    // prioritised (UI subscribers wait on these first) then sparklines
    // then histories. Each quote costs 2 SQLite writes + 1 hub publish;
    // sparks/hists are publish-only. Keeping the budget mixed across types
    // means we don't starve sparks/hists indefinitely if quotes keep
    // arriving.
    refresh_drain_scheduled_ = false;

    int budget = kRefreshChunkSize;

    // Quotes — cache write + publish.
    while (budget > 0 && !refresh_pending_quotes_.isEmpty()) {
        const QuoteData qd = refresh_pending_quotes_.takeFirst();
        refresh_pending_quote_at_.remove(qd.symbol);
        ++refresh_pending_quote_base_; // positions are absolute: no reindex on takeFirst
        store_quote_entries(qd.symbol, quote_to_cache(qd));
        publish_quote_to_hub(qd, /*polled=*/true);
        --budget;
    }

    // Sparklines — publish only.
    while (budget > 0 && !refresh_pending_sparks_.isEmpty()) {
        const PendingSparkline ps = refresh_pending_sparks_.takeFirst();
        publish_sparkline_to_hub(ps.symbol, ps.points);
        --budget;
    }

    // Histories — publish only.
    while (budget > 0 && !refresh_pending_hists_.isEmpty()) {
        const PendingHistory ph = refresh_pending_hists_.takeFirst();
        // The series fetch_bars reads: one cache entry whichever path fetched it.
        if (!ph.rows.isEmpty())
            finterm::CacheManager::instance().put(
                bars_key(ph.symbol, ph.period, ph.interval, /*adjusted=*/true),
                QString::fromUtf8(QJsonDocument(ph.rows).toJson(QJsonDocument::Compact)), bars_ttl_sec(ph.interval),
                "market_data");
        publish_history_to_hub(ph.symbol, ph.period, ph.interval, ph.points);
        --budget;
    }

    const bool anything_left =
        !refresh_pending_quotes_.isEmpty() || !refresh_pending_sparks_.isEmpty() || !refresh_pending_hists_.isEmpty();
    if (anything_left) {
        refresh_drain_scheduled_ = true;
        QPointer<MarketDataService> self = this;
        QTimer::singleShot(0, this, [self]() {
            if (self)
                self->drain_refresh_chunk();
        });
    } else if (refresh_quotes_total_ + refresh_sparks_total_ + refresh_hists_total_ > 0) {
        const qint64 total_ms = QDateTime::currentMSecsSinceEpoch() - refresh_started_ms_;
        LOG_INFO("MarketData", QString("batch_all drain complete: quotes=%1 sparks=%2 hists=%3 in %4ms total")
                                   .arg(refresh_quotes_total_)
                                   .arg(refresh_sparks_total_)
                                   .arg(refresh_hists_total_)
                                   .arg(total_ms));
        refresh_quotes_total_ = 0;
        refresh_sparks_total_ = 0;
        refresh_hists_total_ = 0;
        refresh_started_ms_ = 0;
    }
}

void MarketDataService::publish_quote_to_hub(const QuoteData& q_in, bool polled) {
    // A poll answered while the stream is live is usually OLDER than the last
    // tick (batch quotes lag the tape by seconds): keep the tick's price on the
    // poll's baseline, or every poll would yank the price back for a moment.
    // Past kTickWinsOverPollMs the poll wins — e.g. the closing auction print,
    // which arrives after the last regular-session tick.
    QuoteData q = q_in;
    if (polled) {
        polled_syms_.insert(q.symbol);
        if (auto it = last_tick_.constFind(q.symbol); it != last_tick_.constEnd()) {
            const qint64 age = QDateTime::currentMSecsSinceEpoch() - it->seen_ms;
            const double snap_prev = age < kSnapshotPrevMs ? it->prev_close : qQNaN();
            if (age < kTickWinsOverPollMs)
                q = with_live_price(q, it->price, it->volume, snap_prev);
            else if (std::isfinite(snap_prev)) // poll's price, the snapshot's baseline (no CHG flip)
                q = with_live_price(q, q.price, qQNaN(), snap_prev);
        }
    }
    datahub::DataHub::instance().publish(QStringLiteral("market:quote:") + q.symbol, QVariant::fromValue(q));
}

// ── Push price stream ───────────────────────────────────────────────────────

QuoteData MarketDataService::with_live_price(QuoteData q, double price, double volume, double prev_close) {
    // Store one number (the price), derive the rest from the quote's own
    // previous close — never mix in the stream's change, whose baseline may
    // differ from the one this quote was polled against. A fast-lane snapshot
    // brings its own close (same snapshot as its price) and uses that.
    const double prev = (std::isfinite(prev_close) && prev_close > 0)     ? prev_close
                        : (std::isfinite(q.prev_close) && q.prev_close > 0) ? q.prev_close
                        : (std::isfinite(q.change) && std::isfinite(q.price) && q.price > 0) ? q.price - q.change
                                                                                             : qQNaN();
    apply_live_price(q, prev, price, volume);
    if (std::isfinite(prev) && prev > 0)
        q.prev_close = prev; // change is now measured against this one
    return q;
}

void MarketDataService::overlay_live(QVector<QuoteData>& quotes) const {
    if (last_tick_.isEmpty())
        return;
    auto& hub = datahub::DataHub::instance();
    for (auto& q : quotes) {
        if (!last_tick_.contains(q.symbol) || !polled_syms_.contains(q.symbol))
            continue;
        // The hub copy is the latest poll with ticks laid on it — never older
        // than the cache entry, which is written by the same poll: once a
        // symbol is polled, only polls and ticks write its topic (hydration
        // skips it).
        const QVariant cur = hub.peek_raw(QStringLiteral("market:quote:") + q.symbol);
        if (cur.canConvert<QuoteData>())
            q = cur.value<QuoteData>();
    }
}

void MarketDataService::init_stream() {
    auto& hub = datahub::DataHub::instance();
    stream_sync_timer_ = new QTimer(this);
    stream_sync_timer_->setSingleShot(true);
    stream_sync_timer_->setInterval(500);
    connect(stream_sync_timer_, &QTimer::timeout, this, [this]() { sync_stream(false); });
    const auto on_topic = [this](const QString& topic) {
        if (topic.startsWith(QLatin1String("market:quote:")) || topic.startsWith(QLatin1String("market:sparkline:")))
            stream_sync_timer_->start(); // restart: one sync per burst (a screen opening subscribes dozens)
    };
    connect(&hub, &datahub::DataHub::topic_active, this, on_topic);
    connect(&hub, &datahub::DataHub::topic_idle, this, on_topic);

    auto& worker = python::PythonWorker::instance();
    connect(&worker, &python::PythonWorker::daemon_connected, this, [this]() {
        stream_unavailable_ = false;
        sync_stream(true); // a fresh daemon streams nothing until told
    });
    connect(&worker, &python::PythonWorker::event_frame, this, [this](const QString& event, const QJsonObject& f) {
        if (event == QLatin1String("ticks"))
            apply_ticks(f.value(QStringLiteral("ticks")).toArray());
    });
    if (worker.is_ready())
        sync_stream(true);
}

void MarketDataService::set_stream_extras(QObject* owner, const QStringList& symbols) {
    if (!owner)
        return;
    QStringList syms = symbols;
    syms.removeAll(QString());
    syms.sort();
    syms.removeDuplicates();
    const auto it = stream_extras_.constFind(owner);
    if (it != stream_extras_.constEnd() ? *it == syms : syms.isEmpty())
        return;
    if (syms.isEmpty()) {
        stream_extras_.remove(owner);
    } else {
        if (!extras_watched_.contains(owner)) { // once per owner, however often it re-sets
            extras_watched_.insert(owner);
            connect(owner, &QObject::destroyed, this, [this, owner]() {
                extras_watched_.remove(owner);
                if (stream_extras_.remove(owner) && stream_sync_timer_)
                    stream_sync_timer_->start();
            });
        }
        stream_extras_.insert(owner, syms);
    }
    if (stream_sync_timer_)
        stream_sync_timer_->start(); // debounced, like a topic going active/idle
}

void MarketDataService::sync_stream(bool force) {
    if (stream_unavailable_)
        return;
    QStringList syms;
    for (const auto& st : datahub::DataHub::instance().stats()) {
        if (st.subscriber_count <= 0)
            continue;
        // Quotes, and sparklines (their last point follows the tape too).
        if (st.topic.startsWith(QLatin1String("market:quote:")))
            syms << st.topic.mid(13);
        else if (st.topic.startsWith(QLatin1String("market:sparkline:")))
            syms << st.topic.mid(17);
    }
    for (const auto& extra : std::as_const(stream_extras_))
        syms += extra;
    syms.sort();
    syms.removeDuplicates();
    if (!force && syms == stream_syms_)
        return;
    stream_syms_ = syms;
    // Symbols that left the stream must not keep overriding polls.
    for (auto it = last_tick_.begin(); it != last_tick_.end();)
        it = syms.contains(it.key()) ? std::next(it) : last_tick_.erase(it);
    for (auto it = last_raw_ms_.begin(); it != last_raw_ms_.end();)
        it = syms.contains(it.key()) ? std::next(it) : last_raw_ms_.erase(it);
    QJsonObject payload;
    payload[QStringLiteral("symbols")] = QJsonArray::fromStringList(syms);
    QPointer<MarketDataService> self = this;
    python::PythonWorker::instance().submit(
        QStringLiteral("stream_set"), payload, [self, n = syms.size()](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            if (!ok) {
                self->stream_syms_.clear(); // not applied: the next sync must resend
                if (python::PythonWorker::reply_is_transient(ok, result)) {
                    // Timeout / daemon busy or restarting: try again shortly.
                    LOG_WARN("MarketData", "price stream set not answered — retrying: " + err);
                    QTimer::singleShot(5000, self.data(), [self]() {
                        if (self)
                            self->sync_stream(false);
                    });
                    return;
                }
                // A real refusal (yfinance without the live feed): polling carries
                // on as before; a daemon restart tries again.
                self->stream_unavailable_ = true;
                LOG_WARN("MarketData", "price stream unavailable — quotes stay on polling: " + err);
                return;
            }
            LOG_INFO("MarketData", QString("price stream: %1 symbols").arg(n));
        });
}

void MarketDataService::apply_ticks(const QJsonArray& ticks) {
    auto& hub = datahub::DataHub::instance();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<StreamTick> raw;
    raw.reserve(ticks.size());
    for (const auto& tv : ticks) {
        const QJsonObject t = tv.toObject();
        const QString sym = t.value(QStringLiteral("id")).toString();
        const double price = t.value(QStringLiteral("price")).toDouble(qQNaN());
        if (sym.isEmpty() || !std::isfinite(price) || price <= 0)
            continue;
        // int64 proto fields arrive as JSON strings.
        const qint64 tms = t.value(QStringLiteral("time")).toVariant().toLongLong();
        // Proto3 omits a 0, so an absent market_hours IS pre-market.
        const int mh = t.value(QStringLiteral("market_hours")).toInt(0);
        // A fast-lane quote (cash indices) carries its snapshot's previous
        // close; an alias tick (GC=F, relabelled from its contract's print)
        // carries Yahoo's change, so its close is price − change — a pair
        // from one print either way. A plain push print rides the polled
        // quote's own baseline instead.
        const QString source = t.value(QStringLiteral("source")).toString();
        const bool snapshot = source == QLatin1String("quote") || source == QLatin1String("alias");
        double snap_prev = qQNaN();
        if (source == QLatin1String("quote")) {
            snap_prev = t.value(QStringLiteral("prev_close")).toDouble(qQNaN());
        } else if (source == QLatin1String("alias")) {
            const QJsonValue cv = t.value(QStringLiteral("change"));
            const double chg = cv.isUndefined() ? 0.0 : cv.isDouble() ? cv.toDouble() : qQNaN();
            snap_prev = std::isfinite(chg) ? price - chg : qQNaN();
        }
        if (!snapshot) {
            // Every session, in order: a reconnect can replay older prints,
            // and last_tick_ below only orders the regular session's.
            qint64& last_raw = last_raw_ms_[sym];
            if (tms > 0 && tms < last_raw)
                continue;
            last_raw = std::max(last_raw, tms);
            // Proto3 omits a zero, so a print AT the previous close carries no
            // change field: absent is 0, not unknown (a non-number stays NaN).
            const QJsonValue cv = t.value(QStringLiteral("change"));
            const double chg = cv.isUndefined() ? 0.0 : cv.isDouble() ? cv.toDouble() : qQNaN();
            raw.append(StreamTick{sym, price, chg, mh, tms});
        }
        // Regular session only. Extended-hours prints belong to the AFT%/FUT%
        // surfaces (stream_ticks), whose session pairing is different.
        if (mh != 1)
            continue;
        const QJsonValue vv = t.value(QStringLiteral("day_volume"));
        const double vol = vv.isUndefined() ? qQNaN() : vv.toVariant().toDouble();
        LastTick& lt = last_tick_[sym];
        if (tms > 0 && tms < lt.time_ms)
            continue; // out of order
        // A NEW print — the exchange time moved or the price did; a fast-lane
        // re-send of an unchanged quote (every 5 min, even overnight) is not
        // one. The first new print after a quiet half hour opens a session,
        // and a series from before it no longer ends on today's bar.
        const bool new_print = (tms > 0 && tms > lt.time_ms) || price != lt.price;
        if (new_print) {
            qint64& last_print = last_print_ms_[sym];
            if (last_print == 0 || now - last_print > 30 * 60'000) {
                session_start_ms_.insert(sym, now);
                spark_live_.remove(sym);
            }
            last_print = now;
        }
        lt = LastTick{price, vol, tms, now, snap_prev};

        // The sparkline's last point is the in-progress hourly bar's close —
        // the last price — but only once the series HAS today's bar: one
        // fetched before the open ends on yesterday's last bar, and moving
        // that would erase a real point (and the overnight gap). Its own
        // refresh (10-min TTL) brings today's bar; nothing is force-fetched
        // here — a burst of those at the open is what Yahoo throttles.
        if (spark_live_.contains(sym)) {
            const QString spark_topic = QStringLiteral("market:sparkline:") + sym;
            const QVariant sv = hub.peek_raw(spark_topic);
            if (sv.canConvert<QVector<double>>()) {
                QVector<double> pts = sv.value<QVector<double>>();
                if (!pts.isEmpty() && pts.last() != price) {
                    pts.last() = price;
                    hub.publish_live(spark_topic, QVariant::fromValue(pts));
                }
            }
        }

        // Only on a quote polled this process: a hydrated one may be a prior
        // session's, and its baseline (price − change) that session's previous
        // close — the day change would be measured against the wrong day.
        if (!polled_syms_.contains(sym))
            continue; // no polled baseline yet — the cold-start poll lands it
        const QString topic = QStringLiteral("market:quote:") + sym;
        const QVariant cur = hub.peek_raw(topic);
        if (!cur.canConvert<QuoteData>())
            continue;
        const QuoteData was = cur.value<QuoteData>();
        const QuoteData now_q = with_live_price(was, price, vol, snap_prev);
        if (now_q == was)
            continue;
        hub.publish_live(topic, QVariant::fromValue(now_q)); // poll cadence unchanged
    }
    if (!raw.isEmpty())
        emit stream_ticks(raw);
}

void MarketDataService::publish_history_to_hub(const QString& symbol, const QString& period, const QString& interval,
                                               const QVector<HistoryPoint>& points) {
    const QString topic =
        QStringLiteral("market:history:") + symbol + QLatin1Char(':') + period + QLatin1Char(':') + interval;
    datahub::DataHub::instance().publish(topic, QVariant::fromValue(points));
}

void MarketDataService::publish_sparkline_to_hub(const QString& symbol, const QVector<double>& points) {
    // Same rule as a polled quote: the bars lag the tape, so a fresh tick
    // keeps the last point (no snap-back to the older bar close).
    QVector<double> pts = points;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto it = last_tick_.constFind(symbol);
    const auto lp = last_print_ms_.constFind(symbol); // new prints only, not re-sends
    const bool ticking = it != last_tick_.constEnd() && lp != last_print_ms_.constEnd() && now - *lp < 5 * 60'000;
    // Fetched at least 2 min into a session the symbol is ticking in → Yahoo
    // has formed today's first hourly bar, so the series ends on today's
    // in-progress bar, which the ticks may move (apply_ticks). Earlier — or
    // before any tick (app start) — it may still end on yesterday's last bar.
    const auto start = session_start_ms_.constFind(symbol);
    const bool live = ticking && start != session_start_ms_.constEnd() && now - *start >= 2 * 60'000;
    if (live)
        spark_live_.insert(symbol);
    else
        spark_live_.remove(symbol);
    if (live && !pts.isEmpty() && now - it->seen_ms < kTickWinsOverPollMs)
        pts.last() = it->price;
    datahub::DataHub::instance().publish(QStringLiteral("market:sparkline:") + symbol, QVariant::fromValue(pts));
}

void MarketDataService::ensure_registered_with_hub() {
    if (hub_registered_)
        return;
    auto& hub = datahub::DataHub::instance();
    hub.register_producer(this);

    // Quotes: 30s TTL, 2s min interval. Dropped min_interval from 5s → 2s so
    // user-triggered refreshes and initial cold-start paint don't queue behind
    // the gate. 2s still prevents hammering yfinance on scheduler ticks.
    datahub::TopicPolicy quote_p;
    quote_p.ttl_ms = 30'000;
    quote_p.min_interval_ms = 2'000;
    hub.set_policy_pattern(QStringLiteral("market:quote:*"), quote_p);

    // Sparklines: 5-day hourly data, changes slowly — cache 10 minutes,
    // refresh at most every 30s. Reduced from 60s so a user flipping between
    // screens doesn't wait a full minute for sparkline refresh after swapping
    // the symbol set.
    datahub::TopicPolicy spark_p;
    spark_p.ttl_ms = 10 * 60'000;
    spark_p.min_interval_ms = 30'000;
    hub.set_policy_pattern(QStringLiteral("market:sparkline:*"), spark_p);

    // History: OHLCV series. One-shot per chart; policies are conservative to
    // avoid re-fetching for every open of the same chart. 30 min TTL, 60s
    // min-interval (was 5 min) so a user flipping periods/intervals on a
    // chart doesn't stare at a stale view for 5 minutes.
    datahub::TopicPolicy hist_p;
    hist_p.ttl_ms = 30 * 60'000;
    hist_p.min_interval_ms = 60'000;
    hub.set_policy_pattern(QStringLiteral("market:history:*"), hist_p);

    // Movers: Yahoo's day_gainers/day_losers screeners refresh about once a
    // minute; every screen showing them reads this one topic.
    datahub::TopicPolicy movers_p;
    movers_p.ttl_ms = 60'000;
    movers_p.min_interval_ms = 60'000;
    hub.set_policy(kMoversTopic, movers_p);

    hub_registered_ = true;
    init_stream();
    LOG_INFO("DataHub", "MarketDataService registered (quote:*, sparkline:*, history:*, movers)");

    // Cold-start hydration is deferred to the next event-loop tick so it
    // never competes with the LockScreen for input handling during the
    // first seconds after launch. ensure_registered_with_hub() is called
    // synchronously from main() before MainWindow is constructed; doing
    // disk I/O + N publishes here would extend the pre-paint critical
    // path that already blocks the lock screen from showing/responding.
    //
    // The deferred hydration still runs well before any widget subscribes
    // (widgets subscribe via showEvent — after the user has unlocked),
    // so the cached values are in place by the time the dashboard
    // requests them. DataHub::deliver_initial_value then serves them
    // instantly. The scheduler's normal refresh replaces stale values
    // with fresh data within seconds; users see "stale → fresh" instead
    // of "blank → fresh".
    QTimer::singleShot(0, this, [this]() { hydrate_quotes_from_cache(); });
}

void MarketDataService::store_quote_entries(const QString& symbol, const QJsonObject& full) {
    // 30s key holds the live snapshot including bid/ask.
    finterm::CacheManager::instance().put(
        "market:" + symbol, QVariant(QString::fromUtf8(QJsonDocument(full).toJson(QJsonDocument::Compact))),
        kQuoteCacheTtlSec, "market_data");

    // 7d "last-known" key is a cold-start fallback. Order-book fields
    // (bid/ask/bid_size/ask_size) go stale within seconds, so they are
    // stripped from the long-TTL payload — the UI would otherwise show a
    // week-old spread as if it were live.
    QJsonObject lon = full;
    lon.remove("bid");
    lon.remove("ask");
    lon.remove("bid_size");
    lon.remove("ask_size");
    // Deliberately NOT stamped into the payload: the write time comes from the
    // cache row (expires_at − ttl_seconds, see CacheManager::multi_get_aged).
    // A payload stamp is only as good as every writer remembering it, and the
    // bug this whole path exists to fix was exactly one writer forgetting.
    finterm::CacheManager::instance().put(
        "market_last:" + symbol, QVariant(QString::fromUtf8(QJsonDocument(lon).toJson(QJsonDocument::Compact))),
        kQuoteLastKnownTtlSec, "market_data");
}

void MarketDataService::hydrate_quotes_from_cache() {
    // Decode every cached entry into the pending queue here — this is one
    // SQLite read (fast) plus N small QJsonDocument::fromJson calls. The
    // expensive part (publishing to the hub, which fans out to subscribers
    // and emits state-transition signals) is split off into chunks below.
    const auto last_known = finterm::CacheManager::instance().get_prefix_aged("market_last:");
    hydrate_pending_.clear();
    hydrate_pending_.reserve(last_known.size());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = last_known.cbegin(); it != last_known.cend(); ++it) {
        const QString sym = it.key().mid(sizeof("market_last:") - 1);
        if (sym.isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(it.value().value.toUtf8()).object();
        if (o.isEmpty())
            continue;
        // The cache holds up to 7 days so a rate-limited session still has
        // something to show. That is right for a fallback and wrong for a
        // cold-start publish: past a couple of sessions the number is not a
        // useful approximation of the market price, and an empty cell is
        // more honest than a stale one. The live fetch fills it in seconds.
        // An unparseable write time is treated as too old, matching
        // PortfolioService's reader. The two used to disagree on this — one
        // published on "age unknown" and the other suppressed — which showed
        // the same holding at two different prices on two screens.
        const QDateTime written = it.value().written_at;
        if (!written.isValid() || now - written.toSecsSinceEpoch() > kHydrateMaxAgeSec)
            continue;
        // Deliberately no age field on QuoteData: DataHub skips re-publishing
        // a byte-identical quote, and a per-fetch timestamp would make every
        // tick look changed and defeat that. The age gate above is what keeps
        // a stale price off the screen.
        QuoteData qd = quote_from_cache(o, sym);
        // A restored price is a last-known level; an order book from an
        // earlier session is not a book at all.
        qd.bid = qd.ask = qd.bid_size = qd.ask_size = 0;
        hydrate_pending_.append(qd);
    }
    hydrate_pending_total_ = hydrate_pending_.size();
    LOG_INFO("DataHub", QString("MarketDataService hydration queued %1 quote(s) — "
                                "publishing in chunks of %2 to keep input responsive")
                            .arg(hydrate_pending_total_)
                            .arg(kHydrateChunkSize));
    if (!hydrate_pending_.isEmpty())
        QTimer::singleShot(0, this, [this]() { hydrate_next_chunk(); });
}

void MarketDataService::hydrate_next_chunk() {
    // Publish at most kHydrateChunkSize entries this event-loop turn. Each
    // publish_quote_to_hub call walks DataHub subscribers (during startup
    // typically zero; later potentially many) and emits the freshness-
    // state signal — keeping the chunk small bounds the main-thread cost
    // per slice to ~1–2 ms, leaving room for PIN keystrokes and queued
    // video frames to be delivered between chunks.
    const int n = std::min(kHydrateChunkSize, static_cast<int>(hydrate_pending_.size()));
    for (int i = 0; i < n; ++i)
        // A poll that already landed is newer than any persisted quote — and
        // is the tick baseline (polled_syms_): never paint the old one over it.
        if (!polled_syms_.contains(hydrate_pending_[i].symbol))
            publish_quote_to_hub(hydrate_pending_[i], /*polled=*/false);
    hydrate_pending_.erase(hydrate_pending_.begin(), hydrate_pending_.begin() + n);

    if (!hydrate_pending_.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { hydrate_next_chunk(); });
    } else if (hydrate_pending_total_ > 0) {
        LOG_INFO("DataHub",
                 QString("MarketDataService hydration complete (%1 quote(s) published)").arg(hydrate_pending_total_));
        hydrate_pending_total_ = 0;
    }
}

// ── Cache invalidation ──────────────────────────────────────────────────────

void MarketDataService::invalidate_quotes(const QStringList& symbols) {
    if (symbols.isEmpty()) {
        // Bulk clear: drop everything in the "market:" prefix bucket.
        finterm::CacheManager::instance().remove_prefix("market:");
        return;
    }
    for (const auto& sym : symbols)
        finterm::CacheManager::instance().remove("market:" + sym);
}

// ── Batched + Cached fetch_quotes ───────────────────────────────────────────

QJsonObject MarketDataService::quote_json(const QuoteData& q) {
    auto num = [](double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(); };
    auto price = [](double v) { return std::isfinite(v) && v > 0 ? QJsonValue(v) : QJsonValue(); };
    QJsonObject o{
        {"symbol", q.symbol},
        {"price", price(q.price)},
        {"change", num(q.change)},
        {"change_pct", num(q.change_pct)},
        {"open", price(q.open)},
        {"prev_close", price(q.prev_close)},
        {"day_high", price(q.high)},
        {"day_low", price(q.low)},
        {"volume", num(q.volume)},
    };
    if (!q.name.isEmpty())
        o["name"] = q.name;
    return o;
}

QDateTime MarketDataService::quote_as_of(const QString& symbol) const {
    const auto aged = finterm::CacheManager::instance().try_get_aged(QStringLiteral("market:") + symbol);
    return aged ? aged->written_at : QDateTime();
}

void MarketDataService::fetch_quotes(const QStringList& symbols, QuoteCallback cb, bool stale_on_failure) {
    if (symbols.isEmpty()) {
        later([cb] { cb(true, {}, {}); });
        return;
    }

    // Check if ALL requested symbols are cached and fresh — single SELECT for
    // the whole cohort. The previous implementation did N round-trips and
    // bailed on the first miss, but each round-trip still acquired the cache
    // DB mutex, which stalled the GUI thread on portfolio-sized requests.
    QStringList cache_keys;
    cache_keys.reserve(symbols.size());
    for (const auto& sym : symbols)
        cache_keys.append("market:" + sym);
    const QHash<QString, QString> hits = finterm::CacheManager::instance().multi_get(cache_keys);

    if (hits.size() == symbols.size()) {
        QVector<QuoteData> cached_results;
        cached_results.reserve(symbols.size());
        for (const auto& sym : symbols) {
            const auto it = hits.constFind("market:" + sym);
            if (it == hits.constEnd()) {
                // Defensive: shouldn't happen given the size check above.
                cached_results.clear();
                break;
            }
            cached_results.append(quote_from_cache(QJsonDocument::fromJson(it.value().toUtf8()).object(), sym));
        }
        if (!cached_results.isEmpty()) {
            overlay_live(cached_results);
            later([cb, cached_results] { cb(true, cached_results, {}); });
            return;
        }
    }

    // Queue for batching
    pending_.append({symbols, std::move(cb), stale_on_failure});

    if (!batch_scheduled_) {
        batch_scheduled_ = true;
        QTimer::singleShot(100, this, &MarketDataService::flush_batch);
    }
}

void MarketDataService::flush_batch() {
    batch_scheduled_ = false;

    if (pending_.isEmpty())
        return;

    // Collect all unique symbols from pending requests
    QSet<QString> all_symbols_set;
    for (const auto& req : pending_) {
        for (const auto& sym : req.symbols) {
            all_symbols_set.insert(sym);
        }
    }
    QStringList all_symbols = all_symbols_set.values();

    // Take ownership of pending callbacks
    auto requests = std::move(pending_);
    pending_.clear();

    LOG_INFO("MarketData",
             QString("Batch fetch: %1 unique symbols from %2 requests").arg(all_symbols.size()).arg(requests.size()));

    // Persistent yfinance daemon — same import is reused across refreshes,
    // saving the ~1.5s pandas/yfinance cold-import per call.
    QJsonArray syms_arr;
    for (const auto& s : all_symbols)
        syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    python::PythonWorker::instance().submit(
        "batch_quotes", payload,
        [this, requests = std::move(requests), all_symbols](bool ok, QJsonObject result, QString err) {
            // Only this batch's symbols: PythonWorker may answer with a merged
            // request's union — the other symbols are stored by their own
            // caller.
            const QSet<QString> mine(all_symbols.begin(), all_symbols.end());
            QVector<QuoteData> all_quotes;
            QSet<QString> refused_syms; // rows Yahoo refused (transient)

            if (ok) {
                // Daemon's batch_quotes returns a flat list; PythonWorker
                // wraps it under "_value". Fall back to "quotes" for forward
                // compatibility if the daemon ever returns a wrapped shape.
                const QJsonArray quotes =
                    result.contains("_value") ? result.value("_value").toArray() : result.value("quotes").toArray();
                for (const auto& v : quotes) {
                    auto q = v.toObject();
                    if (q.value("transient").toBool())
                        refused_syms.insert(q["symbol"].toString());
                    if (q.contains("error") || !mine.contains(q["symbol"].toString()))
                        continue;
                    const QuoteData quote = quote_from_daemon(q);
                    all_quotes.append(quote);
                    store_quote_entries(quote.symbol, quote_to_cache(quote));
                    publish_quote_to_hub(quote, /*polled=*/true);
                }
                LOG_INFO("MarketData", QString("Fetched %1 quotes (cached)").arg(all_quotes.size()));
            } else {
                LOG_WARN("MarketData", "Batch fetch failed: " + err);
            }

            // Fan out results to each waiting callback, filtered to their requested symbols
            for (const auto& req : requests) {
                if (!ok && !req.stale_on_failure) {
                    req.cb(false, {}, err.isEmpty() ? QStringLiteral("quote fetch failed") : err);
                    continue;
                }
                if (!ok) {
                    // On failure, try to serve from stale cache (ignoring TTL)
                    QVector<QuoteData> stale;
                    for (const auto& sym : req.symbols) {
                        const QVariant cv = finterm::CacheManager::instance().get("market:" + sym);
                        if (!cv.isNull()) {
                            stale.append(
                                quote_from_cache(QJsonDocument::fromJson(cv.toString().toUtf8()).object(), sym));
                        }
                    }
                    req.cb(!stale.isEmpty(), stale, err);
                    continue;
                }

                QSet<QString> wanted(req.symbols.begin(), req.symbols.end());
                QVector<QuoteData> filtered;
                for (const auto& q : all_quotes) {
                    if (wanted.contains(q.symbol)) {
                        filtered.append(q);
                    }
                }
                // A caller pacing its asks hears a partial refusal (some of
                // ITS tickers refused by Yahoo) as one, with what did answer.
                const bool refused_mine = !req.stale_on_failure && wanted.intersects(refused_syms);
                overlay_live(filtered);
                req.cb(!refused_mine, filtered,
                       refused_mine ? QStringLiteral("Yahoo refused the quote (rate limit)") : QString());
            }
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── News fetch (unchanged) ──────────────────────────────────────────────────

void MarketDataService::fetch_news(const QString& symbol, int count, NewsCallback cb) {
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["count"] = count;
    python::PythonWorker::instance().submit(
        "news", payload,
        [cb](bool ok, QJsonObject result, QString err) {
            // The daemon's own failure is in the payload: {"error": ..., "articles": []}.
            if (ok && !result.value("error").toString().isEmpty()) {
                ok = false;
                err = result.value("error").toString();
            }
            if (!ok) {
                LOG_WARN("MarketData", "News fetch failed: " + err);
                const bool limited = err.contains(QLatin1String("Rate limited"), Qt::CaseInsensitive) ||
                                     err.contains(QLatin1String("Too Many Requests"), Qt::CaseInsensitive);
                cb(false, {},
                   limited ? QStringLiteral("Yahoo is rate-limiting this machine")
                           : (err.isEmpty() ? QStringLiteral("news fetch failed") : err.left(120)));
                return;
            }
            // Daemon returns {articles: [...]} for news.
            cb(true, result.value("articles").toArray(), QString());
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Live order-book snapshot (single-symbol fast_info) ─────────────────────
//
// Routed to its own daemon action so the per-symbol fast_info HTTP call
// stays out of the batch_quotes hot path — calling fast_info inside the
// batch loop balloons a 50-holding refresh from ~2s to 20-40s and trips
// yfinance's rate limit. No caching: bid/ask go stale within seconds and
// callers want the freshest snapshot.
void MarketDataService::fetch_orderbook(const QString& symbol, OrderbookCallback cb) {
    if (symbol.isEmpty()) {
        cb(false, {});
        return;
    }
    QJsonObject payload;
    payload["symbol"] = symbol;
    python::PythonWorker::instance().submit(
        "quote_orderbook", payload,
        [cb, symbol](bool ok, QJsonObject result, QString err) {
            // The Python `get_orderbook` action returns zeros for unavailable
            // fields (closed market, illiquid ticker, paid-feed exchange) and
            // never emits {"error": ...} — exceptions are caught upstream. So
            // the only failure mode that reaches here is daemon-level (timeout,
            // process crash), captured by `ok=false`.
            if (!ok) {
                LOG_WARN("MarketData", "Orderbook fetch failed for " + symbol + ": " + err.left(200));
                cb(false, {});
                return;
            }
            OrderbookData ob;
            ob.symbol = result["symbol"].toString(symbol);
            ob.bid = result["bid"].toDouble();
            ob.ask = result["ask"].toDouble();
            ob.bid_size = result["bid_size"].toDouble();
            ob.ask_size = result["ask_size"].toDouble();
            cb(true, ob);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Info fetch (company fundamentals) ───────────────────────────────────────

void MarketDataService::fetch_info_raw(const QString& symbol_in, InfoReplyCallback cb) {
    const QString symbol = symbol_in.trimmed().toUpper(); // one entry per company, however it was typed
    // Two caches stand between a reader and Yahoo: this one and the daemon's
    // (_CACHE_TTL). A reader sees their SUM, so the daemon's is kept well
    // inside this one. Info carries analyst targets, which move on report days.
    const QString key = QStringLiteral("info:v1:") + symbol;
    if (const auto hit = finterm::CacheManager::instance().try_get_aged(key)) {
        InfoReply r;
        r.ok = true;
        r.info = QJsonDocument::fromJson(hit->value.toUtf8()).object();
        r.as_of = hit->written_at;
        later([cb, r] { cb(r); });
        return;
    }
    auto& waiting = info_waiters_[symbol];
    waiting.append(std::move(cb));
    if (waiting.size() == 1)
        request_info(symbol, /*tries_left=*/1);
}

void MarketDataService::request_info(const QString& symbol, int tries_left) {
    QJsonObject payload;
    payload["symbol"] = symbol;
    QPointer<MarketDataService> self = this;
    python::PythonWorker::instance().submit(
        "info", payload,
        [self, symbol, tries_left](bool ok, QJsonObject obj, QString err) {
            if (!self)
                return;
            // One retry after a TIMEOUT of the daemon call itself (no body):
            // usually a network blip (a DNS resolve that timed out took every
            // provider down for four minutes on 2026-09-06). A vendor error
            // ({"error": …}) is a fact about the symbol and is not retried.
            if (!ok && !obj.contains("error") && tries_left > 0 &&
                err.contains(QLatin1String("timeout"), Qt::CaseInsensitive)) {
                LOG_WARN("MarketData", QString("info timed out for %1 — retrying once").arg(symbol));
                QTimer::singleShot(4000, self.data(), [self, symbol, tries_left] {
                    if (self)
                        self->request_info(symbol, tries_left - 1);
                });
                return;
            }
            InfoReply r;
            if (!ok || obj.contains("error")) {
                r.error = obj.value("error").toString(err.isEmpty() ? QStringLiteral("info fetch failed") : err);
                // A daemon {"error"} arrives as ok:false WITH its body, which
                // says whether it was a refusal; no body counts as one.
                r.transient = python::PythonWorker::reply_is_transient(ok, obj);
                LOG_WARN("MarketData", QString("info failed for %1%2: %3")
                                           .arg(symbol, r.transient ? " (transient)" : "", r.error.left(160)));
            } else {
                r.ok = true;
                r.info = obj;
                r.as_of = QDateTime::currentDateTime();
                finterm::CacheManager::instance().put(
                    QStringLiteral("info:v1:") + symbol,
                    QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)), kInfoTtlSec, "market_data");
            }
            const auto waiters = self->info_waiters_.take(symbol);
            for (const auto& w : waiters)
                w(r);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

void MarketDataService::fetch_info(const QString& symbol, InfoCallback cb) {
    // Use financial_ratios command — it has all ratio fields in one call
    // and falls back gracefully. We run two parallel Python calls and merge:
    // 1) get_info  → name, sector, market cap, 52W range, beta, avg volume
    // 2) financial_ratios → P/E, P/B, dividend yield, ROE, margins, D/E, current ratio

    struct Partial {
        InfoData info;
        bool info_done = false;
        bool ratios_done = false;
        bool info_ok = false;
        bool ratios_ok = false;
    };
    auto shared = std::make_shared<Partial>();

    auto try_complete = [cb, shared, symbol]() {
        if (!shared->info_done || !shared->ratios_done)
            return;
        bool ok = shared->info_ok || shared->ratios_ok;
        if (ok)
            LOG_INFO("MarketData", "Fetched info for " + symbol);
        else
            LOG_WARN("MarketData", "Both info+ratios failed for " + symbol);
        cb(ok, shared->info);
    };

    // ── Call 1: the one company-info read ───────────────────────────────────
    fetch_info_raw(symbol, [shared, symbol, try_complete](const InfoReply& reply) {
            shared->info_done = true;
            if (!reply.ok) {
                shared->info.info_transient = reply.transient;
                try_complete();
                return;
            }
            const QJsonObject& o = reply.info;
            shared->info.symbol = o["symbol"].toString(symbol);
            shared->info.name = o["company_name"].toString();
            // The daemon's get_info defaults missing string fields to "N/A"
            // for human display. The C++ side prefers an empty QString so
            // callers can branch on `.isEmpty()` instead of doing the
            // sentinel check at every consumer.
            auto scrub = [](const QJsonValue& v) {
                QString s = v.toString();
                return s == "N/A" ? QString() : s;
            };
            shared->info.sector = scrub(o["sector"]);
            shared->info.industry = scrub(o["industry"]);
            shared->info.country = scrub(o["country"]);
            shared->info.currency = scrub(o["currency"]);
            shared->info.market_cap = num(o, "market_cap");
            shared->info.beta = num(o, "beta");
            shared->info.week52_high = num(o, "fifty_two_week_high");
            shared->info.week52_low = num(o, "fifty_two_week_low");
            shared->info.avg_volume = num(o, "average_volume");
            // Symmetric with the ratios callback
            // below: the two run in parallel with
            // independent cache entries and can
            // disagree, so neither may clear a
            // value the other supplied.
            if (const double rps = num(o, "revenue_per_share"); std::isfinite(rps))
                shared->info.eps = rps;
            shared->info.description = scrub(o["description"]);
            shared->info.website = scrub(o["website"]);
            shared->info.employees = o["employees"].toInt();
            const QJsonArray off_arr = o["officers"].toArray();
            for (const auto& v : off_arr) {
                const QJsonObject obj = v.toObject();
                OfficerInfo co;
                co.name = obj["name"].toString();
                co.title = obj["title"].toString();
                if (!co.name.isEmpty())
                    shared->info.officers.append(co);
            }
            shared->info_ok = true;
            shared->info.info_answered = true;
            try_complete();
    });

    // ── Call 2: financial_ratios via daemon ───────────────────────────────────
    QJsonObject ratios_payload;
    ratios_payload["symbol"] = symbol;
    python::PythonWorker::instance().submit(
        "financial_ratios", ratios_payload,
        [shared, symbol, try_complete](bool ok, QJsonObject o, QString /*err*/) {
            shared->ratios_done = true;
            if (!ok || o.contains("error")) {
                LOG_WARN("MarketData", "financial_ratios failed for " + symbol);
                try_complete();
                return;
            }
            shared->info.pe_ratio = num(o, "peRatio");
            shared->info.forward_pe = num(o, "forwardPE");
            shared->info.price_to_book = num(o, "priceToBook");
            shared->info.dividend_yield = num(o, "dividendYield");
            shared->info.roe = num(o, "returnOnEquity");
            shared->info.profit_margin = num(o, "profitMargin");
            shared->info.debt_to_equity = num(o, "debtToEquity");
            shared->info.current_ratio = num(o, "currentRatio");
            // get_info runs in PARALLEL with this
            // and already set eps from its own
            // revenue_per_share. Only overwrite
            // with a real number — whichever reply
            // lands second must not clear a value
            // the other one supplied.
            if (const double rps = num(o, "revenuePerShare"); std::isfinite(rps))
                shared->info.eps = rps;
            shared->ratios_ok = true;
            try_complete();
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── History fetch (OHLCV) ────────────────────────────────────────────────────


void MarketDataService::fetch_bars(const QString& symbol_in, const QString& period, const QString& interval,
                                   BarsCallback cb, Adjust adjust) {
    const QString symbol = symbol_in.trimmed().toUpper(); // one series however the ticker was typed
    const bool adjusted = adjust == Adjust::Adjusted;
    const QString key = bars_key(symbol, period, interval, adjusted);
    if (const auto hit = finterm::CacheManager::instance().try_get_aged(key)) {
        BarsReply r;
        r.rows = QJsonDocument::fromJson(hit->value.toUtf8()).array();
        r.bars = bars::parse(r.rows);
        r.ok = true;
        r.as_of = hit->written_at;
        later([cb, r] { cb(r); });
        return;
    }
    auto& waiting = bars_waiters_[key];
    waiting.append(std::move(cb));
    if (waiting.size() > 1)
        return; // the request already out answers this caller too

    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["period"] = period;
    payload["interval"] = interval;
    if (!adjusted)
        payload["auto_adjust"] = false;
    QPointer<MarketDataService> self = this;
    python::PythonWorker::instance().submit(
        "historical_period", payload,
        [self, key, symbol, interval](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            BarsReply r;
            if (!ok || result.contains("error")) {
                r.error = result.value("error").toString(err.isEmpty() ? QStringLiteral("history fetch failed") : err);
            } else {
                // An answer — possibly "no history" (no bars). Only a series
                // with priced bars is cached: an empty one is asked again.
                r.ok = true;
                const QJsonArray raw = result.value("_value").toArray();
                r.rows = bars::priced_rows(raw);
                if (r.rows.size() < raw.size())
                    LOG_WARN("MarketData", QString("history %1: dropped %2 price-less bar(s) of %3 — vendor sent "
                                                   "null/NaN OHLC")
                                               .arg(key)
                                               .arg(raw.size() - r.rows.size())
                                               .arg(raw.size()));
                r.bars = bars::parse(r.rows);
                r.as_of = QDateTime::currentDateTime();
                if (!r.rows.isEmpty())
                    finterm::CacheManager::instance().put(
                        key, QString::fromUtf8(QJsonDocument(r.rows).toJson(QJsonDocument::Compact)),
                        bars_ttl_sec(interval), "market_data");
            }
            if (!r.ok)
                LOG_WARN("MarketData", QString("history %1 (%2): %3").arg(symbol, key, r.error.left(160)));
            const auto waiters = self->bars_waiters_.take(key);
            for (const auto& w : waiters)
                w(r);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Batch company-profile lookup (sector / industry / market cap / …) ───────

void MarketDataService::fetch_company_profiles(const QStringList& symbols, ProfileCallback cb) {
    if (symbols.isEmpty()) {
        cb(true, {});
        return;
    }

    // Cache strategy: profile fields change rarely (sector/industry/longName
    // are essentially stable per-company), so we cache for 7 days under the
    // "ipo_info:" prefix. Only un-cached symbols hit the Python daemon.
    constexpr int kProfileCacheTtlSec = 7 * 24 * 60 * 60;
    QStringList cache_keys;
    cache_keys.reserve(symbols.size());
    for (const auto& s : symbols)
        cache_keys.append("ipo_info:" + s);
    const QHash<QString, QString> hits = finterm::CacheManager::instance().multi_get(cache_keys);

    auto parse_cached = [](const QString& raw, const QString& sym) -> CompanyProfile {
        const QJsonObject o = QJsonDocument::fromJson(raw.toUtf8()).object();
        CompanyProfile p;
        p.symbol = sym;
        p.long_name = o["long_name"].toString();
        p.sector = o["sector"].toString();
        p.industry = o["industry"].toString();
        p.website = o["website"].toString();
        p.country = o["country"].toString();
        p.market_cap = o["market_cap"].toDouble();
        p.employees = o["employees"].toInt();
        return p;
    };

    QVector<CompanyProfile> cached_out;
    QStringList missing;
    cached_out.reserve(symbols.size());
    for (const auto& sym : symbols) {
        const auto it = hits.constFind("ipo_info:" + sym);
        if (it == hits.constEnd())
            missing.append(sym);
        else if (!it.value().contains(QLatin1String("\"none\":true")))
            cached_out.append(parse_cached(it.value(), sym));
        else {
            // Yahoo answered "unknown ticker" within the last hour — not
            // re-asked, and said as such (not left out, which a caller
            // can't tell from "no answer").
            CompanyProfile none;
            none.symbol = sym;
            none.known_none = true;
            cached_out.append(none);
        }
    }
    if (missing.isEmpty()) {
        LOG_INFO("MarketData", QString("fetch_company_profiles: %1 cached, 0 fresh").arg(cached_out.size()));
        cb(true, cached_out);
        return;
    }

    QJsonArray syms_arr;
    for (const auto& s : missing)
        syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    python::PythonWorker::instance().submit(
        "batch_info", payload,
        [cb, cached_out, missing](bool ok, QJsonObject result, QString err) {
            QVector<CompanyProfile> out = cached_out;
            if (!ok) {
                LOG_WARN("MarketData", "batch_info failed: " + err);
                if (!python::PythonWorker::reply_is_transient(ok, result)) {
                    // Not a refusal (a bug in the lookup): every asked ticker
                    // is reported `failed` — a pacing caller settles them
                    // rather than re-sending the same failing chunk forever.
                    for (const auto& sym : missing) {
                        CompanyProfile f;
                        f.symbol = sym;
                        f.failed = true;
                        out.append(f);
                    }
                    cb(true, out);
                    return;
                }
                // A refusal: failed is failed, even with cached rows along —
                // a caller pacing its asks must see it (the rows still help).
                cb(false, out);
                return;
            }
            const QJsonArray rows =
                result.contains("_value") ? result.value("_value").toArray() : result.value("profiles").toArray();
            for (const auto& v : rows) {
                const QJsonObject o = v.toObject();
                CompanyProfile p;
                p.symbol = o["symbol"].toString();
                if (p.symbol.isEmpty())
                    continue;
                if (o.contains("error")) {
                    if (o.value("transient").toBool()) {
                        // Refused for this ticker: said so, not left out (a
                        // caller would settle a missing ticker as "none").
                        CompanyProfile refused;
                        refused.symbol = p.symbol;
                        refused.refused = true;
                        out.append(refused);
                        continue;
                    }
                    // Yahoo ANSWERED that the ticker is unknown (a 404 —
                    // "not_found"): remember that for an hour so it isn't
                    // re-asked every session. A parse error or a yfinance bug
                    // is not an answer: reported as `failed`, not cached (the
                    // next session asks again).
                    if (o.value("not_found").toBool()) {
                        finterm::CacheManager::instance().put("ipo_info:" + p.symbol,
                                                              QVariant(QStringLiteral("{\"none\":true}")),
                                                              // An hour, not a day: a deal priced last night
                                                              // is unknown to Yahoo until it starts trading.
                                                              60 * 60, "ipo_info");
                        CompanyProfile none;
                        none.symbol = p.symbol;
                        none.known_none = true;
                        out.append(none);
                    } else {
                        CompanyProfile failed;
                        failed.symbol = p.symbol;
                        failed.failed = true;
                        out.append(failed);
                    }
                    continue;
                }
                p.long_name = o["long_name"].toString();
                p.sector = o["sector"].toString();
                p.industry = o["industry"].toString();
                p.website = o["website"].toString();
                p.country = o["country"].toString();
                p.market_cap = o["market_cap"].toDouble();
                p.employees = o["employees"].toInt();
                out.append(p);

                // Cache for 7 days — sector rarely changes.
                QJsonObject co;
                co["long_name"] = p.long_name;
                co["sector"] = p.sector;
                co["industry"] = p.industry;
                co["website"] = p.website;
                co["country"] = p.country;
                co["market_cap"] = p.market_cap;
                co["employees"] = p.employees;
                finterm::CacheManager::instance().put(
                    "ipo_info:" + p.symbol,
                    QVariant(QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact))), kProfileCacheTtlSec,
                    "ipo_info");
            }
            LOG_INFO("MarketData",
                     QString("fetch_company_profiles: %1 cached, %2 fresh").arg(cached_out.size()).arg(missing.size()));
            cb(true, out);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Top movers (real day_gainers / day_losers screeners) ───────────────────
// 60-second CacheManager TTL — market movers shift through the trading day
// but cycling between gainers/losers tabs shouldn't burn a fresh daemon call
// each time. Slightly stale-but-real data beats hardcoded-watchlist data.
namespace {
constexpr int kMoversTtlSec = 60; // the screener's own refresh
const QString kMoversCacheKey = QStringLiteral("top_movers:v2:%1").arg(MarketDataService::kMoversMax);

QVector<QuoteData> parse_movers_side(const QJsonArray& arr) {
    QVector<QuoteData> out;
    out.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        QuoteData q;
        q.symbol = o["symbol"].toString();
        q.name = o["name"].toString();
        q.price = o["price"].toDouble();
        q.change = o["change"].toDouble(qQNaN());
        q.change_pct = o["change_pct"].toDouble(qQNaN());
        q.high = o["high"].toDouble();
        q.low = o["low"].toDouble();
        q.volume = o["volume"].toDouble(qQNaN());
        if (!q.symbol.isEmpty())
            out.append(q);
    }
    return out;
}

MarketDataService::TopMovers parse_movers(const QJsonObject& o) {
    MarketDataService::TopMovers tm;
    tm.gainers = parse_movers_side(o["gainers"].toArray());
    tm.losers = parse_movers_side(o["losers"].toArray());
    return tm;
}
} // namespace

void MarketDataService::fetch_movers_all(std::function<void(bool, const TopMovers&, const QString&)> cb, bool fresh) {
    // `fresh`: the hub's refresh — its own policy decides when to ask, so a
    // forced refresh must not be answered from the one-shot cache.
    if (const auto cached = fresh ? std::nullopt : finterm::CacheManager::instance().try_get(kMoversCacheKey)) {
        const TopMovers tm = parse_movers(QJsonDocument::fromJson(cached->toUtf8()).object());
        later([cb, tm] { cb(true, tm, {}); });
        return;
    }
    movers_waiters_.append(std::move(cb));
    if (movers_waiters_.size() > 1)
        return; // the request already out answers this caller too
    QJsonObject payload;
    payload["count"] = kMoversMax;
    QPointer<MarketDataService> self = this;
    python::PythonWorker::instance().submit(
        "top_movers", payload,
        [self](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            const auto waiters = std::exchange(self->movers_waiters_, {});
            // A side the screener refused ({gainers,losers}_error) fails the
            // whole answer, as the daemon's own cache rule does: an empty side
            // read as "no gainers today" would be published to every screen.
            const bool side_failed = result.contains("gainers_error") || result.contains("losers_error");
            if (!ok || result.contains("error") || side_failed) {
                const QString why = result.value("error").toString(
                    result.value("gainers_error").toString(result.value("losers_error").toString(err)));
                LOG_WARN("MarketData", "top_movers failed: " + why);
                for (const auto& w : waiters)
                    w(false, {}, why);
                return;
            }
            finterm::CacheManager::instance().put(
                kMoversCacheKey, QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)),
                kMoversTtlSec, "movers");
            const TopMovers tm = parse_movers(result);
            for (const auto& w : waiters)
                w(true, tm, {});
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

void MarketDataService::fetch_top_movers(int count, TopMoversCallback cb) {
    const int n = qBound(1, count, kMoversMax);
    fetch_movers_all([cb, n](bool ok, const TopMovers& all, const QString&) { cb(ok, all.truncated(n)); });
}

// ── IPO Watch detail-rail enrichment ────────────────────────────────────────
// fetch_ipo_extras: yfinance financials + holders + news in one round-trip.
// fetch_sec_filings: SEC EDGAR submissions API (no key, free).
// fetch_wikipedia_summary: Wikipedia REST summary (no key, free).
// Each is keyed by ticker (or company name for Wikipedia) and is lazy-fetched
// from IpoWatchView when a row is selected — see render_detail.

namespace {
constexpr int kIpoExtrasCacheTtlSec = 15 * 60;          // 15 min — news + financials change slowly
constexpr int kSecFilingsCacheTtlSec = 60 * 60;         // 1h — new SEC filings are infrequent
constexpr int kSecCikMapTtlSec = 7 * 24 * 60 * 60;      // 7d — SEC publishes weekly
constexpr int kWikipediaCacheTtlSec = 7 * 24 * 60 * 60; // 7d — descriptions stable
} // namespace

void MarketDataService::fetch_ipo_extras(const QString& symbol, IpoExtrasCallback cb) {
    if (symbol.isEmpty()) {
        cb(false, {});
        return;
    }

    // 15-minute CacheManager TTL so flipping between rows doesn't re-hit the
    // daemon on every click. The detail rail invalidates this cache on
    // wake-on-resume by clearing IpoWatchView's own info_cache_, but the
    // CacheManager entry stays so subsequent same-session opens are fast.
    const QString cache_key = "ipo_extras:" + symbol;
    auto cached = finterm::CacheManager::instance().try_get(cache_key);
    auto parse = [](const QJsonObject& o, const QString& sym) -> IpoExtras {
        IpoExtras ex;
        ex.symbol = sym;
        auto parse_series = [](const QJsonArray& a) {
            QVector<FinancialPoint> out;
            for (const auto& v : a) {
                const auto r = v.toObject();
                FinancialPoint p;
                p.date = r["date"].toString();
                p.value = r["value"].toDouble();
                if (!p.date.isEmpty())
                    out.append(p);
            }
            return out;
        };
        ex.quarterly_revenue = parse_series(o["quarterly_revenue"].toArray());
        ex.quarterly_net_income = parse_series(o["quarterly_net_income"].toArray());
        ex.annual_revenue = parse_series(o["annual_revenue"].toArray());
        ex.annual_net_income = parse_series(o["annual_net_income"].toArray());
        ex.annual_gross_profit = parse_series(o["annual_gross_profit"].toArray());
        ex.annual_operating_income = parse_series(o["annual_operating_income"].toArray());
        for (const auto& v : o["institutional_holders"].toArray()) {
            const auto r = v.toObject();
            InstitutionalHolder h;
            h.holder = r["holder"].toString();
            h.pct = r["pct"].toDouble();
            h.shares = r["shares"].toDouble();
            h.value = r["value"].toDouble();
            h.date = r["date"].toString();
            if (!h.holder.isEmpty())
                ex.institutional_holders.append(h);
        }
        for (const auto& v : o["major_holders"].toArray()) {
            const auto r = v.toObject();
            MajorHolderRow m;
            m.label = r["label"].toString();
            m.value = r["value"].toDouble();
            if (!m.label.isEmpty())
                ex.major_holders.append(m);
        }
        for (const auto& v : o["news"].toArray()) {
            const auto r = v.toObject();
            NewsItem n;
            n.title = r["title"].toString();
            n.link = r["link"].toString();
            n.publisher = r["publisher"].toString();
            n.ts = r["ts"].toString();
            if (!n.title.isEmpty())
                ex.news.append(n);
        }
        return ex;
    };
    if (cached.has_value()) {
        const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
        cb(true, parse(obj, symbol));
        return;
    }

    QJsonObject payload;
    payload["symbol"] = symbol;
    python::PythonWorker::instance().submit(
        "ipo_extras", payload,
        [cb, parse, symbol](bool ok, QJsonObject result, QString err) {
            if (!ok || result.contains("error")) {
                LOG_WARN("MarketData", "ipo_extras failed for " + symbol + ": " + err);
                cb(false, {});
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
            finterm::CacheManager::instance().put("ipo_extras:" + symbol, raw, kIpoExtrasCacheTtlSec, "ipo_extras");
            cb(true, parse(result, symbol));
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── SEC EDGAR ──────────────────────────────────────────────────────────────
// SEC requires a `User-Agent: <organization> <contact>` header. We send a
// stable identifier so they can throttle us instead of blocking the IP.
//
// Unlike the support links this is NOT cosmetic: EDGAR refuses requests
// without a declared contact. The project URL alone used to be accepted; since
// 2026-10 EDGAR answers it with 403 "Undeclared Automated Tool" (filings and
// the ticker map both went dark). The contact is the USER's — Settings ›
// Credentials › "Contact email", kept in the keyring — sent in the same form
// as the Python side (scripts/finterm_identity.py). Read per request: the
// calls are rare and the reader can change the setting at any time.
namespace {

// The one project identity, for every client that must not carry the user's
// address (and as the SEC fallback when none is configured). Same string as
// scripts/finterm_identity.py.
const char* kProjectUserAgent = "finterm (https://github.com/yumima/finterm)";

struct SecContact {
    QString email;            // validated, or empty
    bool unavailable = false; // the keyring could not be read (locked / unreachable) — NOT "unset"
};

using finterm::services::sec_identity::header_safe_email;

// Read at most once a minute: SecureStorage::retrieve is a blocking libsecret
// round-trip on the GUI thread (a locked keyring prompts), and nothing signals
// a change in Settings. A 403 — and only a 403 — clears it, so a contact the
// reader has just set is used on the next request.
struct SecContactCache {
    SecContact contact;
    qint64 read_at = 0; // 0 = read on next use
};

SecContactCache& sec_cache() {
    static SecContactCache c;
    return c;
}

SecContact sec_contact() {
    auto& cache = sec_cache();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // a contact holds for a minute; "none set" only 10 s, so one the reader
    // has just saved is picked up by the next request, not a refusal later
    const qint64 ttl = cache.contact.email.isEmpty() ? 10'000 : 60'000;
    if (cache.read_at && now - cache.read_at < ttl)
        return cache.contact;
    const auto r = finterm::SecureStorage::instance().retrieve(QStringLiteral("FINTERM_CONTACT_EMAIL"));
    SecContact c;
    if (r.is_ok()) {
        const QString e = r.value().trimmed();
        if (header_safe_email(e))
            c.email = e;
        else if (!e.isEmpty())
            LOG_WARN("MarketData", "Contact email is not a plain address (local@domain.tld); not sent to SEC");
    } else {
        c.unavailable = !finterm::SecureStorage::is_not_found(r);
    }
    cache = {c, now};
    return c;
}

QByteArray sec_user_agent(const SecContact& c) {
    return c.email.isEmpty() ? QByteArray(kProjectUserAgent) : QStringLiteral("finterm (%1)").arg(c.email).toUtf8();
}

// A refusal that the CONTACT explains (none sent, or the keyring could not be
// read) re-reads it on the next request: the reader may be fixing it now. A
// 403 despite a sent contact (rate limiting) must not — every click would then
// make a blocking keyring read on the GUI thread.
void sec_refused(const SecContact& sent, const QString& error) {
    if (error == QLatin1String("HTTP_403") && (sent.email.isEmpty() || sent.unavailable))
        sec_cache().read_at = 0;
}

// Why a request failed, from the contact the REQUEST carried (no keyring
// call) and the real error. Only a 403 is about the identity; anything else
// (DNS, timeout, 5xx) is logged as itself. No side effects.
QString sec_failure(const SecContact& sent, const QString& error) {
    if (error != QLatin1String("HTTP_403"))
        return QStringLiteral(" (%1)").arg(error);
    if (sent.unavailable)
        return QStringLiteral(" (HTTP 403 — the keyring could not be read, so no contact was sent; if one is "
                              "configured, unlock the keyring)");
    if (sent.email.isEmpty())
        return QStringLiteral(" (HTTP 403 — SEC refuses requests without a contact: set Settings › Credentials › "
                              "Contact email)");
    return QStringLiteral(" (HTTP 403 although a contact was sent)");
}

} // namespace

void MarketDataService::fetch_sec_filings(const QString& ticker, SecFilingsCallback cb) {
    if (ticker.isEmpty()) {
        cb(false, {});
        return;
    }
    read_sec_submissions(ticker.toUpper(), [cb](const SecFilingsReply& r, bool mapped) {
        if (!mapped) {
            cb(true, {}); // no CIK yet (a fresh IPO): nothing filed under this ticker
            return;
        }
        if (!r.ok) {
            cb(false, {});
            return;
        }
        cb(true, r.filings.mid(0, 25));
    });
}

void MarketDataService::fetch_sec_filings_of_form(const QString& ticker, const QString& form, int limit,
                                                  SecFilingsReplyCallback cb) {
    const QString tkr = ticker.trimmed().toUpper();
    if (tkr.isEmpty()) {
        later([cb]() { cb({false, {}, QStringLiteral("SEC filings: no ticker")}); });
        return;
    }
    QPointer<MarketDataService> self = this;
    read_sec_submissions(tkr, [self, cb, tkr, form, limit](const SecFilingsReply& r, bool mapped) {
        if (!self)
            return;
        SecFilingsReply out;
        if (!mapped) {
            out.error = QStringLiteral("EDGAR's ticker map has no CIK for %1").arg(tkr);
        } else if (!r.ok) {
            out.error = r.error;
        } else {
            out.ok = true;
            for (const auto& f : r.filings) {
                if (out.filings.size() >= limit)
                    break;
                if (form.isEmpty() || f.form == form)
                    out.filings.append(f);
            }
        }
        // read_sec_submissions can answer inside the call (cache hit).
        self->later([cb, out]() { cb(out); });
    });
}

void MarketDataService::read_sec_submissions(const QString& tkr, SecSubmissionsDone done) {
    // Inner closure runs once the CIK map is loaded — looks up the ticker,
    // builds the submissions URL, returns the list of recent filings.
    auto with_cik_map = [this, tkr, done]() {
        auto it = sec_cik_map_.constFind(tkr);
        if (it == sec_cik_map_.constEnd()) {
            LOG_INFO("MarketData", "SEC: no CIK for ticker " + tkr);
            done({}, false);
            return;
        }
        const int cik = it.value();
        const QString cik_padded = QString("%1").arg(cik, 10, 10, QChar('0'));
        const QString cache_key = "sec_filings:" + tkr;
        auto parse = [cik_padded](const QJsonObject& root) -> QVector<SecFiling> {
            QVector<SecFiling> out;
            const auto recent = root["filings"].toObject()["recent"].toObject();
            const auto forms = recent["form"].toArray();
            const auto dates = recent["filingDate"].toArray();
            const auto accessions = recent["accessionNumber"].toArray();
            const auto primary = recent["primaryDocument"].toArray();
            const int n = std::min({forms.size(), dates.size(), accessions.size(), primary.size()});
            out.reserve(n);
            for (int i = 0; i < n; ++i) {
                SecFiling f;
                f.form = forms.at(i).toString();
                f.filing_date = dates.at(i).toString();
                f.accession = accessions.at(i).toString();
                f.primary_doc = primary.at(i).toString();
                // EDGAR doc URL: https://www.sec.gov/Archives/edgar/data/{cik}/{accession-no-dashes}/{primary_doc}
                QString acc_nd = f.accession;
                acc_nd.remove('-');
                f.url = QString("https://www.sec.gov/Archives/edgar/data/%1/%2/%3")
                            .arg(cik_padded.toInt())
                            .arg(acc_nd, f.primary_doc);
                out.append(f);
            }
            return out;
        };
        if (auto cached = finterm::CacheManager::instance().try_get(cache_key); cached.has_value()) {
            const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
            done({true, parse(obj), {}}, true);
            return;
        }

        auto& waiters = sec_submissions_waiters_[tkr];
        waiters.append(done);
        if (waiters.size() > 1)
            return; // the request already out answers this caller too

        const QString url = QString("https://data.sec.gov/submissions/CIK%1.json").arg(cik_padded);
        QPointer<MarketDataService> self_ptr = this;
        QHash<QByteArray, QByteArray> headers;
        const SecContact contact = sec_contact();
        headers["User-Agent"] = sec_user_agent(contact);
        finterm::HttpClient::instance().get(
            url, headers, [parse, tkr, self_ptr, contact](Result<QJsonDocument> res) {
                if (!self_ptr)
                    return;
                SecFilingsReply reply;
                if (!res.is_ok() || !res.value().isObject()) {
                    const QString err =
                        res.is_ok() ? QStringLiteral("not a JSON object") : QString::fromStdString(res.error());
                    sec_refused(contact, err);
                    const QString why = sec_failure(contact, err);
                    LOG_WARN("MarketData", "SEC submissions fetch failed for " + tkr + why);
                    reply.error = QStringLiteral("SEC submissions fetch failed for %1%2").arg(tkr, why);
                } else {
                    const auto obj = res.value().object();
                    const QString raw = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
                    finterm::CacheManager::instance().put("sec_filings:" + tkr, raw, kSecFilingsCacheTtlSec, "sec");
                    reply = {true, parse(obj), {}};
                }
                const auto waiters = self_ptr->sec_submissions_waiters_.take(tkr);
                for (const auto& w : waiters)
                    w(reply, true);
            });
    };

    if (sec_cik_loaded_) {
        with_cik_map();
        return;
    }
    // A failed ticker map is a FAILED fetch, not "this company has no CIK": the
    // reader must see an error, not an empty filings list read as fact.
    sec_cik_waiters_.append([with_cik_map, done](bool loaded, const QString& why) {
        if (!loaded) {
            done({false, {}, QStringLiteral("SEC ticker map unavailable%1").arg(why)}, true);
            return;
        }
        with_cik_map();
    });
    if (sec_cik_loading_)
        return;
    sec_cik_loading_ = true;

    // Try CacheManager first — the company_tickers.json mapping has a 7-day
    // TTL since SEC refreshes weekly. Falls through to a live fetch if missing.
    auto fire_waiters = [this](bool ok, const QString& why) {
        sec_cik_loaded_ = ok;
        sec_cik_loading_ = false;
        auto waiters = std::move(sec_cik_waiters_);
        for (auto& w : waiters)
            w(ok, why);
    };
    auto populate_from_json = [this](const QJsonDocument& doc) {
        // company_tickers.json is keyed by row index (strings "0","1",…); each
        // value is {cik_str, ticker, title}.
        const auto root = doc.object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            const auto v = it.value().toObject();
            const QString t = v["ticker"].toString().toUpper();
            const int cik = v["cik_str"].toInt();
            if (!t.isEmpty() && cik > 0)
                sec_cik_map_.insert(t, cik);
        }
    };

    auto cached_map = finterm::CacheManager::instance().try_get("sec_cik_map");
    if (cached_map.has_value()) {
        const auto doc = QJsonDocument::fromJson(cached_map->toUtf8());
        if (doc.isObject()) {
            populate_from_json(doc);
            fire_waiters(true, {});
            return;
        }
    }
    QHash<QByteArray, QByteArray> headers;
    const SecContact contact = sec_contact();
    headers["User-Agent"] = sec_user_agent(contact);
    QPointer<MarketDataService> self_ptr = this;
    finterm::HttpClient::instance().get(
        "https://www.sec.gov/files/company_tickers.json", headers,
        [self_ptr, populate_from_json, fire_waiters, contact](Result<QJsonDocument> res) {
            if (!self_ptr)
                return;
            if (!res.is_ok() || !res.value().isObject()) {
                const QString err =
                    res.is_ok() ? QStringLiteral("not a JSON object") : QString::fromStdString(res.error());
                sec_refused(contact, err);
                const QString why = sec_failure(contact, err);
                LOG_WARN("MarketData", "SEC company_tickers.json fetch failed" + why);
                fire_waiters(false, why);
                return;
            }
            populate_from_json(res.value());
            const QString raw = QString::fromUtf8(res.value().toJson(QJsonDocument::Compact));
            finterm::CacheManager::instance().put("sec_cik_map", raw, kSecCikMapTtlSec, "sec");
            fire_waiters(true, {});
        });
}

// ── Option chain (CBOE) ────────────────────────────────────────────────────
void MarketDataService::fetch_options_chain(const QString& symbol, OptionsChainCallback cb) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty()) {
        later([cb]() { cb({false, {}, {}, QStringLiteral("Options chain: no symbol")}); });
        return;
    }
    const QString cache_key = QStringLiteral("options_chain:cboe:") + sym;
    if (const auto aged = finterm::CacheManager::instance().try_get_aged(cache_key)) {
        const QJsonDocument doc = QJsonDocument::fromJson(aged->value.toUtf8());
        if (doc.isObject()) {
            OptionsChainReply r{true, doc.object(), aged->written_at, {}};
            later([cb, r]() { cb(r); });
            return;
        }
    }
    auto& waiters = options_chain_waiters_[sym];
    waiters.append(std::move(cb));
    if (waiters.size() > 1)
        return; // the request already out answers this caller too

    QPointer<MarketDataService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("cboe_data.py"), {QStringLiteral("options_chains"), sym},
        [self, sym, cache_key](const python::PythonResult& res) {
            if (!self)
                return;
            OptionsChainReply reply;
            if (!res.success) {
                reply.error = res.error.isEmpty() ? QStringLiteral("cboe_data.py failed") : res.error;
            } else {
                const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(res.output).trimmed().toUtf8());
                const QJsonObject root = doc.object();
                // CBOE's errors: {"error": true, "message": "...", "status_code": N}.
                if (!doc.isObject()) {
                    reply.error = QStringLiteral("cboe_data.py returned no JSON object");
                } else if (!root.value("success").toBool(false)) {
                    QString msg = root.value("message").toString();
                    if (msg.isEmpty())
                        msg = root.value("error").toString();
                    if (msg.isEmpty())
                        msg = QStringLiteral("CBOE returned no chain");
                    if (root.value("status_code").isDouble())
                        msg += QStringLiteral(" (HTTP %1)").arg(root.value("status_code").toInt());
                    reply.error = msg;
                } else {
                    reply.ok = true;
                    reply.chain = root.value("data").toObject();
                    reply.as_of = QDateTime::currentDateTimeUtc();
                    finterm::CacheManager::instance().put(
                        cache_key, QString::fromUtf8(QJsonDocument(reply.chain).toJson(QJsonDocument::Compact)),
                        kOptionsChainTtlSec, "options");
                }
            }
            if (!reply.ok)
                LOG_WARN("MarketData", "options chain " + sym + " failed: " + reply.error);
            const auto waiters = self->options_chain_waiters_.take(sym);
            // PythonRunner answers a missing script inside run(): keep the rule.
            self->later([waiters, reply]() {
                for (const auto& w : waiters)
                    w(reply);
            });
        });
}

// ── Wikipedia summary ──────────────────────────────────────────────────────
void MarketDataService::fetch_wikipedia_summary(const QString& title_query, WikipediaCallback cb) {
    if (title_query.isEmpty()) {
        cb(false, {});
        return;
    }
    // Wikipedia accepts underscored titles. We don't try to disambiguate —
    // if the article doesn't exist verbatim the REST API returns a 404.
    QString title = title_query;
    title.replace(' ', '_');
    const QString cache_key = "wiki:" + title;
    auto parse = [title_query](const QJsonObject& o) -> WikipediaSummary {
        WikipediaSummary s;
        s.title = o["title"].toString(title_query);
        s.description = o["description"].toString();
        s.extract = o["extract"].toString();
        s.url = o["content_urls"].toObject()["desktop"].toObject()["page"].toString();
        return s;
    };
    auto cached = finterm::CacheManager::instance().try_get(cache_key);
    if (cached.has_value()) {
        const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
        cb(!obj.isEmpty(), parse(obj));
        return;
    }
    const QString url = "https://en.wikipedia.org/api/rest_v1/page/summary/" + QUrl::toPercentEncoding(title);
    QHash<QByteArray, QByteArray> headers;
    headers["User-Agent"] = kProjectUserAgent; // the tool, never the user's address
    finterm::HttpClient::instance().get(url, headers, [cb, parse, cache_key](Result<QJsonDocument> res) {
        if (!res.is_ok() || !res.value().isObject()) {
            cb(false, {});
            return;
        }
        const auto obj = res.value().object();
        // The REST summary endpoint returns 404 with a `{type: "...not_found"}`
        // body for missing pages — treat as a soft miss, not an error.
        if (obj.contains("type") && obj["type"].toString().contains("not_found")) {
            cb(false, {});
            return;
        }
        const QString raw = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        finterm::CacheManager::instance().put(cache_key, raw, kWikipediaCacheTtlSec, "wikipedia");
        cb(true, parse(obj));
    });
}

// ── S-1 funding-history extractor ──────────────────────────────────────────
void MarketDataService::fetch_s1_funding(const QString& s1_url, S1FundingCallback cb) {
    if (s1_url.isEmpty()) {
        cb(false, {});
        return;
    }
    // 24h CacheManager TTL — S-1s never change once filed (amendments are new
    // documents with new URLs), so the value is effectively immutable. A
    // shorter TTL would just waste daemon round-trips.
    constexpr int kS1CacheTtlSec = 24 * 60 * 60;
    const QString cache_key = "s1_funding:" + s1_url;
    auto parse = [](const QJsonObject& o) -> S1Funding {
        S1Funding f;
        f.source_url = o["url"].toString();
        f.section_text = o["section_text"].toString();
        f.principal_stockholders = o["principal_stockholders"].toString();
        f.use_of_proceeds = o["use_of_proceeds"].toString();
        f.underwriters = o["underwriters"].toString();
        f.selected_financials = o["selected_financials"].toString();
        for (const auto& v : o["risk_factors"].toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty())
                f.risk_factors.append(s);
        }
        for (const auto& v : o["rounds"].toArray()) {
            const auto r = v.toObject();
            S1Funding::Round rd;
            rd.date = r["date"].toString();
            rd.amount = r["amount"].toString();
            rd.context = r["context"].toString();
            if (!rd.amount.isEmpty())
                f.rounds.append(rd);
        }
        return f;
    };
    auto cached = finterm::CacheManager::instance().try_get(cache_key);
    if (cached.has_value()) {
        const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
        cb(true, parse(obj));
        return;
    }
    QJsonObject payload;
    payload["url"] = s1_url;
    python::PythonWorker::instance().submit(
        "parse_s1", payload,
        [cb, parse, s1_url](bool ok, QJsonObject result, QString err) {
            if (!ok || result.contains("error")) {
                LOG_WARN("MarketData", "parse_s1 failed for " + s1_url + ": " + err);
                cb(false, {});
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
            finterm::CacheManager::instance().put("s1_funding:" + s1_url, raw, kS1CacheTtlSec, "sec");
            cb(true, parse(result));
        },
        // S-1 documents are heavy (typically 2-5MB, sometimes 10-20MB).
        // SEC throttles to ~10 req/s/IP; download + BeautifulSoup parse on
        // large filings can take 15-30s. 60s of headroom prevents spurious
        // timeouts on the long tail.
        60000);
}

// ── Static symbol lists ─────────────────────────────────────────────────────

QStringList MarketDataService::indices_symbols() {
    return {"^GSPC", "^DJI",  "^IXIC", "^RUT",      "^FTSE",     "^GDAXI",
            "^FCHI", "^N225", "^HSI",  "000001.SS", "000300.SS", "^STOXX50E"};
}

QStringList MarketDataService::forex_symbols() {
    return {"EURUSD=X", "GBPUSD=X", "USDJPY=X", "AUDUSD=X", "USDCAD=X", "USDCHF=X", "NZDUSD=X", "EURCHF=X"};
}

QStringList MarketDataService::crypto_symbols() {
    return {"BTC-USD", "ETH-USD", "BNB-USD", "SOL-USD", "XRP-USD", "ADA-USD", "DOGE-USD", "DOT-USD", "LTC-USD"};
}

QStringList MarketDataService::commodity_symbols() {
    return {"GC=F", "SI=F", "CL=F", "BZ=F", "NG=F", "HG=F", "PL=F", "PA=F"};
}

QStringList MarketDataService::mover_symbols() {
    return {"SMCI", "PLTR", "MSTR", "NVDA", "AMD", "TSLA", "INTC", "PFE", "BA", "NKE", "DIS", "PYPL"};
}

QStringList MarketDataService::global_snapshot_symbols() {
    return {"^VIX", "^TNX", "DX-Y.NYB", "GC=F", "CL=F", "BTC-USD"};
}

QVector<MarketCategory> MarketDataService::default_global_markets() {
    return {
        {"Stock Indices",
         {"^GSPC", "^IXIC", "^DJI", "^RUT", "^VIX", "^FTSE", "^GDAXI", "^N225", "^FCHI", "^HSI", "^AXJO", "000300.SS",
          "000001.SS", "^STOXX50E", "^NYA", "^SOX", "^IBEX", "^AEX"}},
        {"Forex",
         {"EURUSD=X", "GBPUSD=X", "USDJPY=X", "USDCHF=X", "USDCAD=X", "AUDUSD=X", "NZDUSD=X", "EURGBP=X", "EURJPY=X",
          "GBPJPY=X", "USDCNY=X", "USDKRW=X", "USDHKD=X", "USDTWD=X", "USDSGD=X"}},
        {"Commodities",
         // LBS=F (lumber) was delisted; yfinance returns YFPricesMissingError
         // every refresh and the producer-refresh-timeout cycle burned ~25%
         // of the data-hub budget. Lumber is now LBR=F.
         {"GC=F", "SI=F", "PL=F", "PA=F", "HG=F", "CL=F", "BZ=F", "NG=F", "RB=F", "HO=F", "ZC=F", "ZW=F", "ZS=F",
          "KC=F", "CT=F", "SB=F", "CC=F", "LBR=F"}},
        {"Bonds", {"^TNX", "^TYX", "^IRX", "^FVX", "TLT", "IEF", "SHY", "BND", "AGG", "LQD", "HYG", "JNK"}},
        {"ETFs", {"SPY", "QQQ", "DIA", "EEM", "GLD", "XLK", "XLE", "XLF", "XLV", "VNQ", "IWM", "VTI"}},
        {"Cryptocurrencies",
         // UNI-USD removed: yfinance has been failing this ticker; the hub
         // kept retrying and emitting producer-refresh-timeout warnings. If
         // it comes back, add it again — keeping a known-bad symbol just to
         // be polite costs us a refresh slot every cycle.
         {"BTC-USD", "ETH-USD", "BNB-USD", "SOL-USD", "XRP-USD", "ADA-USD", "DOGE-USD", "LINK-USD", "DOT-USD",
          "AVAX-USD", "ATOM-USD"}},
    };
}

QVector<RegionalMarket> MarketDataService::default_regional_markets() {
    return {
        {"United States",
         {
             {"AAPL", "Apple Inc"},
             {"MSFT", "Microsoft Corp"},
             {"GOOGL", "Alphabet Inc"},
             {"AMZN", "Amazon.com"},
             {"NVDA", "NVIDIA Corp"},
             {"META", "Meta Platforms"},
             {"TSLA", "Tesla Inc"},
             {"JPM", "JPMorgan Chase"},
             {"V", "Visa Inc"},
             {"WMT", "Walmart Inc"},
             {"UNH", "UnitedHealth Group"},
             {"MA", "Mastercard Inc"},
         }},
        {"China",
         {
             {"BABA", "Alibaba Group"},
             {"PDD", "PDD Holdings"},
             {"JD", "JD.com"},
             {"BIDU", "Baidu"},
             {"NIO", "NIO Inc"},
             {"LI", "Li Auto"},
             {"XPEV", "XPeng"},
             {"BILI", "Bilibili"},
             {"NTES", "NetEase"},
             {"ZTO", "ZTO Express"},
             {"VNET", "VNET Group"},
             {"TAL", "TAL Education"},
         }},
        {"Europe",
         {
             {"ASML", "ASML Holding"},
             {"SAP", "SAP SE"},
             {"NVO", "Novo Nordisk"},
             {"SHEL", "Shell plc"},
             {"AZN", "AstraZeneca"},
             {"TTE", "TotalEnergies"},
             {"HSBC", "HSBC Holdings"},
             {"NSRGY", "Nestlé"},
             {"SIEGY", "Siemens"},
             {"LRLCY", "L'Oréal"},
             {"VWAGY", "Volkswagen"},
             {"LVMHF", "LVMH"},
         }},
    };
}

void MarketDataService::fetch_sparklines(const QStringList& symbols, SparklineCallback cb) {
    if (symbols.isEmpty()) {
        cb(false, {});
        return;
    }

    QJsonArray syms_arr;
    for (const auto& s : symbols)
        syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    // Only this caller's symbols: PythonWorker may answer with a merged
    // request's union.
    const QSet<QString> mine(symbols.begin(), symbols.end());
    python::PythonWorker::instance().submit(
        "batch_sparklines", payload,
        [cb, mine](bool ok, QJsonObject obj, QString err) {
            // "No data" (an answer, not a refusal) is an empty success, as it
            // was before the daemon sent {"error"} bodies as failed replies.
            if (!ok && obj.contains("error") && !python::PythonWorker::reply_is_transient(ok, obj)) {
                cb(true, {});
                return;
            }
            if (!ok) {
                LOG_WARN("MarketData", "Sparklines failed: " + err.left(200));
                cb(false, {});
                return;
            }
            QHash<QString, QVector<double>> out;
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.key() == "_value" || !mine.contains(it.key()))
                    continue;
                QVector<double> prices;
                for (const auto& v : it.value().toArray())
                    prices.append(v.toDouble());
                if (!prices.isEmpty())
                    out[it.key()] = prices;
            }
            cb(true, out);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

} // namespace finterm::services
