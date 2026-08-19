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

#include <cmath>
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
const QString kSrcDemand = QStringLiteral("demand");
const QString kSrcShortVol = QStringLiteral("shortvol");

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
    snap.insider_rows_filed_as_owner =
        ins.value(QStringLiteral("rows_filed_as_owner")).toInt();
    snap.insider_other_issuers.clear();
    for (const auto& v : ins.value(QStringLiteral("other_issuers")).toArray())
        snap.insider_other_issuers << v.toString();
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
    snap.stakes_found            = stk.value(QStringLiteral("filings_found")).toInt();
    snap.stakes_truncated        = stk.value(QStringLiteral("filings_truncated")).toInt();
    snap.stakes_filed_by_this_cik = stk.value(QStringLiteral("filed_by_this_cik")).toInt();
    snap.stakes_unverified       = stk.value(QStringLiteral("filings_unverified")).toInt();
    snap.stakes.clear();
    for (const auto& v : stk.value(QStringLiteral("stakes")).toArray()) {
        const QJsonObject o = v.toObject();
        BeneficialStake b;
        b.form       = o.value(QStringLiteral("form")).toString();
        b.activist   = o.value(QStringLiteral("activist")).toBool();
        b.amendment  = o.value(QStringLiteral("amendment")).toBool();
        b.filed_date = iso_date(o, "filed_date");
        b.filer      = o.value(QStringLiteral("filer")).toString();
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




/// One filer's book from the local index, with its quarter-over-quarter moves.
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
    load_short_volume(sym);
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


// ── Firms ───────────────────────────────────────────────────────────────────

void OwnershipService::search_firms(const QString& query, FirmRanking ranking) {
    QPointer<OwnershipService> self = this;
    const QString q = query.trimmed();
    // With no query this is the ranked top 50 DISCRETIONARY books, not the
    // largest books outright — ranking by size alone returns BlackRock,
    // Vanguard, State Street and Morgan Stanley, whose quarterly change is an
    // index rebalance rather than a view on anything.
    const QString action = q.isEmpty() ? QStringLiteral("top_firms") : QStringLiteral("firms");
    // Concentrated needs a floor as well as a ceiling: narrowing the book size
    // to surface real managers also surfaces filers that are not managing
    // anything — a corporate cross-holding, a foundation sitting on its
    // founder's stock. A filer with one position has no "what are they doing".
    QJsonObject top{{"limit", 50}};
    if (ranking == FirmRanking::Concentrated) {
        top.insert(QStringLiteral("max_positions"), 150);
        top.insert(QStringLiteral("min_positions"), 10);
    }
    const QString payload = QString::fromUtf8(
        QJsonDocument(q.isEmpty() ? top : QJsonObject{{"query", q}, {"limit", 40}})
            .toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {action, payload},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->firm_results_.clear();
            if (result.success) {
                const auto root =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                for (const auto& v : root.value(QStringLiteral("firms")).toArray()) {
                    const auto o = v.toObject();
                    Manager m;
                    m.cik  = o.value(QStringLiteral("cik")).toString();
                    m.name = o.value(QStringLiteral("manager")).toString();
                    m.book_value     = o.value(QStringLiteral("book_value")).toDouble();
                    m.position_count = o.value(QStringLiteral("position_count")).toInt();
                    m.top_name   = o.value(QStringLiteral("top_name")).toString();
                    m.top_ticker = o.value(QStringLiteral("top_ticker")).toString();
                    m.top_weight = opt_num(o, "top_weight");
                    auto move = [&o](const char* key) {
                        Manager::Move mv;
                        const auto b = o.value(QLatin1String(key)).toObject();
                        if (b.isEmpty())
                            return mv;
                        mv.issuer = b.value(QStringLiteral("issuer")).toString();
                        mv.ticker = b.value(QStringLiteral("ticker")).toString();
                        mv.value  = b.value(QStringLiteral("value")).toDouble();
                        mv.valid  = !mv.issuer.isEmpty() && mv.value > 0;
                        return mv;
                    };
                    m.top_add  = move("top_add");
                    m.top_trim = move("top_trim");
                    m.top_exit = move("top_exit");
                    if (o.contains(QStringLiteral("added"))) {
                        m.opened  = o.value(QStringLiteral("new")).toInt();
                        m.added   = o.value(QStringLiteral("added")).toInt();
                        m.trimmed = o.value(QStringLiteral("trimmed")).toInt();
                        m.exited  = o.value(QStringLiteral("exited")).toInt();
                        m.has_activity = true;
                    }
                    if (!m.cik.isEmpty())
                        self->firm_results_.push_back(m);
                }
            }
            emit self->firms_found();
        },
        /*on_line=*/{}, 60'000);
}

void OwnershipService::load_smart_money(const QString& symbol) {
    // One path now. The old fallback fetched each of twenty curated managers'
    // filings from EDGAR — minutes per ticker, covering 0.2% of the filers —
    // and existed only because the universe was not indexed. Without an index
    // the panel says so and offers to build one, which is a better answer than
    // a slow partial one presented as the whole picture.
    if (index_ready()) {
        load_index_holders(symbol);
        // The holder list and the demand distribution are two reads of the
        // same quarter and are always shown together — the ranked bars answer
        // "who", the quadrant answers "what are they all doing". Fetching one
        // without the other left the quadrant reading "loading" forever.
        load_demand(symbol);
    }
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
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"cik", cik}, {"limit", 250}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("book"), payload},
        [self, cik](python::PythonResult result) {
            if (!self)
                return;
            ManagerBook b;
            b.cik = cik;
            if (!result.success) {
                b.error = result.error.isEmpty() ? QStringLiteral("13F index query failed")
                                                 : result.error.left(200);
            } else {
                const auto root =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
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


bool OwnershipService::index_ready() const {
    // Seeded from the database on first ask. index_status_ was only ever set by
    // a build in THIS session, so after a restart with a fully built index the
    // button read "BUILD 13F INDEX" and pressing it re-downloaded ~200 MB —
    // while pull_current_quarter, which guards on this, could never run at all.
    // Latched on SUCCESS, not on attempt. Setting the flag before the answer
    // was known meant one failed probe — the Python worker not yet warm, a
    // transient — left index_ready() false for the whole session: the screen
    // showed "no index" over a fully built database, and PULL CURRENT QUARTER,
    // which guards on this, was permanently unreachable until a restart.
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
                // Deliberately NOT latched: the next caller retries. A probe
                // that failed tells us nothing about whether an index exists.
                LOG_WARN(TAG, "13F index probe failed, will retry: " + result.error.left(160));
                return;
            }
            self->index_probed_ = true;
            const auto o =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
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
                self->index_status_ = QStringLiteral("Indexed: ") + parts.join(QStringLiteral(", "));
                emit self->index_changed(self->index_status_);
            }
        },
        /*on_line=*/{}, 30'000);
}

