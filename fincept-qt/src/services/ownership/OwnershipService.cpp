#include "services/ownership/OwnershipService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonWorker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

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

} // namespace

OwnershipService& OwnershipService::instance() {
    static OwnershipService s;
    return s;
}

ownership::OwnershipSnapshot OwnershipService::snapshot(const QString& symbol) const {
    return cache_.value(symbol.toUpper());
}

bool OwnershipService::is_loading(const QString& symbol) const {
    return pending_.value(symbol.toUpper(), 0) > 0;
}

void OwnershipService::load(const QString& symbol) {
    const QString sym = symbol.trimmed().toUpper();
    if (sym.isEmpty())
        return;
    if (pending_.value(sym, 0) > 0)
        return; // already in flight

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
    if (pending_.value(sym, 0) > 0)
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
    pending_.insert(sym, 2);
    fetch_market(sym);
    fetch_edgar(sym);
}

void OwnershipService::note_source_done(const QString& symbol) {
    const int left = pending_.value(symbol, 1) - 1;
    pending_.insert(symbol, left);
    emit snapshot_updated(symbol);
    if (left <= 0) {
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
            self->note_source_done(sym);
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
            self->note_source_done(sym);
        },
        // Not kNetworkActionTimeoutMs (10s): ownership_extras makes three
        // separate yfinance calls — institutional holders, major holders and
        // .info — and .info alone regularly takes longer than that.
        python::PythonWorker::kComputeActionTimeoutMs);
}

} // namespace fincept::services
