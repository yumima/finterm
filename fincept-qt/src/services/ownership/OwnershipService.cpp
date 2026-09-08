#include "services/ownership/OwnershipService.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "services/notifications/NotificationService.h"
#include "storage/repositories/SettingsRepository.h"
#include "storage/repositories/PortfolioRepository.h"
#include "storage/repositories/WatchlistRepository.h"
#include "python/PythonWorker.h"
#include "screens/ownership/OwnershipFlags.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::services {

using namespace fincept::ownership;

namespace {

constexpr const char* TAG = "Ownership";

/// Ownership data is the slowest-moving on any screen: Form 4s land within two
/// business days but are then permanent, 13F is quarterly, short interest is a
/// twice-monthly settlement snapshot. An hour of cache costs nothing.
constexpr qint64 kCacheTtlMs = 60 * 60 * 1000;

/// EDGAR parses one XML document per Form 4 at 0.4s apiece, so this is the
/// wall-clock ceiling on a busy issuer rather than a network timeout.
constexpr int kEdgarTimeoutMs = 180'000;
constexpr int kWindowMonths = 24;
constexpr int kMaxFilings = 80;
constexpr int kHolderRows = 100;

/// A quarterly data set is ~100 MB and indexes 3.3m rows; symbol resolution is
/// rate-limited by OpenFIGI.
constexpr int kIndexBuildTimeoutMs = 1'800'000;
/// The breadth cache is a GROUP BY over 3.3m rows, once per quarter pair.
constexpr int kMoversTimeoutMs = 300'000;
/// One Form 4 scan pass: a few business days at ~500 filings a day.
constexpr int kScanDays = 30;
constexpr int kScanDaysPerPass = 4;
constexpr int kScanTimeoutMs = 900'000;
constexpr int kScanRecheckMs = 6 * 60 * 60 * 1000;
/// After a failed pass — a lock, a network fault — try again soon, not in
/// six hours: the tab says "scan failed" until then.
constexpr int kScanRetryMs = 15 * 60 * 1000;
constexpr int kScanMaxPasses = 12;

const QString kSrcEdgar    = QStringLiteral("edgar");
const QString kSrcMarket   = QStringLiteral("market");
const QString kSrcHolders  = QStringLiteral("holders");
const QString kSrcShort    = QStringLiteral("short");
const QString kSrcShortVol = QStringLiteral("shortvol");
const QString kSrcSirio    = QStringLiteral("sirio");
const QString kSrcReturns  = QStringLiteral("returns");

std::optional<double> opt_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull() || !v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

QDate iso_date(const QJsonObject& o, const char* key) {
    return QDate::fromString(o.value(QLatin1String(key)).toString().left(10), Qt::ISODate);
}

/// yfinance reports the short-interest as-of dates as unix seconds.
QDate epoch_date(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isDouble())
        return {};
    // EVENT-STAMP: a date-only as-of stamp at midnight UTC — UTC is the decode that recovers it.
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(v.toDouble()), QTimeZone::UTC).date();
}

Pattern pattern_from(const QString& s) {
    if (s == QLatin1String("routine"))       return Pattern::Routine;
    if (s == QLatin1String("opportunistic")) return Pattern::Opportunistic;
    return Pattern::Unclassified;
}

QJsonObject parse_object(const python::PythonResult& r) {
    return QJsonDocument::fromJson(python::extract_json(r.output).toUtf8()).object();
}