QString OwnershipService::index_status_text() const {
    return index_status_.isEmpty()
               ? QStringLiteral("No 13F index built yet — the holder list is empty until "
                                "one quarter is downloaded.")
               : index_status_;
}

void OwnershipService::pull_current_quarter(int top) {
    if (index_busy_ || !index_ready())
        return;
    index_busy_ = true;
    emit index_changed(QStringLiteral("Reading the current quarter from EDGAR…"));
    QPointer<OwnershipService> self = this;
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"),
        {QStringLiteral("ingest_current"),
         QString::fromUtf8(QJsonDocument(QJsonObject{{"top", top}}).toJson(QJsonDocument::Compact))},
        [self](python::PythonResult result) {
            if (!self)
                return;
            self->index_busy_ = false;
            // A failed run and a run that found nothing are different facts.
            // Collapsing both into "no newer filings" tells the user the index
            // is current when the fetch actually died — including on the
            // timeout, which a 400-filer pull can legitimately approach.
            if (!result.success) {
                emit self->index_changed(
                    QStringLiteral("Current-quarter pull failed: %1")
                        .arg(result.error.isEmpty() ? QStringLiteral("the reader did not finish")
                                                    : result.error.left(200)));
                return;
            }
            const auto o =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            const QString err = o.value(QStringLiteral("error")).toString();
            if (!err.isEmpty()) {
                emit self->index_changed(QStringLiteral("Current-quarter pull: ") + err);
                return;
            }
            const QString q = o.value(QStringLiteral("quarter")).toString();
            self->index_probed_ = false;   // a quarter was written; re-read it
            emit self->index_changed(
                q.isEmpty()
                    ? QStringLiteral("No 13F filings newer than the indexed quarter on EDGAR yet.")
                    : QStringLiteral("%1 · pulled %2 for %3 large filers direct from EDGAR "
                                     "(partial — the bulk data set for it is not published yet)")
                          .arg(self->index_status_, q)
                          .arg(o.value(QStringLiteral("filers_added")).toInt()));
        },
        /*on_line=*/{}, kIndexBuildTimeoutMs);
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
            const auto o =
                QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
            const int unseen = o.value(QStringLiteral("unseen_datasets")).toInt();
            if (unseen > 0) {
                emit self->index_changed(
                    QStringLiteral("%1 · %2 newer SEC data set%3 not yet ingested — rebuild to "
                                   "bring the holder data forward.")
                        .arg(self->index_status_)
                        .arg(unseen)
                        .arg(unseen == 1 ? QString() : QStringLiteral("s")));
            }
        },
        /*on_line=*/{}, 60'000);
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
                    // Re-read from disk rather than trusting what we just
                    // composed: the database is the truth, and a status
                    // assembled from a response can drift from it.
                    self->index_probed_ = false;
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
            self->index_probed_ = false;   // the symbol map changed
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
                    const auto np = root.value(QStringLiteral("newer_partial")).toObject();
                    if (!np.isEmpty()) {
                        snap.partial_quarter = QDate::fromString(
                            np.value(QStringLiteral("quarter")).toString(), Qt::ISODate);
                        snap.partial_filers = np.value(QStringLiteral("filers")).toInt();
                    }
                    snap.buyers  = root.value(QStringLiteral("buyers")).toInt();
                    snap.sellers = root.value(QStringLiteral("sellers")).toInt();
                    snap.exited  = root.value(QStringLiteral("exited")).toInt();
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


