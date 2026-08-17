#include "services/ownership/OwnershipService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QStandardPaths>

namespace fincept::services {

using namespace fincept::ownership;

namespace {

constexpr const char* TAG = "Ownership";

/// Ownership data is the slowest-moving on any screen: Form 4s land within two
/// business days but are then permanent, 13F is quarterly, short interest is a
/// twice-monthly settlement snapshot. An hour of cache costs nothing in
/// freshness and saves a minute of EDGAR round-trips per revisit.
constexpr qint64 kCacheTtlMs = 60 * 60 * 1000;

/// EDGAR parses one XML document per Form 4 at 0.4s apiece, so this is the
/// wall-clock ceiling on a busy issuer rather than a network timeout.
constexpr int kEdgarTimeoutMs = 180'000;

/// Trailing window for both Form 4s and 13D/G. A year is enough to see the
/// current register's behaviour without spending minutes on a large issuer.
constexpr int kWindowMonths = 12;
/// Filing cap. Reported to the user when it bites — a silently truncated
/// window reads as a quiet period when it may be a busy one.
constexpr int kMaxFilings = 60;

/// Every tracked manager costs a submissions fetch, a directory listing and an
/// information table per quarter, all at EDGAR's 0.4s floor. Twenty managers is
/// therefore minutes, not seconds — hence the separate call and the long
/// ceiling.
constexpr int kSmartMoneyTimeoutMs = 600'000;

// Source names for the in-flight set. Each fetch clears its own.
const QString kSrcEdgar  = QStringLiteral("edgar");
const QString kSrcMarket = QStringLiteral("market");
const QString kSrcSmart  = QStringLiteral("smart");

/// A quarterly data set is ~100 MB and indexes 3.3m rows; symbol
/// resolution is rate-limited by OpenFIGI to 25 requests a minute.
constexpr int kIndexBuildTimeoutMs = 1'800'000;

std::optional<double> opt_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    // Absent stays absent. A grant reports no price and a defaulted 0.0 would
    // render as "acquired at $0.00" — a number the filing never stated.
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
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(v.toDouble()), QTimeZone::UTC).date();
}

Pattern pattern_from(const QString& s) {
    if (s == QLatin1String("routine"))       return Pattern::Routine;
    if (s == QLatin1String("opportunistic")) return Pattern::Opportunistic;
    return Pattern::Unclassified;
}

/// Errors from the `all` action live INSIDE each half, not at the top level.
/// Returns the first one found, or an empty string.
///
/// Missing this is not a cosmetic bug: parsing an {"error": ...} object yields
/// zeros and empty arrays for every key, so an unlisted ticker or an EDGAR
/// outage renders as "0 Form 4 filings, no 5% holders" with a clean status bar
/// — the screen stating as fact that a company has no insider activity when the
/// fetch simply failed.
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
    // `all` nests the two halves; `insiders` alone is the flat shape.
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
    snap.window_months     = ins.value(QStringLiteral("window_months")).toInt();

    snap.transactions.clear();
    for (const auto& v : ins.value(QStringLiteral("transactions")).toArray()) {
        const QJsonObject o = v.toObject();
        InsiderTransaction t;
        t.insider     = o.value(QStringLiteral("insider")).toString();
        t.date        = iso_date(o, "date");
        t.code        = o.value(QStringLiteral("code")).toString();
        t.code_label  = o.value(QStringLiteral("code_label")).toString();
        t.security    = o.value(QStringLiteral("security")).toString();
        t.derivative  = o.value(QStringLiteral("derivative")).toBool();
        t.open_market = o.value(QStringLiteral("open_market")).toBool();
        t.acquired    = o.value(QStringLiteral("direction")).toString() == QLatin1String("acquired");
        t.source_url  = o.value(QStringLiteral("source_url")).toString();
        t.filed_date  = iso_date(o, "filed_date");
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

    snap.clusters.clear();
    for (const auto& v : ins.value(QStringLiteral("clusters")).toArray()) {
        const QJsonObject o = v.toObject();
        BuyCluster c;
        c.start = iso_date(o, "start");
        c.end   = iso_date(o, "end");
        for (const auto& n : o.value(QStringLiteral("insiders")).toArray())
            c.insiders << n.toString();
        c.total_value = o.value(QStringLiteral("total_value")).toDouble();
        if (c.start.isValid())
            snap.clusters.push_back(c);
    }

    const QJsonObject stk = root.value(QStringLiteral("stakes")).isObject()
                                ? root.value(QStringLiteral("stakes")).toObject()
                                : QJsonObject{};
    snap.stakes.clear();
    for (const auto& v : stk.value(QStringLiteral("stakes")).toArray()) {
        const QJsonObject o = v.toObject();
        BeneficialStake b;
        b.form       = o.value(QStringLiteral("form")).toString();
        b.activist   = o.value(QStringLiteral("activist")).toBool();
        b.amendment  = o.value(QStringLiteral("amendment")).toBool();
        b.filed_date = iso_date(o, "filed_date");
        b.url        = o.value(QStringLiteral("url")).toString();
        if (!b.form.isEmpty())
            snap.stakes.push_back(b);
    }
}

