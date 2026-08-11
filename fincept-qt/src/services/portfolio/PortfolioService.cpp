// src/services/portfolio/PortfolioService.cpp
#include "services/portfolio/PortfolioService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "services/portfolio/PortfolioLedger.h"
#include "services/sectors/SectorResolver.h"
#include "services/util/DiskCache.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/PortfolioRepository.h"
#include "storage/repositories/SettingsRepository.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QTimeZone>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace fincept::services {

namespace {

fincept::services::util::DiskCache& portfolio_disk_cache() {
    static fincept::services::util::DiskCache c(QStringLiteral("portfolio"));
    return c;
}

// Per-portfolio summary cache filename. Slashes / weird chars in portfolio
// ids would break filesystem use; sanitize to a flat alnum-only basename.
QString summary_filename(const QString& portfolio_id) {
    QString safe = portfolio_id;
    for (auto& ch : safe) {
        if (!ch.isLetterOrNumber()) ch = QChar('_');
    }
    return QStringLiteral("summary_") + safe + QStringLiteral(".json");
}

// ── PortfolioSummary <-> JSON ────────────────────────────────────────────────
//
// Used only for the disk cache. Field set must match build_summary's output
// so a cached summary survives a round-trip and is indistinguishable from a
// freshly-computed one — UI consumers can't tell the difference.

QJsonObject portfolio_to_json(const portfolio::Portfolio& p) {
    QJsonObject o;
    o[QStringLiteral("id")]          = p.id;
    o[QStringLiteral("name")]        = p.name;
    o[QStringLiteral("owner")]       = p.owner;
    o[QStringLiteral("currency")]    = p.currency;
    o[QStringLiteral("description")] = p.description;
    o[QStringLiteral("created_at")]  = p.created_at;
    o[QStringLiteral("updated_at")]  = p.updated_at;
    return o;
}

portfolio::Portfolio portfolio_from_json(const QJsonObject& o) {
    portfolio::Portfolio p;
    p.id          = o[QStringLiteral("id")].toString();
    p.name        = o[QStringLiteral("name")].toString();
    p.owner       = o[QStringLiteral("owner")].toString();
    p.currency    = o[QStringLiteral("currency")].toString(QStringLiteral("USD"));
    p.description = o[QStringLiteral("description")].toString();
    p.created_at  = o[QStringLiteral("created_at")].toString();
    p.updated_at  = o[QStringLiteral("updated_at")].toString();
    return p;
}

QJsonObject holding_to_json(const portfolio::HoldingWithQuote& h) {
    QJsonObject o;
    o[QStringLiteral("symbol")]              = h.symbol;
    o[QStringLiteral("quantity")]            = h.quantity;
    o[QStringLiteral("avg_buy_price")]       = h.avg_buy_price;
    o[QStringLiteral("sector")]              = h.sector;
    o[QStringLiteral("current_price")]       = h.current_price;
    o[QStringLiteral("market_value")]        = h.market_value;
    o[QStringLiteral("cost_basis")]          = h.cost_basis;
    o[QStringLiteral("unrealized_pnl")]      = h.unrealized_pnl;
    o[QStringLiteral("unrealized_pnl_percent")] = h.unrealized_pnl_percent;
    o[QStringLiteral("day_change")]          = h.day_change;
    o[QStringLiteral("day_change_percent")]  = h.day_change_percent;
    o[QStringLiteral("weight")]              = h.weight;
    o[QStringLiteral("day_high")]            = h.day_high;
    o[QStringLiteral("day_low")]             = h.day_low;
    o[QStringLiteral("day_volume")]          = h.day_volume;
    o[QStringLiteral("bid")]                 = h.bid;
    o[QStringLiteral("ask")]                 = h.ask;
    o[QStringLiteral("bid_size")]            = h.bid_size;
    o[QStringLiteral("ask_size")]            = h.ask_size;
    o[QStringLiteral("first_purchase_date")] = h.first_purchase_date;
    o[QStringLiteral("peak_price")]          = h.peak_price;
    o[QStringLiteral("realized_pnl")]        = h.realized_pnl;
    o[QStringLiteral("dividend_income")]     = h.dividend_income;
    o[QStringLiteral("currency")]            = h.currency;
    o[QStringLiteral("fx_rate")]             = h.fx_rate;
    return o;
}

portfolio::HoldingWithQuote holding_from_json(const QJsonObject& o) {
    portfolio::HoldingWithQuote h;
    h.symbol               = o[QStringLiteral("symbol")].toString();
    h.quantity             = o[QStringLiteral("quantity")].toDouble();
    h.avg_buy_price        = o[QStringLiteral("avg_buy_price")].toDouble();
    h.sector               = o[QStringLiteral("sector")].toString();
    h.current_price        = o[QStringLiteral("current_price")].toDouble();
    h.market_value         = o[QStringLiteral("market_value")].toDouble();
    h.cost_basis           = o[QStringLiteral("cost_basis")].toDouble();
    h.unrealized_pnl       = o[QStringLiteral("unrealized_pnl")].toDouble();
    h.unrealized_pnl_percent = o[QStringLiteral("unrealized_pnl_percent")].toDouble();
    h.day_change           = o[QStringLiteral("day_change")].toDouble();
    h.day_change_percent   = o[QStringLiteral("day_change_percent")].toDouble();
    h.weight               = o[QStringLiteral("weight")].toDouble();
    h.day_high             = o[QStringLiteral("day_high")].toDouble();
    h.day_low              = o[QStringLiteral("day_low")].toDouble();
    h.day_volume           = o[QStringLiteral("day_volume")].toDouble();
    // bid/ask are order-book fields that go stale within seconds. The cached
    // summary on disk can be hours old; rendering its bid/ask as if live
    // would mislead the user. Leave them zeroed (the live live_row_ renders
    // em-dashes for 0); the live batch_quotes refresh will repopulate them
    // within the first second after this hydrate fires.
    h.bid                  = 0;
    h.ask                  = 0;
    h.bid_size             = 0;
    h.ask_size             = 0;
    h.first_purchase_date  = o[QStringLiteral("first_purchase_date")].toString();
    // Peak survives across launches so the L% column paints on the cache emit
    // instead of dashing until the history fan-out lands. The drawdown itself
    // is derived, never stored — re-derive it against the cached price.
    h.peak_price           = o[QStringLiteral("peak_price")].toDouble();
    portfolio::refresh_drawdown(h);
    h.realized_pnl         = o[QStringLiteral("realized_pnl")].toDouble();
    h.dividend_income      = o[QStringLiteral("dividend_income")].toDouble();
    h.currency             = o[QStringLiteral("currency")].toString();
    h.fx_rate              = o[QStringLiteral("fx_rate")].toDouble(1.0);
    return h;
}

QJsonObject summary_to_json(const portfolio::PortfolioSummary& s) {
    QJsonObject o;
    o[QStringLiteral("portfolio")] = portfolio_to_json(s.portfolio);
    QJsonArray hs;
    for (const auto& h : s.holdings) hs.append(holding_to_json(h));
    o[QStringLiteral("holdings")]            = hs;
    o[QStringLiteral("total_market_value")]  = s.total_market_value;
    o[QStringLiteral("total_cost_basis")]    = s.total_cost_basis;
    o[QStringLiteral("total_unrealized_pnl")] = s.total_unrealized_pnl;
    o[QStringLiteral("total_unrealized_pnl_percent")] = s.total_unrealized_pnl_percent;
    o[QStringLiteral("total_day_change")]    = s.total_day_change;
    o[QStringLiteral("total_day_change_percent")] = s.total_day_change_percent;
    o[QStringLiteral("total_realized_pnl")]  = s.total_realized_pnl;
    o[QStringLiteral("fx_incomplete")]       = s.fx_incomplete;
    QJsonObject rates;
    for (auto it = s.fx_rates.cbegin(); it != s.fx_rates.cend(); ++it)
        rates[it.key()] = it.value();
    o[QStringLiteral("fx_rates")]            = rates;
    o[QStringLiteral("total_dividend_income")] = s.total_dividend_income;
    o[QStringLiteral("total_positions")]     = s.total_positions;
    o[QStringLiteral("gainers")]             = s.gainers;
    o[QStringLiteral("losers")]              = s.losers;
    o[QStringLiteral("last_updated")]        = s.last_updated;
    return o;
}

portfolio::PortfolioSummary summary_from_json(const QJsonObject& o) {
    portfolio::PortfolioSummary s;
    s.portfolio = portfolio_from_json(o[QStringLiteral("portfolio")].toObject());
    for (const auto& v : o[QStringLiteral("holdings")].toArray())
        s.holdings.append(holding_from_json(v.toObject()));
    s.total_market_value         = o[QStringLiteral("total_market_value")].toDouble();
    s.total_cost_basis           = o[QStringLiteral("total_cost_basis")].toDouble();
    s.total_unrealized_pnl       = o[QStringLiteral("total_unrealized_pnl")].toDouble();
    s.total_unrealized_pnl_percent = o[QStringLiteral("total_unrealized_pnl_percent")].toDouble();
    s.total_day_change           = o[QStringLiteral("total_day_change")].toDouble();
    s.total_day_change_percent   = o[QStringLiteral("total_day_change_percent")].toDouble();
    s.total_realized_pnl         = o[QStringLiteral("total_realized_pnl")].toDouble();
    s.fx_incomplete              = o[QStringLiteral("fx_incomplete")].toBool();
    const QJsonObject rates = o[QStringLiteral("fx_rates")].toObject();
    for (auto it = rates.constBegin(); it != rates.constEnd(); ++it)
        s.fx_rates.insert(it.key(), it.value().toDouble(1.0));
    s.total_dividend_income      = o[QStringLiteral("total_dividend_income")].toDouble();
    s.total_positions            = o[QStringLiteral("total_positions")].toInt();
    s.gainers                    = o[QStringLiteral("gainers")].toInt();
    s.losers                     = o[QStringLiteral("losers")].toInt();
    s.last_updated               = o[QStringLiteral("last_updated")].toString();
    return s;
}

} // namespace

PortfolioService& PortfolioService::instance() {
    static PortfolioService s;
    return s;
}

PortfolioService::PortfolioService() : QObject(nullptr) {
    // When the SectorResolver lands a fresh sector for a symbol, persist it
    // onto whichever portfolios hold it and invalidate their summary caches
    // so the Sectors tab refreshes on next refresh.
    connect(&SectorResolver::instance(), &SectorResolver::sector_resolved, this,
            [this](QString symbol, QString sector) {
                if (symbol.isEmpty() || sector.isEmpty())
                    return;
                auto& repo = PortfolioRepository::instance();
                auto ports = repo.list_portfolios();
                if (ports.is_err())
                    return;
                for (const auto& p : ports.value()) {
                    auto assets = repo.get_assets(p.id);
                    if (assets.is_err())
                        continue;
                    bool touched = false;
                    for (const auto& a : assets.value()) {
                        if (a.symbol == symbol && a.sector != sector) {
                            repo.set_asset_sector(p.id, symbol, sector);
                            touched = true;
                        }
                    }
                    if (touched) {
                        invalidate_cache(p.id);
                        // Refresh the active portfolio view so sectors appear
                        // without the user having to hit refresh manually.
                        load_summary(p.id);
                    }
                }
            });
}

// ── Portfolio CRUD ───────────────────────────────────────────────────────────

void PortfolioService::load_portfolios() {
    auto r = PortfolioRepository::instance().list_portfolios();
    if (r.is_ok()) {
        emit portfolios_loaded(r.value());
    } else {
        LOG_ERROR("PortfolioSvc", "Failed to load portfolios: " + QString::fromStdString(r.error()));
    }
}

void PortfolioService::create_portfolio(const QString& name, const QString& owner, const QString& currency,
                                        const QString& description) {
    auto r = PortfolioRepository::instance().create_portfolio(name, owner, currency, description);
    if (r.is_ok()) {
        auto p = PortfolioRepository::instance().get_portfolio(r.value());
        if (p.is_ok())
            emit portfolio_created(p.value());
        load_portfolios();
    } else {
        LOG_ERROR("PortfolioSvc", "Failed to create portfolio: " + QString::fromStdString(r.error()));
    }
}

void PortfolioService::delete_portfolio(const QString& id) {
    invalidate_cache(id);
    auto r = PortfolioRepository::instance().delete_portfolio(id);
    if (r.is_ok()) {
        emit portfolio_deleted(id);
        load_portfolios();
    } else {
        LOG_ERROR("PortfolioSvc", "Failed to delete portfolio: " + QString::fromStdString(r.error()));
    }
}

// ── Summary ──────────────────────────────────────────────────────────────────

void PortfolioService::update_portfolio(const QString& id, const QString& name, const QString& owner,
                                       const QString& currency) {
    if (id.isEmpty() || name.trimmed().isEmpty()) {
        return;
    }
    auto& repo = PortfolioRepository::instance();
    // Keep the description: the edit dialog doesn't expose it, and passing the
    // default would silently wipe whatever an import or the MCP tools set.
    QString description;
    if (const auto existing = repo.get_portfolio(id); existing.is_ok()) {
        description = existing.value().description;
    }

    const auto result = repo.update_portfolio(id, name.trimmed(), owner.trimmed(),
                                              currency.isEmpty() ? QStringLiteral("USD") : currency, description);
    if (result.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to update portfolio: " + QString::fromStdString(result.error()));
        return;
    }

    // The cached summary carries the portfolio header (name/currency), so it
    // would keep serving the old name until its TTL expired.
    invalidate_cache(id);
    load_portfolios();
}

