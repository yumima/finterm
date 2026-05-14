// src/services/portfolio/PortfolioService.cpp
#include "services/portfolio/PortfolioService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"
#include "services/sectors/SectorResolver.h"
#include "services/util/DiskCache.h"
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

void PortfolioService::load_summary(const QString& portfolio_id) {
    // Check in-memory cache first (P11). 5-min TTL — short-circuits everything.
    {
        QMutexLocker lock(&cache_mutex_);
        auto it = summary_cache_.find(portfolio_id);
        if (it != summary_cache_.end()) {
            qint64 now = QDateTime::currentSecsSinceEpoch();
            if (now - it->timestamp < kCacheTtlSec) {
                emit summary_loaded(it->summary);
                return;
            }
        }
    }

    // Disk-cache hydration: emit the last-built summary from the previous
    // session immediately so the UI paints with real (if stale) numbers while
    // the quote refetch + recompute runs below. The cached summary carries
    // `from_cache=true` so the UI can flag it as stale until the live recompute
    // overwrites it.
    const auto cached_doc = portfolio_disk_cache().load(summary_filename(portfolio_id));
    if (cached_doc.isObject()) {
        auto summary = summary_from_json(cached_doc.object());
        if (!summary.portfolio.id.isEmpty()) {
            summary.from_cache = true;
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

void PortfolioService::build_summary(const QString& portfolio_id, const QVector<portfolio::PortfolioAsset>& assets,
                                     const portfolio::Portfolio& portfolio) {
    // Collect symbols for batch quote fetch
    QStringList symbols;
    symbols.reserve(assets.size());
    for (const auto& a : assets) {
        symbols.append(a.symbol);
    }

    // Use QPointer for safe async callback (P8)
    QPointer<PortfolioService> self = this;

    MarketDataService::instance().fetch_quotes(symbols, [self, portfolio_id, assets,
                                                         portfolio](bool ok, QVector<QuoteData> quotes) {
        if (!self)
            return;

        // Build quote lookup
        QHash<QString, QuoteData> quote_map;
        if (ok) {
            for (const auto& q : quotes)
                quote_map[q.symbol] = q;
        }

        portfolio::PortfolioSummary summary;
        summary.portfolio = portfolio;
        summary.holdings.reserve(assets.size());

        double total_mv = 0;
        double total_cost = 0;
        double total_day = 0;

        for (const auto& asset : assets) {
            portfolio::HoldingWithQuote h;
            h.symbol = asset.symbol;
            h.quantity = asset.quantity;
            h.avg_buy_price = asset.avg_buy_price;
            h.cost_basis = asset.quantity * asset.avg_buy_price;
            // Prefer stored sector; fall back to resolver cache (which may
            // populate async — see sector_resolved handler in constructor).
            h.sector = asset.sector.isEmpty()
                           ? SectorResolver::instance().sector_for(asset.symbol)
                           : asset.sector;

            auto it = quote_map.find(asset.symbol);
            if (it != quote_map.end()) {
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
            } else {
                // Fallback to avg buy price if no quote
                h.current_price = asset.avg_buy_price;
            }

            h.market_value = h.quantity * h.current_price;
            h.unrealized_pnl = h.market_value - h.cost_basis;
            h.unrealized_pnl_percent = (h.cost_basis > 0) ? (h.unrealized_pnl / h.cost_basis) * 100.0 : 0;

            total_mv += h.market_value;
            total_cost += h.cost_basis;
            total_day += h.day_change * h.quantity;

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

        // Save snapshot for performance history
        QString today = QDate::currentDate().toString(Qt::ISODate);
        PortfolioRepository::instance().save_snapshot(portfolio_id, summary.total_market_value,
                                                      summary.total_cost_basis, summary.total_unrealized_pnl,
                                                      summary.total_unrealized_pnl_percent, today);

        emit self->summary_loaded(summary);
    });
}

// ── Asset operations ─────────────────────────────────────────────────────────

void PortfolioService::add_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                                 const QString& date) {
    auto& repo = PortfolioRepository::instance();

    auto r = repo.add_asset(portfolio_id, symbol, qty, price, date);
    if (r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to add asset: " + QString::fromStdString(r.error()));
        return;
    }

    // Record transaction
    QString txn_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;
    repo.add_transaction(portfolio_id, symbol, "BUY", qty, price, txn_date);

    invalidate_cache(portfolio_id);
    emit asset_added(portfolio_id);
}

void PortfolioService::sell_asset(const QString& portfolio_id, const QString& symbol, double qty, double price,
                                  const QString& date) {
    auto& repo = PortfolioRepository::instance();

    // Get current holdings to update quantity
    auto assets_r = repo.get_assets(portfolio_id);
    if (assets_r.is_err()) {
        LOG_ERROR("PortfolioSvc", "Failed to get assets for sell: " + QString::fromStdString(assets_r.error()));
        return;
    }

    // Find the asset
    const portfolio::PortfolioAsset* found = nullptr;
    for (const auto& a : assets_r.value()) {
        if (a.symbol == symbol.toUpper()) {
            found = &a;
            break;
        }
    }

    if (!found) {
        LOG_ERROR("PortfolioSvc", QString("Asset %1 not found in portfolio %2").arg(symbol, portfolio_id));
        return;
    }

    double remaining = found->quantity - qty;
    if (remaining <= 0.0001) {
        // Full sell — remove asset
        repo.remove_asset(portfolio_id, symbol);
    } else {
        // Partial sell — keep same avg price
        repo.update_asset(portfolio_id, symbol, remaining, found->avg_buy_price);
    }

    // Record transaction
    QString txn_date = date.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : date;
    repo.add_transaction(portfolio_id, symbol, "SELL", qty, price, txn_date);

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

void PortfolioService::update_transaction(const QString& id, double qty, double price, const QString& date,
                                          const QString& notes) {
    PortfolioRepository::instance().update_transaction(id, qty, price, date, notes);
}

void PortfolioService::delete_transaction(const QString& id, const QString& portfolio_id) {
    PortfolioRepository::instance().delete_transaction(id);
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

// ── Risk-free rate (FRED DGS10) ───────────────────────────────────────────────

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

    // Fetch from FRED via inline Python (requests library)
    const QString code = QString(R"python(
import json, urllib.request
api_key = "9da0d86e0cf58f4a4023e5e686226d69"
url = (
    "https://api.stlouisfed.org/fred/series/observations"
    "?series_id=DGS10&api_key=" + api_key +
    "&file_type=json&sort_order=desc&limit=5"
)
try:
    with urllib.request.urlopen(url, timeout=10) as r:
        data = json.loads(r.read().decode())
    rate = None
    for obs in data.get("observations", []):
        v = obs.get("value", ".")
        if v != ".":
            rate = float(v) / 100.0  # convert percent to decimal
            break
    print(json.dumps({"rate": rate if rate is not None else 0.04}))
except Exception as e:
    print(json.dumps({"rate": 0.04, "error": str(e)}))
)python");

    QPointer<PortfolioService> self = this;
    python::PythonRunner::instance().run_code(code, [self, now_secs](python::PythonResult result) {
        if (!self)
            return;
        double rate = 0.04; // fallback
        if (result.success && !result.output.trimmed().isEmpty()) {
            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(result.output.trimmed().toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.object().contains("rate"))
                rate = doc.object()["rate"].toDouble(0.04);
        }
        // Persist to 24h cache
        auto& settings = SettingsRepository::instance();
        settings.set("portfolio.rf_rate_timestamp", QString::number(now_secs));
        settings.set("portfolio.rf_rate_value", QString::number(rate, 'f', 6));
        self->rf_rate_ = rate;
        emit self->risk_free_rate_loaded(rate);
    });
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
        // Fallback: derive volatility from cross-sectional day changes only
        double sum = 0, sum_sq = 0;
        int n = 0;
        for (const auto& h : summary.holdings) {
            if (std::abs(h.day_change_percent) > 0.0001) {
                sum += h.day_change_percent;
                sum_sq += h.day_change_percent * h.day_change_percent;
                ++n;
            }
        }
        if (n >= 2) {
            const double mean = sum / n;
            const double var = (sum_sq / n) - (mean * mean);
            const double daily_vol = std::sqrt(std::max(var, 0.0));
            const double ann_vol = daily_vol * std::sqrt(252.0);
            metrics.volatility = ann_vol * 100.0; // store as %
            const double rf_daily = rf_rate_ / 252.0;
            if (daily_vol > 1e-6)
                metrics.sharpe = ((mean / 100.0 - rf_daily) / (daily_vol / 100.0)) * std::sqrt(252.0);
            if (summary.total_market_value > 0)
                metrics.var_95 = summary.total_market_value * std::abs(mean / 100.0 - 1.645 * daily_vol / 100.0);
            const double vol_score = std::min(ann_vol / 40.0, 1.0) * 50.0;
            const double conc_score = std::min(conc / 80.0, 1.0) * 50.0;
            metrics.risk_score = vol_score + conc_score;
        }
        emit metrics_computed(metrics);
        return;
    }

    // ── Build daily return series from snapshots ──────────────────────────────
    auto snaps = snap_r.value();
    // Sort ascending by date
    std::sort(snaps.begin(), snaps.end(),
              [](const auto& a, const auto& b) { return a.snapshot_date < b.snapshot_date; });

    QVector<double> port_returns; // daily log returns (%)
    port_returns.reserve(snaps.size() - 1);
    for (int i = 1; i < snaps.size(); ++i) {
        const double prev = snaps[i - 1].total_value;
        const double curr = snaps[i].total_value;
        if (prev > 1e-6)
            port_returns.append((curr - prev) / prev * 100.0);
    }

    if (port_returns.size() < 2) {
        emit metrics_computed(metrics);
        return;
    }

    const int n = port_returns.size();

    // ── Mean and std-dev ──────────────────────────────────────────────────────
    const double mean = std::accumulate(port_returns.begin(), port_returns.end(), 0.0) / n;
    double sum_sq = 0;
    for (const double r : port_returns)
        sum_sq += (r - mean) * (r - mean);
    const double daily_vol = std::sqrt(sum_sq / (n - 1)); // sample std-dev
    const double ann_vol = daily_vol * std::sqrt(252.0);
    metrics.volatility = ann_vol; // already in %

    // ── Sharpe ratio (annualised) ─────────────────────────────────────────────
    // rf_rate_ = live DGS10 annual decimal (e.g. 0.043); convert to daily %
    const double rf_daily = rf_rate_ / 252.0 * 100.0;
    if (daily_vol > 1e-6)
        metrics.sharpe = ((mean - rf_daily) / daily_vol) * std::sqrt(252.0);

    // ── Max drawdown ──────────────────────────────────────────────────────────
    double peak = snaps.first().total_value;
    double max_dd = 0.0;
    for (const auto& s : snaps) {
        if (s.total_value > peak)
            peak = s.total_value;
        if (peak > 1e-6) {
            const double dd = (s.total_value - peak) / peak * 100.0;
            if (dd < max_dd)
                max_dd = dd;
        }
    }
    metrics.max_drawdown = max_dd; // negative %

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

            const double spy_ret = (spy_curr - spy_prev) / spy_prev * 100.0;
            const double pv = snaps[i - 1].total_value;
            const double cv = snaps[i].total_value;
            if (pv < 1e-6)
                continue;
            const double port_ret = (cv - pv) / pv * 100.0;
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
            if (var_spy > 1e-10)
                metrics.beta = cov / var_spy;
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
        const double dd_score = std::min(std::abs(max_dd) / 50.0, 1.0) * 25.0;
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
    auto txns_r = PortfolioRepository::instance().get_transactions(portfolio_id, 10000);

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
    auto txns_r = PortfolioRepository::instance().get_transactions(portfolio_id, 10000);

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

    // Replay transactions chronologically
    for (const auto& val : txn_arr) {
        auto obj = val.toObject();
        QString type = obj["type"].toString();
        if (type != "BUY" && type != "SELL")
            continue; // Skip DIVIDEND/SPLIT for now

        QString sym = obj["symbol"].toString();
        double qty = obj["quantity"].toDouble();
        double price = obj["price"].toDouble();
        QString date = obj["date"].toString();

        if (type == "BUY") {
            QString hint_sector = sector_hints.value(sym.toUpper());
            auto r = repo.add_asset(target_id, sym, qty, price, date, hint_sector);
            if (r.is_err()) {
                errors.append(QString("BUY %1: %2").arg(sym, QString::fromStdString(r.error())));
                continue;
            }
        } else { // SELL
            auto assets = repo.get_assets(target_id);
            bool sell_ok = false;
            if (assets.is_ok()) {
                for (const auto& a : assets.value()) {
                    if (a.symbol == sym.toUpper()) {
                        if (qty > a.quantity + 0.0001) {
                            errors.append(QString("SELL %1: qty %2 > held %3").arg(sym).arg(qty).arg(a.quantity));
                        } else {
                            double remaining = a.quantity - qty;
                            if (remaining <= 0.0001)
                                repo.remove_asset(target_id, sym);
                            else
                                repo.update_asset(target_id, sym, remaining, a.avg_buy_price);
                            sell_ok = true;
                        }
                        break;
                    }
                }
                if (!sell_ok && errors.isEmpty())
                    errors.append(QString("SELL %1: asset not found").arg(sym));
            }
            if (!sell_ok)
                continue; // Don't record failed sell as transaction
        }

        // Record the transaction only on success
        repo.add_transaction(target_id, sym, type, qty, price, date, obj["notes"].toString());
        ++replayed;
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
    auto assets_r = repo.get_assets(portfolio_id);
    if (assets_r.is_err() || assets_r.value().isEmpty()) {
        emit history_backfilled(portfolio_id, 0);
        return;
    }
    const auto assets = assets_r.value();

    // Persistent yfinance daemon — same shape, no per-call import cost.
    QJsonArray positions_arr;
    double total_cost_basis = 0.0;
    for (const auto& a : assets) {
        QJsonObject p;
        p["symbol"] = a.symbol;
        p["quantity"] = a.quantity;
        positions_arr.append(p);
        total_cost_basis += a.quantity * a.avg_buy_price;
    }
    QJsonObject payload;
    payload["positions"] = positions_arr;
    payload["period"] = period;

    QPointer<PortfolioService> self = this;
    python::PythonWorker::instance().submit("portfolio_nav_history", payload,
        [self, portfolio_id, total_cost_basis](bool ok, QJsonObject obj, QString err) {
        if (!self)
            return;
        if (!ok) {
            LOG_WARN("PortfolioSvc",
                     QString("backfill_history failed for %1: %2").arg(portfolio_id, err.left(200)));
            emit self->history_backfilled(portfolio_id, 0);
            return;
        }
        if (obj.contains("error")) {
            LOG_WARN("PortfolioSvc", "backfill_history: " + obj["error"].toString());
            emit self->history_backfilled(portfolio_id, 0);
            return;
        }
        const auto dates = obj["dates"].toArray();
        const auto navs = obj["navs"].toArray();
        if (dates.isEmpty() || dates.size() != navs.size()) {
            emit self->history_backfilled(portfolio_id, 0);
            return;
        }

        // Upsert each row. INSERT OR REPLACE keyed by (portfolio_id, snapshot_date)
        // so re-running backfill (with a different period or after the user adds
        // holdings) corrects existing rows in place.
        auto& repo = PortfolioRepository::instance();
        int written = 0;
        for (int i = 0; i < dates.size(); ++i) {
            const QString d = dates[i].toString();
            const double nav = navs[i].toDouble();
            const double pnl = nav - total_cost_basis;
            const double pnl_pct = total_cost_basis > 0 ? (pnl / total_cost_basis) * 100.0 : 0.0;
            auto wr = repo.save_snapshot(portfolio_id, nav, total_cost_basis, pnl, pnl_pct, d);
            if (wr.is_ok())
                ++written;
        }

        LOG_INFO("PortfolioSvc",
                 QString("Backfilled %1 historical snapshots for %2").arg(written).arg(portfolio_id));
        self->invalidate_cache(portfolio_id);
        emit self->history_backfilled(portfolio_id, written);
    },
    python::PythonWorker::kComputeActionTimeoutMs);
}

// ── Portfolio analyst fundamentals ───────────────────────────────────────────

void PortfolioService::fetch_portfolio_fundamentals(const QString& portfolio_id) {
    if (portfolio_id.isEmpty()) return;

    // Build MV map from cached summary so we can weight each holding.
    QHash<QString, double> mv_by_symbol;
    double total_mv = 0;
    {
        QMutexLocker lock(&cache_mutex_);
        auto cit = summary_cache_.constFind(portfolio_id);
        if (cit != summary_cache_.cend()) {
            for (const auto& h : cit.value().summary.holdings) {
                const QString up = h.symbol.toUpper();
                mv_by_symbol[up] = h.market_value;
                total_mv += h.market_value;
            }
        }
    }
    if (mv_by_symbol.isEmpty() || total_mv <= 0) return;

    // Per-symbol accumulator, shared across all callbacks.
    struct SymResult {
        double tgt_low   = 0;
        double tgt_mean  = 0;
        double tgt_high  = 0;
        double pe        = 0;
        double yield     = 0;
        double rec_score = 0;  // 1=strong_buy … 5=strong_sell, 0=missing
        bool   ok        = false;
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
            [self, portfolio_id, sym, state, mv_by_symbol, total_mv](
                bool ok, QJsonObject obj, QString /*err*/) {
                if (!self) return;

                SymResult& r = state->results[sym];
                if (ok && !obj.contains("error")) {
                    r.tgt_low   = obj["target_low_price"].toDouble();
                    r.tgt_mean  = obj["target_mean_price"].toDouble();
                    r.tgt_high  = obj["target_high_price"].toDouble();
                    r.pe        = obj["trailing_pe"].toDouble();
                    // yfinance returns dividendYield as a fraction (e.g. 0.014)
                    r.yield     = obj["dividend_yield"].toDouble();

                    // Map recommendation_key to a 1-5 score (lower = more bullish).
                    const QString rec = obj["recommendation_key"].toString().toLower();
                    if      (rec == "strong_buy"  || rec == "strongbuy")  r.rec_score = 1;
                    else if (rec == "buy")                                 r.rec_score = 2;
                    else if (rec == "hold"        || rec == "neutral")    r.rec_score = 3;
                    else if (rec == "sell"        || rec == "underperform") r.rec_score = 4;
                    else if (rec == "strong_sell" || rec == "strongsell") r.rec_score = 5;

                    r.ok = (r.tgt_mean > 0);
                }

                if (--state->pending > 0) return;

                // All callbacks done — compute MV-weighted aggregates.
                portfolio::PortfolioFundamentals f;
                double w_tgt_low = 0, w_tgt_mean = 0, w_tgt_high = 0;
                double w_pe = 0,   w_yield = 0,   w_rec = 0;
                double cov_tgt = 0, cov_pe = 0, cov_yield = 0, cov_rec = 0;

                for (auto jt = state->results.cbegin(); jt != state->results.cend(); ++jt) {
                    const double w = mv_by_symbol.value(jt.key(), 0) / total_mv;
                    const SymResult& sr = jt.value();
                    if (sr.tgt_mean > 0) {
                        w_tgt_low  += w * sr.tgt_low;
                        w_tgt_mean += w * sr.tgt_mean;
                        w_tgt_high += w * sr.tgt_high;
                        cov_tgt    += w;
                    }
                    if (sr.pe > 0) { w_pe  += w * sr.pe;    cov_pe    += w; }
                    if (sr.yield > 0) { w_yield += w * sr.yield; cov_yield += w; }
                    if (sr.rec_score > 0) { w_rec += w * sr.rec_score; cov_rec += w; }
                }

                // Re-normalise by actual coverage weight so sparse data stays unbiased.
                if (cov_tgt > 0) {
                    f.tgt_low  = w_tgt_low  / cov_tgt * total_mv;
                    f.tgt_mean = w_tgt_mean / cov_tgt * total_mv;
                    f.tgt_high = w_tgt_high / cov_tgt * total_mv;
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

                emit self->portfolio_fundamentals_loaded(portfolio_id, f);
            },
            python::PythonWorker::kNetworkActionTimeoutMs);
    }
}

// ── Cache control ────────────────────────────────────────────────────────────

void PortfolioService::invalidate_cache(const QString& portfolio_id) {
    QMutexLocker lock(&cache_mutex_);
    summary_cache_.remove(portfolio_id);
}

} // namespace fincept::services
