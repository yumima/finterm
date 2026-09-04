// src/services/equity/EquityResearchService.cpp
#include "services/equity/EquityResearchService.h"

#include "core/logging/Logger.h"
#include "services/equity/TechnicalRating.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "services/util/DiskCache.h"
#include "storage/cache/CacheManager.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fincept::services::equity {

namespace {

/// True only if all four OHLC fields are present and finite.
///
/// Deliberately NOT a positivity test. Zero and negative prints are real
/// market data — ^IRX closed at 0.000 through much of 2020-2021 and CL=F
/// settled at -37.63 on 2020-04-20, both on this app's watchlists — so a
/// `> 0` rule would delete history rather than repair it.
///
/// What this rejects is the all-NaN row yfinance emits for a session it has
/// no prices for yet, typically today's bar. The daemon drops those at the
/// source (_has_prices in yfinance_data.py); this is the C++ boundary that
/// keeps payloads cached before that fix — and anything else that ever sends
/// a null — out of the app.
///
/// Why a null is dangerous rather than merely absent: the daemon's
/// _sanitize_for_json must turn NaN into JSON null, because bare NaN is not
/// valid JSON and QJsonDocument::fromJson rejects the whole document. But
/// QJsonValue::toDouble() then reads that null back as 0.0 without
/// complaint, so "no price" becomes "priced at zero" silently.
bool row_has_prices(const QJsonObject& o) {
    static const char* kFields[] = {"open", "high", "low", "close"};
    for (const char* f : kFields) {
        const double v = o.value(QLatin1String(f))
                             .toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(v))
            return false;
    }
    return true;
}

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

QJsonArray priced_rows(const QJsonArray& arr, const QString& what) {
    const bool all_priced = std::all_of(arr.begin(), arr.end(), [](const QJsonValue& v) {
        return row_has_prices(v.toObject());
    });
    if (all_priced)
        return arr;
    QJsonArray out;
    for (const auto& v : arr) {
        if (row_has_prices(v.toObject()))
            out.append(v);
    }
    LOG_WARN("EquityResearch",
             QString("%1: dropped %2 price-less bar(s) of %3 — vendor sent null/NaN OHLC")
                 .arg(what).arg(arr.size() - out.size()).arg(arr.size()));
    return out;
}

// A daemon reply carrying an error — or no rows at all — is not data.
//
// The daemon deliberately refuses to cache those (see _is_cacheable_result
// there); caching them on this side undoes that decision, and the companion
// disk write makes one transient failure outlive a restart. The observed
// result was an empty chart pinned for the full TTL and rehydrated on the
// next launch, from a single rate-limit blip.
//
// ok == true is not sufficient on its own: get_historical_period returns []
// for an empty history and {"error": ...} for an exception, and BOTH arrive
// with ok == true.
bool series_is_cacheable(const QJsonObject& result, const QJsonArray& rows) {
    return !result.contains("error") && !rows.isEmpty();
}

/// The user-facing message for a history fetch that came back empty/failed.
QString history_error_text(const QJsonObject& result) {
    return QString("Historical fetch returned no data: %1")
        .arg(result.value("error").toString(QStringLiteral("empty history")));
}


// Persistent on-disk cache so the equity research panels can paint the
// most-recently-viewed symbol immediately on next launch. One file per
// symbol holds every category (quote, info, candles by period, financials,
// technicals by period, peers, news) — keeps the directory bounded by the
// universe the user has actually browsed.
fincept::services::util::DiskCache& disk_cache() {
    static fincept::services::util::DiskCache c(QStringLiteral("equity_research"));
    return c;
}

// Sanitize a symbol into an alnum-only filename stem. Tickers can include
// dots (BRK.B) or hyphens (BRK-B) which would break case-insensitive FAT
// lookups on cross-platform mounts. ".json" is appended by the caller.
QString symbol_filename(const QString& symbol) {
    QString s;
    s.reserve(symbol.size());
    for (const QChar c : symbol)
        if (c.isLetterOrNumber())
            s.append(c.toUpper());
    return s + QStringLiteral(".json");
}

// Load the per-symbol blob (or empty obj) and update one category key,
// then save back. We do this rather than keep a process-wide in-memory
// mirror because each fetch path can complete independently and we want
// the on-disk file to always reflect the latest of every category.
void update_symbol_cache(const QString& symbol, const QString& key,
                          const QJsonValue& value) {
    if (symbol.isEmpty()) return;
    const QString fname = symbol_filename(symbol);
    QJsonObject root = disk_cache().load(fname).object();
    root.insert(key, value);
    // A versioned sub-key leaves its predecessor behind: nothing reads the
    // old one once the hydrator moves to "<key>_vN", but every save would
    // keep re-serialising a payload that can never be used again. Drop it
    // when writing the successor. ("info" carried dividend_yield as Yahoo's
    // percentage; that is the whole reason the key moved.)
    if (key.endsWith(QLatin1String("_v2")))
        root.remove(key.chopped(3));
    disk_cache().save(fname, QJsonDocument(root));
}

