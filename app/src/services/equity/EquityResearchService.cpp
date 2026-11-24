// src/services/equity/EquityResearchService.cpp
#include "services/equity/EquityResearchService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "services/equity/EarningsRefresh.h"
#include "services/equity/TechnicalRating.h"
#include "services/markets/MarketDataService.h"
#include "storage/cache/CacheManager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace finterm::services::equity {

namespace {

/// A string field, with the daemon's "N/A" filler treated as absent.
///
/// get_info defaults longName / sector / industry / country / exchange /
/// longBusinessSummary to the literal string "N/A" when Yahoo has none. Left
/// as-is, every isEmpty() check reads it as a real value: the AI prompt told
/// the model "Company: N/A / Sector: N/A / N/A" as though those were facts,
/// and the overview rendered it as the company description.
/// MarketDataService::fetch_info already scrubs this; parse_info did not.
QString str(const QJsonObject& o, const char* key) {
    const QString v = o.value(QLatin1String(key)).toString();
    return v == QLatin1String("N/A") ? QString() : v;
}

/// A numeric field, or NaN when the vendor did not supply one.
///
/// yfinance's `info` dict is routinely PARTIAL — measured over 231 cached
/// payloads, beta was absent in 148, revenue_growth in 114, float_shares in
/// 82, market_cap and held_percent_institutions in 76, and the margins in 72.
/// _sanitize_for_json sends each absent value as JSON null, and a bare
/// toDouble() reads null as 0.0, so a third of the time the research panels
/// stated that a company had a 0.00% gross margin, 0 float and 0% growth —
/// with the same confidence as a real figure.
///
/// NaN is the representation the rest of the app already asks for:
/// NumberFormat.h's rule is "callers must gate on NaN / optional, never on
/// == 0.0", and a real 0.0 must render as 0. NaN also fails every `> 0` test
/// the panels already perform, and Qt writes it back out as JSON null, so the
/// MCP tools report "unknown" to an agent instead of a fabricated zero.
double num(const QJsonObject& o, const char* key) {
    return o.value(QLatin1String(key)).toDouble(std::numeric_limits<double>::quiet_NaN());
}



} // namespace

// ── Singleton ─────────────────────────────────────────────────────────────────
EquityResearchService& EquityResearchService::instance() {
    static EquityResearchService inst;
    return inst;
}

EquityResearchService::EquityResearchService(QObject* parent) : QObject(parent) {
    search_debounce_ = new QTimer(this);
    search_debounce_->setSingleShot(true);
    search_debounce_->setInterval(kDebounceMs);
    connect(search_debounce_, &QTimer::timeout, this, [this]() { search_symbols(pending_query_); });

    // One store: cache.db (CacheManager persists across restarts, each entry
    // with its own TTL and write time). The per-symbol JSON copy this used to
    // keep — and replay into the cache at launch — duplicated it.
}

// ── Python helper ─────────────────────────────────────────────────────────────
void EquityResearchService::run_python(const QString& script, const QStringList& args,
                                       std::function<void(bool, const QString&)> cb) {
    QPointer<EquityResearchService> self = this;
    python::PythonRunner::instance().run(script, args, [self, cb](python::PythonResult result) {
        if (!self)
            return;
        cb(result.success, result.success ? result.output : result.error);
    });
}

bool EquityResearchService::acquire_inflight(const QString& key) {
    if (in_flight_keys_.contains(key)) {
        // A daemon request for the same key is already pending — the
        // duplicate caller relies on the existing request's *_loaded signal
        // to deliver the result. No second daemon spawn needed.
        return false;
    }
    in_flight_keys_.insert(key);
    return true;
}

void EquityResearchService::release_inflight(const QString& key) {
    in_flight_keys_.remove(key);
}

void EquityResearchService::run_daemon(const QString& action, const QJsonObject& payload,
                                       std::function<void(bool, QJsonObject, QString)> cb, int timeout_ms) {
    QPointer<EquityResearchService> self = this;
    python::PythonWorker::instance().submit(
        action, payload,
        [self, cb = std::move(cb)](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            cb(ok, std::move(result), std::move(err));
        },
        timeout_ms);
}

// ── Search ────────────────────────────────────────────────────────────────────
void EquityResearchService::schedule_search(const QString& query) {
    pending_query_ = query;
    search_debounce_->start();
}

namespace {

QVector<SearchResult> parse_search_results(const QJsonObject& result) {
    // "results", or a flat array under "_value" (the other reply shape).
    const auto arr =
        result.value("_value").isArray() ? result.value("_value").toArray() : result.value("results").toArray();
    QVector<SearchResult> results;
    results.reserve(arr.size());
    for (const auto& v : arr) {
        auto o = v.toObject();
        SearchResult r;
        r.symbol = o["symbol"].toString();
        r.name = o["name"].toString();
        r.exchange = o["exchange"].toString();
        r.type = o["type"].toString();
        r.currency = o["currency"].toString();
        r.industry = o["industry"].toString();
        r.country = o["country"].toString();
        if (!r.symbol.isEmpty())
            results.append(r);
    }
    return results;
}

} // namespace

void EquityResearchService::search_symbols(const QString& query) {
    if (query.trimmed().isEmpty())
        return;
    QJsonObject payload;
    payload["query"] = query;
    payload["limit"] = 20;
    run_daemon("search", payload, [this, query](bool ok, QJsonObject result, QString err) {
        if (!ok) {
            // Said as a failure (its own signal — error_occurred with no
            // symbol would fail every MCP equity tool listening for one), not
            // as an empty result list, which reads as "no such ticker". The
            // query lets a listener ignore a late failure of an older one.
            LOG_WARN("EquityResearch", "Symbol search failed: " + err);
            emit search_failed(query, err, python::PythonWorker::reply_is_transient(ok, result));
            return;
        }
        emit search_results_loaded(parse_search_results(result), query);
    });
}

// ── QueryStore-backed subscriptions ───────────────────────────────────────────
//
// Each subscribe_* builds a fetcher closure that
//   1. Checks CacheManager. Hit → resolve with parsed value synchronously.
//   2. Miss → fire the existing daemon action. On daemon response, write
//      CacheManager + per-symbol disk + emit the legacy broadcast signal
//      (so non-migrated tabs keep receiving updates) + call QueryStore's
//      resolver with the parsed payload.
//
// The compat broadcast emission stays for Round 2; Round 5 will remove it
// once every consumer is on QueryStore. Doing both is harmless because the
// in-flight dedup is keyed by category+symbol — only one daemon spawn per
// concurrent subscribe wave.