QString payload_of(const QJsonObject& o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

/// Errors from the `all` action live INSIDE each half, not at the top level.
/// Missing this is not cosmetic: parsing an {"error": ...} object yields zeros
/// and empty arrays for every key, so an EDGAR outage renders as "0 Form 4
/// filings" — the screen stating as fact that a company has no insider
/// activity when the fetch simply failed.
QString edgar_error_in(const QJsonObject& root) {
    const QString top = root.value(QStringLiteral("error")).toString();
    if (!top.isEmpty())
        return top;
    for (const char* half : {"insiders", "stakes"}) {
        const QJsonValue v = root.value(QLatin1String(half));
        if (!v.isObject())
            continue;
        const QString e = v.toObject().value(QStringLiteral("error")).toString();
        if (!e.isEmpty())
            return e;
    }
    return {};
}

void parse_edgar_into(const QJsonObject& root, OwnershipSnapshot& snap) {
    const QJsonObject ins = root.value(QStringLiteral("insiders")).isObject()
                                ? root.value(QStringLiteral("insiders")).toObject()
                                : root;
    if (snap.company.isEmpty())
        snap.company = ins.value(QStringLiteral("company")).toString();
    if (snap.cik.isEmpty())
        snap.cik = ins.value(QStringLiteral("cik")).toString();
    snap.filings_found     = ins.value(QStringLiteral("filings_found")).toInt();
    snap.filings_parsed    = ins.value(QStringLiteral("filings_parsed")).toInt();
    snap.filings_truncated = ins.value(QStringLiteral("filings_truncated")).toInt();
    snap.insider_rows_filed_as_owner = ins.value(QStringLiteral("rows_filed_as_owner")).toInt();
    snap.insider_other_issuers.clear();
    for (const auto& v : ins.value(QStringLiteral("other_issuers")).toArray())
        snap.insider_other_issuers << v.toString();
    snap.window_months = ins.value(QStringLiteral("window_months")).toInt();

    snap.transactions.clear();
    for (const auto& v : ins.value(QStringLiteral("transactions")).toArray()) {
        const QJsonObject o = v.toObject();
        InsiderTransaction t;
        t.insider     = o.value(QStringLiteral("insider")).toString();
        t.date        = iso_date(o, "date");
        t.filed_date  = iso_date(o, "filed_date");
        t.code        = o.value(QStringLiteral("code")).toString();
        t.code_label  = o.value(QStringLiteral("code_label")).toString();
        t.security    = o.value(QStringLiteral("security")).toString();
        t.derivative  = o.value(QStringLiteral("derivative")).toBool();
        t.open_market = o.value(QStringLiteral("open_market")).toBool();
        t.acquired    = o.value(QStringLiteral("direction")).toString() == QLatin1String("acquired");
        t.ten_percent_owner = o.value(QStringLiteral("ten_percent_owner")).toBool();
        const auto plan = o.value(QStringLiteral("plan_10b5_1"));
        if (plan.isBool())
            t.plan_10b5_1 = plan.toBool();
        t.source_url  = o.value(QStringLiteral("source_url")).toString();
        for (const auto& r : o.value(QStringLiteral("roles")).toArray())
            t.roles << r.toString();
        t.shares            = opt_num(o, "shares");
        t.price             = opt_num(o, "price");
        t.value             = opt_num(o, "value");
        t.shares_held_after = opt_num(o, "shares_held_after");
        if (t.date.isValid())
            snap.transactions.push_back(t);
    }

    snap.insiders.clear();
    for (const auto& v : ins.value(QStringLiteral("insiders")).toArray()) {
        const QJsonObject o = v.toObject();
        InsiderProfile p;
        p.insider          = o.value(QStringLiteral("insider")).toString();
        p.pattern          = pattern_from(o.value(QStringLiteral("pattern")).toString());
        p.trades           = o.value(QStringLiteral("trades")).toInt();
        p.trades_in_window = o.value(QStringLiteral("trades_in_window")).toInt();
        p.years_observed   = o.value(QStringLiteral("years_observed")).toInt();
        p.routine_month    = o.value(QStringLiteral("routine_month")).toInt();
        p.reason           = o.value(QStringLiteral("reason")).toString();
        if (!p.insider.isEmpty())
            snap.insiders.push_back(p);
    }

    const QJsonObject stk = root.value(QStringLiteral("stakes")).isObject()
                                ? root.value(QStringLiteral("stakes")).toObject()
                                : QJsonObject{};
    snap.stakes_truncated         = stk.value(QStringLiteral("filings_truncated")).toInt();
    snap.stakes_filed_by_this_cik = stk.value(QStringLiteral("filed_by_this_cik")).toInt();
    snap.stakes_unverified        = stk.value(QStringLiteral("filings_unverified")).toInt();
    snap.stakes.clear();
    for (const auto& v : stk.value(QStringLiteral("stakes")).toArray()) {
        const QJsonObject o = v.toObject();
        BeneficialStake b;
        b.form       = o.value(QStringLiteral("form")).toString();
        b.activist   = o.value(QStringLiteral("activist")).toBool();
        b.amendment  = o.value(QStringLiteral("amendment")).toBool();
        b.filed_date = iso_date(o, "filed_date");
        b.filer      = o.value(QStringLiteral("filer")).toString();
        b.subject_verified = o.value(QStringLiteral("subject_verified")).toBool(true);
        b.url        = o.value(QStringLiteral("url")).toString();
        if (!b.form.isEmpty())
            snap.stakes.push_back(b);
    }
}

void parse_market_into(const QJsonObject& o, OwnershipSnapshot& snap) {
    const QJsonObject s = o.value(QStringLiteral("short_interest")).toObject();
    VendorFloat v;
    v.shares_outstanding    = opt_num(s, "shares_outstanding");
    v.float_shares          = opt_num(s, "float_shares");
    v.held_pct_insiders     = opt_num(s, "held_pct_insiders");
    v.held_pct_institutions = opt_num(s, "held_pct_institutions");
    v.shares_short          = opt_num(s, "shares_short");
    v.short_pct_float       = opt_num(s, "short_pct_float");
    v.short_ratio           = opt_num(s, "short_ratio");
    v.short_as_of           = epoch_date(s, "date_short_interest");
    snap.vendor = v;
}

void parse_holders_into(const QJsonObject& root, OwnershipSnapshot& snap) {
    HoldersSummary sum;
    sum.quarter        = QDate::fromString(root.value(QStringLiteral("quarter")).toString(), Qt::ISODate);
    sum.prior_quarter  = QDate::fromString(root.value(QStringLiteral("prior_quarter")).toString(), Qt::ISODate);
    sum.holder_count   = root.value(QStringLiteral("holder_count")).toInt();
    sum.option_holders = root.value(QStringLiteral("option_holder_count")).toInt();
    sum.total_shares   = root.value(QStringLiteral("total_shares_held")).toDouble();
    sum.total_value    = root.value(QStringLiteral("total_value_held")).toDouble();
    sum.buyers         = root.value(QStringLiteral("buyers")).toInt();
    sum.new_holders    = root.value(QStringLiteral("new")).toInt();
    sum.sellers        = root.value(QStringLiteral("sellers")).toInt();
    sum.exited         = root.value(QStringLiteral("exited")).toInt();
    sum.exited_shares  = root.value(QStringLiteral("exited_shares")).toDouble();
    sum.top10_share    = opt_num(root, "top10_share");
    sum.broad_share    = opt_num(root, "broad_share");
    sum.sort           = root.value(QStringLiteral("sort")).toString();
    sum.min_book_value = root.value(QStringLiteral("min_book_value")).toDouble();
    const auto np = root.value(QStringLiteral("newer_partial")).toObject();
    if (!np.isEmpty()) {
        sum.partial_quarter = QDate::fromString(np.value(QStringLiteral("quarter")).toString(), Qt::ISODate);
        sum.partial_filers  = np.value(QStringLiteral("filers")).toInt();
    }
    if (snap.company.isEmpty())
        snap.company = root.value(QStringLiteral("company")).toString();
    snap.summary = sum;

    snap.holders.clear();
    for (const auto& v : root.value(QStringLiteral("holders")).toArray()) {
        const auto o = v.toObject();
        Holder h;
        h.manager        = o.value(QStringLiteral("manager")).toString();
        h.cik            = o.value(QStringLiteral("cik")).toString();
        h.tier           = o.value(QStringLiteral("tier")).toString();
        h.shares         = opt_num(o, "shares");
        h.value          = opt_num(o, "value");
        h.weight         = opt_num(o, "weight");
        h.book_total     = opt_num(o, "book_total");
        h.position_count = o.value(QStringLiteral("position_count")).toInt();
        h.action         = o.value(QStringLiteral("action")).toString();
        h.shares_delta   = opt_num(o, "shares_delta");
        h.pct_change     = opt_num(o, "pct_change");
        h.put_call       = o.value(QStringLiteral("put_call")).toString();
        h.is_derivative  = o.value(QStringLiteral("is_derivative")).toBool();
        h.note           = o.value(QStringLiteral("note")).toString();
        if (!h.manager.isEmpty() && !h.is_derivative)
            snap.holders.push_back(h);
    }
}

ShortReading parse_reading(const QJsonObject& o) {
    ShortReading r;
    r.settlement       = iso_date(o, "settlement");
    r.published_after  = iso_date(o, "published_after");
    r.shares_short     = opt_num(o, "short");
    r.prior            = opt_num(o, "prior");
    r.avg_daily_volume = opt_num(o, "adv");
    r.days_to_cover    = opt_num(o, "dtc");
    r.change_pct       = opt_num(o, "change_pct");
    return r;
}

void parse_book_into(const QJsonObject& root, ManagerBook& b) {
    b.manager      = root.value(QStringLiteral("manager")).toString();
    b.period       = iso_date(root, "quarter");
    b.prior_period = iso_date(root, "prior_quarter");
    b.total_value  = root.value(QStringLiteral("total_value")).toDouble();
    b.position_count = root.value(QStringLiteral("position_count")).toInt();

    auto read = [](const QJsonObject& o) {
        BookPosition p;
        p.issuer = o.value(QStringLiteral("issuer")).toString();
        p.cusip = o.value(QStringLiteral("cusip")).toString();
        p.security_class = o.value(QStringLiteral("class")).toString();
        p.ticker = o.value(QStringLiteral("ticker")).toString();
        p.shares = opt_num(o, "shares");
        p.value = opt_num(o, "value");
        p.weight = opt_num(o, "weight");
        p.action = o.value(QStringLiteral("action")).toString();
        p.shares_delta = opt_num(o, "shares_delta");
        p.pct_change = opt_num(o, "pct_change");
        return p;
    };
    for (const auto& v : root.value(QStringLiteral("positions")).toArray()) {
        const auto p = read(v.toObject());
        // Option lines are a different instrument; a put is bearish and does
        // not belong in a list of what a firm owns.
        if (!p.cusip.isEmpty() && !v.toObject().value(QStringLiteral("is_derivative")).toBool())
            b.positions.push_back(p);
    }
    for (const auto& v : root.value(QStringLiteral("exits")).toArray()) {
        const auto p = read(v.toObject());
        if (!p.cusip.isEmpty())
            b.exits.push_back(p);
    }
}

} // namespace

