#include "services/markets/MarketDataService.h"

#include "core/logging/Logger.h"
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
            // publish to hub, store in cache. We don't need the flush_batch code
            // path here because no callback chain is waiting on this refresh tick.
            const QJsonArray quotes_arr = root.value("quotes").toArray();
            int quotes_ok = 0;
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

                // Cache write — mirrors store_quote() in flush_batch.
                QJsonObject co;
                co["symbol"] = qd.symbol;
                co["name"] = qd.name;
                co["price"] = qd.price;
                co["change"] = qd.change;
                co["change_pct"] = qd.change_pct;
                co["high"] = qd.high;
                co["low"] = qd.low;
                co["volume"] = qd.volume;
                co["bid"] = qd.bid;
                co["ask"] = qd.ask;
                co["bid_size"] = qd.bid_size;
                co["ask_size"] = qd.ask_size;
                const QString payload = QString::fromUtf8(QJsonDocument(co).toJson(QJsonDocument::Compact));
                // 30s key holds the live snapshot including bid/ask.
                fincept::CacheManager::instance().put(
                    "market:" + qd.symbol, QVariant(payload),
                    kQuoteCacheTtlSec, "market_data");
                // 7d "last-known" key is a cold-start fallback. Order-book
                // fields (bid/ask/bid_size/ask_size) go stale within seconds,
                // so we strip them from the long-TTL payload — the UI would
                // otherwise show a week-old spread as if it were live.
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

                self->publish_quote_to_hub(qd);
                ++quotes_ok;
            }

            // Sparklines — {sym: [closes]}
            const QJsonObject sparks = root.value("sparklines").toObject();
            int sparks_ok = 0;
            for (auto it = sparks.begin(); it != sparks.end(); ++it) {
                const QJsonArray closes = it.value().toArray();
                if (closes.isEmpty())
                    continue;
                QVector<double> prices;
                prices.reserve(closes.size());
                for (const auto& c : closes)
                    prices.append(c.toDouble());
                self->publish_sparkline_to_hub(it.key(), prices);
                ++sparks_ok;
            }

            // Histories — array of {symbol, period, interval, points[]}
            const QJsonArray hists = root.value("histories").toArray();
            int hists_ok = 0;
            for (const auto& hv : hists) {
                const QJsonObject h = hv.toObject();
                if (h.contains("error"))
                    continue;
                const QString sym = h.value("symbol").toString();
                const QString per = h.value("period").toString();
                const QString ivl = h.value("interval").toString();
                const QJsonArray pts = h.value("points").toArray();
                QVector<HistoryPoint> points;
                points.reserve(pts.size());
                for (const auto& pv : pts) {
                    const QJsonObject p = pv.toObject();
                    HistoryPoint pt;
                    pt.timestamp = static_cast<qint64>(p["timestamp"].toDouble());
                    pt.open = p["open"].toDouble();
                    pt.high = p["high"].toDouble();
                    pt.low = p["low"].toDouble();
                    pt.close = p["close"].toDouble();
                    pt.volume = static_cast<qint64>(p["volume"].toDouble());
                    points.append(pt);
                }
                self->publish_history_to_hub(sym, per, ivl, points);
                ++hists_ok;
            }

            LOG_INFO("MarketData",
                     QString("batch_all OK in %1ms: quotes=%2/%3 sparks=%4/%5 hists=%6/%7")
                         .arg(elapsed)
                         .arg(quotes_ok).arg(quote_syms.size())
                         .arg(sparks_ok).arg(spark_syms.size())
                         .arg(hists_ok).arg(hist_reqs.size()));
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
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
             "MarketDataService registered as producer for market:quote:*, market:sparkline:*, market:history:*");
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
                                             shared->info.sector = o["sector"].toString();
                                             shared->info.industry = o["industry"].toString();
                                             shared->info.country = o["country"].toString();
                                             shared->info.currency = o["currency"].toString("USD");
                                             shared->info.market_cap = o["market_cap"].toDouble();
                                             shared->info.beta = o["beta"].toDouble();
                                             shared->info.week52_high = o["fifty_two_week_high"].toDouble();
                                             shared->info.week52_low = o["fifty_two_week_low"].toDouble();
                                             shared->info.avg_volume = o["average_volume"].toDouble();
                                             shared->info.eps = o["revenue_per_share"].toDouble();
                                             shared->info.description = o["description"].toString();
                                             // yfinance returns "N/A" sentinel
                                             // for missing description — treat
                                             // as empty so callers can branch.
                                             if (shared->info.description == "N/A") shared->info.description.clear();
                                             shared->info.website = o["website"].toString();
                                             if (shared->info.website == "N/A") shared->info.website.clear();
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
         {"GC=F", "SI=F", "PL=F", "PA=F", "HG=F", "CL=F", "BZ=F", "NG=F", "RB=F", "HO=F", "ZC=F", "ZW=F", "ZS=F",
          "KC=F", "CT=F", "SB=F", "CC=F", "LBS=F"}},
        {"Bonds", {"^TNX", "^TYX", "^IRX", "^FVX", "TLT", "IEF", "SHY", "BND", "AGG", "LQD", "HYG", "JNK"}},
        {"ETFs", {"SPY", "QQQ", "DIA", "EEM", "GLD", "XLK", "XLE", "XLF", "XLV", "VNQ", "IWM", "VTI"}},
        {"Cryptocurrencies",
         {"BTC-USD", "ETH-USD", "BNB-USD", "SOL-USD", "XRP-USD", "ADA-USD", "DOGE-USD", "LINK-USD", "DOT-USD",
          "AVAX-USD", "UNI-USD", "ATOM-USD"}},
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