void parse_market_into(const QJsonObject& o, OwnershipSnapshot& snap) {
    snap.holders.clear();
    for (const auto& v : o.value(QStringLiteral("institutional_holders")).toArray()) {
        const QJsonObject h = v.toObject();
        InstitutionalHolder ih;
        ih.holder = h.value(QStringLiteral("holder")).toString();
        ih.as_of  = iso_date(h, "date");
        ih.pct    = opt_num(h, "pct");
        ih.shares = opt_num(h, "shares");
        ih.value  = opt_num(h, "value");
        if (!ih.holder.isEmpty())
            snap.holders.push_back(ih);
    }

    // The vendor's holder table carries its own as-of quarter, which is what
    // lets the screen tell the reader when it is fresher than the bulk index.
    for (const auto& v : o.value(QStringLiteral("institutional_holders")).toArray()) {
        const QDate d = iso_date(v.toObject(), "date");
        if (d.isValid() && (!snap.vendor_quarter.isValid() || d > snap.vendor_quarter))
            snap.vendor_quarter = d;
    }

    const QJsonObject s = o.value(QStringLiteral("short_interest")).toObject();
    ShortInterest si;
    si.shares_short          = opt_num(s, "shares_short");
    si.shares_short_prior    = opt_num(s, "shares_short_prior");
    si.short_ratio           = opt_num(s, "short_ratio");
    si.pct_float             = opt_num(s, "short_pct_float");
    si.float_shares          = opt_num(s, "float_shares");
    si.shares_outstanding    = opt_num(s, "shares_outstanding");
    si.held_pct_insiders     = opt_num(s, "held_pct_insiders");
    si.held_pct_institutions = opt_num(s, "held_pct_institutions");
    si.as_of                 = epoch_date(s, "date_short_interest");
    si.prior_as_of           = epoch_date(s, "date_short_prior");
    snap.shorts = si;
}


/// Attach manager names and styles to the positions the script returned.
///
/// The script works in CIKs because that is what EDGAR keys on; the display
/// name and the style live in the curated list. Joining here rather than in the
/// script keeps the user's editable list as the single source of both.
void parse_smart_money_into(const QJsonObject& root, OwnershipSnapshot& snap,
                            const QVector<Manager>& managers) {
    QHash<QString, Manager> by_cik;
    for (const auto& m : managers) {
        if (!m.cik.isEmpty())
            by_cik.insert(m.cik, m);
    }

    snap.smart_money.clear();
    for (const auto& v : root.value(QStringLiteral("holders")).toArray()) {
        const QJsonObject o = v.toObject();
        ManagerPosition p;
        p.cik = o.value(QStringLiteral("cik")).toString();
        const Manager m = by_cik.value(p.cik);
        // Fall back to the CIK rather than inventing a name. An unnamed row is
        // still a true row; a wrong name attached to a real position is not.
        p.manager = m.name.isEmpty() ? QStringLiteral("CIK ") + p.cik : m.name;
        p.style = m.style;
        p.issuer = o.value(QStringLiteral("issuer")).toString();
        p.cusip = o.value(QStringLiteral("cusip")).toString();
        p.period = iso_date(o, "period");
        p.filed_date = iso_date(o, "filed_date");
        p.shares = opt_num(o, "shares");
        p.value = opt_num(o, "value");
        p.weight = opt_num(o, "weight");
        p.book_total = opt_num(o, "book_total");
        p.position_count = o.value(QStringLiteral("position_count")).toInt();
        p.action = o.value(QStringLiteral("action")).toString();
        p.shares_delta = opt_num(o, "shares_delta");
        p.pct_change = opt_num(o, "pct_change");
        if (!p.cik.isEmpty())
            snap.smart_money.push_back(p);
    }
}