void PortfolioService::load_summary(const QString& portfolio_id) {
    // Check in-memory cache first (P11). 5-min TTL — short-circuits everything.
    // Copy the cached summary out before releasing the lock: emitting a signal
    // while holding cache_mutex_ causes a deadlock because on_summary_loaded
    // → fetch_portfolio_fundamentals re-acquires the same non-recursive mutex
    // on the same thread.
    {
        portfolio::PortfolioSummary cached;
        bool hit = false;
        {
            QMutexLocker lock(&cache_mutex_);
            auto it = summary_cache_.find(portfolio_id);
            if (it != summary_cache_.end()) {
                const qint64 now = QDateTime::currentSecsSinceEpoch();
                if (now - it->timestamp < kCacheTtlSec) {
                    cached = it->summary;
                    hit = true;
                }
            }
        } // lock released before emit
        if (hit) {
            emit summary_loaded(cached);
            return;
        }
    }

    // Disk-cache hydration: emit the last-built summary from the previous
    // session immediately so the UI paints with real (if stale) numbers while
    // the quote refetch + recompute runs below. Before emitting we refresh
    // every price-dependent field from CacheManager's `market_last:*` entries
    // — that cache is updated on every successful quote fetch by any code
    // path (dashboard ticker, watchlist, prior portfolio refresh) so it's
    // usually fresher than the portfolio's own disk snapshot, which was
    // written when PortfolioService last ran build_summary. Without this,
    // the ribbon flashed yesterday's totals briefly before DataHub's
    // per-symbol hydration raced to overwrite individual holdings —
    // producing a visible "wrong-looking intermediate" on the user's first
    // portfolio click after launch.
    const auto cached_doc = portfolio_disk_cache().load(summary_filename(portfolio_id));
    if (cached_doc.isObject()) {
        auto summary = summary_from_json(cached_doc.object());
        if (!summary.portfolio.id.isEmpty()) {
            summary.from_cache = true;
            // Peaks persisted with the snapshot are the only ones we have until
            // the history fan-out returns — seed the in-memory cache with them
            // so the live rebuild below keeps showing L% instead of flicking
            // back to a dash.
            seed_peak_cache_from_summary(summary);
            refresh_summary_prices_from_market_last(summary);
            emit summary_loaded(summary);
        }
    }

    auto portfolio_r = PortfolioRepository::instance().get_portfolio(portfolio_id);
    if (portfolio_r.is_err()) {
        emit summary_error(portfolio_id, QString::fromStdString(portfolio_r.error()));
        return;
    }

    auto assets_r = PortfolioRepository::instance().get_assets(portfolio_id);
    if (assets_r.is_err()) {
        emit summary_error(portfolio_id, QString::fromStdString(assets_r.error()));
        return;
    }

    if (assets_r.value().isEmpty()) {
        // Empty portfolio — emit summary with zero values
        portfolio::PortfolioSummary empty;
        empty.portfolio = portfolio_r.value();
        empty.last_updated = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        emit summary_loaded(empty);
        return;
    }

    build_summary(portfolio_id, assets_r.value(), portfolio_r.value());
}

void PortfolioService::refresh_summary(const QString& portfolio_id) {
    invalidate_cache(portfolio_id);
    load_summary(portfolio_id);
}

// static
void PortfolioService::refresh_summary_prices_from_market_last(portfolio::PortfolioSummary& summary) {
    if (summary.holdings.isEmpty())
        return;

    // One SQLite SELECT for the whole cohort — much cheaper than N
    // CacheManager::get() round-trips on a 50-holding portfolio. FX pairs ride
    // the same query: refreshing prices while leaving a days-old rate in place
    // would value fresh prices at a stale exchange rate.
    const QString port_ccy = portfolio::fx_price_factor(summary.portfolio.currency).first;
    QStringList keys;
    keys.reserve(summary.holdings.size() * 2);
    for (const auto& h : summary.holdings) {
        keys.append(QStringLiteral("market_last:") + h.symbol);
        const QString pair = portfolio::fx_pair_for(h.currency, port_ccy).first;
        if (!pair.isEmpty())
            keys.append(QStringLiteral("market_last:") + pair);
    }
    const QHash<QString, QString> hits = fincept::CacheManager::instance().multi_get(keys);

    const auto cached_price = [&hits](const QString& symbol) -> double {
        const auto it = hits.constFind(QStringLiteral("market_last:") + symbol);
        if (it == hits.constEnd())
            return 0.0;
        return QJsonDocument::fromJson(it.value().toUtf8()).object().value("price").toDouble();
    };

    double total_mv   = 0;
    double total_cost = 0;
    double total_day  = 0;
    int    gainers    = 0;
    int    losers     = 0;

    for (auto& h : summary.holdings) {
        const auto it = hits.constFind(QStringLiteral("market_last:") + h.symbol);
        if (it != hits.constEnd()) {
            const QJsonObject o = QJsonDocument::fromJson(it.value().toUtf8()).object();
            // Order-book fields aren't in the 7d market_last payload (they
            // go stale within seconds — see MarketDataService::refresh
            // comment). Read price-derived fields only; bid/ask/sizes
            // stay at whatever the disk-cache had so consumers don't
            // suddenly lose them across emits.
            h.current_price      = o.value("price").toDouble(h.current_price);
            h.day_change         = o.value("change").toDouble(h.day_change);
            h.day_change_percent = o.value("change_pct").toDouble(h.day_change_percent);
            h.day_high           = o.value("high").toDouble(h.day_high);
            h.day_low            = o.value("low").toDouble(h.day_low);
            h.day_volume         = o.value("volume").toDouble(h.day_volume);
        }
        // Refresh the conversion rate alongside the price. cost_basis was
        // serialized already converted at the OLD rate, so it is re-derived
        // here from the instrument-currency figures the asset row carries —
        // otherwise a rate move would shift market value while cost stayed
        // put, inventing P&L out of an FX tick.
        {
            const auto [pair, factor] = portfolio::fx_pair_for(h.currency, port_ccy);
            if (pair.isEmpty()) {
                h.fx_rate = factor; // same currency (or still unknown → 1.0)
            } else if (const double rate = cached_price(pair); rate > 0) {
                const double refreshed = factor * rate;
                if (h.fx_rate > 0 && !qFuzzyCompare(refreshed, h.fx_rate))
                    h.cost_basis *= refreshed / h.fx_rate;
                h.fx_rate = refreshed;
            }
        }
        // Always recompute per-holding aggregates: quantity / cost_basis
        // come from disk cache (canonical for this asset row), price may
        // have been refreshed above, and we want the math consistent
        // regardless of whether the lookup hit.
        h.market_value        = h.quantity * h.current_price * h.fx_rate;
        h.unrealized_pnl      = h.market_value - h.cost_basis;
        h.unrealized_pnl_percent =
            (h.cost_basis > 0) ? (h.unrealized_pnl / h.cost_basis) * 100.0 : 0;
        // Price moved — the drop from the peak moved with it (and a fresh high
        // becomes the peak).
        portfolio::refresh_drawdown(h);

        total_mv   += h.market_value;
        total_cost += h.cost_basis;
        total_day  += h.day_change * h.quantity * h.fx_rate;
        if (h.unrealized_pnl >= 0) ++gainers; else ++losers;
    }

    for (auto& h : summary.holdings)
        h.weight = (total_mv > 0) ? (h.market_value / total_mv) * 100.0 : 0;

    summary.total_market_value          = total_mv;
    summary.total_cost_basis            = total_cost;
    summary.total_unrealized_pnl        = total_mv - total_cost;
    summary.total_unrealized_pnl_percent =
        (total_cost > 0) ? ((total_mv - total_cost) / total_cost) * 100.0 : 0;
    summary.total_day_change            = total_day;
    summary.total_day_change_percent    =
        (total_mv - total_day > 0) ? (total_day / (total_mv - total_day)) * 100.0 : 0;
    summary.gainers                     = gainers;
    summary.losers                      = losers;
    // Defensive: total_positions is normally serialised by summary_from_json,
    // but if a future change ever drops the field, the POSITIONS hero card
    // would render "0 ▲0 ▼0" on the cache emit. Keep it in sync with the
    // actual holdings count we just iterated.
    summary.total_positions             = summary.holdings.size();
    // last_updated stays at the disk-cache value — the data here is a hybrid
    // of disk-cache holdings + market_last prices, both potentially stale.
    // Stamping "now" would lie to consumers (and to the stale-badge UI we
    // may wire later off `from_cache`).
}

void PortfolioService::build_summary(const QString& portfolio_id, const QVector<portfolio::PortfolioAsset>& assets,
                                     const portfolio::Portfolio& portfolio) {
    // Collect symbols for batch quote fetch
    QStringList symbols;
    symbols.reserve(assets.size());
    for (const auto& a : assets) {
        symbols.append(a.symbol);
    }

    // FX: discover trading currencies (async, cached 30 days) and ride the FX
    // pair quotes on the same batch fetch as the holdings. AAPL + RY.TO used
    // to be summed as bare numbers and labelled with the portfolio currency.
    ensure_symbol_currencies(symbols);
    const QString port_ccy = portfolio::fx_price_factor(portfolio.currency).first;
    QSet<QString> fx_pairs;
    for (const auto& a : assets) {
        const QString pair = portfolio::fx_pair_for(cached_symbol_currency(a.symbol), port_ccy).first;
        if (!pair.isEmpty())
            fx_pairs.insert(pair);
    }
    QStringList fetch_symbols = symbols;
    for (const QString& p : std::as_const(fx_pairs))
        fetch_symbols.append(p);

    // Use QPointer for safe async callback (P8)
    QPointer<PortfolioService> self = this;

    MarketDataService::instance().fetch_quotes(fetch_symbols, [self, portfolio_id, assets, port_ccy,
                                                               portfolio](bool ok, QVector<QuoteData> quotes) {
        if (!self)
            return;

        // Build quote lookup
        QHash<QString, QuoteData> quote_map;
        if (ok) {
            for (const auto& q : quotes)
                quote_map[q.symbol] = q;
        }

        // Last-known fallback for holdings the live fetch didn't cover. A quote
        // fetch legitimately comes back empty for an established holding —
        // Yahoo rate-limits once enough refresh ticks accumulate across the
        // app, or a symbol hits a transient data hole — leaving ok=true with no
        // quote for that symbol (a price<=0 partial counts the same). Without a
        // fallback the loop below dropped to avg_buy_price, which silently
        // collapses BOTH unrealized P&L (market_value == cost_basis ⇒ 0) and
        // day-change (⇒ 0) to zero, overwriting the good numbers the user was
        // watching. Recover the last-known price/day-change from the durable 7d
        // `market_last:` cache (the same store refresh_summary_prices_from_
        // market_last() trusts) so a momentary fetch failure shows slightly-
        // stale truth instead of zeros. Only queried for the symbols actually
        // missing, so the common all-fresh path pays nothing.
        QHash<QString, QuoteData> last_known;
        {
            QStringList missing;
            for (const auto& a : assets) {
                const auto it = quote_map.constFind(a.symbol);
                if (it == quote_map.constEnd() || it->price <= 0)
                    missing.append(a.symbol);
            }
            if (!missing.isEmpty()) {
                QStringList keys;
                keys.reserve(missing.size());
                for (const auto& s : missing)
                    keys.append(QStringLiteral("market_last:") + s);
                const QHash<QString, QString> hits =
                    fincept::CacheManager::instance().multi_get(keys);
                for (auto it = hits.cbegin(); it != hits.cend(); ++it) {
                    // Key off the cache key suffix (== the asset symbol we
                    // queried), NOT the payload's "symbol" field — the daemon
                    // echoes whatever Yahoo returns, which can differ in case /
                    // normalisation from the DB-uppercased asset symbol the
                    // holding loop below looks up with. Mirrors how
                    // hydrate_quotes_from_cache() strips the prefix.
                    const QString sym = it.key().mid(sizeof("market_last:") - 1);
                    if (sym.isEmpty())
                        continue;
                    const QJsonObject o =
                        QJsonDocument::fromJson(it.value().toUtf8()).object();
                    if (o.isEmpty())
                        continue;
                    QuoteData qd{};
                    qd.price      = o.value("price").toDouble();
                    qd.change     = o.value("change").toDouble();
                    qd.change_pct = o.value("change_pct").toDouble();
                    qd.high       = o.value("high").toDouble();
                    qd.low        = o.value("low").toDouble();
                    qd.volume     = o.value("volume").toDouble();
                    last_known[sym] = qd;
                }
            }
        }

        portfolio::PortfolioSummary summary;
        summary.portfolio = portfolio;
        summary.holdings.reserve(assets.size());

        // Instrument → portfolio-currency multiplier. Unknown currency or a
        // missing rate returns nullopt; the holding then enters the totals at
        // face value and the summary is flagged approximate — a wrong-but-
        // flagged total beats a silently wrong one.
        const auto fx_rate_for = [&quote_map, &port_ccy](const QString& symbol) -> std::optional<double> {
            const QString raw = cached_symbol_currency(symbol);
            if (raw.isEmpty())
                return std::nullopt; // currency still being discovered
            const auto [pair, factor] = portfolio::fx_pair_for(raw, port_ccy);
            if (pair.isEmpty())
                return factor; // same currency (factor folds GBp → GBP)
            if (const auto it = quote_map.constFind(pair); it != quote_map.constEnd() && it->price > 0)
                return factor * it->price;
            // Last known pair rate — FX closes barely move vs equity risk, and
            // a slightly stale rate beats a face-value cross-currency sum.
            const auto ml = fincept::CacheManager::instance().try_get(QStringLiteral("market_last:") + pair);
            if (ml) {
                const double px = QJsonDocument::fromJson(ml->toUtf8()).object().value("price").toDouble();
                if (px > 0)
                    return factor * px;
            }
            return std::nullopt;
        };

        double total_mv = 0;
        double total_cost = 0;
        double total_day = 0;
        // True when any holding had neither a live quote nor a last-known
        // cached price and fell back to its average buy price. A valuation
        // containing that fallback is an estimate and must not be recorded as
        // a 'live' observation — a cold-start offline launch would otherwise
        // stamp NAV == cost basis (P&L exactly 0) as the day's permanent
        // record, uncorrectable once the date has passed.
        bool valuation_estimated = false;

        for (const auto& asset : assets) {
            portfolio::HoldingWithQuote h;
            h.symbol = asset.symbol;
            h.quantity = asset.quantity;
            h.avg_buy_price = asset.avg_buy_price;
            h.cost_basis = asset.quantity * asset.avg_buy_price;
            h.first_purchase_date = asset.first_purchase_date;
            // Prefer stored sector; fall back to resolver cache (which may
            // populate async — see sector_resolved handler in constructor).
            h.sector = asset.sector.isEmpty()
                           ? SectorResolver::instance().sector_for(asset.symbol)
                           : asset.sector;

            auto it = quote_map.find(asset.symbol);
            if (it != quote_map.end() && it->price > 0) {
                h.current_price = it->price;
                h.day_change = it->change;
                h.day_change_percent = it->change_pct;
                // Copy through the live order-book snapshot for the perf
                // chart's focus-mode info bar to render bid/ask + day range.
                // Zeros pass through unchanged; consumers treat 0 as "unavailable".
                h.day_high  = it->high;
                h.day_low   = it->low;
                h.day_volume = it->volume;
                h.bid       = it->bid;
                h.ask       = it->ask;
                h.bid_size  = it->bid_size;
                h.ask_size  = it->ask_size;
            } else if (auto lk = last_known.constFind(asset.symbol);
                       lk != last_known.constEnd() && lk->price > 0) {
                // No fresh quote — hold the last-known price + day-change so the
                // ribbon/blotter keep showing real (if slightly stale) numbers
                // instead of collapsing P&L and chg% to zero. Order-book fields
                // aren't in the 7d payload; leave them at 0 ("unavailable").
                h.current_price = lk->price;
                h.day_change = lk->change;
                h.day_change_percent = lk->change_pct;
                h.day_high  = lk->high;
                h.day_low   = lk->low;
                h.day_volume = lk->volume;
            } else {
                // Genuine cold start — no quote and nothing cached. Fall back to
                // avg buy price (P&L reads 0 until the first quote lands).
                h.current_price = asset.avg_buy_price;
                valuation_estimated = true;
            }

            // Fold the instrument's currency into the portfolio's. Per-share
            // fields above stay in the instrument currency (see PortfolioTypes);
            // everything summed below is portfolio-currency.
            {
                h.currency = cached_symbol_currency(asset.symbol);
                const auto rate = fx_rate_for(asset.symbol);
                h.fx_rate = rate.value_or(1.0);
                if (!rate)
                    summary.fx_incomplete = true;
            }
            h.cost_basis *= h.fx_rate;
            h.market_value = h.quantity * h.current_price * h.fx_rate;
            h.unrealized_pnl = h.market_value - h.cost_basis;
            h.unrealized_pnl_percent = (h.cost_basis > 0) ? (h.unrealized_pnl / h.cost_basis) * 100.0 : 0;
            // Peak high since entry comes from the cache filled by
            // fetch_position_peaks below — zero (dash in the UI) until the
            // first fan-out for this symbol lands.
            const double cached_peak = self->cached_peak_high(h.symbol, h.first_purchase_date);
            portfolio::set_peak_high(h, cached_peak);
            // A live print above the last fetched daily high IS the new peak;
            // push it back into the cache or the next 20 s rebuild would
            // silently discard it and under-report the drawdown after the
            // price falls back off that spike.
            if (h.peak_price > cached_peak)
                self->raise_cached_peak(h.symbol, h.first_purchase_date, h.peak_price);

            total_mv += h.market_value;
            total_cost += h.cost_basis;
            total_day += h.day_change * h.quantity * h.fx_rate;

            if (h.unrealized_pnl >= 0)
                summary.gainers++;
            else
                summary.losers++;

            summary.holdings.append(h);
        }

        // Compute weights
        for (auto& h : summary.holdings) {
            h.weight = (total_mv > 0) ? (h.market_value / total_mv) * 100.0 : 0;
        }

        // Realized P&L and dividend income from the transaction log —
        // including symbols whose positions are fully closed and therefore
        // have no holding row. One SQLite read + a pure replay per symbol;
        // cheap at refresh-tick cadence, and the only way "REALIZED" can
        // include the winners that were sold.
        {
            auto txns_r = PortfolioRepository::instance().get_transactions(portfolio_id, /*limit=*/0);
            if (txns_r.is_ok()) {
                QHash<QString, QVector<portfolio::Transaction>> by_symbol;
                for (const auto& t : txns_r.value())
                    by_symbol[t.symbol.toUpper()].append(t);

                // Closed positions have no asset row, so the pre-fetch loop
                // never asked for their currency — yet their realized P&L and
                // their cash flows both need converting. Without this, the
                // "≈ approximate" flag would latch permanently for anyone who
                // has ever closed a foreign position.
                QStringList undiscovered;
                for (auto it = by_symbol.cbegin(); it != by_symbol.cend(); ++it)
                    if (cached_symbol_currency(it.key()).isEmpty())
                        undiscovered.append(it.key());
                if (!undiscovered.isEmpty())
                    self->ensure_symbol_currencies(undiscovered);
                QHash<QString, portfolio::LedgerPosition> replayed;
                for (auto it = by_symbol.cbegin(); it != by_symbol.cend(); ++it) {
                    auto pos = portfolio::replay_transactions(it.value());
                    // Instrument-currency figures converted at the CURRENT
                    // rate — an approximation for long-closed positions (a
                    // trade-dated conversion would need historical FX), and
                    // stated as such here rather than silently mixed. Held
                    // symbols resolve to the same rate the holdings loop used;
                    // it is the same function over the same inputs.
                    const auto r = fx_rate_for(it.key());
                    if (!r && (pos.realized_pnl != 0.0 || pos.dividend_income != 0.0))
                        summary.fx_incomplete = true;
                    const double rate = r.value_or(1.0);
                    // Every log symbol, so the return math can convert the
                    // flows of positions that are no longer held.
                    summary.fx_rates.insert(it.key(), rate);
                    summary.total_realized_pnl += pos.realized_pnl * rate;
                    summary.total_dividend_income += pos.dividend_income * rate;
                    replayed.insert(it.key(), std::move(pos));
                }
                for (auto& h : summary.holdings) {
                    const auto it = replayed.constFind(h.symbol.toUpper());
                    if (it != replayed.constEnd()) {
                        h.realized_pnl = it->realized_pnl * h.fx_rate;
                        h.dividend_income = it->dividend_income * h.fx_rate;
                    }
                }
            }
        }

        summary.total_market_value = total_mv;
        summary.total_cost_basis = total_cost;
        summary.total_unrealized_pnl = total_mv - total_cost;
        summary.total_unrealized_pnl_percent = (total_cost > 0) ? ((total_mv - total_cost) / total_cost) * 100.0 : 0;
        summary.total_day_change = total_day;
        summary.total_day_change_percent =
            (total_mv - total_day > 0) ? (total_day / (total_mv - total_day)) * 100.0 : 0;
        summary.total_positions = assets.size();
        summary.last_updated = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        // Cache the result (P11)
        {
            QMutexLocker lock(&self->cache_mutex_);
            self->summary_cache_[portfolio_id] = {summary, QDateTime::currentSecsSinceEpoch()};
        }

        // Persist to disk so the next app launch hydrates instantly without
        // waiting for the quote round-trip.
        portfolio_disk_cache().save(summary_filename(portfolio_id),
                                    QJsonDocument(summary_to_json(summary)));

        // Save snapshot for performance history. Provenance depends on what
        // actually priced the holdings: quotes and `market_last:` fallbacks are
        // real prints, so the row is a 'live' observation that self-corrects on
        // the next tick of the same day. If any holding had to be priced at its
        // average buy cost (cold start, offline), the valuation is an estimate
        // and is written through the backfill path instead — still gap-free,
        // but correctable later, and never able to displace a real observation.
        QString today = QDate::currentDate().toString(Qt::ISODate);
        // fx_incomplete counts as estimated: a face-value cross-currency sum
        // written as 'live' could never be repaired, because the backfill
        // guard refuses to overwrite live rows.
        if (valuation_estimated || summary.fx_incomplete) {
            PortfolioRepository::instance().save_backfill_snapshot(
                portfolio_id, summary.total_market_value, summary.total_cost_basis, summary.total_unrealized_pnl,
                summary.total_unrealized_pnl_percent, today);
        } else {
            PortfolioRepository::instance().save_snapshot(portfolio_id, summary.total_market_value,
                                                          summary.total_cost_basis, summary.total_unrealized_pnl,
                                                          summary.total_unrealized_pnl_percent, today);
        }

        emit self->summary_loaded(summary);

        // Refresh any peak that's missing or older than the TTL. No-op on the
        // common tick where every symbol is already cached.
        self->fetch_position_peaks(portfolio_id, assets);
    });
}