void EquityResearchService::subscribe_quote(QObject* owner, const QString& symbol, query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:quote:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        read_quote(symbol, [resolve, reject](const QuoteData& q, const QDateTime& as_of) {
            if (!q.valid)
                reject(q.error);
            else if (as_of.isValid())
                resolve(QVariant::fromValue(q), as_of);
            else
                resolve(QVariant::fromValue(q));
        });
    };
    // SWR window: 5 × TTL. A quote 5 minutes past its 60s TTL is still
    // useful for "show last-known + refresh indicator" UX. Beyond that we
    // discard — a 30-minute-old quote during market hours is misleading.
    query::QueryStore::instance().subscribe(owner, key, kQuoteTtlSec, kQuoteTtlSec * 5, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::subscribe_info(QObject* owner, const QString& symbol, query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:info:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        read_info(symbol, [resolve, reject](const StockInfo& info, const services::MarketDataService::InfoReply& r) {
            if (!r.ok)
                reject(r.transient ? mark_transient_error(r.error) : r.error);
            else
                resolve(QVariant::fromValue(info), r.as_of);
        });
    };
    // Info is the slowest yfinance call (~1-3s) and changes slowly. Generous
    // SWR window — 8 × TTL ≈ 4 hours — keeps repeat views instant for a
    // working session. After that, treat as cold.
    constexpr int kInfoTtlSec = services::MarketDataService::kInfoTtlSec;
    query::QueryStore::instance().subscribe(owner, key, kInfoTtlSec, kInfoTtlSec * 8, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::subscribe_historical(QObject* owner, const QString& symbol, const QString& period,
                                                 query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:candles:" + symbol + ":" + period;
    auto fetcher = [this, symbol, period](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        // The app's one price-history read (same parser and cache as every
        // other chart and the portfolio returns).
        QPointer<EquityResearchService> self = this;
        services::MarketDataService::instance().fetch_bars(
            symbol, period, QStringLiteral("1d"),
            [self, symbol, period, resolve, reject](const services::MarketDataService::BarsReply& r) {
                if (!self)
                    return;
                if (!r.ok) {
                    const QString msg = "Failed to fetch historical for " + symbol + ": " + r.error;
                    emit self->error_occurred(symbol, "Historical", msg);
                    emit self->historical_loaded(symbol, period, {});
                    reject(msg);
                    return;
                }
                emit self->historical_loaded(symbol, period, r.bars);
                // The series' own fetch time: a cache hit is not "now".
                resolve(QVariant::fromValue(r.bars), r.as_of);
            });
    };
    // Daily candles barely change intraday — generous SWR window so period
    // switches feel instant for the rest of the session. Beyond 24h the
    // most-recent candle could be a day stale during market hours, which is
    // genuinely misleading; cap there.
    query::QueryStore::instance().subscribe(owner, key, kHistoricalTtlSec, /*stale_max*/ 86400, std::move(cb),
                                            std::move(fetcher));
}

QString EquityResearchService::technicals_history_period(const QString& requested, const QString& interval) {
    // Weekly bars need far more calendar time for the same warm-up: a
    // 200-period average of weekly closes is four years of history by itself.
    if (interval == QLatin1String("1wk"))
        return QStringLiteral("10y");

    // Anything shorter than two years cannot warm up the daily indicators the
    // rating requires. Longer selections are honoured as-is — they cost nothing
    // extra beyond the fetch, since every reading is taken off the last bar.
    static const QSet<QString> kBelowFloor = {
        QStringLiteral("1d"),  QStringLiteral("5d"),  QStringLiteral("1mo"), QStringLiteral("3mo"),
        QStringLiteral("6mo"), QStringLiteral("ytd"), QStringLiteral("1y"),
    };
    return kBelowFloor.contains(requested) ? QStringLiteral("2y") : requested;
}

QString EquityResearchService::technicals_key(const QString& symbol, const QString& requested,
                                              const QString& interval) {
    return "equity:technicals:" + QLatin1String(kTechnicalsSchemaTag) + ":" + symbol + ":" + interval + ":" +
           technicals_history_period(requested, interval);
}

void EquityResearchService::subscribe_technicals(QObject* owner, const QString& symbol, const QString& requested_period,
                                                 const QString& interval, query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    // Everything below works in the floored period, so the cache entry, the
    // QueryStore key and the `d.period` the watcher matches on all agree.
    const QString period = technicals_history_period(requested_period, interval);
    const QString key = technicals_key(symbol, requested_period, interval);
    auto fetcher = [this, symbol, period, interval, key](query::QueryStore::Resolver resolve,
                                                         query::QueryStore::Rejecter reject) {
        // Cache check first — fetch_technicals also checks but it emits the
        // broadcast synchronously without going through resolve, which would
        // skip the QueryStore delivery path. So we do an inline check that
        // calls resolve directly on hit.
        const auto tcv_aged = finterm::CacheManager::instance().try_get_aged(key);
        const QVariant tcv = tcv_aged ? QVariant(tcv_aged->value) : QVariant();
        if (!tcv.isNull()) {
            const auto doc = QJsonDocument::fromJson(tcv.toString().toUtf8());
            if (doc.isArray()) {
                TechnicalsData parsed = parse_technicals(symbol, period, interval, doc.array());
                emit technicals_loaded(parsed);
                // Resolve with the cache entry\'s WRITE time: a hit answers
                // instantly, and stamping "now" would present a value hydrated
                // from disk at startup as brand new.
                resolve(QVariant::fromValue(parsed), tcv_aged->written_at);
                return;
            }
        }

        // Cache miss → the fetch_technicals two-stage chain, bridged back.
        // Interval is part of the identity: daily and weekly requests share
        // the same floored period ("10y") and would otherwise resolve each
        // other and poison the QueryStore entry.
        bridge_once(
            &EquityResearchService::technicals_loaded, symbol, QStringLiteral("Technicals"),
            [symbol, period, interval](const TechnicalsData& d) {
                return d.symbol.compare(symbol, Qt::CaseInsensitive) == 0 && d.period == period &&
                       d.interval == interval;
            },
            [resolve](const TechnicalsData& d) { resolve(QVariant::fromValue(d)); },
            [reject](const QString& msg) { reject(msg); });
        fetch_technicals(symbol, period, interval);
    };
    // SWR window — technicals are derived from daily candles and barely
    // change intraday. Same 24h cap as historical: beyond that the most-
    // recent indicator values could be a day stale during market hours.
    query::QueryStore::instance().subscribe(owner, key, kTechnicalsTtlSec, /*stale_max*/ 86400, std::move(cb),
                                            std::move(fetcher));
}

// Financials and news resolve through the callback reads (cache first, with
// the entry's write time; else the fetch_* broadcast, bridged once). Peers
// bridge directly: fetch_peers keys its own cache on the basket.

void EquityResearchService::subscribe_financials(QObject* owner, const QString& symbol,
                                                 query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:financials:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        read_financials(symbol, [resolve, reject](bool ok, const FinancialsData& d, const QString& err,
                                                  const QDateTime& as_of) {
            if (ok)
                resolve(QVariant::fromValue(d), as_of);
            else
                reject(err);
        });
    };
    query::QueryStore::instance().subscribe(owner, key, kFinancialsTtlSec, kFinancialsTtlSec * 4, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::subscribe_news(QObject* owner, const QString& symbol, query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:news:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        read_news(symbol, 20, [resolve, reject](bool ok, const QVector<NewsArticle>& articles, const QString& err,
                                                const QDateTime& as_of) {
            if (ok)
                resolve(QVariant::fromValue(articles), as_of);
            else
                reject(err);
        });
    };
    query::QueryStore::instance().subscribe(owner, key, kNewsTtlSec, kNewsTtlSec * 5, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::subscribe_peers(QObject* owner, const QString& symbol, const QStringList& peer_symbols,
                                            query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    // Basket-aware key: same symbol with different peer baskets is a
    // different query and shouldn't share cache or in-flight slot.
    QStringList sorted_peers = peer_symbols;
    std::sort(sorted_peers.begin(), sorted_peers.end());
    const QString basket = sorted_peers.join(",");
    const QString key = "equity:peers:v4:" + symbol + ":" + basket;
    auto fetcher = [this, symbol, peer_symbols](query::QueryStore::Resolver resolve,
                                                query::QueryStore::Rejecter reject) {
        // No cache short-circuit at this layer — fetch_peers's own cache
        // path keys on the same basket and emits peers_loaded synchronously
        // on hit. We just attach the bridge and call.
        bridge_once(
            &EquityResearchService::peers_loaded, symbol, QStringLiteral("Peers"),
            [symbol](const QString& s, const QVector<PeerData>&) {
                return s.compare(symbol, Qt::CaseInsensitive) == 0;
            },
            [resolve](const QString&, const QVector<PeerData>& peers) { resolve(QVariant::fromValue(peers)); },
            [reject](const QString& msg) { reject(msg); });
        fetch_peers(symbol, peer_symbols);
    };
    query::QueryStore::instance().subscribe(owner, key, kPeersTtlSec, kPeersTtlSec * 2, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::subscribe_earnings(QObject* owner, const QString& symbol, query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    const QString key = "equity:earnings:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        load_earnings(symbol, [resolve, reject](const EarningsDatesReply& r) {
            // A refusal or no answer at all is not "no earnings" — don't
            // hold an empty answer for the TTL.
            if (r.transient) {
                reject(mark_transient_error(r.error));
                return;
            }
            // A plain failure is non-fatal: many tickers have no earnings
            // (ETFs, indices, certain ADRs). Resolved empty so the chart just
            // doesn't draw markers, rather than reporting an error.
            resolve(QVariant::fromValue(r.ok ? r.events : QVector<EarningsEvent>{}), r.as_of);
        });
    };
    // Earnings dates are session-stable. Generous SWR window.
    query::QueryStore::instance().subscribe(owner, key, kEarningsTtlSec, kEarningsTtlSec * 6, std::move(cb),
                                            std::move(fetcher));
}

void EquityResearchService::read_earnings(const QString& symbol, std::function<void(const EarningsDatesReply&)> done) {
    if (!done)
        return;
    // A cache hit answers inline; the caller is owed its answer after the
    // call returns, like a fetched one.
    load_earnings(symbol, [this, done = std::move(done)](const EarningsDatesReply& r) {
        QTimer::singleShot(0, this, [done, r]() { done(r); });
    });
}

void EquityResearchService::load_earnings(const QString& symbol, std::function<void(const EarningsDatesReply&)> done) {
    const QString cache_key = "equity:earnings:" + symbol;
    const auto ecv_aged = finterm::CacheManager::instance().try_get_aged(cache_key);
    const QVariant ecv = ecv_aged ? QVariant(ecv_aged->value) : QVariant();
    if (!ecv.isNull()) {
        const auto doc = QJsonDocument::fromJson(ecv.toString().toUtf8());
        if (doc.isArray()) {
            EarningsDatesReply r;
            r.ok = true;
            r.events = parse_earnings(doc.array());
            // The cache entry's WRITE time: stamping "now" would present a
            // value hydrated from disk at startup as brand new.
            r.as_of = ecv_aged->written_at;
            done(r);
            return;
        }
    }
    auto& waiting = earnings_in_flight_[symbol];
    waiting.append(std::move(done));
    if (waiting.size() > 1)
        return; // one daemon call per symbol; this caller shares it

    QJsonObject payload;
    payload["symbol"] = symbol;
    run_daemon("earnings_dates", payload, [this, symbol, cache_key](bool ok, QJsonObject result, QString err) {
        EarningsDatesReply r;
        if (python::PythonWorker::reply_is_transient(ok, result)) {
            r.transient = true;
            r.error = result.contains("error") ? result.value("error").toString() : err;
        } else if (!ok || result.contains("error")) {
            // Usually an ETF/index with no earnings.
            r.error = result.contains("error") ? result.value("error").toString() : err;
        } else {
            const QJsonArray arr = result.value("dates").toArray();
            finterm::CacheManager::instance().put(
                cache_key, QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
                kEarningsTtlSec, "equity");
            r.ok = true;
            r.events = parse_earnings(arr);
        }
        const auto waiters = earnings_in_flight_.take(symbol);
        for (const auto& w : waiters)
            w(r);
    });
}

void EquityResearchService::prefetch_historical(const QString& symbol, const QString& period) {
    if (symbol.isEmpty() || period.isEmpty())
        return;
    const QString key = "equity:candles:" + symbol + ":" + period;
    auto fetcher = [symbol, period](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        // No legacy broadcast on a prefetch — nobody is waiting, and it would
        // tell a subscribed tab the user switched to this period.
        services::MarketDataService::instance().fetch_bars(
            symbol, period, QStringLiteral("1d"), [resolve, reject](const services::MarketDataService::BarsReply& r) {
                if (!r.ok)
                    reject(r.error);
                else
                    resolve(QVariant::fromValue(r.bars), r.as_of);
            });
    };
    query::QueryStore::instance().prefetch(key, kHistoricalTtlSec, std::move(fetcher));
}

void EquityResearchService::read_info(
    const QString& symbol,
    std::function<void(const StockInfo&, const services::MarketDataService::InfoReply&)> done) {
    // The app's one company-info read (MarketDataService::fetch_info_raw:
    // one cache, coalesced, one retry on a daemon timeout).
    QPointer<EquityResearchService> self = this;
    services::MarketDataService::instance().fetch_info_raw(
        symbol, [self, symbol, done](const services::MarketDataService::InfoReply& r) {
            if (!self)
                return;
            StockInfo info;
            if (!r.ok) {
                emit self->error_occurred(symbol, "Info", r.error);
                info.symbol = symbol;
                info.valid = false;
            } else {
                info = self->parse_info(r.info);
            }
            emit self->info_loaded(info);
            if (done)
                done(info, r);
        });
}

void EquityResearchService::read_quote(const QString& symbol,
                                       std::function<void(const QuoteData&, const QDateTime&)> done) {
    // The app's one quote: MarketDataService's batch action, parser and cache
    // — the numbers the market:quote topic, the dashboard and the header's
    // live price carry. fetch_quotes batches and dedupes concurrent asks.
    QPointer<EquityResearchService> self = this;
    services::MarketDataService::instance().fetch_quotes(
        {symbol},
        [self, symbol, done](bool ok, QVector<services::QuoteData> quotes, const QString& err) {
            // The vendor's own reason: a rate limit is not "no such symbol".
            const QString why = !err.isEmpty() ? err : QStringLiteral("no quote for %1").arg(symbol);
            if (!self)
                return;
            QuoteData q;
            QDateTime as_of;
            if (!ok || quotes.isEmpty()) {
                emit self->error_occurred(symbol, "Quote", why);
                q.symbol = symbol;
                q.valid = false;
                q.error = why;
            } else {
                as_of = services::MarketDataService::instance().quote_as_of(symbol);
                q = to_equity_quote(quotes.first(), as_of);
            }
            emit self->quote_loaded(q);
            if (done)
                done(q, as_of);
        },
        /*stale_on_failure=*/false);
}

void EquityResearchService::search_symbols_for(const QString& query,
                                               std::function<void(QString, QVector<SearchResult>, QString)> cb) {
    if (!cb)
        return;
    search_symbols_reply(query, 20, QString(), [cb = std::move(cb)](SymbolSearchReply r) {
        // Answered even on failure — a caller that asked is owed an answer,
        // or its box sits there looking like the search is still running —
        // but the failure is said as one, not as an empty result list.
        if (r.failed) {
            // Whether a retry can help, said in the text (the one string
            // this callback carries): the MCP tool's agent decides on it.
            const QString why = r.error.isEmpty() ? QStringLiteral("search failed") : r.error;
            cb(r.query, {}, r.transient ? QStringLiteral("no answer from Yahoo (retry may help): ") + why : why);
            return;
        }
        cb(r.query, std::move(r.results), QString());
    });
}

void EquityResearchService::search_symbols_reply(const QString& query, int limit, const QString& type,
                                                 std::function<void(SymbolSearchReply)> cb) {
    const QString q = query.trimmed();
    if (q.isEmpty() || !cb)
        return;
    QJsonObject payload;
    payload["query"] = q;
    payload["limit"] = limit;
    if (!type.isEmpty())
        payload["type"] = type; // the daemon filters by Yahoo quoteType
    run_daemon("search", payload, [q, cb = std::move(cb)](bool ok, QJsonObject result, QString err) {
        SymbolSearchReply r;
        r.query = q;
        if (!ok) {
            LOG_WARN("EquityResearch", "Symbol search failed: " + err);
            r.failed = true;
            r.transient = python::PythonWorker::reply_is_transient(ok, result);
            r.error = err;
        } else {
            r.results = parse_search_results(result);
        }
        cb(std::move(r));
    });
}

// ── Load symbol (quote + info + historical in parallel) ───────────────────────
void EquityResearchService::load_symbol(const QString& symbol, const QString& period) {
    if (symbol.isEmpty())
        return;

    // Each block below must fall through independently — info and historical
    // must run regardless of the quote outcome. Do NOT add early `return`s
    // inside the blocks; that would abort the remaining fetches and leave the
    // overview overlay hung waiting on signals that never arrive.

    // ── Quote ────────────────────────────────────────────────────────────────
    read_quote(symbol, {});

    // ── Info ─────────────────────────────────────────────────────────────────
    read_info(symbol, {});

    // ── Historical ───────────────────────────────────────────────────────────
    // The app's one price-history read: keyed by period (a period switch is
    // a different series), concurrent asks coalesced there.
    {
        QPointer<EquityResearchService> self = this;
        services::MarketDataService::instance().fetch_bars(
            symbol, period, QStringLiteral("1d"),
            [self, symbol, period](const services::MarketDataService::BarsReply& r) {
                if (!self)
                    return;
                if (!r.ok)
                    emit self->error_occurred(symbol, "Historical",
                                              "Failed to fetch historical for " + symbol + ": " + r.error);
                // Empty candles signal failure; the overlay gate still closes
                // via on_historical_loaded.
                emit self->historical_loaded(symbol, period, r.ok ? r.bars : QVector<Candle>{});
            });
    }
}

// ── Financials ────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_financials(const QString& symbol) {
    // Tier 0: SWR cache — emit cached immediately and skip the network if
    // we have a fresh entry. Quarterly data; 1h TTL is plenty.
    {
        const auto fcv_aged = finterm::CacheManager::instance().try_get_aged("equity:financials:" + symbol);
        const QVariant fcv = fcv_aged ? QVariant(fcv_aged->value) : QVariant();
        if (!fcv.isNull()) {
            const auto cached = QJsonDocument::fromJson(fcv.toString().toUtf8()).object();
            emit financials_loaded(parse_financials(cached));
            return;
        }
    }

    const QString inflight_key = "financials:" + symbol;
    if (!acquire_inflight(inflight_key))
        return;
    QJsonObject payload;
    payload["symbol"] = symbol;
    run_daemon("financials", payload, [this, symbol, inflight_key](bool ok, QJsonObject obj, QString err) {
        release_inflight(inflight_key);
        if (!ok) {
            emit error_occurred(symbol, "Financials", "Failed to fetch financials for " + symbol + ": " + err);
            return;
        }
        if (obj.contains("error")) {
            emit error_occurred(symbol, "Financials", obj["error"].toString());
            return;
        }
        finterm::CacheManager::instance().put(
            "equity:financials:" + symbol,
            QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))), kFinancialsTtlSec,
            "equity");
        emit financials_loaded(parse_financials(obj));
    });
}