/// One manager's book, newest quarter, plus the moves that produced it.
void parse_book_into(const QJsonObject& root, ManagerBook& b) {
    const auto books = root.value(QStringLiteral("books")).toArray();
    if (books.isEmpty()) {
        b.error = QStringLiteral("no 13F filings found for this manager");
        return;
    }
    const QJsonObject latest = books.first().toObject();
    b.period      = iso_date(latest, "period");
    b.filed_date  = iso_date(latest, "filed_date");
    b.total_value = latest.value(QStringLiteral("total_value")).toDouble();
    b.position_count = latest.value(QStringLiteral("position_count")).toInt();
    b.value_basis = latest.value(QStringLiteral("value_basis")).toString();

    for (const auto& v : latest.value(QStringLiteral("positions")).toArray()) {
        const QJsonObject o = v.toObject();
        BookPosition p;
        p.issuer = o.value(QStringLiteral("issuer")).toString();
        p.cusip  = o.value(QStringLiteral("cusip")).toString();
        p.security_class = o.value(QStringLiteral("class")).toString();
        p.shares = opt_num(o, "shares");
        p.value  = opt_num(o, "value");
        p.weight = opt_num(o, "weight");
        if (!p.cusip.isEmpty())
            b.positions.push_back(p);
    }

    for (const auto& v : root.value(QStringLiteral("moves")).toObject()
                             .value(QStringLiteral("moves")).toArray()) {
        const QJsonObject o = v.toObject();
        ManagerPosition m;
        m.manager = b.manager;
        m.cik     = b.cik;
        m.style   = b.style;
        // `label` carries the share class — Alphabet A and C share an issuer
        // name and would otherwise appear as one row printed twice.
        m.issuer  = o.value(QStringLiteral("label")).toString().isEmpty()
                        ? o.value(QStringLiteral("issuer")).toString()
                        : o.value(QStringLiteral("label")).toString();
        m.cusip   = o.value(QStringLiteral("cusip")).toString();
        m.action  = o.value(QStringLiteral("action")).toString();
        m.shares  = opt_num(o, "shares");
        m.weight  = opt_num(o, "weight");
        m.shares_delta = opt_num(o, "shares_delta");
        m.pct_change   = opt_num(o, "pct_change");
        m.period = b.period;
        if (!m.cusip.isEmpty())
            b.moves.push_back(m);
    }
}

} // namespace

OwnershipService& OwnershipService::instance() {
    static OwnershipService s;
    return s;
}

ownership::OwnershipSnapshot OwnershipService::snapshot(const QString& symbol) const {
    return cache_.value(symbol.toUpper());
}

bool OwnershipService::is_loading(const QString& symbol) const {
    return !pending_.value(symbol.toUpper()).isEmpty();
}

void OwnershipService::load(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    // Only the register's own sources block a register load. A smart-money
    // fetch running alongside is unrelated and must not gate it.
    const auto in_flight = pending_.value(sym);
    if (in_flight.contains(kSrcEdgar) || in_flight.contains(kSrcMarket))
        return;

    const qint64 age = QDateTime::currentMSecsSinceEpoch() - fetched_at_.value(sym, 0);
    if (cache_.contains(sym) && age < kCacheTtlMs) {
        emit snapshot_updated(sym);
        emit load_finished(sym);
        return;
    }
    refresh(sym);
}

