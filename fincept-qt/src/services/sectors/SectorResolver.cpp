#include "services/sectors/SectorResolver.h"

#include "core/logging/Logger.h"
#include "services/markets/MarketDataService.h"
#include "storage/sqlite/Database.h"

#include <QDateTime>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace fincept::services {

namespace {

constexpr qint64 kRetryTransientSec = 120;  // refused / unanswered: says nothing about the symbol
constexpr qint64 kRetryErrorSec = 3600;     // failed some other way: still not an answer

// Symbols that look like TSX bare tickers (letters only, 1–4 chars, no dot).
// We try a `.TO` suffix first before giving up, since many Canadian holdings
// in imported portfolios arrive without the exchange suffix.
bool looks_like_canadian_bare(const QString& symbol) {
    if (symbol.contains('.') || symbol.contains('-') || symbol.contains('='))
        return false;
    if (symbol.length() < 1 || symbol.length() > 5)
        return false;
    for (auto ch : symbol) {
        if (!ch.isLetter())
            return false;
    }
    return true;
}

} // namespace

SectorResolver& SectorResolver::instance() {
    static SectorResolver s;
    return s;
}

SectorResolver::SectorResolver() {
    load_cache();
}

QString SectorResolver::normalize(const QString& symbol) {
    return symbol.trimmed().toUpper();
}

QString SectorResolver::fallback_for_unresolvable(const QString& symbol) {
    // Heuristic: symbols ending in letters with no exchange suffix and length
    // >= 5 that failed yfinance are almost always Canadian mutual fund codes
    // like TDB8150. Treat those as Cash/Fund.
    if (symbol.length() >= 6 && symbol.startsWith("TDB"))
        return "Cash";
    return "Unclassified";
}

void SectorResolver::load_cache() {
    auto& db_mgr = fincept::Database::instance();
    if (!db_mgr.is_open())
        return;

    QSqlQuery q(db_mgr.raw_db());
    if (!q.exec("SELECT symbol, sector, quote_type FROM sector_cache")) {
        LOG_WARN("SectorResolver", "Cache load failed: " + q.lastError().text());
        return;
    }

    QMutexLocker lock(&mutex_);
    int loaded = 0;
    while (q.next()) {
        QString sym = q.value(0).toString();
        QString sec = q.value(1).toString();

        // A negative entry is re-confirmed once per session, not trusted:
        // until 2026-10 a refused lookup (Yahoo's 401 bursts) was stored as
        // "Unclassified" — 13 of 43 such rows here were real companies. One
        // lookup per fund/ETF per launch is the price of never serving one.
        // A sector the user set (override) stands.
        const bool is_negative = sec.isEmpty() || sec == "Unclassified";
        if (is_negative && q.value(2).toString() != QLatin1String("override"))
            continue;

        if (!sec.isEmpty()) {
            cache_.insert(sym, sec);
            ++loaded;
        }
    }
    LOG_INFO("SectorResolver", QString("Loaded %1 sector cache entries").arg(loaded));
}

void SectorResolver::persist(const QString& symbol, const QString& sector, const QString& industry,
                             const QString& quote_type) {
    auto& db_mgr = fincept::Database::instance();
    if (!db_mgr.is_open())
        return;

    QSqlQuery q(db_mgr.raw_db());
    q.prepare("INSERT INTO sector_cache (symbol, sector, industry, quote_type, resolved_at) "
              "VALUES (?, ?, ?, ?, ?) "
              "ON CONFLICT(symbol) DO UPDATE SET "
              "sector=excluded.sector, industry=excluded.industry, "
              "quote_type=excluded.quote_type, resolved_at=excluded.resolved_at");
    q.addBindValue(symbol);
    q.addBindValue(sector);
    q.addBindValue(industry);
    q.addBindValue(quote_type);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec())
        LOG_WARN("SectorResolver", "Cache write failed: " + q.lastError().text());
}

void SectorResolver::remember(const QString& symbol, const QString& sector) {
    QString sym = normalize(symbol);
    QString sec = sector.trimmed();
    if (sym.isEmpty() || sec.isEmpty())
        return;

    {
        QMutexLocker lock(&mutex_);
        cache_.insert(sym, sec);
    }
    persist(sym, sec, {}, "override");
    emit sector_resolved(sym, sec);
}

QString SectorResolver::sector_for(const QString& symbol) {
    QString sym = normalize(symbol);
    if (sym.isEmpty())
        return {};

    QMutexLocker lock(&mutex_);
    auto it = cache_.constFind(sym);
    if (it != cache_.constEnd())
        return it.value();

    if (const auto ra = retry_after_.constFind(sym);
        ra != retry_after_.constEnd() && QDateTime::currentSecsSinceEpoch() < ra.value())
        return {};
    if (!inflight_.contains(sym)) {
        inflight_.insert(sym);
        lock.unlock();
        resolve_async(sym);
    }
    return {};
}

void SectorResolver::prefetch(const QStringList& symbols) {
    for (const auto& s : symbols)
        (void)sector_for(s);
}

void SectorResolver::defer(const QString& symbol, qint64 secs) {
    QMutexLocker lock(&mutex_);
    inflight_.remove(symbol);
    retry_after_.insert(symbol, QDateTime::currentSecsSinceEpoch() + secs);
}

void SectorResolver::resolve_async(const QString& symbol) {
    QPointer<SectorResolver> self = this;

    auto finish = [self, symbol](QString sector, QString industry, QString quote_type) {
        if (!self)
            return;
        QString final_sector = sector.trimmed();
        if (final_sector.isEmpty() || final_sector == "N/A") {
            // yfinance didn't resolve — use a graceful fallback.
            final_sector = SectorResolver::fallback_for_unresolvable(symbol);
        }
        {
            QMutexLocker lock(&self->mutex_);
            self->cache_.insert(symbol, final_sector);
            self->inflight_.remove(symbol);
        }
        self->persist(symbol, final_sector, industry, quote_type);
        emit self->sector_resolved(symbol, final_sector);
    };

    // Try 1: as-is.
    MarketDataService::instance().fetch_info(symbol, [self, symbol, finish](bool ok, InfoData info) {
        if (!self)
            return;
        QString sector = info.sector.trimmed();
        bool resolved = ok && !sector.isEmpty() && sector != "N/A";
        if (resolved) {
            finish(sector, info.industry, {});
            return;
        }
        // Only an ANSWER without a sector may become "Unclassified" — a
        // refused or failed lookup (a 401 burst) used to be stored as one
        // for 7 days, for companies Yahoo does classify.
        if (!info.info_answered) {
            LOG_WARN("SectorResolver", QString("No answer for %1 — not classifying it").arg(symbol));
            self->defer(symbol, info.info_transient ? kRetryTransientSec : kRetryErrorSec);
            return;
        }

        // Try 2: TSX suffix fallback for bare Canadian tickers.
        if (looks_like_canadian_bare(symbol)) {
            QString tsx_symbol = symbol + ".TO";
            MarketDataService::instance().fetch_info(
                tsx_symbol, [self, symbol, finish](bool ok2, InfoData info2) {
                    if (!self)
                        return;
                    // .TO is a guess: refused -> ask again later; failed for
                    // good (no such listing) -> the answer try 1 already gave.
                    if (!info2.info_answered && info2.info_transient) {
                        self->defer(symbol, kRetryTransientSec);
                        return;
                    }
                    finish(info2.info_answered ? info2.sector : QString{}, info2.industry, {});
                });
            return;
        }
        finish({}, info.industry, {});
    });
}

} // namespace fincept::services