// ── Technicals ────────────────────────────────────────────────────────────────
//
// Optimised path (after the worker rewire):
//   • If `equity:technicals:<sym>:<period>` is cached → parse + emit immediately.
//   • Else fetch candles (cached at `equity:candles:<sym>`) via the persistent
//     yfinance daemon, then compute via the same daemon's `compute_technicals`
//     action. Both daemon hops share the warm pandas/yfinance import so the
//     wall time is dominated only by the network roundtrip on cold candles.
//
void EquityResearchService::fetch_technicals(const QString& symbol, const QString& requested_period,
                                             const QString& interval) {
    // Floored so a 1M selection still gets enough history to warm up the
    // indicators the rating needs. See technicals_history_period().
    const QString period = technicals_history_period(requested_period, interval);
    const QString tech_key = technicals_key(symbol, requested_period, interval);

    // ── Tier 0: technicals cache ─────────────────────────────────────────────
    {
        const auto tcv_aged = finterm::CacheManager::instance().try_get_aged(tech_key);
        const QVariant tcv = tcv_aged ? QVariant(tcv_aged->value) : QVariant();
        if (!tcv.isNull()) {
            const auto cached_doc = QJsonDocument::fromJson(tcv.toString().toUtf8());
            if (cached_doc.isArray()) {
                emit technicals_loaded(parse_technicals(symbol, period, interval, cached_doc.array()));
                return;
            }
        }
    }

    // Dedup: a (symbol, period) request already in flight will fan out via
    // technicals_loaded — duplicate caller drops here.
    const QString inflight_key = "technicals:" + symbol + ":" + interval + ":" + period;
    if (!acquire_inflight(inflight_key))
        return;

    QPointer<EquityResearchService> self = this;

    // ── Stage 2: compute via daemon, given a candles array ───────────────────
    auto run_compute = [self, symbol, period, interval, tech_key, inflight_key](const QJsonArray& candles) {
        if (!self) {
            return;
        } // (key remains held — process is exiting anyway)
        // Priced rows only (fetch_bars hands over bars::priced_rows): the
        // compute drops on CLOSE alone, so a null high/low would otherwise
        // reach ATR, Bollinger, Stochastic and Aroon as NaN.
        self->compute_technicals_rows(candles, [self, symbol, period, interval, tech_key,
                                                inflight_key](bool ok, const QJsonArray& data, const QString& err) {
            if (!self)
                return;
            self->release_inflight(inflight_key);
            // A failure, or a success with no rows, must not be pinned under
            // the shared tech key.
            if (!ok) {
                emit self->error_occurred(symbol, "Technicals", err);
                return;
            }
            // Cache the computed series so re-opens are <100ms.
            const QString blob = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
            finterm::CacheManager::instance().put(tech_key, QVariant(blob), kTechnicalsTtlSec, "equity");
            emit self->technicals_loaded(self->parse_technicals(symbol, period, interval, data));
        });
    };

    // ── Stage 1: the series, through the app's one price-history read ────────
    // One cache entry per (symbol, period, interval): the chart, the prefetch,
    // TALIpp and this compute all read the same daily series.
    services::MarketDataService::instance().fetch_bars(
        symbol, period, interval,
        [self, symbol, run_compute, inflight_key](const services::MarketDataService::BarsReply& r) {
            if (!self)
                return;
            if (!r.ok || r.rows.isEmpty()) {
                // Stage 2 will never run to release the dedup key.
                self->release_inflight(inflight_key);
                emit self->error_occurred(symbol, "Technicals",
                                          QStringLiteral("Historical fetch returned no data: %1")
                                              .arg(r.ok ? QStringLiteral("empty history") : r.error));
                return;
            }
            run_compute(r.rows);
        });
}