void OwnershipService::load_demand(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || !index_ready() || pending_.value(sym).contains(kSrcDemand))
        return;
    // index_changed fires several times during a symbol map or a quarter pull,
    // and each one re-enters here. The distribution is per quarter, not per
    // event, so once it has been answered there is nothing to re-ask.
    if (cache_.value(sym).demand.has_data())
        return;
    pending_[sym].insert(kSrcDemand);

    QPointer<OwnershipService> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"ticker", sym}, {"max_points", 1500}})
            .toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("sec_13f_bulk.py"), {QStringLiteral("demand"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            InstitutionalDemand d;
            d.symbol = sym;
            if (!result.success) {
                d.error = result.error.isEmpty() ? QStringLiteral("demand query failed")
                                                 : result.error.left(200);
            } else {
                const auto o =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                const QString err = o.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    d.error = err;
                } else {
                    d.company       = o.value(QStringLiteral("company")).toString();
                    d.quarter       = iso_date(o, "quarter");
                    d.prior_quarter = iso_date(o, "prior_quarter");
                    d.holders       = o.value(QStringLiteral("holders_now")).toInt();
                    d.buyers        = o.value(QStringLiteral("buyers")).toInt();
                    d.sellers       = o.value(QStringLiteral("sellers")).toInt();
                    d.exited        = o.value(QStringLiteral("exited")).toInt();
                    d.unchanged     = o.value(QStringLiteral("unchanged")).toInt();
                    d.median_weight = o.value(QStringLiteral("median_weight")).toDouble();
                    auto num = [&o](const char* k) -> std::optional<double> {
                        const auto v = o.value(QLatin1String(k));
                        return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
                    };
                    d.net_value_flow = num("net_value_flow");
                    d.value_bought   = num("value_bought");
                    d.value_sold     = num("value_sold");
                    d.median_pct     = num("median_pct");
                    const auto fit = o.value(QStringLiteral("fit")).toObject();
                    if (!fit.isEmpty()) {
                        d.fit_slope     = fit.value(QStringLiteral("slope")).toDouble();
                        d.fit_intercept = fit.value(QStringLiteral("intercept")).toDouble();
                        d.fit_r         = fit.value(QStringLiteral("r")).toDouble();
                        d.fit_n         = fit.value(QStringLiteral("n")).toInt();
                    }
                    d.points_truncated = o.value(QStringLiteral("points_truncated")).toInt();
                    d.max_book_positions =
                        o.value(QStringLiteral("max_book_positions")).toInt();
                    const auto q = o.value(QStringLiteral("quadrants")).toObject();
                    d.high_add = q.value(QStringLiteral("high_add")).toInt();
                    d.high_cut = q.value(QStringLiteral("high_cut")).toInt();
                    d.low_add  = q.value(QStringLiteral("low_add")).toInt();
                    d.low_cut  = q.value(QStringLiteral("low_cut")).toInt();
                    for (const auto& v : o.value(QStringLiteral("points")).toArray()) {
                        const auto po = v.toObject();
                        DemandPoint p;
                        p.manager = po.value(QStringLiteral("manager")).toString();
                        p.weight  = po.value(QStringLiteral("weight")).toDouble();
                        p.shares  = opt_num(po, "shares");
                        p.value   = opt_num(po, "value");
                        p.delta   = opt_num(po, "delta");
                        p.pct     = opt_num(po, "pct");
                        p.action  = po.value(QStringLiteral("action")).toString();
                        p.top     = po.value(QStringLiteral("top")).toBool();
                        p.rank    = po.value(QStringLiteral("rank")).toInt();
                        if (!p.manager.isEmpty())
                            d.points.push_back(p);
                    }
                }
            }
            snap.demand = d;
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcDemand);
        },
        /*on_line=*/{}, 90'000);
}