// ── Asset operations ─────────────────────────────────────────────────────────
//
// One write model: record the transaction, then rebuild_position() replays
// the symbol's full log through the single PortfolioLedger convention and
// syncs the asset row to the result. The module previously kept two
// incompatible average-cost computations (a running upsert here, an
// all-BUYs-ignoring-SELLs recompute in edit_position), so the number shown
// as AVG COST depended on which code path last touched the position.

portfolio::LedgerPosition PortfolioService::rebuild_position(const QString& portfolio_id, const QString& symbol) {
    auto& repo = PortfolioRepository::instance();
    const QString up = symbol.toUpper();

    auto txns = repo.get_symbol_transactions(portfolio_id, up);
    if (txns.is_err()) {
        // Do NOT touch the asset row on a read failure — deleting a position
        // because SQLite hiccuped would be the destructive path this engine
        // exists to remove.
        LOG_ERROR("PortfolioSvc", QString("rebuild_position %1: %2").arg(up, QString::fromStdString(txns.error())));
        return {};
    }

    // An empty log has exactly one meaning since v049 synthesized opening
    // BUYs for every pre-ledger holding: the position does not exist (its
    // last transaction was deleted). Leaving the cached row alive here would
    // strand a phantom holding no ledger operation could ever remove.
    auto pos = portfolio::replay_transactions(txns.value());
    for (const auto& w : pos.warnings)
        LOG_WARN("PortfolioSvc", QString("Ledger %1/%2: %3").arg(portfolio_id, up, w));

    if (pos.is_open())
        repo.set_position(portfolio_id, up, pos.quantity, pos.avg_cost, pos.first_buy_date);
    else
        repo.remove_asset(portfolio_id, up);
    return pos;
}

void PortfolioService::add_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                                 const QString& date) {
    if (qty <= 0) {
        LOG_ERROR("PortfolioSvc", QString("Rejected BUY of %1 x%2 — quantity must be positive").arg(symbol).arg(qty));
        return;
    }
    auto& repo = PortfolioRepository::instance();
    QString txn_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;
    auto r = repo.add_transaction(portfolio_id, symbol, "BUY", qty, price, txn_date);
    if (r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to record buy: " + QString::fromStdString(r.error()));
        return;
    }
    rebuild_position(portfolio_id, symbol);

    invalidate_cache(portfolio_id);
    emit asset_added(portfolio_id);
}

void PortfolioService::sell_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                                  const QString& date) {
    auto& repo = PortfolioRepository::instance();

    // Validate against the ledger, not the asset row — the row is a cache.
    auto txns = repo.get_symbol_transactions(portfolio_id, symbol);
    const double held = txns.is_ok() && !txns.value().isEmpty()
                            ? portfolio::replay_transactions(txns.value()).quantity
                            : 0.0;
    if (qty <= 0 || qty > held + 1e-6) {
        // Surface the rejection — a silently ignored sell looks like data loss.
        const QString reason = tr("Cannot sell %1 %2 — the transaction log shows %3 held.")
                                   .arg(qty)
                                   .arg(symbol)
                                   .arg(held);
        LOG_ERROR("PortfolioSvc", reason);
        emit position_edit_rejected(portfolio_id, symbol, reason);
        return;
    }

    QString txn_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;
    auto r = repo.add_transaction(portfolio_id, symbol, "SELL", qty, price, txn_date);
    if (r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to record sell: " + QString::fromStdString(r.error()));
        return;
    }
    // The replay keeps the realized gain; a fully closed position removes its
    // asset row but its P&L stays in the log (and in the summary totals).
    rebuild_position(portfolio_id, symbol);

    invalidate_cache(portfolio_id);
    emit asset_sold(portfolio_id);
}

// ── Transactions ─────────────────────────────────────────────────────────────

void PortfolioService::load_transactions(const QString& portfolio_id, int limit) {
    auto r = PortfolioRepository::instance().get_transactions(portfolio_id, limit);
    if (r.is_ok()) {
        emit transactions_loaded(r.value());
    } else {
        LOG_ERROR("PortfolioSvc", "Failed to load transactions: " + QString::fromStdString(r.error()));
    }
}

QVector<portfolio::Transaction> PortfolioService::all_transactions(const QString& portfolio_id, bool* ok) {
    auto r = PortfolioRepository::instance().get_transactions(portfolio_id, /*limit=*/0); // 0 = no limit
    if (ok)
        *ok = r.is_ok();
    if (r.is_err()) {
        LOG_WARN("PortfolioSvc", "Failed to load transactions: " + QString::fromStdString(r.error()));
        return {};
    }
    return r.value();
}

QVector<portfolio::Transaction> PortfolioService::symbol_transactions(const QString& portfolio_id,
                                                                     const QString& symbol) {
    auto r = PortfolioRepository::instance().get_symbol_transactions(portfolio_id, symbol);
    if (r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to load transactions for " + symbol + ": " +
                                      QString::fromStdString(r.error()));
        return {};
    }
    return r.value();
}

void PortfolioService::edit_position(const QString& portfolio_id, const QString& symbol, const QString& txn_id,
                                     double qty, double price, const QString& date, const QString& notes) {
    auto& repo = PortfolioRepository::instance();

    // Validate BEFORE writing, by replaying the log with the proposed values
    // substituted in. Shrinking a BUY below the SELLs already recorded against
    // it makes the history impossible — a real case: a 425-share buy with a
    // 424-share sell against it, edited down to 213, wrote -211 shares to the
    // asset row. Nothing is written until the substituted replay is sane.
    //
    // Historical note: this path used to recompute average cost over BUY rows
    // only, ignoring intervening SELLs — a different convention from the buy
    // upsert, so editing a transaction's NOTE could move cost basis by double
    // digits. Both paths now replay through the same ledger.
    auto txns = repo.get_symbol_transactions(portfolio_id, symbol);
    if (txns.is_ok()) {
        QVector<portfolio::Transaction> proposed = txns.value();
        for (auto& t : proposed) {
            if (t.id == txn_id) {
                t.quantity = qty;
                t.price = price;
                t.transaction_date = date;
            }
        }
        const auto count_oversells = [](const portfolio::LedgerPosition& p) {
            int n = 0;
            for (const auto& w : p.warnings)
                if (w.contains(QLatin1String("exceeds")))
                    ++n;
            return n;
        };
        // Compare against the CURRENT replay: an edit is rejected only for an
        // inconsistency it introduces, never for one the history already had
        // (otherwise a note edit on a long-imported symbol becomes impossible).
        const auto proposed_replay = portfolio::replay_transactions(proposed);
        const int before = count_oversells(portfolio::replay_transactions(txns.value()));
        if (count_oversells(proposed_replay) > before) {
            QString detail;
            for (const auto& w : proposed_replay.warnings)
                if (w.contains(QLatin1String("exceeds")))
                    detail = w;
            const QString reason = tr("This edit would make the history impossible for %1: %2\n\n"
                                      "Adjust or remove the conflicting sell first.")
                                       .arg(symbol, detail);
            LOG_WARN("PortfolioSvc", QString("Rejected edit of %1 in %2: %3").arg(symbol, portfolio_id, detail));
            emit position_edit_rejected(portfolio_id, symbol, reason);
            return; // nothing written — transaction and asset both untouched
        }
    }

    repo.update_transaction(txn_id, qty, price, date, notes);
    rebuild_position(portfolio_id, symbol);

    invalidate_cache(portfolio_id);
    load_transactions(portfolio_id, 50); // refresh the txn-history panel
    emit asset_added(portfolio_id);      // -> PortfolioScreen::on_asset_changed -> refresh_summary
}