// ── Peers ─────────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_peers(const QString& symbol, const QStringList& peer_symbols) {
    // Build the cache key from the full symbol set so different peer
    // selections for the same primary symbol don't collide.
    QStringList key_syms;
    key_syms.append(symbol);
    key_syms.append(peer_symbols);
    const QString cache_key = "equity:peers:v4:" + key_syms.join(",");

    // Tier 0: SWR cache — peer ratios are stable over hour-scale.
    {
        const auto pcv_aged = finterm::CacheManager::instance().try_get_aged(cache_key);
        const QVariant pcv = pcv_aged ? QVariant(pcv_aged->value) : QVariant();
        if (!pcv.isNull()) {
            const auto arr = QJsonDocument::fromJson(pcv.toString().toUtf8()).array();
            emit peers_loaded(symbol, parse_peers(arr));
            return;
        }
    }

    // Reuse the cache key as the dedup key — if a fetch for this exact
    // peer set is already pending, drop the second caller.
    if (!acquire_inflight(cache_key))
        return;

    QJsonArray syms_arr;
    syms_arr.append(symbol);
    for (const auto& s : peer_symbols)
        syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    run_daemon("multiple_ratios", payload, [this, symbol, cache_key](bool ok, QJsonObject result, QString err) {
        release_inflight(cache_key);
        if (!ok) {
            emit error_occurred(symbol, "Peers", "Failed to fetch peer data: " + err);
            return;
        }
        // multiple_ratios returns a list; PythonWorker wraps under "_value".
        const auto arr =
            result.contains("_value") ? result.value("_value").toArray() : result.value("ratios").toArray();
        finterm::CacheManager::instance().put(
            cache_key, QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))), kPeersTtlSec,
            "equity");
        emit peers_loaded(symbol, parse_peers(arr));
    });
}

// ── News ──────────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_news(const QString& symbol, int count) {
    // Tier 0: SWR cache — emit cached immediately and skip the network.
    {
        const auto ncv_aged = finterm::CacheManager::instance().try_get_aged("equity:news:" + symbol);
        const QVariant ncv = ncv_aged ? QVariant(ncv_aged->value) : QVariant();
        if (!ncv.isNull()) {
            const QJsonArray arr = QJsonDocument::fromJson(ncv.toString().toUtf8()).array();
            emit news_loaded(symbol, parse_news(arr));
            return;
        }
    }

    const QString inflight_key = "news:" + symbol;
    if (!acquire_inflight(inflight_key))
        return;
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["count"] = count;
    run_daemon("news", payload, [this, symbol, inflight_key](bool ok, QJsonObject obj, QString err) {
        release_inflight(inflight_key);
        // The daemon reports a refused request (e.g. a Yahoo rate limit) in the
        // payload. Read as an empty success it was CACHED as "no news" for the
        // whole TTL, outliving the outage.
        if (ok && !obj.value("error").toString().isEmpty()) {
            ok = false;
            err = obj.value("error").toString();
        }
        if (!ok) {
            emit error_occurred(symbol, "News", "Failed to fetch news for " + symbol + ": " + err);
            return;
        }
        const QJsonArray arr = obj.value("articles").toArray();
        finterm::CacheManager::instance().put(
            "equity:news:" + symbol, QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
            kNewsTtlSec, "equity");
        emit news_loaded(symbol, parse_news(arr));
    });
}

// ── Callback reads ────────────────────────────────────────────────────────────
//
// Cache first (answered with the entry's write time), else the broadcast
// fetch_* path bridged once — so the in-flight dedupe and the disk copy stay
// fetch_*'s alone. Both start on the next event-loop turn: these answer after
// the call returns, cache hit included.

void EquityResearchService::read_financials(const QString& symbol, FinancialsCallback cb) {
    const QString sym = symbol.trimmed().toUpper();
    QPointer<EquityResearchService> self = this;
    QTimer::singleShot(0, this, [self, sym, cb = std::move(cb)]() {
        if (!self)
            return;
        if (sym.isEmpty()) {
            cb(false, {}, QStringLiteral("Financials: no symbol"), {});
            return;
        }
        if (const auto aged = finterm::CacheManager::instance().try_get_aged("equity:financials:" + sym)) {
            const auto doc = QJsonDocument::fromJson(aged->value.toUtf8());
            if (doc.isObject()) {
                const FinancialsData parsed = self->parse_financials(doc.object());
                emit self->financials_loaded(parsed);
                cb(true, parsed, {}, aged->written_at);
                return;
            }
        }
        self->bridge_once(
            &EquityResearchService::financials_loaded, sym, QStringLiteral("Financials"),
            [sym](const FinancialsData& d) { return d.symbol.compare(sym, Qt::CaseInsensitive) == 0; },
            [cb](const FinancialsData& d) { cb(true, d, {}, QDateTime::currentDateTime()); },
            [cb](const QString& msg) { cb(false, {}, msg, {}); });
        self->fetch_financials(sym);
    });
}

void EquityResearchService::read_news(const QString& symbol, int count, NewsCallback cb) {
    const QString sym = symbol.trimmed().toUpper();
    QPointer<EquityResearchService> self = this;
    QTimer::singleShot(0, this, [self, sym, count, cb = std::move(cb)]() {
        if (!self)
            return;
        if (sym.isEmpty()) {
            cb(false, {}, QStringLiteral("News: no symbol"), {});
            return;
        }
        if (const auto aged = finterm::CacheManager::instance().try_get_aged("equity:news:" + sym)) {
            const auto doc = QJsonDocument::fromJson(aged->value.toUtf8());
            if (doc.isArray()) {
                const QVector<NewsArticle> parsed = self->parse_news(doc.array());
                emit self->news_loaded(sym, parsed);
                cb(true, parsed, {}, aged->written_at);
                return;
            }
        }
        self->bridge_once(
            &EquityResearchService::news_loaded, sym, QStringLiteral("News"),
            [sym](const QString& s, const QVector<NewsArticle>&) { return s.compare(sym, Qt::CaseInsensitive) == 0; },
            [cb](const QString&, const QVector<NewsArticle>& articles) {
                cb(true, articles, {}, QDateTime::currentDateTime());
            },
            [cb](const QString& msg) { cb(false, {}, msg, {}); });
        self->fetch_news(sym, count);
    });
}