void OwnershipService::load_short_volume(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty() || pending_.value(sym).contains(kSrcShortVol))
        return;
    const auto have = cache_.value(sym).short_volume;
    if (have.has_data())
        return;
    pending_[sym].insert(kSrcShortVol);

    QPointer<OwnershipService> self = this;
    const QString payload = QString::fromUtf8(
        QJsonDocument(QJsonObject{{"symbol", sym}, {"days", 60}}).toJson(QJsonDocument::Compact));
    python::PythonRunner::instance().run(
        QStringLiteral("finra_short_volume.py"), {QStringLiteral("series"), payload},
        [self, sym](python::PythonResult result) {
            if (!self)
                return;
            auto snap = self->cache_.value(sym);
            ShortVolume sv;
            if (!result.success) {
                sv.error = result.error.isEmpty() ? QStringLiteral("short-volume fetch failed")
                                                  : result.error.left(200);
            } else {
                const auto o =
                    QJsonDocument::fromJson(python::extract_json(result.output).toUtf8()).object();
                const QString err = o.value(QStringLiteral("error")).toString();
                if (!err.isEmpty()) {
                    sv.error = err;
                } else {
                    sv.as_of      = iso_date(o, "as_of");
                    sv.latest     = o.value(QStringLiteral("latest_ratio")).toDouble();
                    sv.avg_5      = o.value(QStringLiteral("avg_5")).toDouble();
                    sv.avg_20     = o.value(QStringLiteral("avg_20")).toDouble();
                    sv.min_ratio  = o.value(QStringLiteral("min_ratio")).toDouble();
                    sv.max_ratio  = o.value(QStringLiteral("max_ratio")).toDouble();
                    sv.percentile = o.value(QStringLiteral("percentile")).toDouble();
                    sv.days       = o.value(QStringLiteral("days")).toInt();
                    for (const auto& v : o.value(QStringLiteral("rows")).toArray())
                        sv.ratios.push_back(v.toObject().value(QStringLiteral("ratio")).toDouble());
                }
            }
            snap.short_volume = sv;
            self->cache_.insert(sym, snap);
            self->note_source_done(sym, kSrcShortVol);
        },
        /*on_line=*/{}, 180'000);
}