void OwnershipService::refresh(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    // Same in-flight guard as load(). Without it, pressing REFRESH during a
    // load resets the counter to 2 with four callbacks live; it then decrements
    // past zero, so load_finished fires while two fetches are still running and
    // the status bar drops "loading…" early.
    const auto in_flight = pending_.value(sym);
    if (in_flight.contains(kSrcEdgar) || in_flight.contains(kSrcMarket))
        return;

    OwnershipSnapshot fresh;
    fresh.symbol = sym;
    cache_.insert(sym, fresh);
    // Deliberately NOT stamping fetched_at_ here. Stamping before the fetch
    // returns means a failure — offline, EDGAR 403, daemon down — is cached as
    // fresh for the full hour, and every retry inside that window re-serves the
    // empty snapshot without going near the network. The clock starts only
    // once something actually came back.
    fetched_at_.remove(sym);

    // Both halves go out together and render as each lands, so the fast one
    // (holders, short interest) is on screen while EDGAR is still parsing.
    auto& set = pending_[sym];
    set.insert(kSrcEdgar);
    set.insert(kSrcMarket);
    fetch_market(sym);
    fetch_edgar(sym);
}

void OwnershipService::note_source_done(const QString& symbol, const QString& source) {
    auto& set = pending_[symbol];
    set.remove(source);
    emit snapshot_updated(symbol);
    if (set.isEmpty()) {
        pending_.remove(symbol);
        // Only a snapshot with something in it earns a cache timestamp; a
        // failed pair stays uncached so the next visit retries.
        const auto snap = cache_.value(symbol);
        if (snap.edgar_ok || snap.market_ok)
            fetched_at_.insert(symbol, QDateTime::currentMSecsSinceEpoch());
        emit load_finished(symbol);
    }
}

void OwnershipService::fetch_edgar(const QString& sym) {
    QPointer<OwnershipService> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"symbol", sym},
                                  {"months", kWindowMonths},
                                  {"max_filings", kMaxFilings}})
            .toJson(QJsonDocument::Compact));

    python::PythonRunner::instance().run(
        QStringLiteral("sec_ownership_data.py"), {QStringLiteral("all"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            if (!result.success) {
                snap.edgar_error = result.error.isEmpty()
                                       ? QStringLiteral("EDGAR fetch failed")
                                       : result.error.left(200);
                LOG_WARN(TAG, "sec_ownership_data failed for " + sym + ": " + snap.edgar_error);
            } else {
                const QJsonDocument doc =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8());
                const QJsonObject root = doc.object();
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
                    snap.market_error = QStringLiteral("holder data unavailable");
                LOG_WARN(TAG, "ownership_extras failed for " + sym + ": " + snap.market_error);
            } else {
                parse_market_into(result, snap);
                snap.market_ok = true;
                // The script reports per-section failures rather than one
                // top-level error, so a .info exception leaves short_interest
                // absent while everything else succeeded. Without surfacing it,
                // the screen prints "No short-interest figures reported for X"
                // — a claim about the company derived from a fetch error.
                QStringList partial;
                for (const char* key : {"holders_error", "major_error", "short_error"}) {
                    const QString e = result.value(QLatin1String(key)).toString();
                    if (!e.isEmpty())
                        partial << QString::fromLatin1(key).remove(QStringLiteral("_error")) +
                                       QStringLiteral(": ") + e.left(120);
                }
                if (!partial.isEmpty()) {
                    snap.market_error = partial.join(QStringLiteral("; "));
                    LOG_WARN(TAG, "ownership_extras partial for " + sym + ": " + snap.market_error);
                }
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcMarket);
        },
        // Not kNetworkActionTimeoutMs (10s): ownership_extras makes three
        // separate yfinance calls — institutional holders, major holders and
        // .info — and .info alone regularly takes longer than that.
        python::PythonWorker::kComputeActionTimeoutMs);
}


