#include "screens/futures/FuturesQuoteCache.h"

#include "screens/futures/FuturesContracts.h"

namespace fincept::screens::futures {

FuturesQuoteCache& FuturesQuoteCache::instance() {
    static FuturesQuoteCache inst;
    return inst;
}

FuturesQuoteCache::FuturesQuoteCache() {
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &FuturesQuoteCache::refresh);
}

QVector<FuturesQuote> FuturesQuoteCache::quotes_for_class(const QString& asset_class) const {
    QVector<FuturesQuote> out;
    out.reserve(8);
    // Preserve catalog order so panels render the contract list deterministically.
    for (const auto& def : all_contracts()) {
        if (def.asset_class != asset_class) continue;
        const auto it = rows_.constFind(def.symbol);
        if (it != rows_.constEnd()) out.append(it.value());
    }
    return out;
}

QVector<FuturesQuote> FuturesQuoteCache::all_quotes() const {
    QVector<FuturesQuote> out;
    out.reserve(rows_.size());
    for (const auto& def : all_contracts()) {
        const auto it = rows_.constFind(def.symbol);
        if (it != rows_.constEnd()) out.append(it.value());
    }
    return out;
}

QHash<QString, QVector<FuturesQuote>> FuturesQuoteCache::grouped_by_class() const {
    QHash<QString, QVector<FuturesQuote>> out;
    for (const auto& def : all_contracts()) {
        const auto it = rows_.constFind(def.symbol);
        if (it != rows_.constEnd()) out[def.asset_class].append(it.value());
    }
    return out;
}

void FuturesQuoteCache::refresh() {
    if (in_flight_) return;
    if (last_request_.isValid() && last_request_.msecsTo(QDateTime::currentDateTime()) < min_interval_ms_)
        return;

    QStringList syms;
    syms.reserve(all_contracts().size());
    for (const auto& def : all_contracts()) syms.push_back(def.symbol);
    if (syms.isEmpty()) return;

    in_flight_ = true;
    last_request_ = QDateTime::currentDateTime();

    FuturesDataService::instance().fetch_quotes(
        syms, [this](bool ok, QVector<FuturesQuote> rows, QString source) {
            in_flight_ = false;
            if (!ok) {
                emit refresh_failed("fetch_quotes failed");
                return;
            }
            // Merge — don't wipe rows whose symbol happened to be missing this
            // round. yfinance occasionally drops one ticker per batch.
            for (const auto& q : rows) rows_.insert(q.symbol, q);
            last_source_ = source;
            last_refresh_ = QDateTime::currentDateTime();
            emit quotes_updated();
        });
}

void FuturesQuoteCache::start_auto_refresh(int interval_ms) {
    interval_ms_ = interval_ms;
    timer_->setInterval(interval_ms);
    if (!timer_->isActive()) timer_->start();
}

void FuturesQuoteCache::stop_auto_refresh() {
    timer_->stop();
}

void FuturesQuoteCache::retain() {
    ++retain_count_;
    if (retain_count_ == 1) {
        // First visible consumer — wake the cache up.
        timer_->setInterval(interval_ms_);
        if (!timer_->isActive()) timer_->start();
        // Kick an immediate refresh too, in case the data is stale from
        // having been suspended for a while.
        refresh();
    }
}

void FuturesQuoteCache::release() {
    if (retain_count_ <= 0) return;  // defensive — unbalanced release
    --retain_count_;
    if (retain_count_ == 0) {
        // No visible consumers — stop polling Yahoo. Cached values stay
        // available for the next consumer that retain()s.
        timer_->stop();
    }
}

} // namespace fincept::screens::futures