void EquityResearchService::compute_technicals_rows(const QJsonArray& candles, RowsCallback cb) {
    if (candles.isEmpty()) {
        QTimer::singleShot(0, this, [cb = std::move(cb)]() {
            cb(false, {}, QStringLiteral("Indicator computation: no price rows to compute from"));
        });
        return;
    }
    QPointer<EquityResearchService> self = this;
    QJsonObject payload;
    payload["candles"] = candles;
    python::PythonWorker::instance().submit(
        "compute_technicals", payload,
        [self, cb = std::move(cb)](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            if (!ok || !result.value("success").toBool(false)) {
                cb(false, {},
                   ok ? QString("compute_technicals returned failure: %1").arg(result.value("error").toString())
                      : QString("Indicator computation failed: %1").arg(err));
                return;
            }
            const QJsonArray data = result.value("data").toArray();
            if (data.isEmpty()) {
                cb(false, {}, QStringLiteral("Indicator computation returned no rows"));
                return;
            }
            cb(true, data, {});
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

QVector<QPair<QString, QString>> EquityResearchService::technicals_stage_errors(const QJsonObject& row) {
    QVector<QPair<QString, QString>> lost;
    for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
        const QString& key = it.key();
        if (key.startsWith(QLatin1Char('_')) && key.endsWith(QLatin1String("_error")) && key.size() > 7)
            lost.append({key.mid(1, key.size() - 7), it.value().toString()});
    }
    std::sort(lost.begin(), lost.end());
    return lost;
}

QJsonArray EquityResearchService::statement_json(const QVector<QPair<QString, QJsonObject>>& statement) {
    QJsonArray arr;
    for (const auto& kv : statement)
        arr.append(QJsonObject{{"period", kv.first}, {"items", kv.second}});
    return arr;
}

QJsonArray EquityResearchService::news_json(const QVector<NewsArticle>& articles) {
    QJsonArray arr;
    for (const auto& a : articles) {
        arr.append(QJsonObject{
            {"title", a.title},
            {"description", a.description},
            {"url", a.url},
            {"publisher", a.publisher},
            {"published_date", a.published_date},
        });
    }
    return arr;
}

// ── TALIpp ────────────────────────────────────────────────────────────────────
void EquityResearchService::compute_talipp(const QString& symbol, const QString& indicator, const QVariantMap& params,
                                           const QString& period) {
    // Takes the array rather than the serialized string so both callers get
    // the price-less-bar filter. equity_talipp.py refuses any bar with a
    // missing or null price field (its field() check) — correctly, since a
    // zero price would corrupt every downstream value — so the rows must be
    // filtered to fully-priced bars BEFORE they reach it.
    auto run_talipp = [this, symbol, indicator, params](const QJsonArray& raw) {
        // Priced rows only (fetch_bars): equity_talipp.py refuses a null price.
        const QByteArray hist_json = QJsonDocument(raw).toJson(QJsonDocument::Compact);
        QJsonObject p_obj;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            p_obj[it.key()] = QJsonValue::fromVariant(it.value());
        QString params_json = QJsonDocument(p_obj).toJson(QJsonDocument::Compact);

        run_python("equity_talipp.py", {indicator, QString::fromUtf8(hist_json), params_json},
                   [this, symbol, indicator](bool ok, const QString& out) {
                       if (!ok) {
                           emit error_occurred(symbol, "TALIpp", out);
                           return;
                       }
                       auto doc = QJsonDocument::fromJson(python::extract_json(out).toUtf8()).object();
                       QVector<double> values;
                       QVector<qint64> timestamps;
                       for (const auto& v : doc["values"].toArray())
                           values.append(v.toDouble());
                       for (const auto& t : doc["timestamps"].toArray())
                           timestamps.append(static_cast<qint64>(t.toDouble()));
                       emit talipp_result(indicator, values, timestamps);
                   });
    };

    // The series through the app's one price-history read (cached per
    // period, concurrent asks coalesced — parameter tweaks during an outage
    // ride the one request out).
    QPointer<EquityResearchService> self = this;
    services::MarketDataService::instance().fetch_bars(
        symbol, period, QStringLiteral("1d"),
        [self, symbol, run_talipp](const services::MarketDataService::BarsReply& r) {
            if (!self)
                return;
            if (!r.ok || r.rows.isEmpty()) {
                emit self->error_occurred(symbol, "TALIpp",
                                          r.ok ? QStringLiteral("Historical fetch returned no data")
                                               : "Historical fetch failed: " + r.error);
                return;
            }
            run_talipp(r.rows);
        });
}

// ── Parsers ───────────────────────────────────────────────────────────────────
QuoteData EquityResearchService::to_equity_quote(const services::QuoteData& m, const QDateTime& as_of) {
    // The market quote, in this screen's shape. Absent stays NaN (never 0).
    auto known = [](double v) { return std::isfinite(v) && v != 0.0 ? v : kUnknown; };
    QuoteData q;
    q.symbol = m.symbol;
    q.price = known(m.price);
    q.change = m.change;
    q.change_pct = m.change_pct;
    q.open = m.open;
    q.high = known(m.high);
    q.low = known(m.low);
    q.prev_close = m.prev_close;
    q.volume = m.volume;
    q.timestamp = as_of.isValid() ? as_of.toSecsSinceEpoch() : 0; // EVENT-STAMP: the fetch's own instant
    return q;
}

StockInfo EquityResearchService::parse_info(const QJsonObject& o) const {
    StockInfo s;
    s.symbol = str(o, "symbol");
    s.company_name = str(o, "company_name");
    s.sector = str(o, "sector");
    s.industry = str(o, "industry");
    s.description = str(o, "description");
    s.website = str(o, "website");
    s.country = str(o, "country");
    s.currency = str(o, "currency");
    s.exchange = str(o, "exchange");
    s.quote_type = str(o, "quote_type");
    s.employees = o["employees"].toInt();

    s.market_cap = num(o, "market_cap");
    s.enterprise_value = num(o, "enterprise_value");
    s.pe_ratio = num(o, "pe_ratio");
    s.forward_pe = num(o, "forward_pe");
    s.peg_ratio = num(o, "peg_ratio");
    s.price_to_book = num(o, "price_to_book");
    s.ev_to_revenue = num(o, "enterprise_to_revenue");
    s.ev_to_ebitda = num(o, "enterprise_to_ebitda");

    s.gross_margins = num(o, "gross_margins");
    s.operating_margins = num(o, "operating_margins");
    s.ebitda_margins = num(o, "ebitda_margins");
    s.profit_margins = num(o, "profit_margins");
    s.roe = num(o, "return_on_equity");
    s.roa = num(o, "return_on_assets");
    s.gross_profits = num(o, "gross_profits");

    s.book_value = num(o, "book_value");
    s.revenue_per_share = num(o, "revenue_per_share");
    s.free_cashflow = num(o, "free_cashflow");
    s.operating_cashflow = num(o, "operating_cashflow");
    s.total_cash = num(o, "total_cash");
    s.total_debt = num(o, "total_debt");
    s.total_revenue = num(o, "total_revenue");

    s.earnings_growth = num(o, "earnings_growth");
    s.revenue_growth = num(o, "revenue_growth");

    s.shares_outstanding = num(o, "shares_outstanding");
    s.float_shares = num(o, "float_shares");
    s.held_insiders_pct = num(o, "held_percent_insiders");
    s.held_institutions_pct = num(o, "held_percent_institutions");
    s.short_ratio = num(o, "short_ratio");
    s.short_pct_of_float = num(o, "short_percent_of_float");

    s.week52_high = num(o, "fifty_two_week_high");
    s.week52_low = num(o, "fifty_two_week_low");
    s.avg_volume = num(o, "average_volume");
    s.beta = num(o, "beta");
    s.dividend_yield = num(o, "dividend_yield");
    s.current_price = num(o, "current_price");

    s.target_high = num(o, "target_high_price");
    s.target_low = num(o, "target_low_price");
    s.target_mean = num(o, "target_mean_price");
    s.recommendation_mean = num(o, "recommendation_mean");
    s.recommendation_key = str(o, "recommendation_key");
    s.analyst_count = o["number_of_analyst_opinions"].toInt();
    return s;
}

FinancialsData EquityResearchService::parse_financials(const QJsonObject& obj) const {
    FinancialsData fd;
    fd.symbol = obj["symbol"].toString();

    auto parse_stmt = [](const QJsonObject& stmt) {
        QVector<QPair<QString, QJsonObject>> result;
        for (auto it = stmt.constBegin(); it != stmt.constEnd(); ++it)
            result.append({it.key(), it.value().toObject()});
        // Sort by period descending (most recent first)
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        return result;
    };

    fd.income_statement = parse_stmt(obj["income_statement"].toObject());
    fd.balance_sheet = parse_stmt(obj["balance_sheet"].toObject());
    fd.cash_flow = parse_stmt(obj["cash_flow"].toObject());

    const QJsonObject ttm = obj["ttm"].toObject();
    fd.ttm_revenue = ttm["revenue"].toDouble();
    fd.ttm_net_income = ttm["net_income"].toDouble();
    fd.ttm_operating_income = ttm["operating_income"].toDouble();
    fd.ttm_ebitda = ttm["ebitda"].toDouble();
    const QJsonArray qs = ttm["quarters"].toArray();
    if (qs.size() >= 2)
        fd.ttm_period = qs.last().toString() + QStringLiteral(" … ") + qs.first().toString();
    return fd;
}

namespace {

/// Copy every numeric column of a JSON row into an IndicatorRow.
///
/// The scorer needs columns the tab never displays — DI+/DI- to give ADX a
/// direction, the MACD signal line to turn the MACD line into a histogram,
/// Aroon's other leg — so the whole row is loaded rather than the display
/// subset. Nulls (the JSON encoding of a NaN warm-up value) are not `isDouble`
/// and drop out here; IndicatorRow::set filters any remaining non-finite value.
void fill_row(finterm::services::equity::IndicatorRow& row, const QJsonObject& src) {
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        if (it.value().isDouble())
            row.set(it.key(), it.value().toDouble());
    }
}

} // namespace

TechnicalsData EquityResearchService::parse_technicals(const QString& symbol, const QString& period,
                                                       const QString& interval, const QJsonArray& rows) const {
    TechnicalsData td;
    td.symbol = symbol;
    td.period = period;
    td.interval = interval;
    if (rows.isEmpty())
        return td;

    // compute_technicals emits lowercase snake_case columns, one row per candle.
    const QJsonObject last = rows.last().toObject();
    td.last_bar_ts = static_cast<qint64>(last.value("timestamp").toDouble(0));

    RatingInput input;
    input.bars = static_cast<int>(rows.size());
    input.close = last.value("close").toDouble();
    fill_row(input.now, last);
    if (rows.size() >= 2)
        fill_row(input.prev, rows.at(rows.size() - 2).toObject());
    if (rows.size() > technical_rating::kSlopeLookback)
        fill_row(input.back, rows.at(rows.size() - 1 - technical_rating::kSlopeLookback).toObject());

    // The Python side isolates a failing indicator stage and leaves a
    // `_<stage>_error` column behind rather than aborting the compute. Surface
    // it: without this the panel merely looks thinner than usual while the
    // rating is quietly derived from whatever survived.
    QStringList lost;
    for (const auto& [stage, why] : technicals_stage_errors(last))
        lost << stage;
    if (!lost.isEmpty()) {
        td.data_warning = QStringLiteral("Could not compute: %1").arg(lost.join(QStringLiteral(", ")));
    }

    // Display name → column key in the computed output.
    // The averages span fast to slow on purpose — see is_moving_average() in
    // TechnicalRating.cpp for why a narrow set biased the whole rating.
    // CANONICAL indicator list: tests/services/test_technical_rating.cpp and
    // tools/technicals_series/score_series.cpp carry copies that must match,
    // or the tests and the measurement tool score a different panel than ships.
    static const QList<QPair<QString, QString>> kTrend = {
        {"SMA 10", "sma_10"},     {"SMA 20", "sma_20"},
        {"SMA 30", "sma_30"},     {"SMA 50", "sma_50"},
        {"SMA 100", "sma_100"},   {"SMA 200", "sma_200"},
        {"EMA 12", "ema_12"},     {"EMA 26", "ema_26"},
        {"WMA 9", "wma_9"},       {"Ichimoku Base", "ichimoku_base"},
        {"MACD", "macd"},         {"MACD Signal", "macd_signal"},
        {"CCI", "cci"},           {"ADX", "adx"},
        {"Aroon Up", "aroon_up"}, {"Aroon Down", "aroon_down"},
    };
    static const QList<QPair<QString, QString>> kMomentum = {
        {"RSI", "rsi"}, {"Stoch %K", "stoch_k"}, {"Stoch %D", "stoch_d"}, {"Williams %R", "williams_r"},
        {"ROC", "roc"}, {"MFI", "mfi"},          {"Awesome Osc", "ao"},   {"KAMA", "kama"},
    };
    static const QList<QPair<QString, QString>> kVolatility = {
        {"ATR", "atr"},           {"BB Mid", "bb_mavg"}, {"BB Upper", "bb_hband"},
        {"BB Lower", "bb_lband"}, {"BB %B", "bb_pband"}, {"BB Width", "bb_wband"},
    };
    static const QList<QPair<QString, QString>> kVolume = {
        {"OBV", "obv"},
        {"VWAP (14-bar rolling)", "vwap"},
        {"CMF", "cmf"},
        {"ADI", "adi"},
    };

    auto build = [&](const QList<QPair<QString, QString>>& keys, const QString& cat) {
        QVector<TechIndicator> out;
        for (const auto& kv : keys) {
            const QString& col = kv.second;
            if (!input.now.has(col))
                continue; // never computed, or still inside its warm-up window
            const IndicatorVerdict verdict = technical_rating::score(col, input);
            TechIndicator ti;
            ti.name = kv.first;
            ti.value = input.now.get(col);
            ti.category = cat;
            ti.signal = verdict.signal;
            ti.votes = verdict.votes;
            ti.rating_bucket = verdict.bucket;
            out.append(ti);
        }
        return out;
    };

    td.trend = build(kTrend, "trend");
    td.momentum = build(kMomentum, "momentum");
    td.volatility = build(kVolatility, "volatility");
    td.volume = build(kVolume, "volume");

    QVector<TechIndicator> all;
    all << td.trend << td.momentum << td.volatility << td.volume;

    const RatingVerdict verdict = technical_rating::aggregate(all, input);
    td.rated = verdict.rated;
    td.overall_signal = verdict.overall;
    td.net_score = verdict.net;
    td.rating_basis = verdict.basis;
    td.voting_count = verdict.voting;
    td.strong_buy = verdict.strong_buy;
    td.buy = verdict.buy;
    td.neutral = verdict.neutral;
    td.sell = verdict.sell;
    td.strong_sell = verdict.strong_sell;

    return td;
}

QVector<PeerData> EquityResearchService::parse_peers(const QJsonArray& arr) const {
    QVector<PeerData> peers;
    for (const auto& v : arr) {
        auto o = v.toObject();
        PeerData p;
        p.symbol = str(o, "symbol");
        p.pe_ratio = num(o, "peRatio");
        p.forward_pe = num(o, "forwardPE");
        p.price_to_book = num(o, "priceToBook");
        p.price_to_sales = num(o, "priceToSales");
        p.peg_ratio = num(o, "pegRatio");
        p.debt_to_equity = num(o, "debtToEquity");
        p.roe = num(o, "returnOnEquity");
        p.roa = num(o, "returnOnAssets");
        p.profit_margin = num(o, "profitMargin");
        p.operating_margin = num(o, "operatingMargin");
        p.gross_margin = num(o, "grossMargin");
        p.current_ratio = num(o, "currentRatio");
        p.quick_ratio = num(o, "quickRatio");
        p.dividend_yield = num(o, "dividendYield");
        // PRICE, REV GRWTH and BETA are columns in the peers table; without
        // these assignments they rendered empty for every row.
        p.price = num(o, "price");
        p.beta = num(o, "beta");
        p.market_cap = num(o, "marketCap");
        p.revenue_growth = num(o, "revenueGrowth");
        p.earnings_growth = num(o, "earningsGrowth");
        p.name = str(o, "name");
        p.sector = str(o, "sector");
        peers.append(p);
    }
    return peers;
}

QVector<NewsArticle> EquityResearchService::parse_news(const QJsonArray& arr) const {
    QVector<NewsArticle> articles;
    articles.reserve(arr.size());
    for (const auto& v : arr) {
        auto o = v.toObject();
        NewsArticle a;
        a.title = o["title"].toString();
        a.description = o["description"].toString();
        a.url = o["url"].toString();
        a.publisher = o["publisher"].toString();
        a.published_date = o["published_date"].toString();
        if (!a.title.isEmpty())
            articles.append(a);
    }
    return articles;
}

QVector<EarningsEvent> EquityResearchService::parse_earnings(const QJsonArray& arr) const {
    QVector<EarningsEvent> events;
    events.reserve(arr.size());
    for (const auto& v : arr) {
        const auto o = v.toObject();
        EarningsEvent e;
        e.timestamp = o.value("timestamp").toVariant().toLongLong();
        if (e.timestamp <= 0)
            continue;
        const auto est_v = o.value("eps_estimate");
        if (est_v.isDouble()) {
            e.eps_estimate = est_v.toDouble();
            e.has_estimate = true;
        }
        const auto act_v = o.value("eps_actual");
        if (act_v.isDouble()) {
            e.eps_actual = act_v.toDouble();
            e.has_actual = true;
        }
        const auto sur_v = o.value("surprise_pct");
        if (sur_v.isDouble()) {
            e.surprise_pct = sur_v.toDouble();
            e.has_surprise = true;
        }
        events.append(e);
    }
    return events;
}

// ── Earnings analysis ────────────────────────────────────────────────────────
namespace {

/// JSON number → optional. `null` (the daemon's "no data") and non-numbers
/// both yield nullopt, so a real 0.0 survives as a value.
std::optional<double> opt_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (v.isNull() || v.isUndefined() || !v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

void read_period_label(const QJsonObject& o, QString& period, QString& label) {
    period = o.value("period").toString();
    label = o.value("label").toString();
    if (label.isEmpty())
        label = period;
}

} // namespace

EarningsAnalysis EquityResearchService::parse_earnings_analysis(const QJsonObject& obj) const {
    EarningsAnalysis a;
    a.symbol = obj.value("symbol").toString();
    a.currency = obj.value("currency").toString();
    a.as_of = obj.value("as_of").toVariant().toLongLong();
    // `valid` means "the daemon answered", not "this symbol has earnings" —
    // ETFs and funds legitimately come back with every section empty, which
    // the tab reports as a clean empty state rather than an error.
    a.valid = !obj.contains("error");

    const auto nx = obj.value("next").toObject();
    if (!nx.isEmpty()) {
        const auto ts = nx.value("timestamp");
        if (!ts.isNull() && !ts.isUndefined())
            a.next.timestamp = ts.toVariant().toLongLong();
        a.next.is_estimated = nx.value("is_estimated").toBool(true);
        a.next.eps_avg = opt_num(nx, "eps_avg");
        a.next.eps_low = opt_num(nx, "eps_low");
        a.next.eps_high = opt_num(nx, "eps_high");
        a.next.analysts = opt_num(nx, "analysts");
        a.next.rev_avg = opt_num(nx, "rev_avg");
        a.next.rev_low = opt_num(nx, "rev_low");
        a.next.rev_high = opt_num(nx, "rev_high");
        a.next.year_ago_eps = opt_num(nx, "year_ago_eps");
        a.next.year_ago_rev = opt_num(nx, "year_ago_rev");
        a.next.eps_growth = opt_num(nx, "eps_growth");
        a.next.rev_growth = opt_num(nx, "rev_growth");

        // Absent whenever the daemon could not compute it honestly (no listed
        // options, or no expiry within ten days of the print) — see the model.
        const auto imp = nx.value("implied").toObject();
        if (!imp.isEmpty()) {
            EarningsImpliedMove m;
            m.expiry = imp.value("expiry").toString();
            m.days_after_print = imp.value("days_after_print").toInt();
            m.total_move_pct = opt_num(imp, "total_move_pct");
            m.event_move_pct = opt_num(imp, "event_move_pct");
            m.straddle = opt_num(imp, "straddle");
            m.spot = opt_num(imp, "spot");
            m.strike = opt_num(imp, "strike");
            if (m.total_move_pct.has_value())
                a.next.implied = m;
        }
    }

    const auto val = obj.value("valuation").toObject();
    a.valuation.price = opt_num(val, "price");
    a.valuation.trailing_eps = opt_num(val, "trailing_eps");
    a.valuation.forward_eps = opt_num(val, "forward_eps");
    a.valuation.trailing_pe = opt_num(val, "trailing_pe");
    a.valuation.forward_pe = opt_num(val, "forward_pe");
    a.valuation.target_mean = opt_num(val, "target_mean");
    a.valuation.target_high = opt_num(val, "target_high");
    a.valuation.target_low = opt_num(val, "target_low");
    a.valuation.recommendation_mean = opt_num(val, "recommendation_mean");
    a.valuation.analyst_count = opt_num(val, "analyst_count");
    a.valuation.earnings_growth = opt_num(val, "earnings_growth");
    a.valuation.revenue_growth = opt_num(val, "revenue_growth");
    a.valuation.recommendation = val.value("recommendation").toString();

    for (const auto& v : obj.value("history").toArray()) {
        const auto o = v.toObject();
        EarningsPoint p;
        p.timestamp = o.value("timestamp").toVariant().toLongLong();
        if (p.timestamp <= 0)
            continue;
        p.eps_estimate = opt_num(o, "eps_estimate");
        p.eps_actual = opt_num(o, "eps_actual");
        p.surprise_pct = opt_num(o, "surprise_pct");
        p.surprise_suspect = o.value("surprise_suspect").toBool(false);
        p.eps_qoq_pct = opt_num(o, "eps_qoq_pct");
        p.eps_yoy_pct = opt_num(o, "eps_yoy_pct");
        p.reaction_pct = opt_num(o, "reaction_pct");
        p.reaction_live_pct = opt_num(o, "reaction_live_pct");
        p.runup_pct = opt_num(o, "runup_pct");
        p.runup_20d_pct = opt_num(o, "runup_20d_pct");
        p.pre_vol_pct = opt_num(o, "pre_vol_pct");
        p.price_before = opt_num(o, "price_before");
        p.price_after = opt_num(o, "price_after");
        p.is_estimate = o.value("is_estimate").toBool(false);
        p.move_since_last_pct = opt_num(o, "move_since_last_pct");
        p.price_now = opt_num(o, "price_now");
        p.has_forward_estimate = o.value("has_forward_estimate").toBool(false);
        a.history.append(p);
    }

    for (const auto& v : obj.value("estimates").toArray()) {
        const auto o = v.toObject();
        EarningsEstimateRow r;
        read_period_label(o, r.period, r.label);
        r.eps_avg = opt_num(o, "eps_avg");
        r.eps_low = opt_num(o, "eps_low");
        r.eps_high = opt_num(o, "eps_high");
        r.analysts = opt_num(o, "analysts");
        r.year_ago_eps = opt_num(o, "year_ago_eps");
        r.eps_growth = opt_num(o, "eps_growth");
        r.rev_avg = opt_num(o, "rev_avg");
        r.rev_low = opt_num(o, "rev_low");
        r.rev_high = opt_num(o, "rev_high");
        r.year_ago_rev = opt_num(o, "year_ago_rev");
        r.rev_growth = opt_num(o, "rev_growth");
        a.estimates.append(r);
    }

    for (const auto& v : obj.value("trend").toArray()) {
        const auto o = v.toObject();
        EarningsTrendRow r;
        read_period_label(o, r.period, r.label);
        r.current = opt_num(o, "current");
        r.d7 = opt_num(o, "d7");
        r.d30 = opt_num(o, "d30");
        r.d60 = opt_num(o, "d60");
        r.d90 = opt_num(o, "d90");
        a.trend.append(r);
    }

    for (const auto& v : obj.value("revisions").toArray()) {
        const auto o = v.toObject();
        EarningsRevisionRow r;
        read_period_label(o, r.period, r.label);
        r.up_7d = opt_num(o, "up_7d");
        r.up_30d = opt_num(o, "up_30d");
        r.down_7d = opt_num(o, "down_7d");
        r.down_30d = opt_num(o, "down_30d");
        a.revisions.append(r);
    }

    for (const auto& v : obj.value("growth").toArray()) {
        const auto o = v.toObject();
        EarningsGrowthRow r;
        read_period_label(o, r.period, r.label);
        r.stock = opt_num(o, "stock");
        r.index = opt_num(o, "index");
        a.growth.append(r);
    }

    const auto recent = obj.value("recent").toObject();
    a.runup_5d_pct = opt_num(recent, "runup_5d");
    a.runup_20d_pct = opt_num(recent, "runup_20d");
    a.runup_90d_pct = opt_num(recent, "runup_90d");
    a.rel_runup_20d_pct = opt_num(recent, "rel_runup_20d");
    a.pct_from_52w_high = opt_num(recent, "pct_from_52w_high");
    a.pre_vol_pct = opt_num(recent, "pre_vol_pct");

    return a;
}

void EquityResearchService::subscribe_earnings_analysis(QObject* owner, const QString& symbol,
                                                        query::QueryStore::Callback cb) {
    if (symbol.isEmpty())
        return;
    // Version segment (earnings_analysis_key): this project has been bitten
    // by unversioned cache keys before — a bump makes a stale shape's window
    // zero instead of one TTL.
    //   v3  reaction_pct withheld until the reaction session closes (a v2
    //       entry could settle a still-running close into the ledger).
    //   v4  a date-only next.timestamp is midnight ET, not UTC.
    const QString key = earnings_analysis_key(symbol);
    auto fetcher = [this, symbol, key](query::QueryStore::Resolver resolve, query::QueryStore::Rejecter reject) {
        // A scheduled or ↻ refresh knows the copies are out of date: skip
        // the disk copy here and the daemon's with `fresh`.
        const bool fresh = force_fresh_.remove(key);
        if (!fresh) {
            const auto cached_aged = finterm::CacheManager::instance().try_get_aged(key);
            if (cached_aged) {
                const auto doc = QJsonDocument::fromJson(cached_aged->value.toUtf8());
                if (doc.isObject()) {
                    // Resolve with the cache entry's WRITE time: a hit answers
                    // instantly, and stamping "now" would present a value
                    // hydrated from disk at startup as brand new.
                    const auto a = parse_earnings_analysis(doc.object());
                    // Plan from when Yahoo was ASKED (the payload's as_of),
                    // not when the copy was written — the daemon may have
                    // handed the app an already-aged answer.
                    // EVENT-STAMP: the daemon's upstream fetch instant.
                    note_earnings(symbol, a,
                                  a.as_of > 0 ? QDateTime::fromSecsSinceEpoch(a.as_of) : cached_aged->written_at);
                    resolve(QVariant::fromValue(a), cached_aged->written_at);
                    return;
                }
            }
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        if (fresh)
            payload["fresh"] = true;
        // 25 s, not the 10 s default: the daemon fans this out into six
        // upstream Yahoo calls (info, three estimate frames, earnings dates,
        // 3 y of daily bars) and any one of them can stall.
        run_daemon(
            "earnings_analysis", payload,
            [this, symbol, key, resolve, reject](bool ok, QJsonObject result, QString err) {
                if (!ok) {
                    reject(err);
                    replan_earnings(symbol); // a failed refresh is retried on the schedule
                    return;
                }
                if (result.contains("error")) {
                    reject(result.value("error").toString());
                    replan_earnings(symbol);
                    return;
                }
                finterm::CacheManager::instance().put(
                    key, QVariant(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))),
                    kEarningsAnalysisTtlSec, "equity");
                const auto a = parse_earnings_analysis(result);
                // EVENT-STAMP: the daemon's upstream fetch instant.
                const QDateTime upstream = a.as_of > 0 ? QDateTime::fromSecsSinceEpoch(a.as_of)
                                                       : QDateTime::currentDateTimeUtc();
                note_earnings(symbol, a, upstream);
                resolve(QVariant::fromValue(a));
            },
            25'000);
    };
    query::QueryStore::instance().subscribe(owner, key, kEarningsAnalysisTtlSec, kEarningsAnalysisTtlSec * 2,
                                            std::move(cb), std::move(fetcher));
    // A copy already in memory is delivered without a fetch, so nothing would
    // look at whether it is past its moment (a print since it was fetched).
    // Plan from it — from the store's copy when the watch was pruned.
    if (earnings_watch_.contains(symbol) && earnings_watch_[symbol].fetched_at.isValid()) {
        replan_earnings(symbol);
    } else {
        const QVariant v = query::QueryStore::instance().peek(key);
        if (v.isValid() && !v.isNull()) {
            const auto a = v.value<EarningsAnalysis>();
            if (a.as_of > 0)
                // EVENT-STAMP: the daemon's upstream fetch instant.
                note_earnings(symbol, a, QDateTime::fromSecsSinceEpoch(a.as_of));
        }
    }
}

// ── Earnings refresh schedule ───────────────────────────────────────────────
// One timer per symbol, set from EarningsRefresh's plan each time new data
// arrives. It does nothing nobody would see: when it fires with no live
// subscriber it stops, and the next subscribe re-plans (and refreshes at once
// if a planned moment passed meanwhile).

void EquityResearchService::note_earnings(const QString& symbol, const EarningsAnalysis& a,
                                          const QDateTime& fetched_at) {
    auto& w = earnings_watch_[symbol];
    w.analysis = a;
    w.fetched_at = fetched_at.isValid() ? fetched_at : QDateTime::currentDateTimeUtc();
    replan_earnings(symbol);
}

void EquityResearchService::replan_earnings(const QString& symbol) {
    auto it = earnings_watch_.find(symbol);
    if (it == earnings_watch_.end())
        return;
    auto& w = it.value();
    if (!w.fetched_at.isValid())
        return; // created by a links subscribe; nothing to plan from until the analysis lands
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto plan = plan_earnings_refresh(w.analysis, w.fetched_at, now, w.release_seen);
    w.next_at = plan.at;
    w.next_why = plan.why;
    if (!w.timer) {
        w.timer = new QTimer(this);
        w.timer->setSingleShot(true);
        connect(w.timer, &QTimer::timeout, this, [this, symbol]() { on_earnings_due(symbol); });
    }
    // Loop guard: a refresh that came back with the same old data must not
    // fire again at once. QTimer takes int ms; a day is far inside that.
    qint64 ms = std::max<qint64>(0, now.msecsTo(plan.at));
    if (w.last_forced.isValid())
        ms = std::max<qint64>(ms, now.msecsTo(w.last_forced.addSecs(60)));
    w.timer->start(static_cast<int>(std::min<qint64>(ms, 24LL * 3600 * 1000)));
    emit earnings_schedule_changed(symbol);
}

void EquityResearchService::on_earnings_due(const QString& symbol) {
    auto it = earnings_watch_.find(symbol);
    if (it == earnings_watch_.end())
        return;
    auto& store = query::QueryStore::instance();
    if (!store.has_live_subscribers(earnings_analysis_key(symbol))) {
        // Nobody is looking: drop the watch (its analysis copy and timer).
        // The next subscribe plans again from the store's copy and refreshes
        // at once if a planned moment passed meanwhile.
        if (it->timer)
            it->timer->deleteLater();
        earnings_watch_.erase(it);
        return;
    }
    auto& w = it.value();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto plan = plan_earnings_refresh(w.analysis, w.fetched_at, now, w.release_seen);
    if (plan.at > now.addSecs(5)) { // the day moved on under a long timer — re-plan
        replan_earnings(symbol);
        return;
    }
    if (plan.mode == EarningsRefreshMode::Watch && !w.sec) {
        // Nothing on EDGAR to watch (a 6-K filer): the full refresh is the
        // only signal, so take it at most every 15 minutes, not each minute.
        constexpr int kNoSecWatchSec = 15 * 60;
        if (w.last_forced.isValid() && w.last_forced.secsTo(now) < kNoSecWatchSec) {
            w.timer->start(static_cast<int>(now.msecsTo(w.last_forced.addSecs(kNoSecWatchSec))));
            return;
        }
        refresh_earnings(symbol);
        return;
    }
    if (plan.mode == EarningsRefreshMode::Refresh) {
        refresh_earnings(symbol);
        return;
    }
    // WATCH: one SEC request instead of the full refresh.
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["report_date"] = plan.print.toString(Qt::ISODate);
    const QDate print = plan.print;
    run_daemon(
        "earnings_release_filed", payload,
        [this, symbol, print](bool ok, QJsonObject r, QString) {
            auto jt = earnings_watch_.find(symbol);
            if (jt == earnings_watch_.end())
                return;
            if (ok && !r.contains("error")) {
                if (r.value("filed").toBool()) {
                    LOG_INFO("Earnings", QString("%1: %2 release is on EDGAR — refreshing").arg(symbol, print.toString(Qt::ISODate)));
                    jt->release_seen = print;
                    refresh_earnings(symbol);
                    return;
                }
                if (!r.value("sec").toBool(true))
                    jt->sec = false; // nothing to watch on EDGAR — the plan refreshes instead
            }
            replan_earnings(symbol);
        },
        15'000);
}

void EquityResearchService::refresh_earnings(const QString& symbol) {
    if (symbol.isEmpty())
        return;
    auto& w = earnings_watch_[symbol];
    w.last_forced = QDateTime::currentDateTimeUtc();
    auto& store = query::QueryStore::instance();
    // The flag is consumed by the fetch revalidate() starts; when it starts
    // none (nothing subscribed, or a fetch already in flight) the flag must
    // not linger for some later, unrelated fetch to spend.
    const auto force = [&](const QString& key) {
        force_fresh_.insert(key);
        if (store.revalidate(key, /*force=*/true))
            return true;
        force_fresh_.remove(key);
        return false;
    };
    const QString akey = earnings_analysis_key(symbol);
    const bool started = force(akey);
    if (!w.links_key.isEmpty() && !force(w.links_key) && !store.has_live_subscribers(w.links_key))
        w.links_key.clear(); // a print the tab has left
    if (!started && store.has_live_subscribers(akey)) {
        // A fetch is already in flight — and may be the stale one this
        // refresh exists to replace. Ask again once it has landed.
        QTimer::singleShot(3000, this, [this, symbol]() {
            auto jt = earnings_watch_.find(symbol);
            if (jt != earnings_watch_.end() && jt->last_forced.secsTo(QDateTime::currentDateTimeUtc()) >= 3)
                refresh_earnings(symbol);
        });
    }
}

QPair<QDateTime, QString> EquityResearchService::earnings_next_refresh(const QString& symbol) const {
    const auto it = earnings_watch_.constFind(symbol);
    if (it == earnings_watch_.cend())
        return {};
    return {it->next_at, it->next_why};
}

namespace {

/// Only a web address may become a link — the URL was lifted from a filing's
/// text, and a `javascript:` or `file:` target must never reach a QLabel.
QString web_url_or_empty(const QString& raw) {
    const QUrl u(raw.trimmed(), QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || u.host().isEmpty() || (scheme != "http" && scheme != "https"))
        return {};
    return u.toString(QUrl::FullyEncoded);
}

} // namespace

static EarningsLinks parse_earnings_links(const QJsonObject& r) {
    EarningsLinks l;
    l.report_date = QDate::fromString(r.value("report_date").toString(), Qt::ISODate);
    const auto call = r.value("call").toObject();
    l.call_url = web_url_or_empty(call.value("url").toString());
    l.call_page = web_url_or_empty(call.value("page").toString());
    const auto audio = r.value("audio").toObject();
    l.audio_url = web_url_or_empty(audio.value("url").toString());
    l.audio_kind = audio.value("kind").toString();
    l.ir_url = web_url_or_empty(r.value("ir_url").toString());
    const auto rel = r.value("release").toObject();
    l.release_url = web_url_or_empty(rel.value("url").toString());
    l.release_filed = QDate::fromString(rel.value("filed").toString(), Qt::ISODate);
    l.release_due = QDate::fromString(r.value("release_due").toString(), Qt::ISODate);
    l.note = r.value("note").toString();
    return l;
}

void EquityResearchService::subscribe_earnings_links(QObject* owner, const QString& symbol, const QDate& report_date,
                                                     query::QueryStore::Callback cb) {
    if (symbol.isEmpty() || !report_date.isValid())
        return;
    // The date is in the key: when the print the tab is about changes (the
    // numbers came out, Yahoo moved on to the next quarter), it is a new
    // question, not a stale answer to refresh.
    const QString key = earnings_links_key(symbol, report_date);
    earnings_watch_[symbol].links_key = key; // refreshed with the analysis
    auto fetcher = [this, symbol, key, report_date](query::QueryStore::Resolver resolve,
                                                    query::QueryStore::Rejecter reject) {
        const bool fresh = force_fresh_.remove(key);
        if (!fresh) {
            // Disk copy first, like the analysis, so a launch shows the links
            // without waiting on SEC and the IR site. The schedule refreshes
            // it past this copy the moment the release lands.
            const auto cached_aged = finterm::CacheManager::instance().try_get_aged(key);
            if (cached_aged) {
                const auto doc = QJsonDocument::fromJson(cached_aged->value.toUtf8());
                if (doc.isObject()) {
                    resolve(QVariant::fromValue(parse_earnings_links(doc.object())), cached_aged->written_at);
                    return;
                }
            }
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        payload["report_date"] = report_date.toString(Qt::ISODate);
        if (fresh)
            payload["fresh"] = true;
        // EDGAR (submissions, a filing index, up to three documents per
        // release, at most three releases) then the IR site (its page, the
        // events feed, up to two events pages), each request capped at 10 s
        // and the whole cut off at 40 s in scripts/earnings_links.py.
        // Typically 2-4 s in all.
        run_daemon(
            "earnings_links", payload,
            [key, resolve, reject](bool ok, QJsonObject r, QString err) {
                if (!ok) {
                    reject(err);
                    return;
                }
                if (r.contains("error")) {
                    reject(r.value("error").toString());
                    return;
                }
                // An IR site that timed out: keep the disk copy only briefly,
                // so the next launch asks again rather than trusting it.
                finterm::CacheManager::instance().put(
                    key, QVariant(QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact))),
                    r.contains("ir_error") ? 300 : kEarningsLinksTtlSec, "equity");
                resolve(QVariant::fromValue(parse_earnings_links(r)));
            },
            60'000);
    };
    query::QueryStore::instance().subscribe(owner, key, kEarningsLinksTtlSec, kEarningsLinksTtlSec * 2,
                                            std::move(cb), std::move(fetcher));
}

} // namespace finterm::services::equity