// ── Tracked 13F managers ────────────────────────────────────────────────────

QString OwnershipService::managers_file_path() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("managers_13f.json"));
}

QVector<ownership::Manager> OwnershipService::managers() const {
    if (!managers_cache_.isEmpty())
        return managers_cache_;

    // The user's list wins when present. Written as plain JSON in the app data
    // directory rather than hidden in QSettings so it can be edited by hand —
    // "which managers do I care about" is exactly the kind of preference
    // someone wants to keep in a file they can diff and back up.
    QFile f(managers_file_path());
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(f.readAll());
        for (const auto& v : doc.array()) {
            const auto o = v.toObject();
            ownership::Manager m;
            m.name = o.value(QStringLiteral("name")).toString();
            m.cik = o.value(QStringLiteral("cik")).toString();
            m.style = o.value(QStringLiteral("style")).toString();
            m.user_added = true;
            if (!m.name.isEmpty())
                managers_cache_.push_back(m);
        }
        if (!managers_cache_.isEmpty())
            return managers_cache_;
        LOG_WARN(TAG, "managers_13f.json present but empty or unreadable — using defaults");
    }
    return {};  // caller falls back to the script's curated defaults
}

bool OwnershipService::set_managers(const QVector<ownership::Manager>& list) {
    const QString path = managers_file_path();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (list.isEmpty()) {
        managers_cache_.clear();
        return QFile::exists(path) ? QFile::remove(path) : true;
    }
    QJsonArray arr;
    for (const auto& m : list) {
        arr.append(QJsonObject{{"name", m.name}, {"cik", m.cik}, {"style", m.style}});
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_WARN(TAG, "cannot write manager list to " + path);
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    managers_cache_ = list;
    emit managers_changed();
    return true;
}

void OwnershipService::load_smart_money(const QString& symbol) {
    // Served from the local 13F index when one exists: the whole universe,
    // ~20ms, no network. The per-manager EDGAR path below is the fallback for
    // an un-indexed install and is kept only for that.
    if (index_ready()) {
        load_index_holders(symbol);
        return;
    }

    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    auto snap = cache_.value(sym);
    if (snap.smart_money_ok && !snap.smart_money.isEmpty())
        return;  // already have it for this symbol

    QJsonObject payload{{"symbol", sym}};
    QJsonArray ciks;
    for (const auto& m : managers()) {
        if (!m.cik.isEmpty())
            ciks.append(m.cik);
    }
    if (!ciks.isEmpty())
        payload.insert(QStringLiteral("ciks"), ciks);
    payload.insert(QStringLiteral("company"),
                   snap.company.isEmpty() ? sym : snap.company);

    if (pending_.value(sym).contains(kSrcSmart))
        return; // this fetch is already running
    pending_[sym].insert(kSrcSmart);
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_data.py"),
        {QStringLiteral("holders"),
         QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            if (!result.success) {
                snap.smart_money_error = result.error.isEmpty()
                                             ? QStringLiteral("13F fetch failed")
                                             : result.error.left(200);
            } else {
                const auto root =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                const QString err = root.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    snap.smart_money_error = err;
                } else {
                    // The script resolves any manager CIKs it had to look up and
                    // hands them back. Persisting them turns a per-call cost of
                    // ~22 EDGAR company searches into a one-off.
                    const auto resolved = root.value(QStringLiteral("resolved_managers")).toArray();
                    if (!resolved.isEmpty() && self->managers().isEmpty()) {
                        QVector<Manager> list;
                        for (const auto& v : resolved) {
                            const auto o = v.toObject();
                            Manager m;
                            m.name = o.value(QStringLiteral("name")).toString();
                            m.cik = o.value(QStringLiteral("cik")).toString();
                            m.style = o.value(QStringLiteral("style")).toString();
                            if (!m.name.isEmpty())
                                list.push_back(m);
                        }
                        if (!list.isEmpty()) {
                            self->set_managers(list);
                            LOG_INFO(TAG, QString("Seeded %1 tracked 13F managers into %2")
                                              .arg(list.size())
                                              .arg(OwnershipService::managers_file_path()));
                        }
                    }
                    parse_smart_money_into(root, snap, self->managers());
                    snap.smart_money_ok = true;
                }
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcSmart);
        },
        /*on_line=*/{}, kSmartMoneyTimeoutMs);
}