OwnershipService& OwnershipService::instance() {
    static OwnershipService s;
    return s;
}

OwnershipService::OwnershipService() {
    form4_timer_ = new QTimer(this);
    form4_timer_->setSingleShot(true);
    connect(form4_timer_, &QTimer::timeout, this, [this]() { run_form4_scan(); });
}

// ── Per stock ────────────────────────────────────────────────────────────────

ownership::OwnershipSnapshot OwnershipService::snapshot(const QString& symbol) const {
    return cache_.value(symbol.toUpper());
}

bool OwnershipService::is_loading(const QString& symbol) const {
    return !pending_.value(symbol.toUpper()).isEmpty();
}

OwnershipService::HolderSort OwnershipService::holder_sort(const QString& symbol) const {
    return holder_sort_.value(symbol.toUpper(), HolderSort::BySize);
}

void OwnershipService::load(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    const auto in_flight = pending_.value(sym);
    if (in_flight.contains(kSrcEdgar) || in_flight.contains(kSrcMarket))
        return;
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - fetched_at_.value(sym, 0);
    if (cache_.contains(sym) && age < kCacheTtlMs) {
        emit snapshot_updated(sym);
        emit load_finished(sym);
        ensure_holders(sym);
        return;
    }
    refresh(sym);
}

void OwnershipService::ensure_holders(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || !cache_.contains(sym) || !index_ready())
        return;
    const auto& snap = cache_.value(sym);
    if (snap.holders_ok || !snap.holders_error.isEmpty() || pending_.value(sym).contains(kSrcHolders))
        return;
    pending_[sym].insert(kSrcHolders);
    fetch_holders(sym, holder_sort(sym));
}

void OwnershipService::refresh(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    const auto in_flight = pending_.value(sym);
    if (in_flight.contains(kSrcEdgar) || in_flight.contains(kSrcMarket))
        return;

    OwnershipSnapshot fresh;
    fresh.symbol = sym;
    cache_.insert(sym, fresh);
    // Not stamped until something comes back: a failure cached as fresh would
    // re-serve an empty snapshot for an hour without going near the network.
    fetched_at_.remove(sym);

    auto& set = pending_[sym];
    set.insert(kSrcEdgar);
    set.insert(kSrcMarket);
    set.insert(kSrcShort);
    set.insert(kSrcShortVol);
    fetch_market(sym);
    fetch_short_history(sym);
    fetch_short_volume(sym);
    fetch_edgar(sym);
    if (index_ready()) {
        set.insert(kSrcHolders);
        fetch_holders(sym, holder_sort(sym));
    }
}

void OwnershipService::note_source_done(const QString& symbol, const QString& source) {
    auto& set = pending_[symbol];
    set.remove(source);
    emit snapshot_updated(symbol);
    if (set.isEmpty()) {
        pending_.remove(symbol);
        const auto snap = cache_.value(symbol);
        if (snap.edgar_ok || snap.market_ok || snap.holders_ok)
            fetched_at_.insert(symbol, QDateTime::currentMSecsSinceEpoch());
        emit load_finished(symbol);
    }
}

void OwnershipService::fetch_edgar(const QString& sym) {
    QPointer<OwnershipService> self = this;
    const QString payload = payload_of(
        {{"symbol", sym}, {"months", kWindowMonths}, {"max_filings", kMaxFilings}});
    python::PythonRunner::instance().run(
        QStringLiteral("sec_ownership_data.py"), {QStringLiteral("all"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            if (!result.success) {
                snap.edgar_error = result.error.isEmpty() ? QStringLiteral("EDGAR fetch failed")
                                                          : result.error.left(200);
                LOG_WARN(TAG, "sec_ownership_data failed for " + sym + ": " + snap.edgar_error);
            } else {
                const QJsonObject root = parse_object(result);
                const QString err = edgar_error_in(root);
                if (!err.isEmpty()) {
                    snap.edgar_error = err;
                } else {
                    parse_edgar_into(root, snap);
                    snap.edgar_ok = true;
                }
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcEdgar);
            if (snap.edgar_ok)
                self->price_transactions(sym);
        },
        /*on_line=*/{}, kEdgarTimeoutMs);
}

void OwnershipService::fetch_market(const QString& sym) {
    QPointer<OwnershipService> self = this;
    python::PythonWorker::instance().submit(
        QStringLiteral("ownership_extras"), QJsonObject{{"symbol", sym}},
        [self, sym](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            if (!ok || result.contains(QStringLiteral("error"))) {
                snap.market_error =
                    err.isEmpty() ? result.value(QStringLiteral("error")).toString() : err;
                if (snap.market_error.isEmpty())
                    snap.market_error = QStringLiteral("share data unavailable");
                LOG_WARN(TAG, "ownership_extras failed for " + sym + ": " + snap.market_error);
            } else {
                parse_market_into(result, snap);
                snap.market_ok = true;
                const QString e = result.value(QStringLiteral("short_error")).toString();
                if (!e.isEmpty())
                    snap.market_error = QStringLiteral("share count: ") + e.left(120);
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcMarket);
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void OwnershipService::fetch_holders(const QString& sym, HolderSort sort) {
    QPointer<OwnershipService> self = this;
    const QString payload = payload_of(
        {{"ticker", sym}, {"limit", kHolderRows},
         {"sort", sort == HolderSort::ByWeight ? QStringLiteral("weight") : QStringLiteral("value")}});
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("holders"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            snap.holders.clear();
            snap.holders_ok = false;
            if (!result.success) {
                snap.holders_error = result.error.isEmpty() ? QStringLiteral("13F index query failed")
                                                            : result.error.left(200);
            } else {
                const auto root = parse_object(result);
                const QString err = root.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    snap.holders_error = err;
                } else {
                    snap.holders_error.clear();
                    parse_holders_into(root, snap);
                    snap.holders_ok = true;
                }
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcHolders);
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::set_holder_sort(const QString& symbol, HolderSort sort) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || holder_sort_.value(sym, HolderSort::BySize) == sort)
        return;
    holder_sort_.insert(sym, sort);
    if (!index_ready() || pending_.value(sym).contains(kSrcHolders))
        return;
    pending_[sym].insert(kSrcHolders);
    fetch_holders(sym, sort);
}

void OwnershipService::fetch_short_history(const QString& sym) {
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("finra_short_interest.py"),
        {QStringLiteral("history"), payload_of({{"symbol", sym}, {"months", 12}})},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            ShortHistory h;
            if (!result.success) {
                h.error = result.error.isEmpty() ? QStringLiteral("FINRA fetch failed")
                                                 : result.error.left(200);
            } else {
                const auto o = parse_object(result);
                const QString err = o.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    h.error = err;
                } else {
                    h.source = o.value(QStringLiteral("source")).toString();
                    for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                        const auto r = parse_reading(v.toObject());
                        if (r.settlement.isValid())
                            h.rows.push_back(r);
                    }
                }
            }
            snap.short_history = h;
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcShort);
            // Where this reading sits across the market needs the 13F
            // denominator for every stock, which only the index can supply.
            if (h.has_data() && self->index_ready() &&
                !self->pending_.value(sym).contains(kSrcSirio)) {
                self->pending_[sym].insert(kSrcSirio);
                QPointer<OwnershipService> self2 = self;
                python::PythonRunner::instance().run(
                    QStringLiteral("finra_short_interest.py"),
                    {QStringLiteral("percentile"), payload_of({{"symbol", sym}})},
                    [self2, sym](python::PythonResult r2) {
                        if (!self2)
                            return;
                        auto s2 = self2->cache_.value(sym);
                        if (r2.success) {
                            const auto o2 = parse_object(r2);
                            s2.sirio_percentile = opt_num(o2, "percentile");
                            s2.sirio_universe = o2.value(QStringLiteral("universe")).toInt();
                        }
                        self2->cache_.insert(sym, s2);
                        self2->note_source_done(sym, kSrcSirio);
                    },
                    /*on_line=*/{}, 120'000);
            }
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::fetch_short_volume(const QString& sym) {
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("finra_short_volume.py"),
        {QStringLiteral("series"), payload_of({{"symbol", sym}, {"days", 40}})},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            ShortVolume sv;
            if (!result.success) {
                sv.error = result.error.isEmpty() ? QStringLiteral("short-volume fetch failed")
                                                  : result.error.left(200);
            } else {
                const auto o = parse_object(result);
                const QString err = o.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    sv.error = err;
                } else {
                    sv.as_of  = iso_date(o, "as_of");
                    sv.latest = o.value(QStringLiteral("latest_ratio")).toDouble();
                    sv.avg_20 = o.value(QStringLiteral("avg_20")).toDouble();
                    sv.days   = o.value(QStringLiteral("days")).toInt();
                }
            }
            snap.short_volume = sv;
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcShortVol);
        },
        /*on_line=*/{}, 180'000);
}

