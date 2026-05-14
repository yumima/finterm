#include "screens/futures/FuturesQuoteCache.h"

#include "screens/futures/FuturesContracts.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace fincept::screens::futures {

FuturesQuoteCache& FuturesQuoteCache::instance() {
    static FuturesQuoteCache inst;
    return inst;
}

FuturesQuoteCache::FuturesQuoteCache() {
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &FuturesQuoteCache::refresh);
    // Hydrate from disk so the grid has values to display before the
    // first live refresh returns. Snapshot is overwritten as soon as
    // the polling timer's first response merges into rows_.
    load_from_disk();
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
            save_to_disk();
            emit quotes_updated();
        });
}

QString FuturesQuoteCache::cache_path() const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString dir  = root + "/cache/futures";
    QDir().mkpath(dir);
    return dir + "/quotes.json";
}

void FuturesQuoteCache::save_to_disk() {
    QJsonArray arr;
    for (auto it = rows_.cbegin(); it != rows_.cend(); ++it) {
        const auto& q = it.value();
        QJsonObject o;
        o.insert("symbol",        q.symbol);
        o.insert("name",          q.name);
        o.insert("asset_class",   q.asset_class);
        o.insert("last",          q.last);
        o.insert("change",        q.change);
        o.insert("change_pct",    q.change_pct);
        o.insert("volume",        q.volume);
        o.insert("open_interest", q.open_interest);
        o.insert("high",          q.high);
        o.insert("low",           q.low);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("last_refresh", last_refresh_.toString(Qt::ISODate));
    root.insert("source",       last_source_);
    root.insert("quotes",       arr);

    // QSaveFile = write-to-tmp-then-rename. Survives a crash mid-write:
    // the previous cache file stays intact until commit() succeeds.
    QSaveFile f(cache_path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

void FuturesQuoteCache::load_from_disk() {
    QFile f(cache_path());
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto bytes = f.readAll();
    f.close();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) return;
    const auto root = doc.object();

    const QString ts = root.value("last_refresh").toString();
    if (!ts.isEmpty()) {
        last_refresh_ = QDateTime::fromString(ts, Qt::ISODate);
    }
    last_source_ = root.value("source").toString();

    const auto arr = root.value("quotes").toArray();
    for (const auto& v : arr) {
        const auto o = v.toObject();
        FuturesQuote q;
        q.symbol        = o.value("symbol").toString();
        q.name          = o.value("name").toString();
        q.asset_class   = o.value("asset_class").toString();
        q.last          = o.value("last").toDouble();
        q.change        = o.value("change").toDouble();
        q.change_pct    = o.value("change_pct").toDouble();
        q.volume        = o.value("volume").toDouble();
        q.open_interest = o.value("open_interest").toDouble();
        q.high          = o.value("high").toDouble();
        q.low           = o.value("low").toDouble();
        if (!q.symbol.isEmpty()) rows_.insert(q.symbol, q);
    }
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