// ── BY FIRM ─────────────────────────────────────────────────────────────────

ownership::ManagerBook OwnershipService::book(const QString& cik) const {
    return books_.value(cik);
}

bool OwnershipService::is_book_loading(const QString& cik) const {
    return books_in_flight_.contains(cik);
}

void OwnershipService::load_book(const QString& cik) {
    if (cik.isEmpty() || books_in_flight_.contains(cik))
        return;
    // Books are quarterly and immutable once filed, so a cached one is never
    // stale within a session.
    if (books_.contains(cik) && books_.value(cik).error.isEmpty()) {
        emit book_updated(cik);
        return;
    }
    books_in_flight_.insert(cik);

    QString name, style;
    for (const auto& m : managers()) {
        if (m.cik == cik) { name = m.name; style = m.style; break; }
    }

    QPointer<OwnershipService> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"cik", cik}, {"quarters", 2}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_data.py"), {QStringLiteral("book"), payload},
        [self, cik, name, style](python::PythonResult result) {
            if (!self)
                return;
            ManagerBook b;
            b.cik = cik;
            b.manager = name.isEmpty() ? QStringLiteral("CIK ") + cik : name;
            b.style = style;
            if (!result.success) {
                b.error = result.error.isEmpty() ? QStringLiteral("13F fetch failed")
                                                 : result.error.left(200);
            } else {
                const auto root =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                const QString err = root.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    b.error = err;
                } else {
                    parse_book_into(root, b);
                }
            }
            self->books_.insert(cik, b);
            self->books_in_flight_.remove(cik);
            emit self->book_updated(cik);
        },
        /*on_line=*/{}, kSmartMoneyTimeoutMs);
}


void OwnershipService::seed_default_managers() {
    if (books_in_flight_.contains(QStringLiteral("__seed__")))
        return;
    books_in_flight_.insert(QStringLiteral("__seed__"));
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_data.py"),
        {QStringLiteral("managers"), QStringLiteral("{\"resolve\":true}")},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->books_in_flight_.remove(QStringLiteral("__seed__"));
            if (!result.success) {
                LOG_WARN(TAG, "manager seed failed: " + result.error.left(200));
                emit self->managers_changed();
                return;
            }
            const auto root =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            QVector<Manager> list;
            for (const auto& v : root.value(QStringLiteral("managers")).toArray()) {
                const auto o = v.toObject();
                Manager m;
                m.name = o.value(QStringLiteral("name")).toString();
                m.cik = o.value(QStringLiteral("cik")).toString();
                m.style = o.value(QStringLiteral("style")).toString();
                // A manager whose CIK would not resolve is dropped rather than
                // kept with an empty one: a row that can never be fetched is
                // just a dead entry in the user's list.
                if (!m.name.isEmpty() && !m.cik.isEmpty())
                    list.push_back(m);
            }
            if (!list.isEmpty())
                self->set_managers(list);
            LOG_INFO(TAG, QString("Seeded %1 tracked 13F managers").arg(list.size()));
            emit self->managers_changed();
        },
        /*on_line=*/{}, kSmartMoneyTimeoutMs);
}


// ── Local 13F index ─────────────────────────────────────────────────────────

bool OwnershipService::index_ready() const {
    return !index_status_.isEmpty();
}

QString OwnershipService::index_status_text() const {
    return index_status_.isEmpty()
               ? QStringLiteral("No 13F index built yet — the holder list is empty until "
                                "one quarter is downloaded.")
               : index_status_;
}