void OwnershipService::price_transactions(const QString& sym) {
    auto snap = cache_.value(sym);
    QDate earliest;
    for (const auto& t : snap.transactions) {
        if (!t.open_market || t.derivative || !t.date.isValid())
            continue;
        if (!earliest.isValid() || t.date < earliest)
            earliest = t.date;
    }
    if (!earliest.isValid() || pending_.value(sym).contains(kSrcReturns))
        return;
    pending_[sym].insert(kSrcReturns);
    QPointer<OwnershipService> self = this;
    python::PythonWorker::instance().submit(
        QStringLiteral("batch_closes"),
        QJsonObject{{"symbols", QJsonArray{sym}},
                    {"start", earliest.addDays(-7).toString(Qt::ISODate)},
                    {"end", QDate::currentDate().addDays(1).toString(Qt::ISODate)}},
        [self, sym](bool ok, QJsonObject result, QString) {
            if (!self)
                return;
            auto s = self->cache_.value(sym);
            if (ok) {
                const auto closes = result.value(QStringLiteral("closes")).toObject()
                                        .value(sym).toArray();
                for (auto& t : s.transactions) {
                    if (!t.open_market || t.derivative)
                        continue;
                    const auto fr = forward_returns(closes, t.date);
                    t.ret_1w = fr.r1w;
                    t.ret_1m = fr.r1m;
                    t.ret_3m = fr.r3m;
                }
                s.returns_ok = !closes.isEmpty();
            }
            self->cache_.insert(sym, s);
            self->note_source_done(sym, kSrcReturns);
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

// ── Market-wide scans ────────────────────────────────────────────────────────

void OwnershipService::load_insider_buys(const ownership::InsiderBuyQuery& q) {
    if (insider_buys_loading_) {
        queued_insider_query_ = q;
        return;
    }
    insider_buys_loading_ = true;
    insider_buys_.query = q;
    QPointer<OwnershipService> self = this;
    const QString payload = payload_of({{"days", q.days},
                                        {"min_insiders", q.min_insiders},
                                        {"min_value", q.min_value},
                                        {"exclude_ten_pct", q.exclude_ten_pct},
                                        {"exclude_plan", q.exclude_plan},
                                        {"limit", 300}});
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form4_market.py"), {QStringLiteral("recent"), payload},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->insider_buys_loading_ = false;
            InsiderBuys ib;
            ib.query = self->insider_buys_.query;
            ib.loaded = true;
            if (!result.success) {
                ib.error = result.error.isEmpty() ? QStringLiteral("Form 4 store query failed")
                                                  : result.error.left(200);
            } else {
                const auto o = parse_object(result);
                ib.error = o.value(QStringLiteral("error")).toString();
                ib.days_scanned = o.value(QStringLiteral("days_scanned")).toInt();
                ib.days_wanted  = o.value(QStringLiteral("days_wanted")).toInt();
                ib.last_scanned = iso_date(o, "last_scanned");
                for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                    const auto r = v.toObject();
                    InsiderBuyRow row;
                    row.symbol   = r.value(QStringLiteral("symbol")).toString();
                    row.issuer   = r.value(QStringLiteral("issuer")).toString();
                    row.insiders = r.value(QStringLiteral("insiders")).toInt();
                    row.trades   = r.value(QStringLiteral("trades")).toInt();
                    row.value    = r.value(QStringLiteral("value")).toDouble();
                    row.shares   = r.value(QStringLiteral("shares")).toDouble();
                    row.avg_price = opt_num(r, "avg_price");
                    row.first_trade = iso_date(r, "first_trade");
                    row.last_trade  = iso_date(r, "last_trade");
                    row.last_filed  = iso_date(r, "last_filed");
                    for (const auto& rr : r.value(QStringLiteral("roles")).toArray())
                        row.roles << rr.toString();
                    row.stake_increase = opt_num(r, "stake_increase");
                    row.any_plan    = r.value(QStringLiteral("any_plan")).toBool();
                    row.any_ten_pct = r.value(QStringLiteral("any_ten_pct")).toBool();
                    row.plan_unknown = r.value(QStringLiteral("plan_unknown")).toInt();
                    ib.rows.push_back(row);
                }
            }
            self->insider_buys_ = ib;
            emit self->insider_buys_updated();
            if (self->queued_insider_query_) {
                const auto next = *self->queued_insider_query_;
                self->queued_insider_query_.reset();
                self->load_insider_buys(next);
                return;
            }
            if (ib.error.isEmpty() && !ib.rows.isEmpty())
                self->price_insider_buys();
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::price_insider_buys() {
    QJsonArray syms;
    QDate earliest;
    for (const auto& r : insider_buys_.rows) {
        if (r.symbol.isEmpty())
            continue;
        syms.append(r.symbol);
        if (!earliest.isValid() || (r.first_trade.isValid() && r.first_trade < earliest))
            earliest = r.first_trade;
        if (syms.size() >= 120)
            break;
    }
    if (syms.isEmpty() || !earliest.isValid())
        return;
    QPointer<OwnershipService> self = this;
    python::PythonWorker::instance().submit(
        QStringLiteral("batch_closes"),
        QJsonObject{{"symbols", syms},
                    {"start", earliest.addDays(-7).toString(Qt::ISODate)},
                    {"end", QDate::currentDate().addDays(1).toString(Qt::ISODate)}},
        [self](bool ok, QJsonObject result, QString) {
            if (!self || !ok)
                return;
            const auto closes = result.value(QStringLiteral("closes")).toObject();
            for (auto& r : self->insider_buys_.rows) {
                if (!closes.contains(r.symbol))
                    continue;
                const auto fr = forward_returns(closes.value(r.symbol).toArray(), r.last_trade);
                r.ret_1w = fr.r1w;
                r.ret_1m = fr.r1m;
            }
            self->insider_buys_.returns_ok = true;
            emit self->insider_buys_updated();
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

void OwnershipService::load_short_rank(const QString& sort) {
    if (short_rank_loading_)
        return;
    short_rank_loading_ = true;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("finra_short_interest.py"),
        {QStringLiteral("rank"), payload_of({{"limit", 300}, {"sort", sort}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->short_rank_loading_ = false;
            ShortRank sr;
            sr.loaded = true;
            if (!result.success) {
                sr.error = result.error.isEmpty() ? QStringLiteral("FINRA ranking failed")
                                                  : result.error.left(200);
            } else {
                const auto o = parse_object(result);
                sr.error = o.value(QStringLiteral("error")).toString();
                sr.settlement      = iso_date(o, "settlement");
                sr.published_after = iso_date(o, "published_after");
                sr.quarter         = iso_date(o, "quarter");
                sr.symbols         = o.value(QStringLiteral("symbols")).toInt();
                sr.joined          = o.value(QStringLiteral("joined")).toInt();
                sr.funds_dropped   = o.value(QStringLiteral("funds_dropped")).toInt();
                sr.sort            = o.value(QStringLiteral("sort")).toString();
                for (const auto& v : o.value(QStringLiteral("sirio_deciles")).toArray())
                    sr.sirio_deciles.push_back(v.toDouble());
                for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                    const auto r = v.toObject();
                    ShortRankRow row;
                    row.symbol = r.value(QStringLiteral("symbol")).toString();
                    row.name   = r.value(QStringLiteral("name")).toString();
                    row.shares_short     = opt_num(r, "short");
                    row.prior            = opt_num(r, "prior");
                    row.avg_daily_volume = opt_num(r, "adv");
                    row.days_to_cover    = opt_num(r, "dtc");
                    row.change_pct       = opt_num(r, "change_pct");
                    row.inst_shares      = opt_num(r, "inst_shares");
                    row.holders          = r.value(QStringLiteral("holders")).toInt();
                    row.sirio            = opt_num(r, "sirio");
                    sr.rows.push_back(row);
                }
            }
            self->short_rank_ = sr;
            emit self->short_rank_updated();
        },
        /*on_line=*/{}, 180'000);
}

void OwnershipService::load_movers(const QString& sort) {
    if (movers_loading_)
        return;
    movers_loading_ = true;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("movers"), payload_of({{"limit", 300}, {"sort", sort}, {"min_holders", 20}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->movers_loading_ = false;
            Movers m;
            m.loaded = true;
            if (!result.success) {
                m.error = result.error.isEmpty() ? QStringLiteral("13F breadth query failed")
                                                 : result.error.left(200);
            } else {
                const auto o = parse_object(result);
                m.error         = o.value(QStringLiteral("error")).toString();
                m.quarter       = iso_date(o, "quarter");
                m.prior_quarter = iso_date(o, "prior_quarter");
                m.sort          = o.value(QStringLiteral("sort")).toString();
                for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                    const auto r = v.toObject();
                    MoverRow row;
                    row.ticker        = r.value(QStringLiteral("ticker")).toString();
                    row.name          = r.value(QStringLiteral("name")).toString();
                    row.fund          = r.value(QStringLiteral("fund")).toBool();
                    row.holders       = r.value(QStringLiteral("holders")).toInt();
                    row.holders_prior = r.value(QStringLiteral("holders_prior")).toInt();
                    row.delta_holders = r.value(QStringLiteral("delta_holders")).toInt();
                    row.new_holders   = r.value(QStringLiteral("new")).toInt();
                    row.closed        = r.value(QStringLiteral("closed")).toInt();
                    row.added         = r.value(QStringLiteral("added")).toInt();
                    row.reduced       = r.value(QStringLiteral("reduced")).toInt();
                    row.shares        = r.value(QStringLiteral("shares")).toDouble();
                    row.value         = r.value(QStringLiteral("value")).toDouble();
                    row.focused_holders    = r.value(QStringLiteral("focused_holders")).toInt();
                    row.focused_net_shares = opt_num(r, "focused_net_shares");
                    row.top10_share        = opt_num(r, "top10_share");
                    if (!row.ticker.isEmpty())
                        m.rows.push_back(row);
                }
            }
            self->movers_ = m;
            emit self->movers_updated();
        },
        /*on_line=*/{}, kMoversTimeoutMs);
}

void OwnershipService::ensure_form4_current() {
    if (form4_scanning_ || form4_timer_->isActive())
        return;
    form4_passes_ = 0;
    run_form4_scan();
}

void OwnershipService::run_form4_scan() {
    if (form4_scanning_)
        return;
    form4_scanning_ = true;
    ++form4_passes_;
    form4_status_ = QStringLiteral("Reading recent Form 4 filings from EDGAR…");
    emit form4_status_changed(form4_status_);
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_form4_market.py"),
        {QStringLiteral("scan"), payload_of({{"days", kScanDays}, {"max_new_days", kScanDaysPerPass}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->form4_scanning_ = false;
            int remaining = 0;
            const QJsonObject o = result.success ? parse_object(result) : QJsonObject{};
            const QString soft_error = o.value(QStringLiteral("error")).toString();
            const int fetched = o.value(QStringLiteral("fetched")).toInt();
            if (!result.success || !soft_error.isEmpty()) {
                // The traceback, if any, sits after a line of dependency
                // warnings on stderr; keep the tail, which is the part that
                // says what happened.
                const QString err = !soft_error.isEmpty() ? soft_error
                                    : result.error.isEmpty() ? QStringLiteral("the reader did not finish")
                                                             : result.error.trimmed().section(QLatin1Char('\n'), -1).left(160);
                self->form4_status_ = QStringLiteral("Form 4 scan paused: %1 — retrying in 15 minutes").arg(err);
                self->form4_timer_->start(kScanRetryMs);
            } else {
                remaining = o.value(QStringLiteral("days_remaining")).toInt();
                const int read = o.value(QStringLiteral("days_read")).toInt();
                if (remaining > 0 && self->form4_passes_ < kScanMaxPasses) {
                    self->form4_status_ = QStringLiteral("Read %1 filings · %2 more business day%3 to go")
                                              .arg(fetched).arg(remaining)
                                              .arg(remaining == 1 ? QString() : QStringLiteral("s"));
                    // Straight on to the next slice; the store commits per day
                    // so the scan tab can already show what has landed.
                    self->form4_timer_->start(500);
                } else if (remaining > 0) {
                    // Pass budget spent with days still unread: say so rather
                    // than calling the store current, and come back sooner.
                    self->form4_status_ = QStringLiteral("%1 business day%2 of the window still unread — "
                                                         "continues in 15 minutes")
                                              .arg(remaining).arg(remaining == 1 ? QString() : QStringLiteral("s"));
                    self->form4_passes_ = 0;
                    self->form4_timer_->start(kScanRetryMs);
                } else {
                    self->form4_status_ = read > 0 || fetched > 0
                        ? QStringLiteral("Form 4 store current to %1")
                              .arg(QDate::currentDate().toString(QStringLiteral("d MMM")))
                        : QStringLiteral("Form 4 store current");
                    self->form4_passes_ = 0;
                    self->form4_timer_->start(kScanRecheckMs);
                }
            }
            emit self->form4_status_changed(self->form4_status_);
            // Whatever landed belongs on the scan, and on the reader's own
            // names — once the cycle has ended, not after every slice.
            if (self->insider_buys_.loaded && !self->insider_buys_loading_)
                self->load_insider_buys(self->insider_buys_.query);
            if (result.success && remaining == 0 && fetched > 0)
                self->check_holdings_alerts();
        },
        /*on_line=*/{}, kScanTimeoutMs);
}

// ── Where ownership reaches the reader ──────────────────────────────────────

void OwnershipService::load_watch(const QStringList& symbols) {
    QStringList syms;
    for (const auto& s : symbols) {
        const QString u = s.trimmed().toUpper();
        if (!u.isEmpty() && !syms.contains(u))
            syms << u;
    }
    if (syms.isEmpty())
        return;
    if (watch_loading_) {
        queued_watch_ = syms;
        return;
    }
    watch_loading_ = true;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("ownership_watch.py"),
        {QStringLiteral("rows"), payload_of({{"symbols", QJsonArray::fromStringList(syms)}, {"days", 30}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->watch_loading_ = false;
            WatchRows w;
            w.loaded = true;
            if (!result.success) {
                w.error = result.error.isEmpty() ? QStringLiteral("ownership rows failed")
                                                 : result.error.trimmed().section(QLatin1Char('\n'), -1).left(160);
            } else {
                const auto o = parse_object(result);
                w.error = o.value(QStringLiteral("error")).toString();
                w.quarter         = iso_date(o, "quarter");
                w.settlement      = iso_date(o, "settlement");
                w.published_after = iso_date(o, "published_after");
                w.form4_scanned_to = iso_date(o, "form4_scanned_to");
                w.days = o.value(QStringLiteral("days")).toInt(30);
                const auto rows = o.value(QStringLiteral("rows")).toObject();
                for (auto it = rows.begin(); it != rows.end(); ++it) {
                    const auto r = it.value().toObject();
                    WatchRow row;
                    if (r.contains(QStringLiteral("holders")))
                        row.holders = r.value(QStringLiteral("holders")).toInt();
                    if (r.value(QStringLiteral("delta_holders")).isDouble())
                        row.delta_holders = r.value(QStringLiteral("delta_holders")).toInt();
                    row.top10_share   = opt_num(r, "top10_share");
                    row.shares_short  = opt_num(r, "short");
                    row.days_to_cover = opt_num(r, "dtc");
                    row.si_change_pct = opt_num(r, "si_change_pct");
                    row.sirio         = opt_num(r, "sirio");
                    if (r.contains(QStringLiteral("insider_buys")))
                        row.insider_buys = r.value(QStringLiteral("insider_buys")).toInt();
                    row.insider_buyers = r.value(QStringLiteral("insider_buyers")).toInt();
                    row.insider_buy_value = r.value(QStringLiteral("insider_buy_value")).toDouble();
                    row.last_insider_buy = iso_date(r, "last_insider_buy");
                    w.rows.insert(it.key(), row);
                }
            }
            self->watch_ = w;
            emit self->watch_updated();
            if (!self->queued_watch_.isEmpty()) {
                const auto next = self->queued_watch_;
                self->queued_watch_.clear();
                self->load_watch(next);
            }
        },
        /*on_line=*/{}, 120'000);
}

void OwnershipService::load_calendar(int days) {
    if (calendar_loading_)
        return;
    calendar_loading_ = true;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("ownership_watch.py"),
        {QStringLiteral("calendar"), payload_of({{"days", days}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->calendar_loading_ = false;
            OwnershipCalendar c;
            c.loaded = true;
            if (!result.success) {
                c.error = result.error.isEmpty() ? QStringLiteral("calendar failed")
                                                 : result.error.trimmed().section(QLatin1Char('\n'), -1).left(160);
            } else {
                const auto o = parse_object(result);
                c.as_of = iso_date(o, "as_of");
                c.days = o.value(QStringLiteral("days")).toInt(90);
                for (const auto& v : o.value(QStringLiteral("form13f")).toArray()) {
                    const auto e = v.toObject();
                    CalendarEntry ce;
                    ce.kind = CalendarEntry::Kind::Form13FDeadline;
                    ce.date = iso_date(e, "due");
                    ce.basis = iso_date(e, "quarter_end");
                    c.entries.push_back(ce);
                }
                for (const auto& v : o.value(QStringLiteral("finra")).toArray()) {
                    const auto e = v.toObject();
                    CalendarEntry ce;
                    ce.kind = CalendarEntry::Kind::FinraPublication;
                    ce.date = iso_date(e, "published");
                    ce.basis = iso_date(e, "settlement");
                    c.entries.push_back(ce);
                }
                for (const auto& v : o.value(QStringLiteral("lockups")).toArray()) {
                    const auto e = v.toObject();
                    CalendarEntry ce;
                    ce.kind = CalendarEntry::Kind::LockupExpiry;
                    ce.date = iso_date(e, "expiry");
                    ce.basis = iso_date(e, "priced");
                    ce.symbol = e.value(QStringLiteral("symbol")).toString();
                    ce.company = e.value(QStringLiteral("company")).toString();
                    c.entries.push_back(ce);
                }
                std::stable_sort(c.entries.begin(), c.entries.end(),
                                 [](const CalendarEntry& a, const CalendarEntry& b) { return a.date < b.date; });
            }
            self->calendar_ = c;
            emit self->calendar_updated();
        },
        /*on_line=*/{}, 120'000);
}

QStringList OwnershipService::own_symbols() const {
    QStringList out;
    auto add = [&out](const QString& s) {
        const QString u = s.trimmed().toUpper();
        if (!u.isEmpty() && !out.contains(u))
            out << u;
    };
    auto& pf = fincept::PortfolioRepository::instance();
    if (const auto lists = pf.list_portfolios(); lists.is_ok())
        for (const auto& p : lists.value())
            if (const auto assets = pf.get_assets(p.id); assets.is_ok())
                for (const auto& a : assets.value())
                    add(a.symbol);
    auto& wl = fincept::WatchlistRepository::instance();
    if (const auto lists = wl.list_all(); lists.is_ok())
        for (const auto& w : lists.value())
            if (const auto stocks = wl.get_stocks(w.id); stocks.is_ok())
                for (const auto& s : stocks.value())
                    add(s.symbol);
    return out;
}

namespace {
QString alert_stamps_path() {
    return fincept::AppPaths::data() + QStringLiteral("/ownership_alerts.json");
}
QJsonObject read_stamps() {
    QFile f(alert_stamps_path());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
void write_stamps(const QJsonObject& o) {
    QDir().mkpath(fincept::AppPaths::data());
    QFile f(alert_stamps_path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
} // namespace

void OwnershipService::check_holdings_alerts() {
    if (alerts_checking_)
        return;
    // The same switch the notification service honours, read here too so the
    // in-app toast and the stamp file respect it: off means nothing fires and
    // nothing is remembered as "already announced".
    {
        // The repository answers "" for a key never saved; that is the
        // default, and the default is on.
        const auto r = fincept::SettingsRepository::instance().get(QStringLiteral("notifications.ownership_alerts"));
        if (r.is_ok() && r.value() == QLatin1String("0"))
            return;
    }
    const QStringList syms = own_symbols();
    if (syms.isEmpty())
        return;
    alerts_checking_ = true;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("ownership_watch.py"),
        {QStringLiteral("alerts"), payload_of({{"symbols", QJsonArray::fromStringList(syms)}, {"days", 7}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->alerts_checking_ = false;
            if (!result.success)
                return;
            const auto o = parse_object(result);
            QJsonObject stamps = read_stamps();
            bool changed = false;
            for (const auto& v : o.value(QStringLiteral("rows")).toArray()) {
                const auto r = v.toObject();
                const QString sym = r.value(QStringLiteral("symbol")).toString();
                const QString filed = r.value(QStringLiteral("last_filed")).toString();
                const int trades = r.value(QStringLiteral("trades")).toInt();
                const int insiders = r.value(QStringLiteral("insiders")).toInt();
                if (sym.isEmpty() || filed.isEmpty())
                    continue;
                // Once per issuer per NEW filing. The stamp is the newest filing
                // date announced and the trade count seen with it: a second
                // insider whose filing lands the same day in a later pass is
                // news, and a re-read of the same filings is not.
                const QJsonObject prev = stamps.value(sym).toObject();
                const QString prev_filed = prev.value(QStringLiteral("filed")).toString();
                const int prev_trades = prev.value(QStringLiteral("trades")).toInt();
                if (prev_filed > filed || (prev_filed == filed && prev_trades >= trades))
                    continue;
                stamps.insert(sym, QJsonObject{{QStringLiteral("filed"), filed}, {QStringLiteral("trades"), trades}});
                changed = true;
                const double value = r.value(QStringLiteral("value")).toDouble();
                const QString who = insiders >= 2 ? QStringLiteral("%1 insiders").arg(insiders)
                                                  : QStringLiteral("an insider");
                // A filing, stated as one. The evidence line is what the
                // twelve-month check found for THIS category, anchored on the
                // filing date — the day a reader could act — with clusters
                // marked point in time (plans/research/form4_forward_returns).
                // It is not a call. One delivery, through the notification
                // service, which owns the in-app toast and the user's switches.
                const QString msg = QStringLiteral(
                    "%1: %2 bought on the open market (Form 4 filed %3, $%4 in total). "
                    "On this universe a %5 was followed by a median %6 over the index in a "
                    "month, %7 of the time (Jul 2025 – Jun 2026).")
                    .arg(sym, who, filed, QString::number(value / 1000.0, 'f', 0) + QStringLiteral("K"),
                         insiders >= 2 ? QStringLiteral("cluster buy") : QStringLiteral("single insider buy"),
                         insiders >= 2 ? QStringLiteral("+1.8%") : QStringLiteral("+1.2%"),
                         insiders >= 2 ? QStringLiteral("59%") : QStringLiteral("55%"));
                notifications::NotificationRequest req;
                req.title = QStringLiteral("Insider buy — %1").arg(sym);
                req.message = msg;
                req.level = notifications::NotifLevel::Alert;
                req.trigger = notifications::NotifTrigger::OwnershipAlert;
                notifications::NotificationService::instance().send(req);
            }
            if (changed)
                write_stamps(stamps);
        },
        /*on_line=*/{}, 60'000);
}

// ── Filers ───────────────────────────────────────────────────────────────────

void OwnershipService::search_firms(const QString& query) {
    QPointer<OwnershipService> self = this;
    const QString q = query.trimmed();
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("firms"), payload_of({{"query", q}, {"limit", 40}})},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->firm_results_.clear();
            if (result.success) {
                const auto root = parse_object(result);
                for (const auto& v : root.value(QStringLiteral("firms")).toArray()) {
                    const auto o = v.toObject();
                    Manager m;
                    m.cik  = o.value(QStringLiteral("cik")).toString();
                    m.name = o.value(QStringLiteral("manager")).toString();
                    m.book_value     = o.value(QStringLiteral("book_value")).toDouble();
                    m.position_count = o.value(QStringLiteral("position_count")).toInt();
                    if (!m.cik.isEmpty())
                        self->firm_results_.push_back(m);
                }
            }
            emit self->firms_found();
        },
        /*on_line=*/{}, 60'000);
}

ownership::ManagerBook OwnershipService::book(const QString& cik) const {
    return books_.value(cik);
}

bool OwnershipService::is_book_loading(const QString& cik) const {
    return books_in_flight_.contains(cik);
}

void OwnershipService::load_book(const QString& cik) {
    if (cik.isEmpty() || books_in_flight_.contains(cik))
        return;
    // A filed quarter is immutable, so a cached book is never stale in-session.
    if (books_.contains(cik) && books_.value(cik).error.isEmpty()) {
        emit book_updated(cik);
        return;
    }
    books_in_flight_.insert(cik);
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("book"), payload_of({{"cik", cik}, {"limit", 250}})},
        [self, cik](python::PythonResult result) {
            if (!self)
                return;
            ManagerBook b;
            b.cik = cik;
            if (!result.success) {
                b.error = result.error.isEmpty() ? QStringLiteral("13F index query failed")
                                                 : result.error.left(200);
            } else {
                const auto root = parse_object(result);
                const QString err = root.value(QStringLiteral("error")).toString();
                if (!err.isEmpty())
                    b.error = err;
                else
                    parse_book_into(root, b);
            }
            self->books_.insert(cik, b);
            self->books_in_flight_.remove(cik);
            emit self->book_updated(cik);
            if (b.error.isEmpty() && !b.positions.isEmpty())
                self->price_book(cik);
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::price_book(const QString& cik) {
    auto b = books_.value(cik);
    if (b.positions.isEmpty() || !b.period.isValid() || pricing_in_flight_.contains(cik))
        return;
    pricing_in_flight_.insert(cik);

    QJsonArray syms;
    QSet<QString> seen;
    for (const auto& p : b.positions) {
        if (p.ticker.isEmpty() || seen.contains(p.ticker))
            continue;
        seen.insert(p.ticker);
        syms.append(p.ticker);
        if (syms.size() >= 120)
            break;
    }
    if (syms.isEmpty()) {
        pricing_in_flight_.remove(cik);
        return;
    }
    const QDate from = b.period.addMonths(-7);
    QPointer<OwnershipService> self = this;
    python::PythonWorker::instance().submit(
        QStringLiteral("batch_closes"),
        QJsonObject{{"symbols", syms},
                    {"start", from.toString(Qt::ISODate)},
                    {"end", QDate::currentDate().toString(Qt::ISODate)}},
        [self, cik](bool ok, QJsonObject result, QString err) {
            if (!self)
                return;
            self->pricing_in_flight_.remove(cik);
            auto book = self->books_.value(cik);
            if (book.positions.isEmpty())
                return;
            if (!ok) {
                book.return_error = err.isEmpty() ? QStringLiteral("price history unavailable") : err;
                self->books_.insert(cik, book);
                emit self->book_updated(cik);
                return;
            }
            const auto closes = result.value(QStringLiteral("closes")).toObject();
            const QDate now = QDate::currentDate();
            const QVector<QDate> marks{book.period, now, now.addMonths(-3), now.addMonths(-6)};
            double covered = 0.0, wsum_qe = 0.0, wsum_3m = 0.0, w3 = 0.0;
            for (auto& p : book.positions) {
                if (p.ticker.isEmpty() || !closes.contains(p.ticker))
                    continue;
                const auto px = closes_on_or_before(closes.value(p.ticker).toArray(), marks);
                const auto& now_px = px[1];
                if (!now_px || *now_px <= 0)
                    continue;
                p.priced = true;
                const double value = p.value.value_or(0.0);
                if (px[0] && *px[0] > 0) {
                    p.ret_since_quarter_end = (*now_px / *px[0]) - 1.0;
                    covered += value;
                    wsum_qe += *p.ret_since_quarter_end * value;
                }
                if (px[2] && *px[2] > 0) {
                    p.ret_3m = (*now_px / *px[2]) - 1.0;
                    wsum_3m += *p.ret_3m * value;
                    w3 += value;
                }
                if (px[3] && *px[3] > 0)
                    p.ret_6m = (*now_px / *px[3]) - 1.0;
            }
            // Against the filer's WHOLE book, not the fetched slice.
            book.return_coverage = book.total_value > 0 ? covered / book.total_value : 0.0;
            if (covered > 0)
                book.book_return_since_quarter_end = wsum_qe / covered;
            if (w3 > 0)
                book.book_return_3m = wsum_3m / w3;
            self->books_.insert(cik, book);
            emit self->book_updated(cik);
        },
        python::PythonWorker::kComputeActionTimeoutMs);
}

// ── Local 13F index ──────────────────────────────────────────────────────────

bool OwnershipService::index_ready() const {
    // Seeded from the database on first ask, latched on SUCCESS: a failed probe
    // tells us nothing about whether an index exists, and latching on attempt
    // left a built index reading "no index" for the whole session.
    if (!index_probed_ && !index_probing_) {
        index_probing_ = true;
        const_cast<OwnershipService*>(this)->probe_index();
    }
    return !index_status_.isEmpty();
}

void OwnershipService::probe_index() {
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("status"), QStringLiteral("{}")},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->index_probing_ = false;
            if (!result.success) {
                LOG_WARN(TAG, "13F index probe failed, will retry: " + result.error.left(160));
                return;
            }
            self->index_probed_ = true;
            const auto o = parse_object(result);
            QStringList parts;
            for (const auto& v : o.value(QStringLiteral("quarters")).toArray()) {
                const auto q = v.toObject();
                parts << QStringLiteral("%1 (%2 filers%3)")
                             .arg(q.value(QStringLiteral("quarter")).toString())
                             .arg(q.value(QStringLiteral("filers")).toInt())
                             .arg(q.value(QStringLiteral("partial")).toBool()
                                      ? QStringLiteral(", partial") : QString());
            }
            if (!parts.isEmpty()) {
                self->index_status_ = QStringLiteral("13F index: ") + parts.join(QStringLiteral(", "));
                emit self->index_changed(self->index_status_);
            }
        },
        /*on_line=*/{}, 30'000);
}

QString OwnershipService::index_status_text() const {
    return index_status_.isEmpty()
               ? QStringLiteral("No 13F index built yet — the holder list, the 13F movers and "
                                "the short-interest ranking need one.")
               : index_status_;
}

void OwnershipService::check_for_newer_quarter() {
    if (index_busy_ || !index_ready())
        return;
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("newer"), QStringLiteral("{}")},
        [self](python::PythonResult result) {
            if (!self || !result.success)
                return;
            const auto o = parse_object(result);
            const int unseen = o.value(QStringLiteral("unseen_datasets")).toInt();
            if (unseen > 0) {
                emit self->index_changed(
                    QStringLiteral("%1 · %2 newer SEC data set%3 not yet ingested — rebuild to "
                                   "bring the holder data forward.")
                        .arg(self->index_status_).arg(unseen)
                        .arg(unseen == 1 ? QString() : QStringLiteral("s")));
            }
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::build_index() {
    if (index_busy_)
        return;
    index_busy_ = true;
    emit index_changed(QStringLiteral("Downloading SEC 13F data sets…"));
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        // Two quarters: a single quarter is a photograph, and every change
        // count on the screen needs the frame before it.
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("ingest_recent"), QStringLiteral("{\"quarters\":2}")},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->index_busy_ = false;
            QString msg;
            if (!result.success) {
                msg = QStringLiteral("Index build failed: ") + result.error.left(200);
            } else {
                const auto o = parse_object(result);
                const QString err = o.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    msg = err;
                } else {
                    QStringList qs;
                    for (const auto& v : o.value(QStringLiteral("ingested")).toArray()) {
                        const auto q = v.toObject();
                        qs << QStringLiteral("%1 (%2 filers)")
                                  .arg(q.value(QStringLiteral("quarter")).toString())
                                  .arg(q.value(QStringLiteral("filers")).toInt());
                    }
                    self->index_status_ = qs.isEmpty() ? QStringLiteral("13F index built")
                                                       : QStringLiteral("13F index: ") + qs.join(QStringLiteral(", "));
                    self->index_probed_ = false;
                    // Chain straight into symbol mapping: an index nobody can
                    // search by ticker is not finished.
                    self->resolve_symbols();
                    return;
                }
            }
            emit self->index_changed(msg);
        },
        /*on_line=*/{}, kIndexBuildTimeoutMs);
}

void OwnershipService::resolve_symbols(int /*limit*/) {
    if (index_busy_)
        return;
    index_busy_ = true;
    emit index_changed(QStringLiteral("Mapping CUSIPs to tickers — usable within a few "
                                      "minutes, finishes in the background."));
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("resolve_all"), QStringLiteral("{\"chunk\":500}")},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->index_busy_ = false;
            const auto o = result.success ? parse_object(result) : QJsonObject{};
            const int left = o.value(QStringLiteral("remaining")).toInt();
            self->index_probed_ = false;
            self->index_status_ =
                left > 0 ? QStringLiteral("Mapped %1 symbols · %2 still unmapped")
                               .arg(o.value(QStringLiteral("resolved")).toInt()).arg(left)
                         : QStringLiteral("Symbol map complete · %1 mapped")
                               .arg(o.value(QStringLiteral("resolved")).toInt());
            emit self->index_changed(self->index_status_);
        },
        /*on_line=*/{}, kIndexBuildTimeoutMs);
}

} // namespace fincept::services