void PortfolioService::delete_transaction(const QString& id, const QString& portfolio_id) {
    auto& repo = PortfolioRepository::instance();
    // Fetch first: after the delete there is nothing left to say which
    // position must be re-derived. Trust the transaction's own portfolio_id
    // over the caller's — the delete is keyed globally by id, so a stale UI
    // selection would otherwise rebuild the wrong portfolio and leave the
    // affected one showing a position that includes the deleted row.
    const auto txn = repo.get_transaction(id);
    repo.delete_transaction(id);
    if (txn.is_ok()) {
        rebuild_position(txn.value().portfolio_id, txn.value().symbol);
        invalidate_cache(txn.value().portfolio_id);
    }
    if (!txn.is_ok() || txn.value().portfolio_id != portfolio_id)
        invalidate_cache(portfolio_id);
}

// ── Dividend ──────────────────────────────────────────────────────────────────

void PortfolioService::record_dividend(const QString& portfolio_id, const QString& symbol, double qty,
                                       double amount_per_share, double total, const QString& date,
                                       const QString& notes) {
    const QString txn_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;
    auto& repo = PortfolioRepository::instance();
    auto r = repo.add_transaction(portfolio_id, symbol, "DIVIDEND", qty, amount_per_share, txn_date, notes);
    if (r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to record dividend: " + QString::fromStdString(r.error()));
        return;
    }
    Q_UNUSED(total)
    invalidate_cache(portfolio_id);
    // Reload transactions so the txn panel updates
    load_transactions(portfolio_id, 50);
}

// ── Historical correlation ────────────────────────────────────────────────────

void PortfolioService::fetch_correlation(const QStringList& symbols) {
    if (symbols.size() < 2) {
        emit correlation_computed({});
        return;
    }

    // Build inline Python that embeds the symbol list, fetches 30-day closes,
    // and prints a JSON correlation matrix to stdout.
    QJsonArray sym_arr;
    for (const auto& s : symbols)
        sym_arr.append(s);
    const QString sym_json = QString::fromUtf8(QJsonDocument(sym_arr).toJson(QJsonDocument::Compact));

    const QString code = QString(R"python(
import json, sys
import yfinance as yf
import numpy as np

symbols = %1
data = {}
for sym in symbols:
    try:
        hist = yf.download(sym, period="30d", interval="1d", progress=False)
        if hist is not None and not hist.empty:
            closes = hist["Close"].dropna().tolist()
            if hasattr(closes[0], 'item'):
                closes = [v.item() for v in closes]
            data[sym] = closes
    except Exception:
        pass

# Compute daily returns
returns = {}
for sym, prices in data.items():
    if len(prices) >= 5:
        r = [(prices[i] - prices[i-1]) / prices[i-1] for i in range(1, len(prices))]
        returns[sym] = r

syms = list(returns.keys())
matrix = {}
for i in range(len(syms)):
    for j in range(len(syms)):
        a = returns[syms[i]]
        b = returns[syms[j]]
        n = min(len(a), len(b))
        if n < 2:
            val = 1.0 if i == j else 0.0
        else:
            a, b = a[-n:], b[-n:]
            ma, mb = sum(a)/n, sum(b)/n
            num = sum((a[k]-ma)*(b[k]-mb) for k in range(n))
            da  = sum((a[k]-ma)**2 for k in range(n))
            db  = sum((b[k]-mb)**2 for k in range(n))
            denom = (da*db)**0.5
            val = num/denom if denom > 1e-10 else (1.0 if i==j else 0.0)
            val = max(-1.0, min(1.0, val))
        matrix[syms[i] + "|" + syms[j]] = round(val, 4)

print(json.dumps(matrix))
)python")
                             .arg(sym_json);

    QPointer<PortfolioService> self = this;
    python::PythonRunner::instance().run_code(code, [self](python::PythonResult result) {
        if (!self)
            return;
        if (!result.success || result.output.trimmed().isEmpty()) {
            LOG_WARN("PortfolioSvc", "Correlation fetch failed: " + result.error.left(200));
            emit self->correlation_computed({});
            return;
        }
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(result.output.trimmed().toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit self->correlation_computed({});
            return;
        }
        QHash<QString, double> matrix;
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            matrix[it.key()] = it.value().toDouble();
        emit self->correlation_computed(matrix);
    });
}

// ── SPY benchmark data ────────────────────────────────────────────────────────

QString PortfolioService::default_benchmark_for_currency(const QString& currency) {
    const QString c = currency.trimmed().toUpper();
    if (c == "CAD") return QStringLiteral("^GSPTSE");
    if (c == "GBP") return QStringLiteral("^FTSE");
    if (c == "EUR") return QStringLiteral("^STOXX50E");
    if (c == "AUD") return QStringLiteral("^AXJO");
    if (c == "INR") return QStringLiteral("^NSEI");
    if (c == "JPY") return QStringLiteral("^N225");
    if (c == "HKD") return QStringLiteral("^HSI");
    return QStringLiteral("SPY"); // USD and unknown
}

void PortfolioService::fetch_spy_history(const QString& period) {
    fetch_benchmark_history(QStringLiteral("SPY"), period);
}

