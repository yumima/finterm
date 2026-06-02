#include "services/markets/MarketDataService.h"

#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "storage/cache/CacheManager.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"
#    include "datahub/TopicPolicy.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace fincept::services {

MarketDataService& MarketDataService::instance() {
    static MarketDataService s;
    return s;
}

MarketDataService::MarketDataService() {}


// ── DataHub Producer integration ────────────────────────────────────────────

QStringList MarketDataService::topic_patterns() const {
    return {QStringLiteral("market:quote:*"), QStringLiteral("market:sparkline:*"),
            QStringLiteral("market:history:*")};
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
    static const QString kHist  = QStringLiteral("market:history:");

    const qint64 refresh_t0 = QDateTime::currentMSecsSinceEpoch();

    QStringList quote_syms;
    QStringList spark_syms;
    struct HistReq { QString topic; QString symbol; QString period; QString interval; };
    QVector<HistReq> hist_reqs;
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
        for (const auto& s : quote_syms) arr.append(s);
        payload["quotes"] = arr;
    }
    if (!spark_syms.isEmpty()) {
        QJsonArray arr;
        for (const auto& s : spark_syms) arr.append(s);
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
        return;  // Nothing to fetch — hub guarantees this won't happen in practice.
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

            if (!ok) {
                LOG_WARN("MarketData",
                         QString("batch_all failed in %1ms: %2").arg(elapsed).arg(err.left(200)));
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
            const QJsonArray quotes_arr = root.value("quotes").toArray();
            int quotes_parsed = 0;
            for (const auto& v : quotes_arr) {
                const QJsonObject q = v.toObject();
                if (q.isEmpty() || q.contains("error"))
                    continue;
                QuoteData qd{};
                qd.symbol     = q["symbol"].toString();
                qd.name       = q["name"].toString(q["symbol"].toString());
                qd.price      = q["price"].toDouble();
                qd.change     = q["change"].toDouble();
                qd.change_pct = q["change_percent"].toDouble();
                qd.high       = q["high"].toDouble();
                qd.low        = q["low"].toDouble();
                qd.volume     = q["volume"].toDouble();
                qd.bid        = q["bid"].toDouble();
                qd.ask        = q["ask"].toDouble();
                qd.bid_size   = q["bid_size"].toDouble();
                qd.ask_size   = q["ask_size"].toDouble();
                self->refresh_pending_quotes_.append(qd);
                ++quotes_parsed;
            }

            // Sparklines — {sym: [closes]}
            const QJsonObject sparks = root.value("sparklines").toObject();
            int sparks_parsed = 0;
            for (auto it = sparks.begin(); it != sparks.end(); ++it) {
                const QJsonArray closes = it.value().toArray();
                if (closes.isEmpty())
                    continue;
                PendingSparkline ps;
                ps.symbol = it.key();
                ps.points.reserve(closes.size());
                for (const auto& c : closes)
                    ps.points.append(c.toDouble());
                self->refresh_pending_sparks_.append(std::move(ps));
                ++sparks_parsed;
            }

            // Histories — array of {symbol, period, interval, points[]}
            const QJsonArray hists = root.value("histories").toArray();
            int hists_parsed = 0;
            for (const auto& hv : hists) {
                const QJsonObject h = hv.toObject();
                if (h.contains("error"))
                    continue;
                PendingHistory ph;
                ph.symbol   = h.value("symbol").toString();
                ph.period   = h.value("period").toString();
                ph.interval = h.value("interval").toString();
                const QJsonArray pts = h.value("points").toArray();
                ph.points.reserve(pts.size());
                for (const auto& pv : pts) {
                    const QJsonObject p = pv.toObject();
                    HistoryPoint pt;
                    pt.timestamp = static_cast<qint64>(p["timestamp"].toDouble());
                    pt.open = p["open"].toDouble();
                    pt.high = p["high"].toDouble();
                    pt.low = p["low"].toDouble();
                    pt.close = p["close"].toDouble();
                    pt.volume = static_cast<qint64>(p["volume"].toDouble());
                    ph.points.append(pt);
                }
                self->refresh_pending_hists_.append(std::move(ph));
                ++hists_parsed;
            }

            self->refresh_quotes_total_ += quotes_parsed;
            self->refresh_sparks_total_ += sparks_parsed;
            self->refresh_hists_total_  += hists_parsed;
            if (self->refresh_started_ms_ == 0)
                self->refresh_started_ms_ = refresh_t0;

            LOG_INFO("MarketData",
                     QString("batch_all parsed in %1ms: queued quotes=%2/%3 sparks=%4/%5 hists=%6/%7 "
                             "(draining %8/chunk)")
                         .arg(elapsed)
                         .arg(quotes_parsed).arg(quote_syms.size())
                         .arg(sparks_parsed).arg(spark_syms.size())
                         .arg(hists_parsed).arg(hist_reqs.size())
                         .arg(kRefreshChunkSize));

            if (!self->refresh_drain_scheduled_ &&
                (!self->refresh_pending_quotes_.isEmpty() ||
                 !self->refresh_pending_sparks_.isEmpty() ||
                 !self->refresh_pending_hists_.isEmpty())) {
                self->refresh_drain_scheduled_ = true;
                QTimer::singleShot(0, self.data(), [self]() {
                    if (self) self->drain_refresh_chunk();
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
        QJsonObject co;
        co["symbol"]     = qd.symbol;
        co["name"]       = qd.name;
        co["price"]      = qd.price;
        co["change"]     = qd.change;
        co["change_pct"] = qd.change_pct;
        co["high"]       = qd.high;
        co["low"]        = qd.low;
        co["volume"]     = qd.volume;
        co["bid"]        = qd.bid;
        co["ask"]        = qd.ask;
        co["bid_size"]   = qd.bid_size;
        co["ask_size"]   = qd.ask_size;
        const QString payload =
            QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact));
        // 30s key holds the live snapshot including bid/ask.
        fincept::CacheManager::instance().put(
            "market:" + qd.symbol, QVariant(payload),
            kQuoteCacheTtlSec, "market_data");
        // 7d "last-known" key is a cold-start fallback. Order-book fields
        // (bid/ask/bid_size/ask_size) go stale within seconds, so we strip
        // them from the long-TTL payload — the UI would otherwise show a
        // week-old spread as if it were live.
        QJsonObject co_long = co;
        co_long.remove("bid");
        co_long.remove("ask");
        co_long.remove("bid_size");
        co_long.remove("ask_size");
        const QString payload_long =
            QString::fromUtf8(QJsonDocument(co_long).toJson(QJsonDocument::Compact));
        fincept::CacheManager::instance().put(
            "market_last:" + qd.symbol, QVariant(payload_long),
            kQuoteLastKnownTtlSec, "market_data");
        publish_quote_to_hub(qd);
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
        publish_history_to_hub(ph.symbol, ph.period, ph.interval, ph.points);
        --budget;
    }

    const bool anything_left = !refresh_pending_quotes_.isEmpty() ||
                               !refresh_pending_sparks_.isEmpty() ||
                               !refresh_pending_hists_.isEmpty();
    if (anything_left) {
        refresh_drain_scheduled_ = true;
        QPointer<MarketDataService> self = this;
        QTimer::singleShot(0, this, [self]() {
            if (self) self->drain_refresh_chunk();
        });
    } else if (refresh_quotes_total_ + refresh_sparks_total_ + refresh_hists_total_ > 0) {
        const qint64 total_ms =
            QDateTime::currentMSecsSinceEpoch() - refresh_started_ms_;
        LOG_INFO("MarketData",
                 QString("batch_all drain complete: quotes=%1 sparks=%2 hists=%3 in %4ms total")
                     .arg(refresh_quotes_total_)
                     .arg(refresh_sparks_total_)
                     .arg(refresh_hists_total_)
                     .arg(total_ms));
        refresh_quotes_total_ = 0;
        refresh_sparks_total_ = 0;
        refresh_hists_total_  = 0;
        refresh_started_ms_   = 0;
    }
}

void MarketDataService::publish_quote_to_hub(const QuoteData& q) {
    datahub::DataHub::instance().publish(
        QStringLiteral("market:quote:") + q.symbol,
        QVariant::fromValue(q));
}

void MarketDataService::publish_history_to_hub(const QString& symbol, const QString& period,
                                               const QString& interval,
                                               const QVector<HistoryPoint>& points) {
    const QString topic = QStringLiteral("market:history:") + symbol + QLatin1Char(':') + period +
                          QLatin1Char(':') + interval;
    datahub::DataHub::instance().publish(topic, QVariant::fromValue(points));
}

void MarketDataService::publish_sparkline_to_hub(const QString& symbol, const QVector<double>& points) {
    datahub::DataHub::instance().publish(QStringLiteral("market:sparkline:") + symbol,
                                         QVariant::fromValue(points));
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

    hub_registered_ = true;
    LOG_INFO("DataHub",
             "MarketDataService registered (quote:*, sparkline:*, history:*)");

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

void MarketDataService::hydrate_quotes_from_cache() {
    // Decode every cached entry into the pending queue here — this is one
    // SQLite read (fast) plus N small QJsonDocument::fromJson calls. The
    // expensive part (publishing to the hub, which fans out to subscribers
    // and emits state-transition signals) is split off into chunks below.
    const auto last_known = fincept::CacheManager::instance().get_prefix("market_last:");
    hydrate_pending_.clear();
    hydrate_pending_.reserve(last_known.size());
    for (auto it = last_known.cbegin(); it != last_known.cend(); ++it) {
        const QString sym = it.key().mid(sizeof("market_last:") - 1);
        if (sym.isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(it.value().toUtf8()).object();
        if (o.isEmpty())
            continue;
        QuoteData qd{};
        qd.symbol     = o.value("symbol").toString(sym);
        qd.name       = o.value("name").toString(qd.symbol);
        qd.price      = o.value("price").toDouble();
        qd.change     = o.value("change").toDouble();
        qd.change_pct = o.value("change_pct").toDouble();
        qd.high       = o.value("high").toDouble();
        qd.low        = o.value("low").toDouble();
        qd.volume     = o.value("volume").toDouble();
        hydrate_pending_.append(qd);
    }
    hydrate_pending_total_ = hydrate_pending_.size();
    LOG_INFO("DataHub",
             QString("MarketDataService hydration queued %1 quote(s) — "
                     "publishing in chunks of %2 to keep input responsive")
                 .arg(hydrate_pending_total_).arg(kHydrateChunkSize));
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
        publish_quote_to_hub(hydrate_pending_[i]);
    hydrate_pending_.erase(hydrate_pending_.begin(), hydrate_pending_.begin() + n);

    if (!hydrate_pending_.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { hydrate_next_chunk(); });
    } else if (hydrate_pending_total_ > 0) {
        LOG_INFO("DataHub",
                 QString("MarketDataService hydration complete (%1 quote(s) published)")
                     .arg(hydrate_pending_total_));
        hydrate_pending_total_ = 0;
    }
}


// ── Cache invalidation ──────────────────────────────────────────────────────

void MarketDataService::invalidate_quotes(const QStringList& symbols) {
    if (symbols.isEmpty()) {
        // Bulk clear: drop everything in the "market:" prefix bucket.
        fincept::CacheManager::instance().remove_prefix("market:");
        return;
    }
    for (const auto& sym : symbols)
        fincept::CacheManager::instance().remove("market:" + sym);
}

// ── Batched + Cached fetch_quotes ───────────────────────────────────────────

void MarketDataService::fetch_quotes(const QStringList& symbols, QuoteCallback cb) {
    if (symbols.isEmpty()) {
        cb(true, {});
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
    const QHash<QString, QString> hits = fincept::CacheManager::instance().multi_get(cache_keys);

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
            const QJsonObject o = QJsonDocument::fromJson(it.value().toUtf8()).object();
            QuoteData qd{};
            qd.symbol     = o["symbol"].toString();
            qd.name       = o["name"].toString();
            qd.price      = o["price"].toDouble();
            qd.change     = o["change"].toDouble();
            qd.change_pct = o["change_pct"].toDouble();
            qd.high       = o["high"].toDouble();
            qd.low        = o["low"].toDouble();
            qd.volume     = o["volume"].toDouble();
            qd.bid        = o["bid"].toDouble();
            qd.ask        = o["ask"].toDouble();
            qd.bid_size   = o["bid_size"].toDouble();
            qd.ask_size   = o["ask_size"].toDouble();
            cached_results.append(qd);
        }
        if (!cached_results.isEmpty()) {
            cb(true, cached_results);
            return;
        }
    }

    // Queue for batching
    pending_.append({symbols, std::move(cb)});

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
    for (const auto& s : all_symbols) syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    python::PythonWorker::instance().submit(
        "batch_quotes", payload,
        [this, requests = std::move(requests)](bool ok, QJsonObject result, QString err) {
            QVector<QuoteData> all_quotes;

            if (ok) {
                auto parse_quote = [](const QJsonObject& q) -> QuoteData {
                    QuoteData qd{};
                    qd.symbol     = q["symbol"].toString();
                    qd.name       = q["name"].toString(q["symbol"].toString());
                    qd.price      = q["price"].toDouble();
                    qd.change     = q["change"].toDouble();
                    qd.change_pct = q["change_percent"].toDouble();
                    qd.high       = q["high"].toDouble();
                    qd.low        = q["low"].toDouble();
                    qd.volume     = q["volume"].toDouble();
                    qd.bid        = q["bid"].toDouble();
                    qd.ask        = q["ask"].toDouble();
                    qd.bid_size   = q["bid_size"].toDouble();
                    qd.ask_size   = q["ask_size"].toDouble();
                    return qd;
                };

                auto store_quote = [](const QuoteData& q) {
                    QJsonObject o;
                    o["symbol"] = q.symbol;
                    o["name"] = q.name;
                    o["price"] = q.price;
                    o["change"] = q.change;
                    o["change_pct"] = q.change_pct;
                    o["high"] = q.high;
                    o["low"] = q.low;
                    o["volume"] = q.volume;
                    o["bid"] = q.bid;
                    o["ask"] = q.ask;
                    o["bid_size"] = q.bid_size;
                    o["ask_size"] = q.ask_size;
                    const QString payload =
                        QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
                    fincept::CacheManager::instance().put(
                        "market:" + q.symbol, QVariant(payload),
                        kQuoteCacheTtlSec, "market_data");
                    // 7d fallback: strip order-book fields (bid/ask go stale
                    // within seconds — see comment in refresh() above).
                    QJsonObject o_long = o;
                    o_long.remove("bid");
                    o_long.remove("ask");
                    o_long.remove("bid_size");
                    o_long.remove("ask_size");
                    const QString payload_long =
                        QString::fromUtf8(QJsonDocument(o_long).toJson(QJsonDocument::Compact));
                    fincept::CacheManager::instance().put(
                        "market_last:" + q.symbol, QVariant(payload_long),
                        kQuoteLastKnownTtlSec, "market_data");
                };

                // Daemon's batch_quotes returns a flat list; PythonWorker
                // wraps it under "_value". Fall back to "quotes" for forward
                // compatibility if the daemon ever returns a wrapped shape.
                const QJsonArray quotes = result.contains("_value")
                                              ? result.value("_value").toArray()
                                              : result.value("quotes").toArray();
                for (const auto& v : quotes) {
                    auto q = v.toObject();
                    if (q.contains("error"))
                        continue;
                    auto quote = parse_quote(q);
                    all_quotes.append(quote);
                    store_quote(quote);
                    publish_quote_to_hub(quote);
                }

                LOG_INFO("MarketData", QString("Fetched %1 quotes (cached)").arg(all_quotes.size()));
            } else {
                LOG_WARN("MarketData", "Batch fetch failed: " + err);
            }

            // Fan out results to each waiting callback, filtered to their requested symbols
            for (const auto& req : requests) {
                if (!ok) {
                    // On failure, try to serve from stale cache (ignoring TTL)
                    QVector<QuoteData> stale;
                    for (const auto& sym : req.symbols) {
                        const QVariant cv = fincept::CacheManager::instance().get("market:" + sym);
                        if (!cv.isNull()) {
                            const QJsonObject o = QJsonDocument::fromJson(cv.toString().toUtf8()).object();
                            QuoteData qd{};
                            qd.symbol     = o["symbol"].toString();
                            qd.name       = o["name"].toString();
                            qd.price      = o["price"].toDouble();
                            qd.change     = o["change"].toDouble();
                            qd.change_pct = o["change_pct"].toDouble();
                            qd.high       = o["high"].toDouble();
                            qd.low        = o["low"].toDouble();
                            qd.volume     = o["volume"].toDouble();
                            qd.bid        = o["bid"].toDouble();
                            qd.ask        = o["ask"].toDouble();
                            qd.bid_size   = o["bid_size"].toDouble();
                            qd.ask_size   = o["ask_size"].toDouble();
                            stale.append(qd);
                        }
                    }
                    req.cb(!stale.isEmpty(), stale);
                    continue;
                }

                QSet<QString> wanted(req.symbols.begin(), req.symbols.end());
                QVector<QuoteData> filtered;
                for (const auto& q : all_quotes) {
                    if (wanted.contains(q.symbol)) {
                        filtered.append(q);
                    }
                }
                req.cb(true, filtered);
            }
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── News fetch (unchanged) ──────────────────────────────────────────────────

void MarketDataService::fetch_news(const QString& symbol, int count, NewsCallback cb) {
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["count"] = count;
    python::PythonWorker::instance().submit("news", payload,
        [cb](bool ok, QJsonObject result, QString err) {
            if (!ok) {
                LOG_WARN("MarketData", "News fetch failed: " + err);
                cb(false, {});
                return;
            }
            // Daemon returns {articles: [...]} for news.
            cb(true, result.value("articles").toArray());
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
                LOG_WARN("MarketData",
                         "Orderbook fetch failed for " + symbol + ": " + err.left(200));
                cb(false, {});
                return;
            }
            OrderbookData ob;
            ob.symbol   = result["symbol"].toString(symbol);
            ob.bid      = result["bid"].toDouble();
            ob.ask      = result["ask"].toDouble();
            ob.bid_size = result["bid_size"].toDouble();
            ob.ask_size = result["ask_size"].toDouble();
            cb(true, ob);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Info fetch (company fundamentals) ───────────────────────────────────────

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

    // ── Call 1: get_info via daemon ───────────────────────────────────────────
    QJsonObject info_payload;
    info_payload["symbol"] = symbol;
    python::PythonWorker::instance().submit("info", info_payload,
                                         [shared, symbol, try_complete](bool ok, QJsonObject o, QString /*err*/) {
                                             shared->info_done = true;
                                             if (!ok || o.contains("error")) {
                                                 LOG_WARN("MarketData", "get_info failed for " + symbol);
                                                 try_complete();
                                                 return;
                                             }
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
                                             if (shared->info.currency.isEmpty()) shared->info.currency = "USD";
                                             shared->info.market_cap = o["market_cap"].toDouble();
                                             shared->info.beta = o["beta"].toDouble();
                                             shared->info.week52_high = o["fifty_two_week_high"].toDouble();
                                             shared->info.week52_low = o["fifty_two_week_low"].toDouble();
                                             shared->info.avg_volume = o["average_volume"].toDouble();
                                             shared->info.eps = o["revenue_per_share"].toDouble();
                                             shared->info.description = scrub(o["description"]);
                                             shared->info.website = scrub(o["website"]);
                                             shared->info.employees = o["employees"].toInt();
                                             const QJsonArray off_arr = o["officers"].toArray();
                                             for (const auto& v : off_arr) {
                                                 const QJsonObject obj = v.toObject();
                                                 OfficerInfo co;
                                                 co.name  = obj["name"].toString();
                                                 co.title = obj["title"].toString();
                                                 if (!co.name.isEmpty())
                                                     shared->info.officers.append(co);
                                             }
                                             shared->info_ok = true;
                                             try_complete();
                                         },
                                         python::PythonWorker::kNetworkActionTimeoutMs);

    // ── Call 2: financial_ratios via daemon ───────────────────────────────────
    QJsonObject ratios_payload;
    ratios_payload["symbol"] = symbol;
    python::PythonWorker::instance().submit("financial_ratios", ratios_payload,
                                         [shared, symbol, try_complete](bool ok, QJsonObject o, QString /*err*/) {
                                             shared->ratios_done = true;
                                             if (!ok || o.contains("error")) {
                                                 LOG_WARN("MarketData", "financial_ratios failed for " + symbol);
                                                 try_complete();
                                                 return;
                                             }
                                             shared->info.pe_ratio = o["peRatio"].toDouble();
                                             shared->info.forward_pe = o["forwardPE"].toDouble();
                                             shared->info.price_to_book = o["priceToBook"].toDouble();
                                             shared->info.dividend_yield = o["dividendYield"].toDouble();
                                             shared->info.roe = o["returnOnEquity"].toDouble();
                                             shared->info.profit_margin = o["profitMargin"].toDouble();
                                             shared->info.debt_to_equity = o["debtToEquity"].toDouble();
                                             shared->info.current_ratio = o["currentRatio"].toDouble();
                                             shared->info.eps = o["revenuePerShare"].toDouble();
                                             shared->ratios_ok = true;
                                             try_complete();
                                         },
                                         python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── History fetch (OHLCV) ────────────────────────────────────────────────────

void MarketDataService::fetch_history(const QString& symbol, const QString& period, const QString& interval,
                                      HistoryCallback cb) {
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["period"] = period;
    payload["interval"] = interval;

    python::PythonWorker::instance().submit("historical_period", payload,
        [cb, symbol](bool ok, QJsonObject result, QString err) {
        if (!ok) {
            LOG_WARN("MarketData", "History fetch failed for " + symbol + ": " + err);
            cb(false, {});
            return;
        }
        // Daemon returns a flat list; wrapped under _value.
        const QJsonArray points = result.contains("_value")
                                       ? result.value("_value").toArray()
                                       : result.value("history").toArray();

        QVector<HistoryPoint> history;
        for (const auto& v : points) {
            QJsonObject o = v.toObject();
            HistoryPoint pt;
            pt.timestamp = static_cast<qint64>(o["timestamp"].toDouble());
            pt.open = o["open"].toDouble();
            pt.high = o["high"].toDouble();
            pt.low = o["low"].toDouble();
            pt.close = o["close"].toDouble();
            pt.volume = static_cast<qint64>(o["volume"].toDouble());
            history.append(pt);
        }

        LOG_INFO("MarketData", QString("Fetched %1 history points for %2").arg(history.size()).arg(symbol));
        cb(true, history);
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
    const QHash<QString, QString> hits = fincept::CacheManager::instance().multi_get(cache_keys);

    auto parse_cached = [](const QString& raw, const QString& sym) -> CompanyProfile {
        const QJsonObject o = QJsonDocument::fromJson(raw.toUtf8()).object();
        CompanyProfile p;
        p.symbol     = sym;
        p.long_name  = o["long_name"].toString();
        p.sector     = o["sector"].toString();
        p.industry   = o["industry"].toString();
        p.website    = o["website"].toString();
        p.country    = o["country"].toString();
        p.market_cap = o["market_cap"].toDouble();
        p.employees  = o["employees"].toInt();
        return p;
    };

    QVector<CompanyProfile> cached_out;
    QStringList missing;
    cached_out.reserve(symbols.size());
    for (const auto& sym : symbols) {
        const auto it = hits.constFind("ipo_info:" + sym);
        if (it != hits.constEnd()) cached_out.append(parse_cached(it.value(), sym));
        else                       missing.append(sym);
    }
    if (missing.isEmpty()) {
        LOG_INFO("MarketData", QString("fetch_company_profiles: %1 cached, 0 fresh").arg(cached_out.size()));
        cb(true, cached_out);
        return;
    }

    QJsonArray syms_arr;
    for (const auto& s : missing) syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    python::PythonWorker::instance().submit(
        "batch_info", payload,
        [cb, cached_out, missing](bool ok, QJsonObject result, QString err) {
            QVector<CompanyProfile> out = cached_out;
            if (!ok) {
                LOG_WARN("MarketData", "batch_info failed: " + err);
                cb(!out.isEmpty(), out);
                return;
            }
            const QJsonArray rows = result.contains("_value")
                                        ? result.value("_value").toArray()
                                        : result.value("profiles").toArray();
            for (const auto& v : rows) {
                const QJsonObject o = v.toObject();
                CompanyProfile p;
                p.symbol     = o["symbol"].toString();
                if (p.symbol.isEmpty() || o.contains("error")) continue;
                p.long_name  = o["long_name"].toString();
                p.sector     = o["sector"].toString();
                p.industry   = o["industry"].toString();
                p.website    = o["website"].toString();
                p.country    = o["country"].toString();
                p.market_cap = o["market_cap"].toDouble();
                p.employees  = o["employees"].toInt();
                out.append(p);

                // Cache for 7 days — sector rarely changes.
                QJsonObject co;
                co["long_name"]  = p.long_name;
                co["sector"]     = p.sector;
                co["industry"]   = p.industry;
                co["website"]    = p.website;
                co["country"]    = p.country;
                co["market_cap"] = p.market_cap;
                co["employees"]  = p.employees;
                fincept::CacheManager::instance().put(
                    "ipo_info:" + p.symbol,
                    QVariant(QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact))),
                    kProfileCacheTtlSec, "ipo_info");
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
void MarketDataService::fetch_top_movers(int count, TopMoversCallback cb) {
    constexpr int kTopMoversCacheTtlSec = 60;
    const QString cache_key = QString("top_movers:%1").arg(count);
    auto parse_side = [](const QJsonArray& arr) -> QVector<QuoteData> {
        QVector<QuoteData> out;
        out.reserve(arr.size());
        for (const auto& v : arr) {
            const auto o = v.toObject();
            QuoteData q;
            q.symbol     = o["symbol"].toString();
            q.name       = o["name"].toString();
            q.price      = o["price"].toDouble();
            q.change     = o["change"].toDouble();
            q.change_pct = o["change_pct"].toDouble();
            q.high       = o["high"].toDouble();
            q.low        = o["low"].toDouble();
            q.volume     = o["volume"].toDouble();
            if (!q.symbol.isEmpty()) out.append(q);
        }
        return out;
    };
    auto parse = [parse_side](const QJsonObject& o) -> TopMovers {
        TopMovers tm;
        tm.gainers = parse_side(o["gainers"].toArray());
        tm.losers  = parse_side(o["losers"].toArray());
        return tm;
    };
    auto cached = fincept::CacheManager::instance().try_get(cache_key);
    if (cached.has_value()) {
        const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
        cb(true, parse(obj));
        return;
    }
    QJsonObject payload;
    payload["count"] = count;
    python::PythonWorker::instance().submit(
        "top_movers", payload,
        [cb, parse, cache_key](bool ok, QJsonObject result, QString err) {
            if (!ok || result.contains("error")) {
                LOG_WARN("MarketData", "top_movers failed: " + err);
                cb(false, {});
                return;
            }
            const QString raw = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
            fincept::CacheManager::instance().put(cache_key, raw, kTopMoversCacheTtlSec, "movers");
            cb(true, parse(result));
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── IPO Watch detail-rail enrichment ────────────────────────────────────────
// fetch_ipo_extras: yfinance financials + holders + news in one round-trip.
// fetch_sec_filings: SEC EDGAR submissions API (no key, free).
// fetch_wikipedia_summary: Wikipedia REST summary (no key, free).
// Each is keyed by ticker (or company name for Wikipedia) and is lazy-fetched
// from IpoWatchView when a row is selected — see render_detail.

namespace {
constexpr int kIpoExtrasCacheTtlSec  = 15 * 60;        // 15 min — news + financials change slowly
constexpr int kSecFilingsCacheTtlSec = 60 * 60;        // 1h — new SEC filings are infrequent
constexpr int kSecCikMapTtlSec       = 7 * 24 * 60 * 60; // 7d — SEC publishes weekly
constexpr int kWikipediaCacheTtlSec  = 7 * 24 * 60 * 60; // 7d — descriptions stable
} // namespace

void MarketDataService::fetch_ipo_extras(const QString& symbol, IpoExtrasCallback cb) {
    if (symbol.isEmpty()) { cb(false, {}); return; }

    // 15-minute CacheManager TTL so flipping between rows doesn't re-hit the
    // daemon on every click. The detail rail invalidates this cache on
    // wake-on-resume by clearing IpoWatchView's own info_cache_, but the
    // CacheManager entry stays so subsequent same-session opens are fast.
    const QString cache_key = "ipo_extras:" + symbol;
    auto cached = fincept::CacheManager::instance().try_get(cache_key);
    auto parse  = [](const QJsonObject& o, const QString& sym) -> IpoExtras {
        IpoExtras ex;
        ex.symbol = sym;
        auto parse_series = [](const QJsonArray& a) {
            QVector<FinancialPoint> out;
            for (const auto& v : a) {
                const auto r = v.toObject();
                FinancialPoint p;
                p.date  = r["date"].toString();
                p.value = r["value"].toDouble();
                if (!p.date.isEmpty()) out.append(p);
            }
            return out;
        };
        ex.quarterly_revenue       = parse_series(o["quarterly_revenue"].toArray());
        ex.quarterly_net_income    = parse_series(o["quarterly_net_income"].toArray());
        ex.annual_revenue          = parse_series(o["annual_revenue"].toArray());
        ex.annual_net_income       = parse_series(o["annual_net_income"].toArray());
        ex.annual_gross_profit     = parse_series(o["annual_gross_profit"].toArray());
        ex.annual_operating_income = parse_series(o["annual_operating_income"].toArray());
        for (const auto& v : o["institutional_holders"].toArray()) {
            const auto r = v.toObject();
            InstitutionalHolder h;
            h.holder = r["holder"].toString();
            h.pct    = r["pct"].toDouble();
            h.shares = r["shares"].toDouble();
            h.value  = r["value"].toDouble();
            h.date   = r["date"].toString();
            if (!h.holder.isEmpty()) ex.institutional_holders.append(h);
        }
        for (const auto& v : o["major_holders"].toArray()) {
            const auto r = v.toObject();
            MajorHolderRow m;
            m.label = r["label"].toString();
            m.value = r["value"].toDouble();
            if (!m.label.isEmpty()) ex.major_holders.append(m);
        }
        for (const auto& v : o["news"].toArray()) {
            const auto r = v.toObject();
            NewsItem n;
            n.title     = r["title"].toString();
            n.link      = r["link"].toString();
            n.publisher = r["publisher"].toString();
            n.ts        = r["ts"].toString();
            if (!n.title.isEmpty()) ex.news.append(n);
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
            fincept::CacheManager::instance().put("ipo_extras:" + symbol, raw,
                                                  kIpoExtrasCacheTtlSec, "ipo_extras");
            cb(true, parse(result, symbol));
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── SEC EDGAR ──────────────────────────────────────────────────────────────
// SEC requires a `User-Agent: <organization> <email>` header. We send a
// stable identifier so they can throttle us instead of blocking the IP.
static const char* kSecUserAgent = "finterm admin@hanlexon.com";

void MarketDataService::fetch_sec_filings(const QString& ticker, SecFilingsCallback cb) {
    if (ticker.isEmpty()) { cb(false, {}); return; }
    const QString tkr = ticker.toUpper();

    // Inner closure runs once the CIK map is loaded — looks up the ticker,
    // builds the submissions URL, returns the list of recent filings.
    auto with_cik_map = [this, tkr, cb]() {
        auto it = sec_cik_map_.constFind(tkr);
        if (it == sec_cik_map_.constEnd()) {
            LOG_INFO("MarketData", "SEC: no CIK for ticker " + tkr);
            cb(true, {});
            return;
        }
        const int cik = it.value();
        const QString cik_padded = QString("%1").arg(cik, 10, 10, QChar('0'));
        const QString cache_key  = "sec_filings:" + tkr;
        auto cached = fincept::CacheManager::instance().try_get(cache_key);
        auto parse  = [cik_padded](const QJsonObject& root) -> QVector<SecFiling> {
            QVector<SecFiling> out;
            const auto recent = root["filings"].toObject()["recent"].toObject();
            const auto forms       = recent["form"].toArray();
            const auto dates       = recent["filingDate"].toArray();
            const auto accessions  = recent["accessionNumber"].toArray();
            const auto primary     = recent["primaryDocument"].toArray();
            const int n = std::min({forms.size(), dates.size(), accessions.size(), primary.size()});
            for (int i = 0; i < std::min(n, 25); ++i) {
                SecFiling f;
                f.form        = forms.at(i).toString();
                f.filing_date = dates.at(i).toString();
                f.accession   = accessions.at(i).toString();
                f.primary_doc = primary.at(i).toString();
                // EDGAR doc URL: https://www.sec.gov/Archives/edgar/data/{cik}/{accession-no-dashes}/{primary_doc}
                QString acc_nd = f.accession; acc_nd.remove('-');
                f.url = QString("https://www.sec.gov/Archives/edgar/data/%1/%2/%3")
                            .arg(cik_padded.toInt()).arg(acc_nd, f.primary_doc);
                out.append(f);
            }
            return out;
        };
        if (cached.has_value()) {
            const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
            cb(true, parse(obj));
            return;
        }

        const QString url = QString("https://data.sec.gov/submissions/CIK%1.json").arg(cik_padded);
        QPointer<MarketDataService> self_ptr = this;
        QHash<QByteArray, QByteArray> headers;
        headers["User-Agent"] = kSecUserAgent;
        fincept::HttpClient::instance().get(url, headers,
            [cb, parse, tkr, self_ptr](Result<QJsonDocument> res) {
                if (!self_ptr) return;
                if (!res.is_ok() || !res.value().isObject()) {
                    LOG_WARN("MarketData", "SEC submissions fetch failed for " + tkr);
                    cb(false, {});
                    return;
                }
                const auto obj = res.value().object();
                const QString raw = QString::fromUtf8(
                    QJsonDocument(obj).toJson(QJsonDocument::Compact));
                fincept::CacheManager::instance().put("sec_filings:" + tkr, raw,
                                                      kSecFilingsCacheTtlSec, "sec");
                cb(true, parse(obj));
            });
    };

    if (sec_cik_loaded_) { with_cik_map(); return; }
    sec_cik_waiters_.append([with_cik_map](bool /*loaded*/) { with_cik_map(); });
    if (sec_cik_loading_) return;
    sec_cik_loading_ = true;

    // Try CacheManager first — the company_tickers.json mapping has a 7-day
    // TTL since SEC refreshes weekly. Falls through to a live fetch if missing.
    auto fire_waiters = [this](bool ok) {
        sec_cik_loaded_ = ok;
        sec_cik_loading_ = false;
        auto waiters = std::move(sec_cik_waiters_);
        for (auto& w : waiters) w(ok);
    };
    auto populate_from_json = [this](const QJsonDocument& doc) {
        // company_tickers.json is keyed by row index (strings "0","1",…); each
        // value is {cik_str, ticker, title}.
        const auto root = doc.object();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            const auto v = it.value().toObject();
            const QString t = v["ticker"].toString().toUpper();
            const int cik   = v["cik_str"].toInt();
            if (!t.isEmpty() && cik > 0) sec_cik_map_.insert(t, cik);
        }
    };

    auto cached_map = fincept::CacheManager::instance().try_get("sec_cik_map");
    if (cached_map.has_value()) {
        const auto doc = QJsonDocument::fromJson(cached_map->toUtf8());
        if (doc.isObject()) {
            populate_from_json(doc);
            fire_waiters(true);
            return;
        }
    }
    QHash<QByteArray, QByteArray> headers;
    headers["User-Agent"] = kSecUserAgent;
    QPointer<MarketDataService> self_ptr = this;
    fincept::HttpClient::instance().get(
        "https://www.sec.gov/files/company_tickers.json", headers,
        [self_ptr, populate_from_json, fire_waiters](Result<QJsonDocument> res) {
            if (!self_ptr) return;
            if (!res.is_ok() || !res.value().isObject()) {
                LOG_WARN("MarketData", "SEC company_tickers.json fetch failed");
                fire_waiters(false);
                return;
            }
            populate_from_json(res.value());
            const QString raw = QString::fromUtf8(res.value().toJson(QJsonDocument::Compact));
            fincept::CacheManager::instance().put("sec_cik_map", raw, kSecCikMapTtlSec, "sec");
            fire_waiters(true);
        });
}

// ── Wikipedia summary ──────────────────────────────────────────────────────
void MarketDataService::fetch_wikipedia_summary(const QString& title_query, WikipediaCallback cb) {
    if (title_query.isEmpty()) { cb(false, {}); return; }
    // Wikipedia accepts underscored titles. We don't try to disambiguate —
    // if the article doesn't exist verbatim the REST API returns a 404.
    QString title = title_query;
    title.replace(' ', '_');
    const QString cache_key = "wiki:" + title;
    auto parse = [title_query](const QJsonObject& o) -> WikipediaSummary {
        WikipediaSummary s;
        s.title       = o["title"].toString(title_query);
        s.description = o["description"].toString();
        s.extract     = o["extract"].toString();
        s.url         = o["content_urls"].toObject()["desktop"].toObject()["page"].toString();
        return s;
    };
    auto cached = fincept::CacheManager::instance().try_get(cache_key);
    if (cached.has_value()) {
        const auto obj = QJsonDocument::fromJson(cached->toUtf8()).object();
        cb(!obj.isEmpty(), parse(obj));
        return;
    }
    const QString url = "https://en.wikipedia.org/api/rest_v1/page/summary/" +
                        QUrl::toPercentEncoding(title);
    QHash<QByteArray, QByteArray> headers;
    headers["User-Agent"] = kSecUserAgent;  // Wikipedia also asks for a UA
    fincept::HttpClient::instance().get(url, headers,
        [cb, parse, cache_key](Result<QJsonDocument> res) {
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
            fincept::CacheManager::instance().put(cache_key, raw, kWikipediaCacheTtlSec, "wikipedia");
            cb(true, parse(obj));
        });
}

// ── S-1 funding-history extractor ──────────────────────────────────────────
void MarketDataService::fetch_s1_funding(const QString& s1_url, S1FundingCallback cb) {
    if (s1_url.isEmpty()) { cb(false, {}); return; }
    // 24h CacheManager TTL — S-1s never change once filed (amendments are new
    // documents with new URLs), so the value is effectively immutable. A
    // shorter TTL would just waste daemon round-trips.
    constexpr int kS1CacheTtlSec = 24 * 60 * 60;
    const QString cache_key = "s1_funding:" + s1_url;
    auto parse = [](const QJsonObject& o) -> S1Funding {
        S1Funding f;
        f.source_url             = o["url"].toString();
        f.section_text           = o["section_text"].toString();
        f.principal_stockholders = o["principal_stockholders"].toString();
        f.use_of_proceeds        = o["use_of_proceeds"].toString();
        f.underwriters           = o["underwriters"].toString();
        f.selected_financials    = o["selected_financials"].toString();
        for (const auto& v : o["risk_factors"].toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) f.risk_factors.append(s);
        }
        for (const auto& v : o["rounds"].toArray()) {
            const auto r = v.toObject();
            S1Funding::Round rd;
            rd.date    = r["date"].toString();
            rd.amount  = r["amount"].toString();
            rd.context = r["context"].toString();
            if (!rd.amount.isEmpty()) f.rounds.append(rd);
        }
        return f;
    };
    auto cached = fincept::CacheManager::instance().try_get(cache_key);
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
            fincept::CacheManager::instance().put("s1_funding:" + s1_url, raw,
                                                  kS1CacheTtlSec, "sec");
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
    return {"^GSPC", "^DJI",  "^IXIC", "^RUT",      "^FTSE",  "^GDAXI",
            "^FCHI", "^N225", "^HSI",  "000001.SS", "^BSESN", "^NSEI"};
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
         {"^GSPC", "^IXIC", "^DJI", "^RUT", "^VIX", "^FTSE", "^GDAXI", "^N225", "^FCHI", "^HSI", "^AXJO", "^BSESN",
          "^NSEI", "^STOXX50E", "^NYA", "^SOX", "^IBEX", "^AEX"}},
        {"Forex",
         {"EURUSD=X", "GBPUSD=X", "USDJPY=X", "USDCHF=X", "USDCAD=X", "AUDUSD=X", "NZDUSD=X", "EURGBP=X", "EURJPY=X",
          "GBPJPY=X", "USDCNY=X", "USDINR=X", "USDHKD=X", "USDTWD=X", "USDSGD=X"}},
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
    for (const auto& s : symbols) syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    python::PythonWorker::instance().submit("batch_sparklines", payload,
        [cb](bool ok, QJsonObject obj, QString err) {
        if (!ok) {
            LOG_WARN("MarketData", "Sparklines failed: " + err.left(200));
            cb(false, {});
            return;
        }
        QHash<QString, QVector<double>> out;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.key() == "_value") continue;
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

} // namespace fincept::services