// The ONE implementation of "cache a fetched candle series": guard, memory
// put, per-symbol disk write and the warn-log together. Returns whether the
// series was cacheable — callers that must fan an error out on empty/failed
// history check it; callers that continue with the (possibly empty) parse
// ignore it. This used to be five hand copies that had already diverged in
// logging and disk sub-key, and the sixth history path would have started
// unguarded.
bool cache_history_series(const QJsonObject& result, const QJsonArray& arr, const QString& symbol,
                          const QString& candles_key, const QString& disk_sub, int ttl_sec) {
    if (!series_is_cacheable(result, arr)) {
        LOG_WARN("EquityResearch",
                 QString("Not caching empty/failed history for %1 (%2) — a transient "
                         "failure must not outlive the request").arg(symbol, disk_sub));
        return false;
    }
    fincept::CacheManager::instance().put(
        candles_key,
        QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
        ttl_sec, "equity");
    const QString fname = symbol_filename(symbol);
    QJsonObject root = disk_cache().load(fname).object();
    root.insert("symbol", symbol);
    QJsonObject by_period = root.value("candles").toObject();
    by_period.insert(disk_sub, arr);
    root.insert("candles", by_period);
    disk_cache().save(fname, QJsonDocument(root));
    return true;
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

    // Trim the cache dir before hydrating: an active user can browse
    // hundreds of distinct tickers over months, and each writes its own
    // file. Cap at 200 most-recently-touched symbols so ctor hydration
    // stays bounded and the dir doesn't grow without limit.
    disk_cache().trim_to(200);

    // Hydrate the in-memory CacheManager from every cached per-symbol file
    // so subsequent load_symbol() / fetch_*() calls hit warm cache and emit
    // immediately. The data_loaded-style signals fired by the per-category
    // emits below go to no listeners (UI wires up later) — load_symbol()
    // will re-emit when a panel subscribes, hitting CacheManager again.
    const QStringList files = disk_cache().files();
    for (const QString& fname : files) {
        const QJsonDocument doc = disk_cache().load(fname);
        if (!doc.isObject()) continue;
        const QJsonObject root = doc.object();
        const QString symbol = root.value("symbol").toString();
        if (symbol.isEmpty()) continue;
        const QFileInfo fi(disk_cache().path(fname));
        const int age_sec = fi.exists()
            ? std::max(1, int(fi.lastModified().secsTo(QDateTime::currentDateTime())))
            : 1;

        auto repopulate = [&](const QString& sub, const QString& cache_prefix, int ttl_sec) {
            if (!root.contains(sub)) return;
            // Refuse to repopulate CacheManager entries that would already
            // be expired against their normal TTL. The disk copy survives
            // for the parser hydration path, but we don't want a stale
            // quote masquerading as fresh.
            if (age_sec >= ttl_sec) return;
            const QJsonValue v = root.value(sub);
            const QJsonDocument doc_for_blob = v.isArray()
                ? QJsonDocument(v.toArray())
                : QJsonDocument(v.toObject());
            const QString blob = QString::fromUtf8(
                doc_for_blob.toJson(QJsonDocument::Compact));
            // Full TTL + explicit age: the entry expires on the original
            // schedule and try_get_aged reports the original write time
            // instead of claiming the blob was fetched at app launch.
            fincept::CacheManager::instance().put(
                cache_prefix + symbol, QVariant(blob), ttl_sec, "equity", age_sec);
        };
        // quote / info / financials / news live in the root, peers as array
        if (root.contains("quote"))
            repopulate("quote", "equity:quote:", kQuoteTtlSec);
        // "info_v2": the pre-v2 disk blobs carry dividend_yield as Yahoo's
        // PERCENTAGE. Rehydrating those into the v2 key would put a 100x
        // value back in front of the user, which is exactly what the key
        // bump exists to prevent — so the disk sub-key moves with it.
        if (root.contains("info_v2"))
            repopulate("info_v2", "equity:info:v2:", kInfoTtlSec);
        if (root.contains("financials"))
            repopulate("financials", "equity:financials:", kFinancialsTtlSec);
        if (root.contains("news"))
            repopulate("news", "equity:news:", kNewsTtlSec);
        // peer cache key includes the basket symbols — re-key by what we
        // stored under "peers_key" so the next fetch_peers() with the same
        // basket hits warm cache. If the user requests a different basket
        // we just take the network miss.
        if (root.contains("peers") && root.contains("peers_key")) {
            const QJsonValue v = root.value("peers");
            if (v.isArray() && age_sec < kPeersTtlSec) {
                const QString blob = QString::fromUtf8(
                    QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
                fincept::CacheManager::instance().put(
                    root.value("peers_key").toString(),
                    QVariant(blob), kPeersTtlSec, "equity", age_sec);
            }
        }
        // candles + technicals are nested per-period objects
        const auto repopulate_periodic =
            [&](const QString& sub, const QString& cache_prefix, int ttl_sec) {
                const QJsonObject by_period = root.value(sub).toObject();
                for (auto it = by_period.constBegin(); it != by_period.constEnd(); ++it) {
                    if (age_sec >= ttl_sec) continue;
                    const QJsonValue v = it.value();
                    if (!v.isArray()) continue;
                    const QString blob = QString::fromUtf8(
                        QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
                    fincept::CacheManager::instance().put(
                        cache_prefix + symbol + ":" + it.key(),
                        QVariant(blob), ttl_sec, "equity", age_sec);
                }
            };
        repopulate_periodic("candles", "equity:candles:", kHistoricalTtlSec);
        repopulate_periodic("technicals",
                            "equity:technicals:" + QString(kTechnicalsSchemaTag) + ":",
                            kTechnicalsTtlSec);
    }
    if (!files.isEmpty())
        LOG_INFO("EquityResearch",
                 QString("Hydrated %1 symbol caches from disk").arg(files.size()));
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
                                       std::function<void(bool, QJsonObject, QString)> cb,
                                       int timeout_ms) {
    QPointer<EquityResearchService> self = this;
    python::PythonWorker::instance().submit(
        action, payload,
        [self, cb = std::move(cb)](bool ok, QJsonObject result, QString err) {
            if (!self) return;
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
    const auto arr = result.value("results").toArray();
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
    run_daemon("search", payload, [this](bool ok, QJsonObject result, QString err) {
        if (!ok) {
            LOG_WARN("EquityResearch", "Symbol search failed: " + err);
            return;
        }
        emit search_results_loaded(parse_search_results(result));
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

void EquityResearchService::subscribe_quote(QObject* owner, const QString& symbol,
                                            query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:quote:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve,
                                   query::QueryStore::Rejecter reject) {
        // Cache check inside the fetcher so we participate in QueryStore's
        // in-flight dedup. If multiple subscribers attach concurrently, only
        // one daemon spawn happens; everyone else waits on the same fetch.
        const auto qcv_aged = fincept::CacheManager::instance().try_get_aged("equity:quote:" + symbol);
        const QVariant qcv = qcv_aged ? QVariant(qcv_aged->value) : QVariant();
        if (!qcv.isNull()) {
            QuoteData parsed = parse_quote(QJsonDocument::fromJson(qcv.toString().toUtf8()).object());
            // Mirror to legacy broadcast for non-migrated tabs.
            emit quote_loaded(parsed);
            // Resolve with the cache entry\'s WRITE time: a hit answers
            // instantly, and stamping "now" would present a value hydrated
            // from disk at startup as brand new.
            resolve(QVariant::fromValue(parsed), qcv_aged->written_at);
            return;
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        run_daemon("quote", payload,
            [this, symbol, resolve, reject](bool ok, QJsonObject obj, QString err) {
                if (!ok || obj.contains("error")) {
                    const QString msg = !ok ? err : obj["error"].toString();
                    emit error_occurred(symbol, "Quote", msg);
                    QuoteData failed; failed.symbol = symbol; failed.valid = false;
                    emit quote_loaded(failed);
                    reject(msg);
                    return;
                }
                fincept::CacheManager::instance().put(
                    "equity:quote:" + symbol,
                    QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                    kQuoteTtlSec, "equity");
                update_symbol_cache(symbol, "quote", obj);
                update_symbol_cache(symbol, "symbol", symbol);
                QuoteData parsed = parse_quote(obj);
                emit quote_loaded(parsed);
                resolve(QVariant::fromValue(parsed));
            });
    };
    // SWR window: 5 × TTL. A quote 5 minutes past its 60s TTL is still
    // useful for "show last-known + refresh indicator" UX. Beyond that we
    // discard — a 30-minute-old quote during market hours is misleading.
    query::QueryStore::instance().subscribe(owner, key, kQuoteTtlSec, kQuoteTtlSec * 5,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::subscribe_info(QObject* owner, const QString& symbol,
                                           query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:info:v2:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve,
                                   query::QueryStore::Rejecter reject) {
        const auto icv_aged = fincept::CacheManager::instance().try_get_aged("equity:info:v2:" + symbol);
        const QVariant icv = icv_aged ? QVariant(icv_aged->value) : QVariant();
        if (!icv.isNull()) {
            StockInfo parsed = parse_info(QJsonDocument::fromJson(icv.toString().toUtf8()).object());
            emit info_loaded(parsed);
            // Resolve with the cache entry\'s WRITE time: a hit answers
            // instantly, and stamping "now" would present a value hydrated
            // from disk at startup as brand new.
            resolve(QVariant::fromValue(parsed), icv_aged->written_at);
            return;
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        run_daemon("info", payload,
            [this, symbol, resolve, reject](bool ok, QJsonObject obj, QString err) {
                if (!ok || obj.contains("error")) {
                    const QString msg = !ok ? err : obj["error"].toString();
                    emit error_occurred(symbol, "Info", msg);
                    StockInfo failed; failed.symbol = symbol; failed.valid = false;
                    emit info_loaded(failed);
                    reject(msg);
                    return;
                }
                fincept::CacheManager::instance().put(
                    "equity:info:v2:" + symbol,
                    QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                    kInfoTtlSec, "equity");
                update_symbol_cache(symbol, "info_v2", obj);
                update_symbol_cache(symbol, "symbol", symbol);
                StockInfo parsed = parse_info(obj);
                emit info_loaded(parsed);
                resolve(QVariant::fromValue(parsed));
            });
    };
    // Info is the slowest yfinance call (~1-3s) and changes slowly. Generous
    // SWR window — 8 × TTL ≈ 4 hours — keeps repeat views instant for a
    // working session. After that, treat as cold.
    query::QueryStore::instance().subscribe(owner, key, kInfoTtlSec, kInfoTtlSec * 8,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::subscribe_historical(QObject* owner, const QString& symbol,
                                                  const QString& period,
                                                  query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:candles:" + symbol + ":" + period;
    auto fetcher = [this, symbol, period](query::QueryStore::Resolver resolve,
                                           query::QueryStore::Rejecter reject) {
        const QString candles_key = "equity:candles:" + symbol + ":" + period;
        const auto hcv_aged = fincept::CacheManager::instance().try_get_aged(candles_key);
        const QVariant hcv = hcv_aged ? QVariant(hcv_aged->value) : QVariant();
        if (!hcv.isNull()) {
            QVector<Candle> parsed = parse_candles(
                QJsonDocument::fromJson(hcv.toString().toUtf8()).array());
            emit historical_loaded(symbol, period, parsed);
            // Resolve with the cache entry\'s WRITE time: a hit answers
            // instantly, and stamping "now" would present a value hydrated
            // from disk at startup as brand new.
            resolve(QVariant::fromValue(parsed), hcv_aged->written_at);
            return;
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        payload["period"] = period;
        payload["interval"] = "1d";
        run_daemon("historical_period", payload,
            [this, symbol, period, candles_key, resolve, reject](bool ok, QJsonObject result, QString err) {
                if (!ok) {
                    const QString msg = "Failed to fetch historical for " + symbol + ": " + err;
                    emit error_occurred(symbol, "Historical", msg);
                    emit historical_loaded(symbol, period, {});
                    reject(msg);
                    return;
                }
                const auto arr = result.contains("_value")
                                     ? result.value("_value").toArray()
                                     : result.value("history").toArray();
                cache_history_series(result, arr, symbol, candles_key, period, kHistoricalTtlSec);
                QVector<Candle> parsed = parse_candles(arr);
                emit historical_loaded(symbol, period, parsed);
                resolve(QVariant::fromValue(parsed));
            });
    };
    // Daily candles barely change intraday — generous SWR window so period
    // switches feel instant for the rest of the session. Beyond 24h the
    // most-recent candle could be a day stale during market hours, which is
    // genuinely misleading; cap there.
    query::QueryStore::instance().subscribe(owner, key, kHistoricalTtlSec, /*stale_max*/86400,
                                            std::move(cb), std::move(fetcher));
}

QString EquityResearchService::technicals_history_period(const QString& requested,
                                                          const QString& interval) {
    // Weekly bars need far more calendar time for the same warm-up: a
    // 200-period average of weekly closes is four years of history by itself.
    if (interval == QLatin1String("1wk"))
        return QStringLiteral("10y");

    // Anything shorter than two years cannot warm up the daily indicators the
    // rating requires. Longer selections are honoured as-is — they cost nothing
    // extra beyond the fetch, since every reading is taken off the last bar.
    static const QSet<QString> kBelowFloor = {
        QStringLiteral("1d"),  QStringLiteral("5d"),  QStringLiteral("1mo"),
        QStringLiteral("3mo"), QStringLiteral("6mo"), QStringLiteral("ytd"),
        QStringLiteral("1y"),
    };
    return kBelowFloor.contains(requested) ? QStringLiteral("2y") : requested;
}

QString EquityResearchService::technicals_key(const QString& symbol, const QString& requested,
                                              const QString& interval) {
    return "equity:technicals:" + QLatin1String(kTechnicalsSchemaTag) + ":" + symbol + ":" +
           interval + ":" + technicals_history_period(requested, interval);
}

void EquityResearchService::subscribe_technicals(QObject* owner, const QString& symbol,
                                                  const QString& requested_period,
                                                  const QString& interval,
                                                  query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
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
        const auto tcv_aged = fincept::CacheManager::instance().try_get_aged(key);
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

        // Cache miss → use the existing fetch_technicals two-stage chain.
        // We hook one-shot listeners on technicals_loaded / error_occurred
        // to bridge the broadcast back into the QueryStore promise pair.
        // A `watcher` QObject owns both connections; deleting it on first
        // matching event auto-disconnects both (no leak if we exit via
        // either path).
        auto* watcher = new QObject(this);
        QPointer<QObject> watcher_guard(watcher);
        connect(this, &EquityResearchService::technicals_loaded, watcher,
            [symbol, period, interval, resolve, watcher_guard](TechnicalsData d) {
                // Interval is part of the identity: daily and weekly requests
                // share the same floored period ("10y") and would otherwise
                // resolve each other and poison the QueryStore entry.
                if (d.symbol != symbol || d.period != period || d.interval != interval) return;
                if (watcher_guard) watcher_guard->deleteLater();
                resolve(QVariant::fromValue(d));
            });
        connect(this, &EquityResearchService::error_occurred, watcher,
            [symbol, reject, watcher_guard](const QString& sym, const QString& ctx, const QString& msg) {
                if (sym != symbol || ctx != "Technicals") return;
                if (watcher_guard) watcher_guard->deleteLater();
                reject(msg);
            });
        fetch_technicals(symbol, period, interval);
    };
    // SWR window — technicals are derived from daily candles and barely
    // change intraday. Same 24h cap as historical: beyond that the most-
    // recent indicator values could be a day stale during market hours.
    query::QueryStore::instance().subscribe(owner, key, kTechnicalsTtlSec, /*stale_max*/86400,
                                            std::move(cb), std::move(fetcher));
}

// All three of these (financials, news, peers) bridge to existing broadcast
// emitters via a one-shot watcher rather than re-implementing the cache /
// fetch chain. Same template as subscribe_technicals: cache check first,
// then attach watcher, then call legacy fetch_* which emits the broadcast.
// The watcher's QObject scope auto-disconnects on first matching event.

void EquityResearchService::subscribe_financials(QObject* owner, const QString& symbol,
                                                  query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:financials:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve,
                                   query::QueryStore::Rejecter reject) {
        const auto fcv_aged = fincept::CacheManager::instance().try_get_aged("equity:financials:" + symbol);
        const QVariant fcv = fcv_aged ? QVariant(fcv_aged->value) : QVariant();
        if (!fcv.isNull()) {
            const auto doc = QJsonDocument::fromJson(fcv.toString().toUtf8());
            if (doc.isObject()) {
                FinancialsData parsed = parse_financials(doc.object());
                emit financials_loaded(parsed);
                // Resolve with the cache entry\'s WRITE time: a hit answers
                // instantly, and stamping "now" would present a value hydrated
                // from disk at startup as brand new.
                resolve(QVariant::fromValue(parsed), fcv_aged->written_at);
                return;
            }
        }
        auto* watcher = new QObject(this);
        QPointer<QObject> watcher_guard(watcher);
        connect(this, &EquityResearchService::financials_loaded, watcher,
            [symbol, resolve, watcher_guard](FinancialsData d) {
                if (d.symbol != symbol) return;
                if (watcher_guard) watcher_guard->deleteLater();
                resolve(QVariant::fromValue(d));
            });
        connect(this, &EquityResearchService::error_occurred, watcher,
            [symbol, reject, watcher_guard](const QString& sym, const QString& ctx, const QString& msg) {
                if (sym != symbol || ctx != "Financials") return;
                if (watcher_guard) watcher_guard->deleteLater();
                reject(msg);
            });
        fetch_financials(symbol);
    };
    query::QueryStore::instance().subscribe(owner, key, kFinancialsTtlSec, kFinancialsTtlSec * 4,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::subscribe_news(QObject* owner, const QString& symbol,
                                            query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:news:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve,
                                   query::QueryStore::Rejecter reject) {
        const auto ncv_aged = fincept::CacheManager::instance().try_get_aged("equity:news:" + symbol);
        const QVariant ncv = ncv_aged ? QVariant(ncv_aged->value) : QVariant();
        if (!ncv.isNull()) {
            const auto doc = QJsonDocument::fromJson(ncv.toString().toUtf8());
            if (doc.isArray()) {
                QVector<NewsArticle> parsed = parse_news(doc.array());
                emit news_loaded(symbol, parsed);
                // Resolve with the cache entry\'s WRITE time: a hit answers
                // instantly, and stamping "now" would present a value hydrated
                // from disk at startup as brand new.
                resolve(QVariant::fromValue(parsed), ncv_aged->written_at);
                return;
            }
        }
        auto* watcher = new QObject(this);
        QPointer<QObject> watcher_guard(watcher);
        connect(this, &EquityResearchService::news_loaded, watcher,
            [symbol, resolve, watcher_guard](QString sym, QVector<NewsArticle> articles) {
                if (sym != symbol) return;
                if (watcher_guard) watcher_guard->deleteLater();
                resolve(QVariant::fromValue(articles));
            });
        connect(this, &EquityResearchService::error_occurred, watcher,
            [symbol, reject, watcher_guard](const QString& sym, const QString& ctx, const QString& msg) {
                if (sym != symbol || ctx != "News") return;
                if (watcher_guard) watcher_guard->deleteLater();
                reject(msg);
            });
        fetch_news(symbol);
    };
    query::QueryStore::instance().subscribe(owner, key, kNewsTtlSec, kNewsTtlSec * 5,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::subscribe_peers(QObject* owner, const QString& symbol,
                                             const QStringList& peer_symbols,
                                             query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    // Basket-aware key: same symbol with different peer baskets is a
    // different query and shouldn't share cache or in-flight slot.
    QStringList sorted_peers = peer_symbols;
    std::sort(sorted_peers.begin(), sorted_peers.end());
    const QString basket = sorted_peers.join(",");
    const QString key = "equity:peers:v3:" + symbol + ":" + basket;
    auto fetcher = [this, symbol, peer_symbols](query::QueryStore::Resolver resolve,
                                                 query::QueryStore::Rejecter reject) {
        // No cache short-circuit at this layer — fetch_peers's own cache
        // path keys on the same basket and emits peers_loaded synchronously
        // on hit. We just attach the bridge and call.
        auto* watcher = new QObject(this);
        QPointer<QObject> watcher_guard(watcher);
        connect(this, &EquityResearchService::peers_loaded, watcher,
            [symbol, resolve, watcher_guard](QString sym, QVector<PeerData> peers) {
                if (sym != symbol) return;
                if (watcher_guard) watcher_guard->deleteLater();
                resolve(QVariant::fromValue(peers));
            });
        connect(this, &EquityResearchService::error_occurred, watcher,
            [symbol, reject, watcher_guard](const QString& sym, const QString& ctx, const QString& msg) {
                if (sym != symbol || ctx != "Peers") return;
                if (watcher_guard) watcher_guard->deleteLater();
                reject(msg);
            });
        fetch_peers(symbol, peer_symbols);
    };
    query::QueryStore::instance().subscribe(owner, key, kPeersTtlSec, kPeersTtlSec * 2,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::subscribe_earnings(QObject* owner, const QString& symbol,
                                                query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    const QString key = "equity:earnings:" + symbol;
    auto fetcher = [this, symbol](query::QueryStore::Resolver resolve,
                                   query::QueryStore::Rejecter reject) {
        const auto ecv_aged = fincept::CacheManager::instance().try_get_aged("equity:earnings:" + symbol);
        const QVariant ecv = ecv_aged ? QVariant(ecv_aged->value) : QVariant();
        if (!ecv.isNull()) {
            const auto doc = QJsonDocument::fromJson(ecv.toString().toUtf8());
            if (doc.isArray()) {
                // Resolve with the cache entry\'s WRITE time: a hit answers
                // instantly, and stamping "now" would present a value hydrated
                // from disk at startup as brand new.
                resolve(QVariant::fromValue(parse_earnings(doc.array())), ecv_aged->written_at);
                return;
            }
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        run_daemon("earnings_dates", payload,
            [this, symbol, resolve, reject](bool ok, QJsonObject result, QString err) {
                if (!ok) { reject(err); return; }
                if (result.contains("error")) {
                    // Non-fatal: many tickers have no earnings (ETFs, indices,
                    // certain ADRs). Resolve with empty so the chart just
                    // doesn't draw markers, rather than reporting an error.
                    resolve(QVariant::fromValue(QVector<EarningsEvent>{}));
                    return;
                }
                const QJsonArray arr = result.value("dates").toArray();
                fincept::CacheManager::instance().put(
                    "equity:earnings:" + symbol,
                    QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
                    kEarningsTtlSec, "equity");
                update_symbol_cache(symbol, "earnings", arr);
                resolve(QVariant::fromValue(parse_earnings(arr)));
            });
    };
    // Earnings dates are session-stable. Generous SWR window.
    query::QueryStore::instance().subscribe(owner, key, kEarningsTtlSec, kEarningsTtlSec * 6,
                                            std::move(cb), std::move(fetcher));
}

void EquityResearchService::prefetch_historical(const QString& symbol, const QString& period) {
    if (symbol.isEmpty() || period.isEmpty()) return;
    const QString key = "equity:candles:" + symbol + ":" + period;
    auto fetcher = [this, symbol, period](query::QueryStore::Resolver resolve,
                                           query::QueryStore::Rejecter reject) {
        const QString candles_key = "equity:candles:" + symbol + ":" + period;
        const auto hcv_aged = fincept::CacheManager::instance().try_get_aged(candles_key);
        const QVariant hcv = hcv_aged ? QVariant(hcv_aged->value) : QVariant();
        if (!hcv.isNull()) {
            QVector<Candle> parsed = parse_candles(
                QJsonDocument::fromJson(hcv.toString().toUtf8()).array());
            // No legacy broadcast emission on prefetch — nobody is waiting,
            // and broadcasting would mislead any subscribed tab into
            // thinking the user actively switched to this period.
            resolve(QVariant::fromValue(parsed));
            return;
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        payload["period"] = period;
        payload["interval"] = "1d";
        run_daemon("historical_period", payload,
            [this, symbol, period, candles_key, resolve, reject](bool ok, QJsonObject result, QString err) {
                if (!ok) { reject(err); return; }
                const auto arr = result.contains("_value")
                                     ? result.value("_value").toArray()
                                     : result.value("history").toArray();
                cache_history_series(result, arr, symbol, candles_key, period, kHistoricalTtlSec);
                resolve(QVariant::fromValue(parse_candles(arr)));
            });
    };
    query::QueryStore::instance().prefetch(key, kHistoricalTtlSec, std::move(fetcher));
}

// ── Quote-only fetch (refresh-timer + showEvent path) ─────────────────────────
void EquityResearchService::fetch_quote(const QString& symbol) {
    if (symbol.isEmpty())
        return;
    const auto qcv_aged = fincept::CacheManager::instance().try_get_aged("equity:quote:" + symbol);
    const QVariant qcv = qcv_aged ? QVariant(qcv_aged->value) : QVariant();
    if (!qcv.isNull()) {
        emit quote_loaded(parse_quote(QJsonDocument::fromJson(qcv.toString().toUtf8()).object()));
        return;
    }
    const QString inflight_key = "quote:" + symbol;
    if (!acquire_inflight(inflight_key))
        return; // existing call will fan out via quote_loaded
    QJsonObject payload;
    payload["symbol"] = symbol;
    run_daemon("quote", payload, [this, symbol, inflight_key](bool ok, QJsonObject obj, QString err) {
        release_inflight(inflight_key);
        if (!ok || obj.contains("error")) {
            emit error_occurred(symbol, "Quote", !ok ? err : obj["error"].toString());
            QuoteData failed; failed.symbol = symbol; failed.valid = false;
            emit quote_loaded(failed);
            return;
        }
        fincept::CacheManager::instance().put(
            "equity:quote:" + symbol,
            QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))), kQuoteTtlSec,
            "equity");
        update_symbol_cache(symbol, "quote", obj);
        update_symbol_cache(symbol, "symbol", symbol);
        emit quote_loaded(parse_quote(obj));
    });
}

// ── Load symbol (quote + info + historical in parallel) ───────────────────────
void EquityResearchService::search_symbols_for(
    const QString& query, std::function<void(QString, QVector<SearchResult>)> cb) {
    const QString q = query.trimmed();
    if (q.isEmpty() || !cb)
        return;
    QJsonObject payload;
    payload["query"] = q;
    payload["limit"] = 20;
    run_daemon("search", payload, [q, cb = std::move(cb)](bool ok, QJsonObject result, QString err) {
        // Answered even on failure. The broadcast path can afford to log and
        // drop, because nothing is waiting on it; a caller that asked a
        // question is owed an answer, if only an empty one, or its box sits
        // there looking like the search is still running.
        if (!ok) {
            LOG_WARN("EquityResearch", "Symbol search failed: " + err);
            cb(q, {});
            return;
        }
        cb(q, parse_search_results(result));
    });
}

void EquityResearchService::load_symbol(const QString& symbol, const QString& period) {
    if (symbol.isEmpty())
        return;

    // Each block below must fall through independently — info and historical
    // must run regardless of the quote outcome. Do NOT add early `return`s
    // inside the blocks; that would abort the remaining fetches and leave the
    // overview overlay hung waiting on signals that never arrive.

    // ── Quote ────────────────────────────────────────────────────────────────
    // Delegated to fetch_quote() so the 20s refresh path can share the same
    // dedup + cache + emit logic without re-firing info and historical.
    fetch_quote(symbol);

    // ── Info ─────────────────────────────────────────────────────────────────
    {
        const auto icv_aged = fincept::CacheManager::instance().try_get_aged("equity:info:v2:" + symbol);
        const QVariant icv = icv_aged ? QVariant(icv_aged->value) : QVariant();
        if (!icv.isNull()) {
            emit info_loaded(parse_info(QJsonDocument::fromJson(icv.toString().toUtf8()).object()));
        } else {
            const QString inflight_key = "info:" + symbol;
            if (acquire_inflight(inflight_key)) {
                QJsonObject payload;
                payload["symbol"] = symbol;
                run_daemon("info", payload, [this, symbol, inflight_key](bool ok, QJsonObject obj, QString err) {
                    release_inflight(inflight_key);
                    if (!ok || obj.contains("error")) {
                        emit error_occurred(symbol, "Info", !ok ? err : obj["error"].toString());
                        StockInfo failed; failed.symbol = symbol; failed.valid = false;
                        emit info_loaded(failed);
                        return;
                    }
                    fincept::CacheManager::instance().put(
                        "equity:info:v2:" + symbol,
                        QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))), kInfoTtlSec,
                        "equity");
                    update_symbol_cache(symbol, "info_v2", obj);
                    update_symbol_cache(symbol, "symbol", symbol);
                    emit info_loaded(parse_info(obj));
                });
            }
        }
    }

    // ── Historical ───────────────────────────────────────────────────────────
    // Cache key MUST include period — without it, switching the period
    // button (1M/3M/6M/1Y/5Y) hits the cached entry from the previous
    // period and emits stale data, leaving the chart visually unchanged.
    {
        const QString candles_key = "equity:candles:" + symbol + ":" + period;
        const auto hcv_aged = fincept::CacheManager::instance().try_get_aged(candles_key);
        const QVariant hcv = hcv_aged ? QVariant(hcv_aged->value) : QVariant();
        if (!hcv.isNull()) {
            emit historical_loaded(symbol, period, parse_candles(QJsonDocument::fromJson(hcv.toString().toUtf8()).array()));
        } else {
            const QString inflight_key = "historical:" + symbol + ":" + period;
            if (acquire_inflight(inflight_key)) {
                QJsonObject payload;
                payload["symbol"] = symbol;
                payload["period"] = period;
                payload["interval"] = "1d";
                run_daemon("historical_period", payload,
                           [this, symbol, period, candles_key, inflight_key](bool ok, QJsonObject result, QString err) {
                               release_inflight(inflight_key);
                               if (!ok) {
                                   emit error_occurred(symbol, "Historical", "Failed to fetch historical for " + symbol + ": " + err);
                                   // Empty candles signals failure; overlay gate still closes via on_historical_loaded.
                                   emit historical_loaded(symbol, period, {});
                                   return;
                               }
                               // Daemon returns a flat list; PythonWorker wraps under "_value".
                               const auto arr = result.contains("_value")
                                                    ? result.value("_value").toArray()
                                                    : result.value("history").toArray();
                               cache_history_series(result, arr, symbol, candles_key, period,
                                                    kHistoricalTtlSec);
                               emit historical_loaded(symbol, period, parse_candles(arr));
                           });
            }
        }
    }
}

// ── Financials ────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_financials(const QString& symbol) {
    // Tier 0: SWR cache — emit cached immediately and skip the network if
    // we have a fresh entry. Quarterly data; 1h TTL is plenty.
    {
        const auto fcv_aged = fincept::CacheManager::instance().try_get_aged("equity:financials:" + symbol);
        const QVariant fcv = fcv_aged ? QVariant(fcv_aged->value) : QVariant();
        if (!fcv.isNull()) {
            const auto cached = QJsonDocument::fromJson(fcv.toString().toUtf8()).object();
            emit financials_loaded(parse_financials(cached));
            return;
        }
    }

    const QString inflight_key = "financials:" + symbol;
    if (!acquire_inflight(inflight_key)) return;
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
        fincept::CacheManager::instance().put(
            "equity:financials:" + symbol,
            QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
            kFinancialsTtlSec, "equity");
        update_symbol_cache(symbol, "financials", obj);
        update_symbol_cache(symbol, "symbol", symbol);
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
        const auto tcv_aged = fincept::CacheManager::instance().try_get_aged(tech_key);
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
    if (!acquire_inflight(inflight_key)) return;

    QPointer<EquityResearchService> self = this;

    // ── Stage 2: compute via daemon, given a candles array ───────────────────
    auto run_compute = [self, symbol, period, interval, tech_key, inflight_key](const QJsonArray& raw) {
        if (!self) { return; }  // (key remains held — process is exiting anyway)
        // This path never builds Candles, so parse_candles' guard does not
        // apply: the array goes straight back to Python. Note what does and
        // does not already protect it there — compute_technicals_from_candles
        // coerces all four OHLC columns and then drops on CLOSE only, so a
        // row with a good close but a null high/low survives into every
        // rolling window that reads them (ATR, Bollinger, Stochastic, Aroon)
        // as NaN. This filter is what covers those columns, and it covers
        // both callers below, including the disk-cache hit.
        const QJsonArray candles = priced_rows(raw, "technicals " + symbol);
        QJsonObject payload;
        payload["candles"] = candles;
        python::PythonWorker::instance().submit("compute_technicals", payload,
            [self, symbol, period, interval, tech_key, inflight_key](bool ok, QJsonObject result,
                                                                        QString err) {
                if (!self) return;
                self->release_inflight(inflight_key);
                if (!ok || !result.value("success").toBool(false)) {
                    emit self->error_occurred(
                        symbol, "Technicals",
                        ok ? QString("compute_technicals returned failure: %1").arg(result.value("error").toString())
                           : QString("Indicator computation failed: %1").arg(err));
                    return;
                }
                const QJsonArray data = result.value("data").toArray();
                // Same poisoning class the stage-1 guard stops, one stage
                // below it: a success-with-empty-rows result must not be
                // pinned under the shared tech key nor written to disk.
                if (data.isEmpty()) {
                    emit self->error_occurred(symbol, "Technicals",
                                              QStringLiteral("Indicator computation returned no rows"));
                    return;
                }
                // Cache the computed series so re-opens are <100ms.
                const QString blob = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
                fincept::CacheManager::instance().put(tech_key, QVariant(blob),
                                                      kTechnicalsTtlSec, "equity");
                {
                    const QString fname = symbol_filename(symbol);
                    QJsonObject root = disk_cache().load(fname).object();
                    root.insert("symbol", symbol);
                    QJsonObject by_period = root.value("technicals").toObject();
                    by_period.insert(interval + ":" + period, data);
                    root.insert("technicals", by_period);
                    disk_cache().save(fname, QJsonDocument(root));
                }
                emit self->technicals_loaded(self->parse_technicals(symbol, period, interval, data));
            },
            python::PythonWorker::kComputeActionTimeoutMs);
    };

    // ── Stage 1: pull candles (cache → daemon historical_period) ─────────────
    // Daily candles keep the legacy key so the chart, prefetch and talipp
    // paths (all daily-only) share one entry with the technicals compute —
    // splitting them meant the same 5y daily series was fetched twice and the
    // "<100ms re-open" path went cold. Only non-daily intervals get their own
    // segment.
    const QString candles_key =
        interval == QLatin1String("1d")
            ? "equity:candles:" + symbol + ":" + period
            : "equity:candles:" + symbol + ":" + interval + ":" + period;
    const QString candles_disk_sub =
        interval == QLatin1String("1d") ? period : interval + ":" + period;
    QJsonArray candles_from_cache;
    {
        const auto hcv_aged = fincept::CacheManager::instance().try_get_aged(candles_key);
        const QVariant hcv = hcv_aged ? QVariant(hcv_aged->value) : QVariant();
        if (!hcv.isNull()) {
            const auto doc = QJsonDocument::fromJson(hcv.toString().toUtf8());
            if (doc.isArray()) candles_from_cache = doc.array();
        }
    }
    if (!candles_from_cache.isEmpty()) {
        run_compute(candles_from_cache);
        return;
    }

    QJsonObject hist_payload;
    hist_payload["symbol"] = symbol;
    hist_payload["period"] = period;
    hist_payload["interval"] = interval;
    python::PythonWorker::instance().submit("historical_period", hist_payload,
        [self, symbol, candles_key, candles_disk_sub, run_compute, inflight_key](bool ok, QJsonObject result,
                                                                                  QString /*err*/) {
            if (!self) return;
            if (!ok) {
                // Stage 1 failed — release the dedup key here since stage 2
                // will never run to release it for us.
                self->release_inflight(inflight_key);
                emit self->error_occurred(symbol, "Technicals", "Failed historical fetch");
                return;
            }
            // Daemon returns a flat array; PythonWorker wraps under _value.
            const QJsonArray candles = result.contains("_value")
                                            ? result.value("_value").toArray()
                                            : result.value("history").toArray();
            // Same guard as every other history path: an {"error":…} result
            // or empty series arrives with ok == true, and putting it under
            // the SHARED daily candles key pinned a blank chart, technicals
            // and TALIpp for the full TTL from one rate-limit blip. (The
            // disk sub-key is interval-qualified for weekly so 10y of weekly
            // bars does not land on top of 10y of daily bars.)
            if (!cache_history_series(result, candles, symbol, candles_key, candles_disk_sub,
                                      kHistoricalTtlSec)) {
                self->release_inflight(inflight_key);
                emit self->error_occurred(symbol, "Technicals", history_error_text(result));
                return;
            }
            run_compute(candles);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Peers ─────────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_peers(const QString& symbol, const QStringList& peer_symbols) {
    // Build the cache key from the full symbol set so different peer
    // selections for the same primary symbol don't collide.
    QStringList key_syms;
    key_syms.append(symbol);
    key_syms.append(peer_symbols);
    const QString cache_key = "equity:peers:v3:" + key_syms.join(",");

    // Tier 0: SWR cache — peer ratios are stable over hour-scale.
    {
        const auto pcv_aged = fincept::CacheManager::instance().try_get_aged(cache_key);
        const QVariant pcv = pcv_aged ? QVariant(pcv_aged->value) : QVariant();
        if (!pcv.isNull()) {
            const auto arr = QJsonDocument::fromJson(pcv.toString().toUtf8()).array();
            emit peers_loaded(symbol, parse_peers(arr));
            return;
        }
    }

    // Reuse the cache key as the dedup key — if a fetch for this exact
    // peer set is already pending, drop the second caller.
    if (!acquire_inflight(cache_key)) return;

    QJsonArray syms_arr;
    syms_arr.append(symbol);
    for (const auto& s : peer_symbols) syms_arr.append(s);
    QJsonObject payload;
    payload["symbols"] = syms_arr;

    run_daemon("multiple_ratios", payload, [this, symbol, cache_key](bool ok, QJsonObject result, QString err) {
        release_inflight(cache_key);
        if (!ok) {
            emit error_occurred(symbol, "Peers", "Failed to fetch peer data: " + err);
            return;
        }
        // multiple_ratios returns a list; PythonWorker wraps under "_value".
        const auto arr = result.contains("_value")
                             ? result.value("_value").toArray()
                             : result.value("ratios").toArray();
        fincept::CacheManager::instance().put(
            cache_key,
            QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
            kPeersTtlSec, "equity");
        update_symbol_cache(symbol, "peers", arr);
        update_symbol_cache(symbol, "peers_key", cache_key);
        update_symbol_cache(symbol, "symbol", symbol);
        emit peers_loaded(symbol, parse_peers(arr));
    });
}

// ── News ──────────────────────────────────────────────────────────────────────
void EquityResearchService::fetch_news(const QString& symbol, int count) {
    // Tier 0: SWR cache — emit cached immediately and skip the network.
    {
        const auto ncv_aged = fincept::CacheManager::instance().try_get_aged("equity:news:" + symbol);
        const QVariant ncv = ncv_aged ? QVariant(ncv_aged->value) : QVariant();
        if (!ncv.isNull()) {
            const QJsonArray arr = QJsonDocument::fromJson(ncv.toString().toUtf8()).array();
            emit news_loaded(symbol, parse_news(arr));
            return;
        }
    }

    const QString inflight_key = "news:" + symbol;
    if (!acquire_inflight(inflight_key)) return;
    QJsonObject payload;
    payload["symbol"] = symbol;
    payload["count"] = count;
    run_daemon("news", payload, [this, symbol, inflight_key](bool ok, QJsonObject obj, QString err) {
        release_inflight(inflight_key);
        if (!ok) {
            emit error_occurred(symbol, "News", "Failed to fetch news for " + symbol + ": " + err);
            return;
        }
        const QJsonArray arr = obj.value("articles").toArray();
        fincept::CacheManager::instance().put(
            "equity:news:" + symbol,
            QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
            kNewsTtlSec, "equity");
        update_symbol_cache(symbol, "news", arr);
        update_symbol_cache(symbol, "symbol", symbol);
        emit news_loaded(symbol, parse_news(arr));
    });
}

// ── TALIpp ────────────────────────────────────────────────────────────────────
void EquityResearchService::compute_talipp(const QString& symbol, const QString& indicator, const QVariantMap& params,
                                           const QString& period) {
    // Takes the array rather than the serialized string so both callers get
    // the price-less-bar filter. equity_talipp.py builds its series with
    // float(r.get("open", 0)) — the default only applies to a MISSING key, so
    // a present null reaches float(None) and raises TypeError, failing the
    // whole indicator rather than the one bar.
    auto run_talipp = [this, symbol, indicator, params](const QJsonArray& raw) {
        const QString hist_json = QString::fromUtf8(
            QJsonDocument(priced_rows(raw, "talipp " + symbol)).toJson(QJsonDocument::Compact));
        QJsonObject p_obj;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            p_obj[it.key()] = QJsonValue::fromVariant(it.value());
        QString params_json = QJsonDocument(p_obj).toJson(QJsonDocument::Compact);

        run_python("equity_talipp.py", {indicator, hist_json, params_json},
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

    // Use cached historical if available, else fetch. Cache key includes
    // period so different period selections don't reuse each other's data.
    const QString candles_key = "equity:candles:" + symbol + ":" + period;
    const auto cached_candles_aged = fincept::CacheManager::instance().try_get_aged(candles_key);
    const QVariant cached_candles = cached_candles_aged ? QVariant(cached_candles_aged->value) : QVariant();
    const auto cached_doc = cached_candles.isNull()
                                ? QJsonDocument()
                                : QJsonDocument::fromJson(cached_candles.toString().toUtf8());
    // A cached blob that no longer parses as an array is not a cache hit.
    // .array() would hand back an empty array, which equity_talipp.py does
    // reject ("OHLCV data must be a non-empty JSON array") — so the user saw
    // an error rather than a blank overlay, but an error for a recoverable
    // condition. Falling through to the fetch is the better answer. Same
    // test fetch_technicals already uses on its own cache read.
    if (cached_doc.isArray()) {
        run_talipp(cached_doc.array());
    } else {
        // Dedup: with errors (correctly) never cached, every parameter tweak
        // during an outage would otherwise fire its own full RPC for the
        // same candles. The first request's outcome reaches the tab via the
        // talipp_result / error_occurred broadcasts either way.
        const QString inflight_key = "talipp-hist:" + symbol + ":" + period;
        if (!acquire_inflight(inflight_key)) return;
        QJsonObject payload;
        payload["symbol"] = symbol;
        payload["period"] = period;
        payload["interval"] = "1d";
        run_daemon("historical_period", payload,
                   [this, symbol, period, candles_key, run_talipp, inflight_key](bool ok, QJsonObject result, QString err) {
                       release_inflight(inflight_key);
                       if (!ok) {
                           emit error_occurred(symbol, "TALIpp", "Historical fetch failed: " + err);
                           return;
                       }
                       const auto arr = result.contains("_value")
                                            ? result.value("_value").toArray()
                                            : result.value("history").toArray();
                       // Same shared-key poisoning guard as fetch_technicals
                       // stage 1 — an error/empty series must not be pinned.
                       if (!cache_history_series(result, arr, symbol, candles_key, period,
                                                 kHistoricalTtlSec)) {
                           emit error_occurred(symbol, "TALIpp", history_error_text(result));
                           return;
                       }
                       run_talipp(arr);
                   });
    }
}

// ── Parsers ───────────────────────────────────────────────────────────────────
QuoteData EquityResearchService::parse_quote(const QJsonObject& o) const {
    QuoteData q;
    q.symbol = o["symbol"].toString();
    q.price = num(o, "price");
    q.change = num(o, "change");
    q.change_pct = num(o, "change_percent");
    q.open = num(o, "open");
    q.high = num(o, "high");
    q.low = num(o, "low");
    q.prev_close = num(o, "previous_close");
    q.volume = num(o, "volume");
    q.exchange = o["exchange"].toString();
    q.timestamp = static_cast<qint64>(o["timestamp"].toDouble());
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

QVector<Candle> EquityResearchService::parse_candles(const QJsonArray& arr) const {
    QVector<Candle> candles;
    candles.reserve(arr.size());
    for (const auto& v : arr) {
        auto o = v.toObject();
        // A price-less row must never become a zero-priced Candle: one such
        // bar drags a chart's y-range low to 0, and on the log axis that
        // spans 1e-9…hi and flattens every real candle into the top 1% of
        // the plot. See row_has_prices.
        if (!row_has_prices(o))
            continue;
        Candle c;
        c.timestamp = static_cast<qint64>(o["timestamp"].toDouble());
        c.open = o["open"].toDouble();
        c.high = o["high"].toDouble();
        c.low = o["low"].toDouble();
        c.close = o["close"].toDouble();
        c.volume = static_cast<qint64>(o["volume"].toDouble());
        candles.append(c);
    }
    return candles;
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
void fill_row(fincept::services::equity::IndicatorRow& row, const QJsonObject& src) {
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        if (it.value().isDouble())
            row.set(it.key(), it.value().toDouble());
    }
}

} // namespace

TechnicalsData EquityResearchService::parse_technicals(const QString& symbol, const QString& period,
                                                        const QString& interval,
                                                        const QJsonArray& rows) const {
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
    for (auto it = last.constBegin(); it != last.constEnd(); ++it) {
        const QString& key = it.key();
        if (key.startsWith(QLatin1Char('_')) && key.endsWith(QLatin1String("_error")))
            lost << key.mid(1, key.size() - 7);
    }
    if (!lost.isEmpty()) {
        lost.sort();
        td.data_warning = QStringLiteral("Could not compute: %1").arg(lost.join(QStringLiteral(", ")));
    }

    // Display name → column key in the computed output.
    // The averages span fast to slow on purpose — see is_moving_average() in
    // TechnicalRating.cpp for why a narrow set biased the whole rating.
    // CANONICAL indicator list: tests/services/test_technical_rating.cpp and
    // tools/technicals_series/score_series.cpp carry copies that must match,
    // or the tests and the measurement tool score a different panel than ships.
    static const QList<QPair<QString, QString>> kTrend = {
        {"SMA 10", "sma_10"},     {"SMA 20", "sma_20"},   {"SMA 30", "sma_30"},
        {"SMA 50", "sma_50"},     {"SMA 100", "sma_100"}, {"SMA 200", "sma_200"},
        {"EMA 12", "ema_12"},     {"EMA 26", "ema_26"},   {"WMA 9", "wma_9"},
        {"Ichimoku Base", "ichimoku_base"},
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
        {"Rolling VWAP", "vwap"},
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
        if (!est_v.isNull()) { e.eps_estimate = est_v.toDouble(); e.has_estimate = true; }
        const auto act_v = o.value("eps_actual");
        if (!act_v.isNull()) { e.eps_actual = act_v.toDouble(); e.has_actual = true; }
        const auto sur_v = o.value("surprise_pct");
        if (!sur_v.isNull()) { e.surprise_pct = sur_v.toDouble(); e.has_surprise = true; }
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
    label  = o.value("label").toString();
    if (label.isEmpty())
        label = period;
}

} // namespace

EarningsAnalysis EquityResearchService::parse_earnings_analysis(const QJsonObject& obj) const {
    EarningsAnalysis a;
    a.symbol   = obj.value("symbol").toString();
    a.currency = obj.value("currency").toString();
    a.as_of    = obj.value("as_of").toVariant().toLongLong();
    // `valid` means "the daemon answered", not "this symbol has earnings" —
    // ETFs and funds legitimately come back with every section empty, which
    // the tab reports as a clean empty state rather than an error.
    a.valid    = !obj.contains("error");

    const auto nx = obj.value("next").toObject();
    if (!nx.isEmpty()) {
        const auto ts = nx.value("timestamp");
        if (!ts.isNull() && !ts.isUndefined())
            a.next.timestamp = ts.toVariant().toLongLong();
        a.next.is_estimated = nx.value("is_estimated").toBool(true);
        a.next.eps_avg      = opt_num(nx, "eps_avg");
        a.next.eps_low      = opt_num(nx, "eps_low");
        a.next.eps_high     = opt_num(nx, "eps_high");
        a.next.analysts     = opt_num(nx, "analysts");
        a.next.rev_avg      = opt_num(nx, "rev_avg");
        a.next.rev_low      = opt_num(nx, "rev_low");
        a.next.rev_high     = opt_num(nx, "rev_high");
        a.next.year_ago_eps = opt_num(nx, "year_ago_eps");
        a.next.year_ago_rev = opt_num(nx, "year_ago_rev");
        a.next.eps_growth   = opt_num(nx, "eps_growth");
        a.next.rev_growth   = opt_num(nx, "rev_growth");

        // Absent whenever the daemon could not compute it honestly (no listed
        // options, or no expiry within ten days of the print) — see the model.
        const auto imp = nx.value("implied").toObject();
        if (!imp.isEmpty()) {
            EarningsImpliedMove m;
            m.expiry           = imp.value("expiry").toString();
            m.days_after_print = imp.value("days_after_print").toInt();
            m.total_move_pct   = opt_num(imp, "total_move_pct");
            m.event_move_pct   = opt_num(imp, "event_move_pct");
            m.straddle         = opt_num(imp, "straddle");
            m.spot             = opt_num(imp, "spot");
            m.strike           = opt_num(imp, "strike");
            if (m.total_move_pct.has_value())
                a.next.implied = m;
        }
    }

    const auto val = obj.value("valuation").toObject();
    a.valuation.price               = opt_num(val, "price");
    a.valuation.trailing_eps        = opt_num(val, "trailing_eps");
    a.valuation.forward_eps         = opt_num(val, "forward_eps");
    a.valuation.trailing_pe         = opt_num(val, "trailing_pe");
    a.valuation.forward_pe          = opt_num(val, "forward_pe");
    a.valuation.target_mean         = opt_num(val, "target_mean");
    a.valuation.target_high         = opt_num(val, "target_high");
    a.valuation.target_low          = opt_num(val, "target_low");
    a.valuation.recommendation_mean = opt_num(val, "recommendation_mean");
    a.valuation.analyst_count       = opt_num(val, "analyst_count");
    a.valuation.earnings_growth     = opt_num(val, "earnings_growth");
    a.valuation.revenue_growth      = opt_num(val, "revenue_growth");
    a.valuation.recommendation      = val.value("recommendation").toString();

    for (const auto& v : obj.value("history").toArray()) {
        const auto o = v.toObject();
        EarningsPoint p;
        p.timestamp = o.value("timestamp").toVariant().toLongLong();
        if (p.timestamp <= 0)
            continue;
        p.eps_estimate = opt_num(o, "eps_estimate");
        p.eps_actual   = opt_num(o, "eps_actual");
        p.surprise_pct = opt_num(o, "surprise_pct");
        p.surprise_suspect = o.value("surprise_suspect").toBool(false);
        p.eps_qoq_pct  = opt_num(o, "eps_qoq_pct");
        p.eps_yoy_pct  = opt_num(o, "eps_yoy_pct");
        p.reaction_pct = opt_num(o, "reaction_pct");
        p.reaction_live_pct = opt_num(o, "reaction_live_pct");
        p.runup_pct    = opt_num(o, "runup_pct");
        p.runup_20d_pct = opt_num(o, "runup_20d_pct");
        p.pre_vol_pct  = opt_num(o, "pre_vol_pct");
        p.price_before = opt_num(o, "price_before");
        p.price_after  = opt_num(o, "price_after");
        p.is_estimate  = o.value("is_estimate").toBool(false);
        p.move_since_last_pct = opt_num(o, "move_since_last_pct");
        p.price_now           = opt_num(o, "price_now");
        p.has_forward_estimate = o.value("has_forward_estimate").toBool(false);
        a.history.append(p);
    }

    for (const auto& v : obj.value("estimates").toArray()) {
        const auto o = v.toObject();
        EarningsEstimateRow r;
        read_period_label(o, r.period, r.label);
        r.eps_avg      = opt_num(o, "eps_avg");
        r.eps_low      = opt_num(o, "eps_low");
        r.eps_high     = opt_num(o, "eps_high");
        r.analysts     = opt_num(o, "analysts");
        r.year_ago_eps = opt_num(o, "year_ago_eps");
        r.eps_growth   = opt_num(o, "eps_growth");
        r.rev_avg      = opt_num(o, "rev_avg");
        r.rev_low      = opt_num(o, "rev_low");
        r.rev_high     = opt_num(o, "rev_high");
        r.year_ago_rev = opt_num(o, "year_ago_rev");
        r.rev_growth   = opt_num(o, "rev_growth");
        a.estimates.append(r);
    }

    for (const auto& v : obj.value("trend").toArray()) {
        const auto o = v.toObject();
        EarningsTrendRow r;
        read_period_label(o, r.period, r.label);
        r.current = opt_num(o, "current");
        r.d7      = opt_num(o, "d7");
        r.d30     = opt_num(o, "d30");
        r.d60     = opt_num(o, "d60");
        r.d90     = opt_num(o, "d90");
        a.trend.append(r);
    }

    for (const auto& v : obj.value("revisions").toArray()) {
        const auto o = v.toObject();
        EarningsRevisionRow r;
        read_period_label(o, r.period, r.label);
        r.up_7d    = opt_num(o, "up_7d");
        r.up_30d   = opt_num(o, "up_30d");
        r.down_7d  = opt_num(o, "down_7d");
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
    a.runup_5d_pct        = opt_num(recent, "runup_5d");
    a.runup_20d_pct       = opt_num(recent, "runup_20d");
    a.runup_90d_pct       = opt_num(recent, "runup_90d");
    a.rel_runup_20d_pct   = opt_num(recent, "rel_runup_20d");
    a.pct_from_52w_high   = opt_num(recent, "pct_from_52w_high");
    a.pre_vol_pct         = opt_num(recent, "pre_vol_pct");

    return a;
}

void EquityResearchService::subscribe_earnings_analysis(QObject* owner, const QString& symbol,
                                                        query::QueryStore::Callback cb) {
    if (symbol.isEmpty()) return;
    // Version segment for the same reason as kTechnicalsSchemaTag: the payload
    // shape changes with the engine, and although the 180s TTL makes stale
    // shapes self-heal in minutes, this project has been bitten by unversioned
    // cache keys before — a version segment makes the window zero instead.
    //
    //   v2  per-row runup_20d_pct; runup_90d moved to a calendar basis.
    //   v3  reaction_pct is withheld until the reaction session closes (the
    //       live figure moved to reaction_live_pct); runup_60d and
    //       rel_runup_90d dropped.
    //
    // v3 is not cosmetic and the TTL is not good enough here. A v2 entry
    // carries a reaction_pct that may be a still-running close, and
    // record_and_resolve would settle that print into earnings_signal_records
    // — a write resolve() never revisits. The bump makes that window zero.
    const QString key = "equity:earnings_analysis:v3:" + symbol;
    auto fetcher = [this, symbol, key](query::QueryStore::Resolver resolve,
                                       query::QueryStore::Rejecter reject) {
        const auto cached_aged = fincept::CacheManager::instance().try_get_aged(key);
        const QVariant cached = cached_aged ? QVariant(cached_aged->value) : QVariant();
        if (!cached.isNull()) {
            const auto doc = QJsonDocument::fromJson(cached.toString().toUtf8());
            if (doc.isObject()) {
                // Resolve with the cache entry's WRITE time: a hit answers
                // instantly, and stamping "now" would present a value hydrated
                // from disk at startup as brand new.
                resolve(QVariant::fromValue(parse_earnings_analysis(doc.object())), cached_aged->written_at);
                return;
            }
        }
        QJsonObject payload;
        payload["symbol"] = symbol;
        // 25 s, not the 10 s default: the daemon fans this out into six
        // upstream Yahoo calls (info, three estimate frames, earnings dates,
        // 3 y of daily bars) and any one of them can stall.
        run_daemon("earnings_analysis", payload,
            [this, key, resolve, reject](bool ok, QJsonObject result, QString err) {
                if (!ok) { reject(err); return; }
                if (result.contains("error")) {
                    reject(result.value("error").toString());
                    return;
                }
                fincept::CacheManager::instance().put(
                    key, QVariant(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))),
                    kEarningsAnalysisTtlSec, "equity");
                resolve(QVariant::fromValue(parse_earnings_analysis(result)));
            },
            25'000);
    };
    query::QueryStore::instance().subscribe(owner, key, kEarningsAnalysisTtlSec,
                                            kEarningsAnalysisTtlSec * 8,
                                            std::move(cb), std::move(fetcher));
}

} // namespace fincept::services::equity