void PortfolioService::fetch_benchmark_history(const QString& symbol, const QString& period) {
    // Allow callers to omit the symbol → defaults to SPY (legacy behaviour).
    const QString sym = symbol.isEmpty() ? QStringLiteral("SPY") : symbol;

    // Use the persistent yfinance daemon (historical_period action) instead of
    // spawning a fresh Python process via PythonRunner::run_code(). The daemon
    // already has yfinance/pandas imported, reducing latency from ~2-4s to
    // ~200-500ms (or near-instant on a cache hit from EquityResearchService).
    QJsonObject payload;
    payload["symbol"]   = sym;
    payload["period"]   = period;
    payload["interval"] = QStringLiteral("1d");

    QPointer<PortfolioService> self = this;
    python::PythonWorker::instance().submit(
        "historical_period", payload,
        [self, sym, period](bool ok, QJsonObject result, QString err) {
            if (!self) return;

            QStringList dates;
            QVector<double> closes;

            if (ok) {
                // Daemon wraps a flat array under "_value".
                const QJsonArray arr = result.contains("_value")
                    ? result["_value"].toArray()
                    : result["history"].toArray();

                dates.reserve(arr.size());
                closes.reserve(arr.size());
                for (const auto& v : arr) {
                    const auto o = v.toObject();
                    const qint64 ts = static_cast<qint64>(o["timestamp"].toDouble());
                    const QDate d = QDateTime::fromSecsSinceEpoch(ts, QTimeZone::UTC).date();
                    dates.append(d.toString(Qt::ISODate));
                    closes.append(o["close"].toDouble());
                }
            } else {
                LOG_WARN("PortfolioSvc",
                         QString("Benchmark %1 fetch failed: %2").arg(sym, err.left(200)));
            }

            // For symbols that yfinance doesn't know (FCASH, SPAXX, money-market
            // funds, etc.) the download returns empty. Synthesise a flat $1.00/share
            // series for the requested period so the chart renders a visible
            // baseline (= 0% return) instead of "No price history".
            if (dates.isEmpty()) {
                const QDate today = QDate::currentDate();
                QDate start = today;
                if      (period == "1mo")  start = today.addMonths(-1);
                else if (period == "3mo")  start = today.addMonths(-3);
                else if (period == "6mo")  start = today.addMonths(-6);
                else if (period == "1y")   start = today.addYears(-1);
                else if (period == "2y")   start = today.addYears(-2);
                else if (period == "5y")   start = today.addYears(-5);
                else                       start = today.addYears(-1);

                // Weekly points — enough resolution for a flat line.
                for (QDate d = start; d <= today; d = d.addDays(7)) {
                    dates.append(d.toString(Qt::ISODate));
                    closes.append(1.0); // $1.00/share: cash holds par value
                }
                LOG_INFO("PortfolioSvc",
                         QString("Synthesised flat $1 series for %1 (%2 points)").arg(sym).arg(dates.size()));
            }

            // Beta computation always regresses against SPY — only update the
            // cache when that is the symbol being loaded.
            if (sym == QStringLiteral("SPY")) {
                self->spy_dates_cache_ = dates;
                self->spy_closes_cache_ = closes;
                emit self->spy_history_loaded(dates, closes);
            }
            emit self->benchmark_history_loaded(sym, dates, closes);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── 1D intraday ──────────────────────────────────────────────────────────────
//
// Both single-symbol and portfolio-aggregate intraday rendering go through
// the same yfinance 1m-interval pull: period="1d", interval="1m" returns
// ~390 close prices for today's RTH (or whatever portion has elapsed).
//
// The implementation is intentionally on-demand and stateless — no
// background sampler, no intraday_snapshots DB table. The aggregate path
// (Path 3) is just N parallel single-symbol fetches; we union the returned
// timestamps and sum (qty × close) at each point.

namespace {

// Today's NYSE regular-trading-hours session in UTC ms. Returns {open, close}
// for the most recent session that has begun: today if now ≥ today's open,
// else the previous calendar day. Weekend/holiday handling is deliberately
// not included — yfinance returns the last trading day's bars for those, and
// the aggregator anchors to those real-bar timestamps when available. This
// helper is only used as a fallback when no real bars exist at all.
QPair<qint64, qint64> nyse_session_today_utc_ms() {
    const QTimeZone et("America/New_York");
    const QDateTime now_et = QDateTime::currentDateTime().toTimeZone(et);
    QDateTime open_et(now_et.date(), QTime(9, 30), et);
    QDateTime close_et(now_et.date(), QTime(16, 0), et);
    if (now_et < open_et) {
        open_et  = open_et.addDays(-1);
        close_et = close_et.addDays(-1);
    }
    return {open_et.toMSecsSinceEpoch(), close_et.toMSecsSinceEpoch()};
}

} // namespace

void PortfolioService::fetch_symbol_intraday(const QString& symbol,
                                              const QString& period,
                                              const QString& interval) {
    const QString sym = symbol.trimmed();
    if (sym.isEmpty()) {
        emit symbol_intraday_loaded(sym, {}, {});
        return;
    }
    QJsonObject payload;
    payload["symbol"]   = sym;
    payload["period"]   = period;
    payload["interval"] = interval;

    const qint64 epoch = ++symbol_intraday_epoch_;
    QPointer<PortfolioService> self = this;
    python::PythonWorker::instance().submit(
        "historical_period", payload,
        [self, sym, epoch, period](bool ok, QJsonObject result, QString err) {
            if (!self) return;
            if (self->symbol_intraday_epoch_ != epoch) {
                // Superseded by a newer fetch (user switched ticker/period).
                // Drop silently — emitting would overwrite fresher results.
                return;
            }
            QVector<qint64> ts_ms;
            QVector<double> closes;
            if (ok) {
                const QJsonArray arr = result.contains("_value")
                    ? result["_value"].toArray() : result["history"].toArray();
                ts_ms.reserve(arr.size());
                closes.reserve(arr.size());
                for (const auto& v : arr) {
                    const auto o = v.toObject();
                    const qint64 sec = static_cast<qint64>(o["timestamp"].toDouble());
                    if (sec <= 0) continue;
                    ts_ms.append(sec * 1000);
                    closes.append(o["close"].toDouble());
                }
            } else {
                LOG_WARN("PortfolioSvc",
                         QString("Intraday %1 fetch failed: %2").arg(sym, err.left(200)));
            }

            // Symbols yfinance doesn't know (FCASH, SPAXX, money-market funds)
            // return an empty intraday pull. Mirror the daily-history fallback
            // (line ~665) and synthesise a flat $1.00/share 2-point series.
            // For 1D we anchor to today's NYSE session (09:30→16:00 ET) so
            // the chart's x-axis matches real bars from other symbols rather
            // than drifting with wall-clock time. Multi-day periods (1W/1M
            // focus) span a fixed window back from now since there's no
            // single "session open" anchor for them.
            if (ts_ms.isEmpty() && closes.isEmpty()) {
                qint64 start_ms = 0;
                qint64 end_ms   = 0;
                if (period == QStringLiteral("1d")) {
                    const auto [open_ms, close_ms] = nyse_session_today_utc_ms();
                    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
                    start_ms = open_ms;
                    end_ms   = std::min(close_ms, now_ms);
                } else {
                    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
                    qint64 span_ms = 5LL * 24 * 60 * 60 * 1000; // 5d default
                    if (period == QStringLiteral("1mo"))
                        span_ms = 30LL * 24 * 60 * 60 * 1000;
                    start_ms = now_ms - span_ms;
                    end_ms   = now_ms;
                }
                ts_ms  = {start_ms, end_ms};
                closes = {1.0, 1.0};
                LOG_INFO("PortfolioSvc",
                         QString("Synthesised flat $1 intraday series for %1 (%2)").arg(sym, period));
            }
            emit self->symbol_intraday_loaded(sym, ts_ms, closes);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

void PortfolioService::fetch_portfolio_intraday(const QString& portfolio_id) {
    auto assets_r = PortfolioRepository::instance().get_assets(portfolio_id);
    if (assets_r.is_err() || assets_r.value().isEmpty()) {
        emit portfolio_intraday_loaded(portfolio_id, {}, {});
        return;
    }
    const auto assets = assets_r.value();

    // Quantity lookup keyed by upper-cased symbol — yfinance normalises case
    // but the user may have stored mixed case in the DB.
    QHash<QString, double> qty_by_symbol;
    QStringList unique_symbols;
    for (const auto& a : assets) {
        const QString up = a.symbol.toUpper();
        if (!qty_by_symbol.contains(up)) unique_symbols.append(up);
        qty_by_symbol[up] += a.quantity;
    }

    // Per-share price hint for the no-bars fallback. The live summary cache
    // holds current_price for every holding (refreshed from MarketDataService
    // batch quotes), which is what cash/MMF/unknown-to-yfinance positions
    // should be valued at — not the legacy hardcoded $1.00 (correct only for
    // a 1-share par-value cash position, wrong for everything else). Fall
    // back to avg_buy_price when no cached quote is available.
    QHash<QString, double> price_hint;
    for (const auto& a : assets) {
        price_hint[a.symbol.toUpper()] = a.avg_buy_price;
    }
    {
        QMutexLocker lock(&cache_mutex_);
        auto cit = summary_cache_.constFind(portfolio_id);
        if (cit != summary_cache_.cend()) {
            for (const auto& h : cit.value().summary.holdings) {
                if (h.current_price > 0)
                    price_hint[h.symbol.toUpper()] = h.current_price;
            }
        }
    }

    // Per-symbol intraday series accumulator. The aggregate emit fires once
    // every symbol has reported (or errored out). Using a shared_ptr lets the
    // multiple callback lambdas all mutate the same accumulator without races
    // on the main thread. epoch is captured at fan-out start; if it no longer
    // matches portfolio_intraday_epoch_ when the last symbol returns, we
    // drop the accumulator without emitting (a newer 1D request is live).
    struct Accum {
        int pending = 0;
        qint64 epoch = 0;
        QHash<QString, QHash<qint64, double>> by_symbol; // symbol → {ts_ms: close}
    };
    auto state = std::make_shared<Accum>();
    state->pending = unique_symbols.size();
    state->epoch   = ++portfolio_intraday_epoch_;

    QPointer<PortfolioService> self = this;
    for (const QString& sym : unique_symbols) {
        QJsonObject payload;
        payload["symbol"]   = sym;
        payload["period"]   = QStringLiteral("1d");
        payload["interval"] = QStringLiteral("1m");
        python::PythonWorker::instance().submit(
            "historical_period", payload,
            [self, portfolio_id, sym, state, qty_by_symbol, price_hint](
                bool ok, QJsonObject result, QString err) {
                if (!self) return;
                QHash<qint64, double>& bars = state->by_symbol[sym];
                if (ok) {
                    const QJsonArray arr = result.contains("_value")
                        ? result["_value"].toArray() : result["history"].toArray();
                    for (const auto& v : arr) {
                        const auto o = v.toObject();
                        const qint64 sec = static_cast<qint64>(o["timestamp"].toDouble());
                        if (sec <= 0) continue;
                        bars.insert(sec * 1000, o["close"].toDouble());
                    }
                } else {
                    LOG_WARN("PortfolioSvc",
                             QString("Aggregate intraday %1 failed: %2").arg(sym, err.left(200)));
                }

                // Cash / MMF / unknown-to-yfinance symbols return zero bars.
                // Synthesis is deferred to the aggregator so it can match the
                // real-bar window (NYSE 09:30 ET onward) instead of anchoring
                // to "now − 6.5h" — which used to push the chart's x-axis
                // start into pre-market wall-clock time (e.g. 04:15 PT at
                // mid-session) and stretched the axis with cash-only padding.
                if (--state->pending > 0) return;

                // All symbols reported — but the user may have moved on
                // (clicked a different period, switched portfolio) while the
                // fan-out was running. Drop the accumulator silently in that
                // case so a fresher fetch's result isn't overwritten.
                if (self->portfolio_intraday_epoch_ != state->epoch) return;

                // Synthesise bars for cash / MMF / unknown-to-yfinance symbols
                // *now* that every real-bar symbol has reported. Anchoring
                // their flat series to the real-bar window (rather than a
                // wall-clock "now − 6.5h") keeps the chart's x-axis aligned
                // with the actual NYSE session. If no symbol returned any
                // real bars (all-cash portfolio), fall back to today's
                // NYSE 09:30→16:00 ET so the chart still has something to
                // anchor against.
                qint64 win_start = std::numeric_limits<qint64>::max();
                qint64 win_end   = std::numeric_limits<qint64>::min();
                for (auto it = state->by_symbol.cbegin();
                     it != state->by_symbol.cend(); ++it) {
                    if (it.value().isEmpty()) continue;
                    for (auto bit = it.value().cbegin();
                         bit != it.value().cend(); ++bit) {
                        win_start = std::min(win_start, bit.key());
                        win_end   = std::max(win_end,   bit.key());
                    }
                }
                if (win_start > win_end) {
                    // No real bars at all — use today's NYSE session.
                    const auto [open_ms, close_ms] = nyse_session_today_utc_ms();
                    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
                    win_start = open_ms;
                    win_end   = std::min(close_ms, now_ms);
                }
                constexpr qint64 kMinMs = 60LL * 1000;
                for (auto it = state->by_symbol.begin();
                     it != state->by_symbol.end(); ++it) {
                    if (!it.value().isEmpty()) continue;
                    const double px = price_hint.value(it.key(), 1.0);
                    for (qint64 t = win_start; t <= win_end; t += kMinMs) {
                        it.value().insert(t, px);
                    }
                }

                // All symbols reported. Union timestamps across every symbol —
                // a NAV point exists wherever at least one symbol has a bar.
                // For missing symbols at a timestamp, fall back to that
                // symbol's most-recent prior bar (matches the "last trade
                // price" behaviour of any live ticker).
                QSet<qint64> all_ts_set;
                for (auto it = state->by_symbol.cbegin();
                     it != state->by_symbol.cend(); ++it) {
                    for (auto bit = it.value().cbegin();
                         bit != it.value().cend(); ++bit) {
                        all_ts_set.insert(bit.key());
                    }
                }
                QVector<qint64> all_ts(all_ts_set.cbegin(), all_ts_set.cend());
                std::sort(all_ts.begin(), all_ts.end());

                // For each symbol, walk its sorted bars and carry the most
                // recent close forward to the next aggregate timestamp.
                QHash<QString, QVector<QPair<qint64, double>>> sorted_bars;
                for (auto it = state->by_symbol.cbegin();
                     it != state->by_symbol.cend(); ++it) {
                    QVector<QPair<qint64, double>> v;
                    v.reserve(it.value().size());
                    for (auto bit = it.value().cbegin();
                         bit != it.value().cend(); ++bit) {
                        v.append({bit.key(), bit.value()});
                    }
                    std::sort(v.begin(), v.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first; });
                    sorted_bars[it.key()] = std::move(v);
                }

                QVector<qint64> ts_ms;
                QVector<double> navs;
                ts_ms.reserve(all_ts.size());
                navs.reserve(all_ts.size());

                // Back-fill baseline: seed every symbol with its first bar so a
                // position contributes to NAV at union timestamps before its
                // first bar arrives. Without this, US stocks contribute zero
                // during pre-market while the cash fallback's bars are present,
                // producing a "cash-only NAV" flat segment that steps up as
                // each stock's first bar lands at 09:30 ET.
                QHash<QString, int> cursor;
                QHash<QString, double> last_close;
                for (auto it = sorted_bars.cbegin(); it != sorted_bars.cend(); ++it) {
                    cursor[it.key()] = 0;
                    if (!it.value().isEmpty())
                        last_close[it.key()] = it.value().first().second;
                }

                for (qint64 t : all_ts) {
                    double nav = 0;
                    for (auto it = sorted_bars.cbegin(); it != sorted_bars.cend(); ++it) {
                        const QString& s2 = it.key();
                        const auto& v = it.value();
                        // Advance cursor while next bar <= t
                        int& c = cursor[s2];
                        while (c < v.size() && v[c].first <= t) {
                            last_close[s2] = v[c].second;
                            ++c;
                        }
                        nav += qty_by_symbol.value(s2) * last_close.value(s2);
                    }
                    ts_ms.append(t);
                    navs.append(nav);
                }

                LOG_INFO("PortfolioSvc",
                         QString("Aggregate intraday %1: %2 points across %3 symbols")
                             .arg(portfolio_id).arg(ts_ms.size()).arg(sorted_bars.size()));
                emit self->portfolio_intraday_loaded(portfolio_id, ts_ms, navs);
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

// ── Risk-free rate (^TNX — CBOE 10-year Treasury yield index) ────────────────
//
// Yahoo quotes ^TNX as the yield in percent (4.28 ⇒ 4.28%), so the existing
// daemon quote path serves the number directly — no API key, no separate
// provider. This replaced a FRED fetch whose API key was hardcoded in this
// file as a string literal.

void PortfolioService::fetch_risk_free_rate() {
    // Check 24h cache in SettingsRepository
    auto& settings = SettingsRepository::instance();
    const qint64 now_secs = QDateTime::currentSecsSinceEpoch();

    auto ts_r = settings.get("portfolio.rf_rate_timestamp");
    auto val_r = settings.get("portfolio.rf_rate_value");
    if (ts_r.is_ok() && val_r.is_ok()) {
        bool ts_ok = false, val_ok = false;
        const qint64 cached_ts = ts_r.value().toLongLong(&ts_ok);
        const double cached_val = val_r.value().toDouble(&val_ok);
        if (ts_ok && val_ok && (now_secs - cached_ts) < 86400) {
            // Cache still valid — use stored value
            rf_rate_ = cached_val;
            emit risk_free_rate_loaded(rf_rate_);
            return;
        }
    }

    QJsonObject payload;
    payload["symbol"] = QStringLiteral("^TNX");

    QPointer<PortfolioService> self = this;
    python::PythonWorker::instance().submit(
        "quote", payload,
        [self, now_secs](bool ok, QJsonObject obj, QString err) {
            if (!self)
                return;
            double rate = self->rf_rate_; // keep the last known value on failure
            const double px = obj.value("current_price").toDouble(obj.value("price").toDouble(0.0));
            // Sanity band: the 10y yield lives in low single digits. A parse
            // artifact (0, or a mis-scaled 42.8) must not become the Sharpe
            // hurdle for every portfolio.
            if (ok && px > 0.1 && px < 25.0) {
                rate = px / 100.0;
                auto& settings = SettingsRepository::instance();
                settings.set("portfolio.rf_rate_timestamp", QString::number(now_secs));
                settings.set("portfolio.rf_rate_value", QString::number(rate, 'f', 6));
            } else {
                LOG_WARN("PortfolioSvc", QString("^TNX risk-free fetch unusable (px=%1): %2")
                                             .arg(px)
                                             .arg(err.left(120)));
            }
            self->rf_rate_ = rate;
            emit self->risk_free_rate_loaded(rate);
        },
        python::PythonWorker::kNetworkActionTimeoutMs);
}

// ── Metrics computation (async, P8-safe) ─────────────────────────────────────

void PortfolioService::compute_metrics(const portfolio::PortfolioSummary& summary) {
    if (summary.holdings.isEmpty()) {
        emit metrics_computed({});
        return;
    }

    portfolio::ComputedMetrics metrics;

    // ── Concentration top-3 ───────────────────────────────────────────────────
    QVector<double> weights;
    weights.reserve(summary.holdings.size());
    for (const auto& h : summary.holdings)
        weights.append(h.weight);
    std::sort(weights.begin(), weights.end(), std::greater<>());
    double conc = 0;
    for (qsizetype i = 0; i < std::min(qsizetype{3}, weights.size()); ++i)
        conc += weights[i];
    metrics.concentration_top3 = conc;
    metrics.risk_score = std::min(conc / 80.0, 1.0) * 50.0; // concentration-only baseline

    // ── Load snapshots synchronously for time-series metrics ─────────────────
    // (this runs on the calling thread — compute_metrics is always called from
    //  the UI thread after summary_loaded, so we keep computation fast by
    //  loading snapshots from SQLite which is sub-millisecond for <365 rows)
    auto snap_r = PortfolioRepository::instance().get_snapshots(summary.portfolio.id, 365);
    if (snap_r.is_err() || snap_r.value().size() < 3) {
        // Trigger an async backfill so the next compute_metrics call has data.
        // This is one-shot per process to avoid hammering yfinance — once we've
        // attempted, the user can manually re-trigger via re-import.
        if (!backfill_attempted_.contains(summary.portfolio.id)) {
            backfill_attempted_.insert(summary.portfolio.id);
            QPointer<PortfolioService> self = this;
            const QString pid = summary.portfolio.id;
            QMetaObject::invokeMethod(this, [self, pid]() {
                if (self) self->backfill_history(pid, "1y");
            }, Qt::QueuedConnection);
        }
        // No return series → no series metrics. The old fallback annualised
        // the dispersion of the holdings' day-changes ACROSS the portfolio —
        // cross-sectional spread, not volatility, double-scaled on top —
        // and fed it into Sharpe, VaR and the risk score. The views promise
        // "engine or dash"; the concentration figure is the only thing this
        // situation can honestly state.
        emit metrics_computed(metrics);
        return;
    }

    // ── Build daily return series from snapshots ──────────────────────────────
    auto snaps = snap_r.value();
    // Sort ascending by date
    std::sort(snaps.begin(), snaps.end(),
              [](const auto& a, const auto& b) { return a.snapshot_date < b.snapshot_date; });

    // Flow-adjusted: raw NAV differences contain the user's deposits and
    // withdrawals, and a single funding day read as a +100% "return" is
    // enough to dominate every statistic computed below. adj is index-aligned
    // with snapshot pairs (adj[i-1] pairs snaps[i-1], snaps[i]) so the beta
    // regression can keep its date alignment; NaN marks uncomputable segments.
    // A FAILED transaction read is not "no transactions" — computing
    // flow-blind statistics on a DB hiccup would silently reintroduce the
    // deposit-as-return bug, so the series metrics sit this tick out.
    bool txns_ok = false;
    const QVector<portfolio::Transaction> txns = all_transactions(summary.portfolio.id, &txns_ok);
    if (!txns_ok) {
        emit metrics_computed(metrics);
        return;
    }
    const QVector<double> adj = portfolio::flow_adjusted_returns(snaps, txns, summary.fx_rates);
    QVector<double> port_returns; // daily flow-adjusted returns (%)
    port_returns.reserve(adj.size());
    for (const double r : adj) {
        if (!std::isnan(r))
            port_returns.append(r);
    }

    if (port_returns.size() < 2) {
        emit metrics_computed(metrics);
        return;
    }

    const int n = port_returns.size();
    metrics.return_days = n;

    // ── Mean and std-dev ──────────────────────────────────────────────────────
    const double mean = std::accumulate(port_returns.begin(), port_returns.end(), 0.0) / n;
    double sum_sq = 0;
    for (const double r : port_returns)
        sum_sq += (r - mean) * (r - mean);
    const double daily_vol = std::sqrt(sum_sq / (n - 1)); // sample std-dev
    const double ann_vol = daily_vol * std::sqrt(252.0);
    metrics.volatility = ann_vol; // already in %

    // ── Sharpe ratio (annualised) ─────────────────────────────────────────────
    // rf_rate_ = live 10y yield, annual decimal (e.g. 0.043); daily %.
    const double rf_daily = rf_rate_ / 252.0 * 100.0;
    if (daily_vol > 1e-6)
        metrics.sharpe = ((mean - rf_daily) / daily_vol) * std::sqrt(252.0);

    // ── Sortino (annualised) ──────────────────────────────────────────────────
    // Downside deviation over the FULL sample against the risk-free MAR.
    // Dividing by the count of down days alone would understate the ratio,
    // increasingly so the fewer down days there are.
    {
        double downside_sq = 0;
        for (const double r : port_returns) {
            const double d = std::min(r - rf_daily, 0.0);
            downside_sq += d * d;
        }
        const double downside_dev = std::sqrt(downside_sq / n);
        if (downside_dev > 1e-6)
            metrics.sortino = ((mean - rf_daily) / downside_dev) * std::sqrt(252.0);
    }

    // ── Max drawdown ──────────────────────────────────────────────────────────
    // Measured on the growth index chained from flow-adjusted returns, not on
    // raw NAV — a withdrawal is not a crash, and a deposit must not paper
    // over a real one.
    {
        double index = 1.0;
        double peak = 1.0;
        double max_dd = 0.0;
        for (const double r : port_returns) {
            index *= 1.0 + r / 100.0;
            peak = std::max(peak, index);
            if (peak > 1e-12)
                max_dd = std::min(max_dd, (index - peak) / peak * 100.0);
        }
        metrics.max_drawdown = max_dd; // negative %
    }

    // ── Beta vs SPY (OLS regression on aligned date windows) ─────────────────
    // Build SPY daily return series from cached closes, aligned to snapshot dates.
    if (spy_closes_cache_.size() >= 2 && spy_dates_cache_.size() == spy_closes_cache_.size()) {
        // Build a date→close map for O(1) lookup
        QHash<QString, double> spy_map;
        spy_map.reserve(spy_dates_cache_.size());
        for (int i = 0; i < spy_dates_cache_.size(); ++i)
            spy_map[spy_dates_cache_[i]] = spy_closes_cache_[i];

        // For each consecutive snapshot pair, find SPY return for the same day
        QVector<double> spy_aligned;
        QVector<double> port_aligned;
        spy_aligned.reserve(snaps.size() - 1);
        port_aligned.reserve(snaps.size() - 1);

        for (int i = 1; i < snaps.size(); ++i) {
            const QString date = snaps[i].snapshot_date;
            if (!spy_map.contains(date))
                continue;
            // Find previous available SPY close
            const QString prev_date = snaps[i - 1].snapshot_date;
            if (!spy_map.contains(prev_date))
                continue;

            const double spy_prev = spy_map[prev_date];
            const double spy_curr = spy_map[date];
            if (spy_prev < 1e-6)
                continue;

            // Same flow-adjusted series the other statistics use — a deposit
            // day must not enter the regression as portfolio "return".
            const double port_ret = adj.value(i - 1, std::numeric_limits<double>::quiet_NaN());
            if (std::isnan(port_ret))
                continue;
            const double spy_ret = (spy_curr - spy_prev) / spy_prev * 100.0;
            spy_aligned.append(spy_ret);
            port_aligned.append(port_ret);
        }

        const int m = spy_aligned.size();
        if (m >= 5) {
            // OLS: beta = cov(port, spy) / var(spy)
            const double spy_mean = std::accumulate(spy_aligned.begin(), spy_aligned.end(), 0.0) / m;
            const double port_mean = std::accumulate(port_aligned.begin(), port_aligned.end(), 0.0) / m;
            double cov = 0.0, var_spy = 0.0;
            for (int i = 0; i < m; ++i) {
                cov += (port_aligned[i] - port_mean) * (spy_aligned[i] - spy_mean);
                var_spy += (spy_aligned[i] - spy_mean) * (spy_aligned[i] - spy_mean);
            }
            if (var_spy > 1e-10) {
                metrics.beta = cov / var_spy;
                // Alpha is the OLS intercept, annualised: the average daily
                // return not explained by the market exposure. It exists only
                // together with the regression that defines it — the old
                // screens showed an "alpha" computed against a hardcoded 8%.
                const double alpha_daily = port_mean - *metrics.beta * spy_mean; // %/day
                metrics.alpha = alpha_daily * 252.0;
            }
        }
    }

    // ── VaR 95% and CVaR 95% (historical simulation) ─────────────────────────
    // Sort returns ascending; VaR = worst 5th percentile; CVaR = mean of tail.
    if (summary.total_market_value > 0 && !port_returns.isEmpty()) {
        QVector<double> sorted_rets = port_returns;
        std::sort(sorted_rets.begin(), sorted_rets.end());
        const int tail_count = std::max(1, static_cast<int>(std::floor(sorted_rets.size() * 0.05)));
        // VaR: loss at 95th percentile (positive value = amount at risk)
        const double var_pct = -sorted_rets[tail_count - 1]; // worst 5th pct return (%)
        metrics.var_95 = summary.total_market_value * std::max(var_pct, 0.0) / 100.0;
        // CVaR: expected loss beyond VaR (average of worst tail_count returns)
        double tail_sum = 0.0;
        for (int i = 0; i < tail_count; ++i)
            tail_sum += sorted_rets[i];
        const double cvar_pct = -(tail_sum / tail_count);
        metrics.cvar_95 = summary.total_market_value * std::max(cvar_pct, 0.0) / 100.0;
    }

    // ── Composite risk score (0-100) ─────────────────────────────────────────
    {
        const double vol_score = std::min(ann_vol / 40.0, 1.0) * 30.0;
        const double conc_score = std::min(conc / 80.0, 1.0) * 25.0;
        const double dd_score = std::min(std::abs(metrics.max_drawdown.value_or(0.0)) / 50.0, 1.0) * 25.0;
        const double beta_val = metrics.beta.value_or(1.0);
        const double beta_score = std::min(std::abs(beta_val) / 2.0, 1.0) * 20.0;
        metrics.risk_score = vol_score + conc_score + dd_score + beta_score;
    }

    emit metrics_computed(metrics);
}

// ── Import / Export ──────────────────────────────────────────────────────────

void PortfolioService::export_csv(const QString& portfolio_id, const QString& file_path) {
    auto assets_r = PortfolioRepository::instance().get_assets(portfolio_id);
    auto portfolio_r = PortfolioRepository::instance().get_portfolio(portfolio_id);
    auto txns_r = PortfolioRepository::instance().get_transactions(portfolio_id, /*limit=*/0);

    if (assets_r.is_err() || portfolio_r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Export CSV failed: cannot load data");
        return;
    }

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("PortfolioSvc", "Cannot open file for writing: " + file_path);
        return;
    }

    QTextStream out(&file);
    auto& p = portfolio_r.value();
    out << "# Portfolio: " << p.name << "\n";
    out << "# Owner: " << p.owner << "\n";
    out << "# Currency: " << p.currency << "\n";
    out << "# Exported: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n\n";

    // Holdings section
    out << "## HOLDINGS\n";
    out << "Symbol,Quantity,AvgBuyPrice,CostBasis\n";
    for (const auto& a : assets_r.value()) {
        out << a.symbol << "," << a.quantity << "," << a.avg_buy_price << "," << (a.quantity * a.avg_buy_price) << "\n";
    }

    // Transactions section
    if (txns_r.is_ok() && !txns_r.value().isEmpty()) {
        out << "\n## TRANSACTIONS\n";
        out << "Date,Symbol,Type,Quantity,Price,TotalValue,Notes\n";
        for (const auto& t : txns_r.value()) {
            out << t.transaction_date << "," << t.symbol << "," << t.transaction_type << "," << t.quantity << ","
                << t.price << "," << t.total_value << ",\""
                << QString(t.notes).replace(QLatin1Char('"'), QStringLiteral("\"\"")) << "\"\n";
        }
    }

    file.close();
    LOG_INFO("PortfolioSvc", "Exported CSV to " + file_path);
    emit export_complete(file_path);
}

void PortfolioService::export_json(const QString& portfolio_id, const QString& file_path) {
    auto portfolio_r = PortfolioRepository::instance().get_portfolio(portfolio_id);
    auto txns_r = PortfolioRepository::instance().get_transactions(portfolio_id, /*limit=*/0);

    if (portfolio_r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Export JSON failed: cannot load portfolio");
        return;
    }

    auto& p = portfolio_r.value();
    QJsonObject root;
    root["format_version"] = "1.0";
    root["portfolio_name"] = p.name;
    root["owner"] = p.owner;
    root["currency"] = p.currency;
    root["export_date"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonArray txn_arr;
    if (txns_r.is_ok()) {
        for (const auto& t : txns_r.value()) {
            QJsonObject obj;
            obj["date"] = t.transaction_date;
            obj["symbol"] = t.symbol;
            obj["type"] = t.transaction_type;
            obj["quantity"] = t.quantity;
            obj["price"] = t.price;
            obj["total_value"] = t.total_value;
            obj["notes"] = t.notes;
            txn_arr.append(obj);
        }
    }
    root["transactions"] = txn_arr;

    QFile file(file_path);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("PortfolioSvc", "Cannot open file for writing: " + file_path);
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO("PortfolioSvc", "Exported JSON to " + file_path);
    emit export_complete(file_path);
}

void PortfolioService::import_json(const QString& file_path, portfolio::ImportMode mode,
                                   const QString& merge_target_id) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit import_complete({"", "", 0, {"Cannot open file: " + file_path}});
        return;
    }

    QJsonParseError parse_err;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse_err);
    file.close();

    if (doc.isNull()) {
        emit import_complete({"", "", 0, {"Invalid JSON: " + parse_err.errorString()}});
        return;
    }

    auto root = doc.object();

    // Schema validation — the importer only accepts the terminal's own export format:
    //   { "portfolio_name": "...", "currency": "...", "transactions": [ {date,symbol,type,quantity,price}, ... ] }
    // Reject anything else up front so we don't create empty/mis-named portfolios.
    const QString schema_msg =
        "Unsupported JSON format. Expected the terminal's export format with fields "
        "'portfolio_name' (string) and 'transactions' (array of {date, symbol, type, quantity, price}). "
        "Holdings-only snapshots are not supported — convert each holding to a BUY transaction first.";

    if (!root.contains("portfolio_name") || !root.value("portfolio_name").isString() ||
        root.value("portfolio_name").toString().trimmed().isEmpty()) {
        emit import_complete({"", "", 0, {schema_msg}});
        LOG_ERROR("PortfolioSvc", "Import rejected: missing/invalid 'portfolio_name'");
        return;
    }
    if (!root.contains("transactions") || !root.value("transactions").isArray()) {
        emit import_complete({"", "", 0, {schema_msg}});
        LOG_ERROR("PortfolioSvc", "Import rejected: missing/invalid 'transactions' array");
        return;
    }

    QString name = root["portfolio_name"].toString();
    QString owner = root["owner"].toString("");
    QString currency = root["currency"].toString("USD");
    auto txn_arr = root["transactions"].toArray();

    if (txn_arr.isEmpty()) {
        emit import_complete({"", name, 0, {"No transactions found in file. " + schema_msg}});
        LOG_ERROR("PortfolioSvc", "Import rejected: 'transactions' array is empty");
        return;
    }

    // Collect symbol → sector mapping from any hints the file provides:
    //   1. top-level "holdings[]" (legacy broker-export format) — symbol + sector
    //   2. per-transaction "sector" field
    // Either/both populate an authoritative override we hand to SectorResolver
    // so the Sectors tab is correct without waiting on a yfinance round-trip.
    QHash<QString, QString> sector_hints;
    if (root.contains("holdings") && root.value("holdings").isArray()) {
        for (const auto& v : root.value("holdings").toArray()) {
            auto obj = v.toObject();
            QString sym = obj.value("symbol").toString().trimmed().toUpper();
            QString sec = obj.value("sector").toString().trimmed();
            if (!sym.isEmpty() && !sec.isEmpty())
                sector_hints.insert(sym, sec);
        }
    }
    for (const auto& v : txn_arr) {
        auto obj = v.toObject();
        QString sym = obj.value("symbol").toString().trimmed().toUpper();
        QString sec = obj.value("sector").toString().trimmed();
        if (!sym.isEmpty() && !sec.isEmpty() && !sector_hints.contains(sym))
            sector_hints.insert(sym, sec);
    }

    auto& repo = PortfolioRepository::instance();
    QString target_id;

    if (mode == portfolio::ImportMode::New) {
        auto r = repo.create_portfolio(name, owner, currency);
        if (r.is_err()) {
            emit import_complete({"", name, 0, {"Failed to create portfolio: " + QString::fromStdString(r.error())}});
            return;
        }
        target_id = r.value();
    } else {
        target_id = merge_target_id;
        if (target_id.isEmpty()) {
            emit import_complete({"", "", 0, {"No merge target specified"}});
            return;
        }
    }

    int replayed = 0;
    QStringList errors;

    // Record every transaction, then derive positions by replaying each
    // symbol's log through the ledger. DIVIDEND and SPLIT are first-class now
    // — the old importer dropped them, which left split positions off by the
    // split factor and dividend income out of every total. Order within the
    // file doesn't matter: the replay sorts chronologically.
    QSet<QString> touched;
    for (const auto& val : txn_arr) {
        auto obj = val.toObject();
        const QString type = obj["type"].toString();
        if (type != "BUY" && type != "SELL" && type != "DIVIDEND" && type != "SPLIT") {
            errors.append(QString("%1 %2: unsupported type").arg(type, obj["symbol"].toString()));
            continue;
        }
        const QString sym = obj["symbol"].toString();
        const double qty = obj["quantity"].toDouble();
        const double price = obj["price"].toDouble();
        // An empty date would string-sort before every dated row and replay
        // first — an undated SELL would clamp against zero held. Default to
        // now, matching what the recording paths do.
        QString date = obj["date"].toString().trimmed();
        if (date.isEmpty())
            date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        if (sym.trimmed().isEmpty() || qty <= 0) {
            errors.append(QString("%1 %2: missing symbol or non-positive quantity").arg(type, sym));
            continue;
        }
        auto r = repo.add_transaction(target_id, sym, type, qty, price, date, obj["notes"].toString());
        if (r.is_err()) {
            errors.append(QString("%1 %2: %3").arg(type, sym, QString::fromStdString(r.error())));
            continue;
        }
        touched.insert(sym.toUpper());
        ++replayed;
    }
    for (const QString& sym : std::as_const(touched)) {
        const auto pos = rebuild_position(target_id, sym);
        for (const auto& w : pos.warnings)
            errors.append(sym + ": " + w);
        // Sector hints apply to symbols that still hold a position.
        const QString hint = sector_hints.value(sym);
        if (!hint.isEmpty() && pos.is_open())
            repo.set_asset_sector(target_id, sym, hint);
    }

    // Seed SectorResolver with authoritative mapping from the import file,
    // and kick off async resolution for any symbols that had no hint.
    for (auto it = sector_hints.constBegin(); it != sector_hints.constEnd(); ++it)
        SectorResolver::instance().remember(it.key(), it.value());

    QStringList unresolved;
    if (auto assets = repo.get_assets(target_id); assets.is_ok()) {
        for (const auto& a : assets.value())
            if (a.sector.isEmpty())
                unresolved << a.symbol;
    }
    if (!unresolved.isEmpty())
        SectorResolver::instance().prefetch(unresolved);

    invalidate_cache(target_id);
    load_portfolios();

    emit import_complete({target_id, name, replayed, errors});
    LOG_INFO("PortfolioSvc",
             QString("Imported %1 transactions into %2, %3 errors").arg(replayed).arg(target_id).arg(errors.size()));

    // Backfill 1y of historical NAV from yfinance so Beta and MDD have data
    // immediately. Async — fires history_backfilled when done; the screen's
    // metrics_computed handler will already have reloaded snapshots once the
    // user refreshes (or on next compute_metrics call).
    backfill_history(target_id, "1y");
}

// ── Snapshots ────────────────────────────────────────────────────────────────

void PortfolioService::load_snapshots(const QString& portfolio_id, int days) {
    auto r = PortfolioRepository::instance().get_snapshots(portfolio_id, days);
    if (r.is_ok()) {
        emit snapshots_loaded(portfolio_id, r.value());
    } else {
        LOG_WARN("PortfolioSvc", "Failed to load snapshots: " + QString::fromStdString(r.error()));
    }
}

void PortfolioService::backfill_history(const QString& portfolio_id, const QString& period) {
    if (portfolio_id.isEmpty())
        return;

    auto& repo = PortfolioRepository::instance();

    // The transaction log drives the reconstruction: each date is valued with
    // the quantities the log states were held ON that date. The old model
    // (today's quantities × adjusted closes, computed daemon-side) put a
    // phantom NAV cliff at every date where the share count has since changed
    // — a -50%/+100% round trip in the return series at each preserved live
    // row, which exploded volatility, VaR and drawdown.
    auto txns_r = repo.get_transactions(portfolio_id, 1000000);
    QHash<QString, QVector<portfolio::Transaction>> txns_by_symbol;
    if (txns_r.is_ok()) {
        for (const auto& t : txns_r.value())
            txns_by_symbol[t.symbol.toUpper()].append(t);
    }

    // v049 synthesized opening BUYs for every pre-ledger holding, so a held
    // position always has transactions. A row without any (direct DB edit)
    // is skipped and logged rather than guessed at.
    auto assets_r = repo.get_assets(portfolio_id);
    if (assets_r.is_ok()) {
        for (const auto& a : assets_r.value()) {
            if (a.quantity > 0 && !txns_by_symbol.contains(a.symbol.toUpper()))
                LOG_WARN("PortfolioSvc", QString("backfill_history: %1 has a holdings row but no "
                                                 "transactions — excluded from reconstructed NAV")
                                             .arg(a.symbol));
        }
    }
    if (txns_by_symbol.isEmpty()) {
        emit history_backfilled(portfolio_id, 0);
        return;
    }

    // FX: reconstructed NAV must be in the portfolio currency at EACH date,
    // so foreign holdings convert by that date's FX close — the pair series
    // ride the same daemon request. Converting history at today's rate would
    // paint FX moves into (or out of) every past NAV.
    QString port_ccy = QStringLiteral("USD");
    if (auto pr = repo.get_portfolio(portfolio_id); pr.is_ok())
        port_ccy = portfolio::fx_price_factor(pr.value().currency).first;
    // `unknown` is recorded HERE, where the fact is read. Deriving it later
    // inside the async callback would re-read the cache across a network
    // round-trip: a concurrent discovery filling that cache would silence the
    // warning while this run still values the symbol at face value.
    struct FxBinding {
        QString pair;         // empty = no conversion needed
        double factor = 1.0;  // sub-unit normalisation (GBp → GBP)
        bool unknown = false; // trading currency not yet discovered
    };
    QHash<QString, FxBinding> fx_of_symbol;
    QSet<QString> fx_pairs;
    for (auto it = txns_by_symbol.cbegin(); it != txns_by_symbol.cend(); ++it) {
        const QString raw = cached_symbol_currency(it.key());
        const auto [pair, factor] = portfolio::fx_pair_for(raw, port_ccy);
        fx_of_symbol.insert(it.key(), {pair, factor, raw.isEmpty()});
        if (!pair.isEmpty())
            fx_pairs.insert(pair);
    }

    // Reconstructing a year of history at face value would persist the error;
    // discovery is cheap and one-shot, so wait for it rather than bake it in.
    // import_json calls backfill_history before any summary has ever run, so
    // this is the ONLY thing that starts discovery on a fresh import.
    QStringList unknown_ccy;
    for (auto it = fx_of_symbol.cbegin(); it != fx_of_symbol.cend(); ++it)
        if (it.value().unknown)
            unknown_ccy.append(it.key());
    if (!unknown_ccy.isEmpty() && !backfill_awaiting_fx_.contains(portfolio_id)) {
        backfill_awaiting_fx_.insert(portfolio_id);
        ensure_symbol_currencies(unknown_ccy);
        QPointer<PortfolioService> self_retry = this;
        const QString pid = portfolio_id;
        const QString per = period;
        QTimer::singleShot(4000, this, [self_retry, pid, per]() {
            if (self_retry)
                self_retry->backfill_history(pid, per);
        });
        LOG_INFO("PortfolioSvc",
                 QString("backfill_history: waiting on currency discovery for %1 symbol(s) in %2")
                     .arg(unknown_ccy.size())
                     .arg(portfolio_id));
        return;
    }
    backfill_awaiting_fx_.remove(portfolio_id);

    QJsonArray symbols_arr;
    for (auto it = txns_by_symbol.cbegin(); it != txns_by_symbol.cend(); ++it)
        symbols_arr.append(it.key());
    for (const QString& p : std::as_const(fx_pairs))
        symbols_arr.append(p);
    QJsonObject payload;
    payload["symbols"] = symbols_arr;
    payload["period"] = period;

    QPointer<PortfolioService> self = this;
    python::PythonWorker::instance().submit("portfolio_closes_history", payload,
        [self, portfolio_id, txns_by_symbol, fx_of_symbol](bool ok, QJsonObject obj, QString err) {
        if (!self)
            return;
        if (!ok || obj.contains("error")) {
            LOG_WARN("PortfolioSvc", QString("backfill_history failed for %1: %2")
                                         .arg(portfolio_id,
                                              ok ? obj["error"].toString().left(200) : err.left(200)));
            emit self->history_backfilled(portfolio_id, 0);
            return;
        }

        // Per-symbol close calendars (raw closes, YYYY-MM-DD keys) plus a
        // union calendar to walk. Symbols yfinance has no data for (money
        // market funds, delisted names) simply contribute nothing dated —
        // same behaviour the live valuation has for them.
        const QJsonObject closes_obj = obj["closes"].toObject();
        const auto parse_series = [&closes_obj](const QString& key, QSet<QString>* dates_out) {
            QVector<QPair<QString, double>> out;
            const QJsonArray series = closes_obj[key].toArray();
            out.reserve(series.size());
            for (const auto& v : series) {
                const QJsonArray pair = v.toArray();
                if (pair.size() != 2)
                    continue;
                const QString d = pair[0].toString();
                const double close = pair[1].toDouble();
                if (d.size() == 10 && close > 0) {
                    out.append({d, close});
                    if (dates_out)
                        dates_out->insert(d);
                }
            }
            std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            return out;
        };

        // FX pair calendars: cursor state shared by every symbol on the pair.
        // Pair closes do NOT extend the union calendar — an FX-only date has
        // no equity close to value.
        struct PairState {
            QVector<QPair<QString, double>> closes;
            int next = 0;
            double last = 0;
        };
        QHash<QString, PairState> pair_states;
        for (auto it = fx_of_symbol.cbegin(); it != fx_of_symbol.cend(); ++it) {
            const QString& pair = it.value().pair;
            if (pair.isEmpty() || pair_states.contains(pair))
                continue;
            PairState ps{.closes = parse_series(pair, nullptr)};
            if (ps.closes.isEmpty())
                LOG_WARN("PortfolioSvc",
                         QString("backfill_history: no FX history for %1 — holdings in that "
                                 "currency are missing from backfilled NAV").arg(pair));
            pair_states.insert(pair, std::move(ps));
        }

        struct SymbolState {
            portfolio::LedgerCursor cursor;
            QVector<QPair<QString, double>> closes; // sorted by date
            int next = 0;
            double last_close = 0;
            QString fx_pair;    // empty = portfolio currency
            double fx_factor = 1.0; // sub-unit normalisation (GBp → GBP)
        };
        std::vector<SymbolState> states;
        QSet<QString> date_set;
        for (auto it = txns_by_symbol.cbegin(); it != txns_by_symbol.cend(); ++it) {
            const FxBinding fx = fx_of_symbol.value(it.key());
            SymbolState st{.cursor = portfolio::LedgerCursor(it.value()),
                           .closes = parse_series(it.key(), &date_set),
                           .fx_pair = fx.pair,
                           .fx_factor = fx.factor};
            if (st.closes.isEmpty()) {
                LOG_WARN("PortfolioSvc",
                         QString("backfill_history: no close history for %1 — its value is "
                                 "missing from backfilled NAV").arg(it.key()));
                continue;
            }
            if (fx.unknown)
                LOG_WARN("PortfolioSvc",
                         QString("backfill_history: trading currency of %1 unknown — valued at "
                                 "face value in the portfolio currency").arg(it.key()));
            states.push_back(std::move(st));
        }

        QStringList dates(date_set.cbegin(), date_set.cend());
        std::sort(dates.begin(), dates.end());
        const QString today = QDate::currentDate().toString(Qt::ISODate);

        auto& repo = PortfolioRepository::instance();
        int written = 0;
        int skipped_live = 0;
        int skipped_partial = 0;
        for (const QString& d : dates) {
            // Today's row belongs to the live path — build_summary values it
            // from actual quotes and writes it as 'live'.
            if (d >= today)
                continue;
            // Advance FX pair cursors first so every symbol on a pair sees
            // this date's (or the last known) rate.
            for (auto pit = pair_states.begin(); pit != pair_states.end(); ++pit) {
                auto& ps = pit.value();
                while (ps.next < ps.closes.size() && ps.closes[ps.next].first <= d) {
                    ps.last = ps.closes[ps.next].second;
                    ++ps.next;
                }
            }
            double nav = 0;
            double cost = 0;
            bool any_open = false;
            bool complete = true; // every open position priced AND converted
            for (auto& st : states) {
                while (st.next < st.closes.size() && st.closes[st.next].first <= d) {
                    st.last_close = st.closes[st.next].second;
                    ++st.next;
                }
                st.cursor.advance_to(d);
                const auto& pos = st.cursor.position();
                if (pos.quantity > 1e-9 && st.last_close > 0) {
                    double rate = st.fx_factor;
                    if (!st.fx_pair.isEmpty()) {
                        const double fx = pair_states.value(st.fx_pair).last;
                        if (fx <= 0) {
                            // No rate at this date. Writing the row anyway
                            // would persist a NAV missing this holding —
                            // half a portfolio recorded as history.
                            complete = false;
                            continue;
                        }
                        rate *= fx;
                    }
                    nav += pos.quantity * st.last_close * rate;
                    cost += pos.quantity * pos.avg_cost * rate;
                    any_open = true;
                }
            }
            if (!any_open)
                continue; // before the first buy — a NAV of zero is not history
            if (!complete) {
                ++skipped_partial;
                continue; // a partial valuation is not this portfolio's history
            }
            const double pnl = nav - cost;
            const double pnl_pct = cost > 0 ? (pnl / cost) * 100.0 : 0.0;
            auto wr = repo.save_backfill_snapshot(portfolio_id, nav, cost, pnl, pnl_pct, d);
            if (wr.is_ok()) {
                if (wr.value() > 0)
                    ++written;
                else
                    ++skipped_live; // live row held its ground — by design
            }
        }

        LOG_INFO("PortfolioSvc",
                 QString("Backfilled %1 snapshots for %2 (%3 live rows preserved, %4 dates skipped "
                         "for an incomplete valuation)")
                     .arg(written)
                     .arg(portfolio_id)
                     .arg(skipped_live)
                     .arg(skipped_partial));
        self->invalidate_cache(portfolio_id);
        emit self->history_backfilled(portfolio_id, written);
    },
    python::PythonWorker::kComputeActionTimeoutMs);
}

// ── Portfolio analyst fundamentals ───────────────────────────────────────────

void PortfolioService::fetch_portfolio_fundamentals(const QString& portfolio_id) {
    if (portfolio_id.isEmpty()) return;
    // Guard: analyst data changes at most daily — skip if we already fetched
    // for this portfolio since the last holding change. Cleared by
    // invalidate_cache() so an add/sell re-fetches on the next summary load.
    // IMPORTANT: insert the guard only AFTER confirming we have live MV data.
    // load_summary emits summary_loaded twice: once from the disk-cache path
    // (before summary_cache_ is populated) and once from the live network
    // result. Inserting the guard on the first (stale) call would mark the
    // portfolio as fetched even though no fan-out was launched, blocking the
    // second (live) call that actually has MV weights available.
    if (fundamentals_fetched_.contains(portfolio_id)) {
        // Re-emit the cached fundamentals so the heatmap analyst panel
        // re-populates on a portfolio switch back to a previously-loaded
        // portfolio. Without this, the panel would keep showing whichever
        // portfolio was loaded most recently because the guard above
        // suppresses the re-fetch and no signal would otherwise fire.
        portfolio::PortfolioFundamentals cached;
        bool have_cached = false;
        {
            QMutexLocker lock(&cache_mutex_);
            auto it = fundamentals_cache_.constFind(portfolio_id);
            if (it != fundamentals_cache_.constEnd()) {
                cached = it.value();
                have_cached = true;
            }
        }
        if (have_cached)
            emit portfolio_fundamentals_loaded(portfolio_id, cached);
        return;
    }

    // Build per-symbol MV and current-price maps from the cached summary.
    // current_price is needed to convert per-share analyst targets into a
    // portfolio-level target NAV: target_mv_i = mv_i × (tgt_i / price_i).
    QHash<QString, double> mv_by_symbol;
    QHash<QString, double> price_by_symbol;
    double total_mv = 0;
    {
        QMutexLocker lock(&cache_mutex_);
        auto cit = summary_cache_.constFind(portfolio_id);
        if (cit != summary_cache_.cend()) {
            for (const auto& h : cit.value().summary.holdings) {
                const QString up = h.symbol.toUpper();
                mv_by_symbol[up]    = h.market_value;
                price_by_symbol[up] = h.current_price;
                total_mv += h.market_value;
            }
        }
    }
    // Only commit the guard once we know the fan-out will actually launch.
    if (mv_by_symbol.isEmpty() || total_mv <= 0) return;
    fundamentals_fetched_.insert(portfolio_id);

    // Per-symbol accumulator, shared across all callbacks.
    struct SymResult {
        double tgt_low   = 0;
        double tgt_mean  = 0;
        double tgt_high  = 0;
        double pe        = 0;
        double yield     = 0;
        double rec_score = 0;  // 1=strong_buy … 5=strong_sell, 0=missing
    };
    struct Accum {
        int pending = 0;
        QHash<QString, SymResult> results;
    };
    auto state = std::make_shared<Accum>();
    state->pending = mv_by_symbol.size();

    QPointer<PortfolioService> self = this;
    for (auto it = mv_by_symbol.cbegin(); it != mv_by_symbol.cend(); ++it) {
        const QString sym = it.key();
        QJsonObject payload;
        payload["symbol"] = sym;
        python::PythonWorker::instance().submit(
            "info", payload,
            [self, portfolio_id, sym, state, mv_by_symbol, price_by_symbol, total_mv](
                bool ok, QJsonObject obj, QString /*err*/) {
                if (!self) return;

                SymResult& r = state->results[sym];
                if (ok && !obj.contains("error")) {
                    r.tgt_low   = obj["target_low_price"].toDouble();
                    r.tgt_mean  = obj["target_mean_price"].toDouble();
                    r.tgt_high  = obj["target_high_price"].toDouble();
                    // Python "info" action uses key "pe_ratio" (mapped from trailingPE).
                    r.pe        = obj["pe_ratio"].toDouble();
                    // yfinance returns dividendYield as a fraction (e.g. 0.014)
                    r.yield     = obj["dividend_yield"].toDouble();

                    // Map recommendation_key to a 1-5 score (lower = more bullish).
                    const QString rec = obj["recommendation_key"].toString().toLower();
                    if      (rec == "strong_buy"  || rec == "strongbuy")    r.rec_score = 1;
                    else if (rec == "buy")                                   r.rec_score = 2;
                    else if (rec == "hold"        || rec == "neutral")      r.rec_score = 3;
                    else if (rec == "sell"        || rec == "underperform") r.rec_score = 4;
                    else if (rec == "strong_sell" || rec == "strongsell")   r.rec_score = 5;

                }

                if (--state->pending > 0) return;

                // All callbacks done — aggregate into portfolio-level fundamentals.
                //
                // TARGET NAV MATH:
                //   Analyst target is a per-share price. To get the portfolio-level
                //   target NAV we need qty_i × tgt_i = mv_i × (tgt_i / price_i).
                //   For uncovered holdings (ETFs, MMFs, no analyst target) we keep
                //   them at their current market value, so they contribute zero
                //   upside/downside — the conservative, honest interpretation.
                //   portfolio_tgt = Σ_covered(mv_i × tgt_i/price_i) + Σ_uncovered(mv_i)
                //
                // PE / YIELD / CONSENSUS use plain MV-weighted averages (dimensionless).
                portfolio::PortfolioFundamentals f;
                double tgt_low_sum = 0, tgt_mean_sum = 0, tgt_high_sum = 0;
                double covered_mv  = 0;
                double w_pe = 0, w_yield = 0, w_rec = 0;
                double cov_pe = 0, cov_yield = 0, cov_rec = 0;

                for (auto jt = state->results.cbegin(); jt != state->results.cend(); ++jt) {
                    const QString& s    = jt.key();
                    const SymResult& sr = jt.value();
                    const double mv     = mv_by_symbol.value(s, 0);
                    const double price  = price_by_symbol.value(s, 0);
                    const double w      = total_mv > 0 ? mv / total_mv : 0;

                    // Require all three target fields to be positive: some
                    // symbols return only tgt_mean with tgt_low/high = 0,
                    // which would push f.tgt_low below f.tgt_mean.
                    if (sr.tgt_low > 0 && sr.tgt_mean > 0 && sr.tgt_high > 0 && price > 0) {
                        tgt_low_sum  += mv * sr.tgt_low  / price;
                        tgt_mean_sum += mv * sr.tgt_mean / price;
                        tgt_high_sum += mv * sr.tgt_high / price;
                        covered_mv   += mv;
                    }
                    if (sr.pe    > 0) { w_pe    += w * sr.pe;        cov_pe    += w; }
                    if (sr.yield > 0) { w_yield += w * sr.yield;     cov_yield += w; }
                    if (sr.rec_score > 0) { w_rec += w * sr.rec_score; cov_rec   += w; }
                }

                if (covered_mv > 0) {
                    // Covered holdings at analyst target + uncovered at current value.
                    const double uncovered_mv = total_mv - covered_mv;
                    f.tgt_low  = tgt_low_sum  + uncovered_mv;
                    f.tgt_mean = tgt_mean_sum + uncovered_mv;
                    f.tgt_high = tgt_high_sum + uncovered_mv;
                    f.has_analyst_data = true;
                }
                if (cov_pe    > 0) f.pe_ratio  = w_pe    / cov_pe;
                if (cov_yield > 0) f.div_yield = w_yield / cov_yield;
                if (cov_rec   > 0) {
                    const double s = w_rec / cov_rec;
                    if      (s < 1.5) f.consensus = QStringLiteral("Strong Buy");
                    else if (s < 2.5) f.consensus = QStringLiteral("Buy");
                    else if (s < 3.5) f.consensus = QStringLiteral("Hold");
                    else if (s < 4.5) f.consensus = QStringLiteral("Sell");
                    else              f.consensus = QStringLiteral("Strong Sell");
                }

                {
                    QMutexLocker lock(&self->cache_mutex_);
                    self->fundamentals_cache_[portfolio_id] = f;
                }
                emit self->portfolio_fundamentals_loaded(portfolio_id, f);
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

// ── Peak high since entry (trailing-stop L%) ─────────────────────────────────

// static
QString PortfolioService::entry_date_of(const QString& first_purchase_date) {
    // Stored values come from SQLite's datetime('now') ("YYYY-MM-DD HH:MM:SS"),
    // from an ISO timestamp written by add_asset, or from an imported
    // date-only string. All three start with the date, so a left(10) parse
    // covers them; anything else is reported unparseable.
    const QString head = first_purchase_date.trimmed().left(10);
    const QDate d = QDate::fromString(head, Qt::ISODate);
    return d.isValid() ? d.toString(Qt::ISODate) : QString();
}

// static
QString PortfolioService::peak_key(const QString& symbol, const QString& entry_date) {
    return symbol.toUpper() + QLatin1Char('|') + entry_date;
}

double PortfolioService::cached_peak_high(const QString& symbol, const QString& first_purchase_date) {
    const QString key = peak_key(symbol, entry_date_of(first_purchase_date));
    QMutexLocker lock(&cache_mutex_);
    const auto it = peak_cache_.constFind(key);
    return it != peak_cache_.constEnd() ? it->high : 0.0;
}

void PortfolioService::raise_cached_peak(const QString& symbol, const QString& first_purchase_date, double price) {
    if (price <= 0)
        return;
    const QString key = peak_key(symbol, entry_date_of(first_purchase_date));
    QMutexLocker lock(&cache_mutex_);
    auto it = peak_cache_.find(key);
    if (it != peak_cache_.end() && price > it->high)
        it->high = price;
}

void PortfolioService::seed_peak_cache_from_summary(const portfolio::PortfolioSummary& summary) {
    QMutexLocker lock(&cache_mutex_);
    for (const auto& h : summary.holdings) {
        if (h.peak_price <= 0)
            continue;
        auto& entry = peak_cache_[peak_key(h.symbol, entry_date_of(h.first_purchase_date))];
        if (h.peak_price > entry.high)
            entry.high = h.peak_price;
    }
}

void PortfolioService::fetch_position_peaks(const QString& portfolio_id,
                                            const QVector<portfolio::PortfolioAsset>& assets) {
    if (portfolio_id.isEmpty() || assets.isEmpty())
        return;

    struct Req {
        QString symbol;
        QString key;
        QString entry;  // YYYY-MM-DD, empty when unparseable
    };

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVector<Req> pending;
    {
        QMutexLocker lock(&cache_mutex_);
        for (const auto& a : assets) {
            if (a.symbol.isEmpty())
                continue;
            const QString entry = entry_date_of(a.first_purchase_date);
            const QString key   = peak_key(a.symbol, entry);
            if (peak_inflight_.contains(key))
                continue;
            const auto it = peak_cache_.constFind(key);
            if (it != peak_cache_.constEnd() && now - it->fetched_at < kPeakTtlSec)
                continue;
            peak_inflight_.insert(key);
            pending.append({a.symbol, key, entry});
        }
    }
    if (pending.isEmpty())
        return;

    struct Accum {
        int pending = 0;
        QHash<QString, double> peaks; // symbol (as stored on the asset) → peak high
    };
    auto state = std::make_shared<Accum>();
    state->pending = pending.size();

    // yfinance's `end` is exclusive — ask through tomorrow so today's bar is
    // included on a live trading day.
    const QString end = QDate::currentDate().addDays(1).toString(Qt::ISODate);

    QPointer<PortfolioService> self = this;
    for (const auto& req : pending) {
        QJsonObject payload;
        payload["symbol"] = req.symbol;
        // Unparseable entry date → bounded 1y window rather than "max": an
        // unbounded peak from a decade ago would render an L% that has nothing
        // to do with the user's position.
        payload["period"] = req.entry.isEmpty()
                                ? QStringLiteral("1y")
                                : QStringLiteral("range:") + req.entry + QLatin1Char(':') + end;
        payload["interval"] = QStringLiteral("1d");

        python::PythonWorker::instance().submit(
            "historical_period", payload,
            [self, portfolio_id, req, state](bool ok, QJsonObject result, QString err) {
                if (!self)
                    return;

                double peak = 0;
                if (ok) {
                    // The daemon wraps a flat array under "_value".
                    const QJsonArray arr = result.contains("_value")
                                               ? result["_value"].toArray()
                                               : result["history"].toArray();
                    for (const auto& v : arr) {
                        const double high = v.toObject()["high"].toDouble();
                        if (high > peak)
                            peak = high;
                    }
                } else {
                    LOG_WARN("PortfolioSvc",
                             QString("Peak-high fetch failed for %1: %2").arg(req.symbol, err.left(200)));
                }

                {
                    QMutexLocker lock(&self->cache_mutex_);
                    self->peak_inflight_.remove(req.key);
                    auto& entry = self->peak_cache_[req.key];
                    // Stamp the attempt either way: a symbol yfinance has no
                    // history for (money-market funds, delisted tickers) must
                    // not re-fan-out on every 20 s tick. A failed attempt
                    // keeps whatever good value we had.
                    entry.fetched_at = QDateTime::currentSecsSinceEpoch();
                    if (peak > 0)
                        entry.high = peak;
                }

                if (peak > 0)
                    state->peaks.insert(req.symbol, peak);

                if (--state->pending > 0)
                    return;
                if (state->peaks.isEmpty())
                    return;

                // Patch the cached summary so a cache-hit emit (and the disk
                // snapshot written by the next build) carries the peaks
                // instead of dropping back to dashes.
                {
                    QMutexLocker lock(&self->cache_mutex_);
                    auto it = self->summary_cache_.find(portfolio_id);
                    if (it != self->summary_cache_.end()) {
                        for (auto& h : it->summary.holdings) {
                            const auto p = state->peaks.constFind(h.symbol);
                            if (p != state->peaks.constEnd())
                                portfolio::set_peak_high(h, p.value());
                        }
                    }
                }

                emit self->position_peaks_loaded(portfolio_id, state->peaks);
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

// ── FX ───────────────────────────────────────────────────────────────────────

namespace {
constexpr qint64 kSymbolCurrencyTtlSec = 30LL * 86400;    // listings don't change currency
constexpr qint64 kCurrencyUnknownTtlSec = 6LL * 3600;     // retry a failed lookup in 6h, not 20s
constexpr const char* kCurrencyUnknown = "?";
QString symbol_currency_key(const QString& symbol) {
    return QStringLiteral("symbol_currency:") + symbol.toUpper();
}
} // namespace

// static
QString PortfolioService::cached_symbol_currency(const QString& symbol) {
    const QString v = fincept::CacheManager::instance().try_get(symbol_currency_key(symbol)).value_or(QString());
    // kCurrencyUnknown is a NEGATIVE cache entry: "asked, got nothing". It
    // reads as unknown to callers while still suppressing the re-request, so
    // an ETF or delisted ticker with no info payload can't drive one full
    // web scrape per symbol every 20 s forever.
    return v == QLatin1String(kCurrencyUnknown) ? QString() : v;
}

void PortfolioService::ensure_symbol_currencies(const QStringList& symbols) {
    QStringList missing;
    {
        // Batch the cache probe: try_get is one SELECT per call, and this
        // runs per holding on every refresh tick. Any hit — including the
        // negative marker — means "don't ask again yet".
        QStringList keys;
        keys.reserve(symbols.size());
        for (const QString& s : symbols)
            keys.append(symbol_currency_key(s));
        const QHash<QString, QString> known = fincept::CacheManager::instance().multi_get(keys);

        QMutexLocker lock(&cache_mutex_);
        for (const QString& s : symbols) {
            const QString up = s.toUpper();
            if (currency_inflight_.contains(up) || known.contains(symbol_currency_key(up)))
                continue;
            currency_inflight_.insert(up);
            missing.append(up);
        }
    }
    QPointer<PortfolioService> self = this;
    auto pending = std::make_shared<int>(static_cast<int>(missing.size()));
    for (const QString& sym : std::as_const(missing)) {
        QJsonObject payload;
        payload["symbol"] = sym;
        python::PythonWorker::instance().submit(
            "info", payload,
            [self, sym, pending](bool ok, QJsonObject obj, QString /*err*/) {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->cache_mutex_);
                    self->currency_inflight_.remove(sym);
                }
                const QString ccy = obj.value("currency").toString();
                if (ok && !ccy.isEmpty()) {
                    fincept::CacheManager::instance().put(symbol_currency_key(sym), ccy,
                                                          kSymbolCurrencyTtlSec);
                } else {
                    // Remember the failure, briefly, so the retry is paced.
                    fincept::CacheManager::instance().put(symbol_currency_key(sym),
                                                          QString::fromLatin1(kCurrencyUnknown),
                                                          kCurrencyUnknownTtlSec);
                }
                if (--(*pending) == 0)
                    emit self->symbol_currencies_resolved();
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

// ── Cache control ────────────────────────────────────────────────────────────

void PortfolioService::invalidate_cache(const QString& portfolio_id) {
    QMutexLocker lock(&cache_mutex_);
    summary_cache_.remove(portfolio_id);
    fundamentals_fetched_.remove(portfolio_id);
    fundamentals_cache_.remove(portfolio_id);
}

} // namespace fincept::services