void OwnershipService::price_book(const QString& cik) {
    auto b = books_.value(cik);
    if (b.positions.isEmpty() || !b.period.isValid())
        return;
    // Selecting a firm loads its book, and loading a book prices it. Arrowing
    // down the ranked list would otherwise queue one wide download per row,
    // each one competing for the daemon's network workers and starving quotes
    // on every other screen. One pricing pass per CIK at a time.
    if (pricing_in_flight_.contains(cik))
        return;
    pricing_in_flight_.insert(cik);

    // Only mapped tickers can be priced. An unmapped CUSIP is left without a
    // return rather than guessed at, and the coverage figure says how much of
    // the book that leaves uncovered.
    QJsonArray syms;
    QSet<QString> seen;
    for (const auto& p : b.positions) {
        if (p.ticker.isEmpty() || seen.contains(p.ticker))
            continue;
        seen.insert(p.ticker);
        syms.append(p.ticker);
        if (syms.size() >= 120)   // the daemon call is one round trip; keep it sane
            break;
    }
    if (syms.isEmpty()) {
        pricing_in_flight_.remove(cik);
        return;
    }

    // Reach back past the quarter being priced so a 6-month window exists.
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
                // The book itself is still valid and on screen; only the return
                // columns are missing. Say why in the one place that would
                // otherwise show three silent dashes, and leave the guard clear
                // so re-selecting the firm retries.
                book.return_error = err.isEmpty()
                                        ? QStringLiteral("price history unavailable")
                                        : err;
                self->books_.insert(cik, book);
                emit self->book_updated(cik);
                return;
            }

            const auto closes = result.value(QStringLiteral("closes")).toObject();
            const QDate now = QDate::currentDate();
            // Index 0 is the quarter end the filing describes; the rest are
            // trailing windows measured from today.
            const QVector<QDate> marks{book.period, now, now.addMonths(-3), now.addMonths(-6)};

            double covered = 0.0;
            double wsum_qe = 0.0, wsum_3m = 0.0, w3 = 0.0;
            for (auto& p : book.positions) {
                if (p.ticker.isEmpty() || !closes.contains(p.ticker))
                    continue;
                const auto px = ownership::closes_on_or_before(
                    closes.value(p.ticker).toArray(), marks);
                const auto& at_qe = px[0];
                const auto& now_px = px[1];
                const auto& at_3m = px[2];
                const auto& at_6m = px[3];
                if (!now_px || *now_px <= 0)
                    continue;
                p.priced = true;
                const double value = p.value.value_or(0.0);
                if (at_qe && *at_qe > 0) {
                    p.ret_since_quarter_end = (*now_px / *at_qe) - 1.0;
                    covered += value;
                    wsum_qe += *p.ret_since_quarter_end * value;
                }
                if (at_3m && *at_3m > 0) {
                    p.ret_3m = (*now_px / *at_3m) - 1.0;
                    wsum_3m += *p.ret_3m * value;
                    w3 += value;
                }
                if (at_6m && *at_6m > 0)
                    p.ret_6m = (*now_px / *at_6m) - 1.0;
            }
            // Against the filer's WHOLE book, not against the fetched slice —
            // otherwise a 3,000-name filer reads "on 92% of it" when the real
            // figure is a fraction of that.
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

} // namespace fincept::services