void OwnershipService::build_index() {
    if (index_busy_)
        return;
    index_busy_ = true;
    emit index_changed(QStringLiteral("Downloading SEC 13F data set…"));
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        // Two quarters, not one. A single quarter is a photograph; the
        // question people actually ask — who built, who exited — needs the
        // frame before it, and asking the user to press the button twice would
        // be a puzzle rather than a feature.
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
                const auto o =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
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
                    self->index_status_ =
                        qs.isEmpty() ? QStringLiteral("13F index built")
                                     : QStringLiteral("Indexed: ") + qs.join(QStringLiteral(", "));
                    msg = self->index_status_;
                    // Chain straight into symbol mapping. An index nobody can
                    // search by ticker is not finished, and asking the user to
                    // discover a second button for it is a puzzle.
                    self->index_busy_ = false;
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
    // Runs to completion rather than a fixed slice. The top 2,000 CUSIPs by
    // value already cover 96% of all institutional value, so the map is usable
    // within minutes; the ~22,800-symbol tail takes about 95 minutes behind it.
    // Making the user press a button a dozen times to get there was the only
    // thing wrong with this.
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
            const auto o = result.success
                ? QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object()
                : QJsonObject{};
            const int left = o.value(QStringLiteral("remaining")).toInt();
            self->index_status_ =
                left > 0 ? QStringLiteral("Mapped %1 symbols · %2 still unmapped")
                               .arg(o.value(QStringLiteral("resolved")).toInt()).arg(left)
                         : QStringLiteral("Symbol map complete · %1 mapped")
                               .arg(o.value(QStringLiteral("resolved")).toInt());
            emit self->index_changed(self->index_status_);
        },
        /*on_line=*/{}, kIndexBuildTimeoutMs);
}


void OwnershipService::load_index_holders(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || pending_.value(sym).contains(kSrcSmart))
        return;
    pending_[sym].insert(kSrcSmart);

    QPointer<OwnershipService> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"ticker", sym}, {"limit", 80}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("holders"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            snap.smart_money.clear();
            if (!result.success) {
                snap.smart_money_error = result.error.isEmpty()
                                             ? QStringLiteral("13F index query failed")
                                             : result.error.left(200);
            } else {
                const auto root =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                const QString err = root.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    snap.smart_money_error = err;
                } else {
                    snap.smart_money_error.clear();
                    snap.holder_universe = root.value(QStringLiteral("holder_count")).toInt();
                    snap.option_holders = root.value(QStringLiteral("option_holder_count")).toInt();
                    snap.index_quarter = QDate::fromString(
                        root.value(QStringLiteral("quarter")).toString(), Qt::ISODate);
                    snap.index_shares_held =
                        root.value(QStringLiteral("total_shares_held")).toDouble();
                    snap.prior_quarter = QDate::fromString(
                        root.value(QStringLiteral("prior_quarter")).toString(), Qt::ISODate);
                    for (const auto& v : root.value(QStringLiteral("holders")).toArray()) {
                        const auto o = v.toObject();
                        ManagerPosition p;
                        p.manager = o.value(QStringLiteral("manager")).toString();
                        p.issuer = o.value(QStringLiteral("issuer")).toString();
                        p.cusip = o.value(QStringLiteral("cusip")).toString();
                        p.shares = opt_num(o, "shares");
                        p.value = opt_num(o, "value");
                        p.weight = opt_num(o, "weight");
                        p.book_total = opt_num(o, "book_total");
                        p.position_count = o.value(QStringLiteral("position_count")).toInt();
                        p.put_call = o.value(QStringLiteral("put_call")).toString();
                        p.is_derivative = o.value(QStringLiteral("is_derivative")).toBool();
                        p.action = o.value(QStringLiteral("action")).toString();
                        p.shares_delta = opt_num(o, "shares_delta");
                        p.pct_change = opt_num(o, "pct_change");
                        p.note = o.value(QStringLiteral("note")).toString();
                        if (!p.manager.isEmpty())
                            snap.smart_money.push_back(p);
                    }
                    snap.smart_money_ok = true;
                }
            }
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcSmart);
        },
        /*on_line=*/{}, 60'000);
}

} // namespace fincept::services
