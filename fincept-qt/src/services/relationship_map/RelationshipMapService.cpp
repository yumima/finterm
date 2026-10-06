// src/services/relationship_map/RelationshipMapService.cpp
#include "services/relationship_map/RelationshipMapService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "services/util/DiskCache.h"
#include "storage/cache/CacheManager.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>

namespace fincept::services {

using namespace fincept::relmap;

namespace {

// JSON null / missing -> NaN (rendered as "—"), never a silent 0. The Python
// fetcher emits null for any field Yahoo didn't report.
double num_or_nan(const QJsonValue& v) {
    return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

// Persistent on-disk cache so the relationship-map panel paints the most
// recently-viewed ticker immediately on next launch. One file per ticker;
// directory bounded by the universe the user has actually browsed.
fincept::services::util::DiskCache& disk_cache() {
    static fincept::services::util::DiskCache c(QStringLiteral("relationship_map"));
    return c;
}

// Sanitize a ticker into an alnum-only filename stem. Tickers can include
// dots (BRK.B) or hyphens (BRK-B) which would break case-insensitive
// filesystem lookups. ".json" is appended by the caller.
QString ticker_filename(const QString& ticker) {
    QString s;
    s.reserve(ticker.size());
    for (const QChar c : ticker)
        if (c.isLetterOrNumber())
            s.append(c.toUpper());
    return s + QStringLiteral(".json");
}

}  // namespace

RelationshipMapService& RelationshipMapService::instance() {
    static RelationshipMapService s;  // C++11 thread-safe local-static init
    return s;
}

RelationshipMapService::RelationshipMapService() {
    // Cap per-ticker cache to last 500 viewed companies.
    disk_cache().trim_to(500);

    // Hydrate: replay each cached ticker into CacheManager so the next
    // fetch(ticker) hits warm in-memory cache and emits instantly. Deferred to
    // the next event-loop tick so the file I/O stays off the startup path
    // (nothing reads relmap:* before a panel navigates). The singleton lives
    // for the process lifetime, so the captured `this` can't dangle.
    QTimer::singleShot(0, [this]() {
    const QStringList files = disk_cache().files();
    for (const QString& fname : files) {
        const QJsonDocument doc = disk_cache().load(fname);
        if (!doc.isObject()) continue;
        // Recover the ticker from the JSON (rather than parsing fname) so
        // a corrupted filename doesn't poison the cache key. Falls back to
        // fname stem if the payload doesn't carry company.ticker.
        const QJsonObject root = doc.object();
        // Pre-"peers_source" payloads carried peers from a hardcoded
        // industry table and 0.0 for missing fields — don't replay them.
        if (!root.contains(QStringLiteral("peers_source"))) continue;
        QString ticker = root.value("company").toObject().value("ticker").toString();
        if (ticker.isEmpty()) {
            ticker = fname;
            if (ticker.endsWith(QStringLiteral(".json"))) ticker.chop(5);
        }
        const QString blob = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        // Use the same TTL the live fetch uses so a stale on-disk copy
        // doesn't masquerade as fresh — CacheManager will expire it.
        fincept::CacheManager::instance().put("relmap:" + ticker.toUpper(),
                                              QVariant(blob),
                                              /*ttl_sec=*/10 * 60, "relmap");
    }
    });
}

static constexpr int kRelMapTtlSec = 10 * 60; // 10 min

void RelationshipMapService::fetch(const QString& ticker) {
    if (loading_)
        return;
    if (ticker.trimmed().isEmpty())
        return;

    current_ticker_ = ticker.toUpper();

    // Check cache first
    const QString cache_key = "relmap:" + current_ticker_;
    const QVariant cached = fincept::CacheManager::instance().get(cache_key);
    if (!cached.isNull()) {
        LOG_DEBUG("RelMapService", "Cache hit for " + current_ticker_);
        emit progress_changed(100, "Loaded from cache");
        parse_result(cached.toString());
        return;
    }

    loading_ = true;
    data_ = {};

    emit progress_changed(5, "Starting data fetch for " + current_ticker_ + "...");

    python::PythonRunner::instance().run(
        "relationship_map.py", {current_ticker_}, [this, cache_key](python::PythonResult result) {
            loading_ = false;

            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_ERROR("RelMapService", "Python script failed: exit=" + QString::number(result.exit_code));
                emit fetch_failed("Failed to fetch data for " + current_ticker_);
                return;
            }

            // Cache raw JSON output
            fincept::CacheManager::instance().put(cache_key, QVariant(result.output), kRelMapTtlSec, "relmap");

            // Persist to disk so the next launch hydrates the CacheManager
            // entry above without a Python round-trip. Save the parsed JSON
            // (not the raw stdout) so we get a clean, validated payload.
            const auto doc = QJsonDocument::fromJson(result.output.toUtf8());
            if (doc.isObject())
                disk_cache().save(ticker_filename(current_ticker_), doc);

            emit progress_changed(70, "Parsing results...");
            parse_result(result.output);
        });
}

void RelationshipMapService::clear() {
    data_ = {};
    loading_ = false;
    current_ticker_.clear();
}

void RelationshipMapService::parse_result(const QString& json_output) {
    QJsonDocument doc = QJsonDocument::fromJson(json_output.toUtf8());
    if (!doc.isObject()) {
        emit fetch_failed("Invalid JSON response");
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("error")) {
        emit fetch_failed(root["error"].toString());
        return;
    }

    emit progress_changed(80, "Building graph data...");

    // ── Company ──────────────────────────────────────────────────────────
    QJsonObject co = root["company"].toObject();
    data_.company.ticker               = co["ticker"].toString();
    data_.company.name                 = co["name"].toString();
    data_.company.sector               = co["sector"].toString();
    data_.company.industry             = co["industry"].toString();
    data_.company.website              = co["website"].toString();
    data_.company.description          = co["description"].toString();
    data_.company.country              = co["country"].toString();
    data_.company.exchange             = co["exchange"].toString();
    data_.company.currency             = co["currency"].toString();
    data_.company.employees            = co["employees"].toInt();
    data_.company.market_cap           = num_or_nan(co["market_cap"]);
    data_.company.current_price        = num_or_nan(co["current_price"]);
    data_.company.previous_close       = num_or_nan(co["previous_close"]);
    data_.company.day_change_pct       = num_or_nan(co["day_change_pct"]);
    data_.company.pe_ratio             = num_or_nan(co["pe_ratio"]);
    data_.company.forward_pe           = num_or_nan(co["forward_pe"]);
    data_.company.price_to_book        = num_or_nan(co["price_to_book"]);
    data_.company.roe                  = num_or_nan(co["roe"]);
    data_.company.roa                  = num_or_nan(co["roa"]);
    data_.company.revenue_growth       = num_or_nan(co["revenue_growth"]);
    data_.company.earnings_growth      = num_or_nan(co["earnings_growth"]);
    data_.company.profit_margins       = num_or_nan(co["profit_margins"]);
    data_.company.revenue              = num_or_nan(co["revenue"]);
    data_.company.ebitda               = num_or_nan(co["ebitda"]);
    data_.company.free_cashflow        = num_or_nan(co["free_cashflow"]);
    data_.company.operating_cashflow   = num_or_nan(co["operating_cashflow"]);
    data_.company.total_cash           = num_or_nan(co["total_cash"]);
    data_.company.total_debt           = num_or_nan(co["total_debt"]);
    data_.company.insider_percent      = num_or_nan(co["insider_percent"]);
    data_.company.institutional_percent= num_or_nan(co["institutional_percent"]);
    data_.company.recommendation       = co["recommendation"].toString();
    data_.company.recommendation_mean  = num_or_nan(co["recommendation_mean"]);
    data_.company.target_high          = num_or_nan(co["target_high"]);
    data_.company.target_low           = num_or_nan(co["target_low"]);
    data_.company.target_mean          = num_or_nan(co["target_mean"]);
    data_.company.target_median        = num_or_nan(co["target_median"]);
    data_.company.analyst_count        = co["analyst_count"].toInt();
    data_.company.dividend_yield       = num_or_nan(co["dividend_yield"]);
    data_.company.payout_ratio         = num_or_nan(co["payout_ratio"]);
    data_.company.trailing_eps         = num_or_nan(co["trailing_eps"]);
    data_.company.forward_eps          = num_or_nan(co["forward_eps"]);
    data_.company.shares_outstanding   = num_or_nan(co["shares_outstanding"]);

    // ── Governance ───────────────────────────────────────────────────────
    QJsonObject gov = root["governance"].toObject();
    data_.governance.audit_risk               = gov["audit_risk"].toInt();
    data_.governance.board_risk               = gov["board_risk"].toInt();
    data_.governance.compensation_risk        = gov["compensation_risk"].toInt();
    data_.governance.shareholder_rights_risk  = gov["shareholder_rights_risk"].toInt();
    data_.governance.overall_risk             = gov["overall_risk"].toInt();

    // ── Technicals ───────────────────────────────────────────────────────
    QJsonObject tech = root["technicals"].toObject();
    data_.technicals.fifty_two_week_high   = num_or_nan(tech["fifty_two_week_high"]);
    data_.technicals.fifty_two_week_low    = num_or_nan(tech["fifty_two_week_low"]);
    data_.technicals.fifty_day_avg         = num_or_nan(tech["fifty_day_avg"]);
    data_.technicals.two_hundred_day_avg   = num_or_nan(tech["two_hundred_day_avg"]);
    data_.technicals.beta                  = num_or_nan(tech["beta"]);
    data_.technicals.week52_change_pct     = num_or_nan(tech["week52_change_pct"]);
    data_.technicals.sp500_52wk_change     = num_or_nan(tech["sp500_52wk_change"]);
    data_.technicals.avg_volume            = tech["avg_volume"].toInt();
    data_.technicals.avg_volume_10d        = tech["avg_volume_10d"].toInt();

    // ── Short Interest ────────────────────────────────────────────────────
    QJsonObject si = root["short_interest"].toObject();
    data_.short_interest.shares_short     = num_or_nan(si["shares_short"]);
    data_.short_interest.short_ratio      = num_or_nan(si["short_ratio"]);
    data_.short_interest.short_pct_float  = num_or_nan(si["short_pct_float"]);
    data_.short_interest.float_shares     = num_or_nan(si["float_shares"]);

    // ── Enterprise ────────────────────────────────────────────────────────
    QJsonObject ent = root["enterprise"].toObject();
    data_.enterprise.enterprise_value = num_or_nan(ent["enterprise_value"]);
    data_.enterprise.ev_to_revenue    = num_or_nan(ent["ev_to_revenue"]);
    data_.enterprise.ev_to_ebitda     = num_or_nan(ent["ev_to_ebitda"]);
    data_.enterprise.peg_ratio        = num_or_nan(ent["peg_ratio"]);
    data_.enterprise.price_to_sales   = num_or_nan(ent["price_to_sales"]);
    data_.enterprise.book_value       = num_or_nan(ent["book_value"]);

    // ── Margins & Debt ────────────────────────────────────────────────────
    QJsonObject mg = root["margins"].toObject();
    data_.margins.gross          = num_or_nan(mg["gross"]);
    data_.margins.operating      = num_or_nan(mg["operating"]);
    data_.margins.ebitda         = num_or_nan(mg["ebitda"]);
    data_.margins.net            = num_or_nan(mg["net"]);
    data_.margins.debt_to_equity = num_or_nan(mg["debt_to_equity"]);
    data_.margins.current_ratio  = num_or_nan(mg["current_ratio"]);
    data_.margins.quick_ratio    = num_or_nan(mg["quick_ratio"]);

    // ── Analyst Targets ───────────────────────────────────────────────────
    QJsonObject at = root["analyst_targets"].toObject();
    data_.analyst_targets.current = num_or_nan(at["current"]);
    data_.analyst_targets.high    = num_or_nan(at["high"]);
    data_.analyst_targets.low     = num_or_nan(at["low"]);
    data_.analyst_targets.mean    = num_or_nan(at["mean"]);
    data_.analyst_targets.median  = num_or_nan(at["median"]);

    // ── Recommendations Summary ───────────────────────────────────────────
    for (const auto& v : root["recommendations_summary"].toArray()) {
        QJsonObject r = v.toObject();
        RecommendationSnapshot snap;
        snap.period      = r["period"].toString();
        snap.strong_buy  = r["strong_buy"].toInt();
        snap.buy         = r["buy"].toInt();
        snap.hold        = r["hold"].toInt();
        snap.sell        = r["sell"].toInt();
        snap.strong_sell = r["strong_sell"].toInt();
        data_.recommendations.append(snap);
    }

    // ── Upgrades / Downgrades ─────────────────────────────────────────────
    for (const auto& v : root["upgrades_downgrades"].toArray()) {
        QJsonObject u = v.toObject();
        AnalystUpgrade upg;
        upg.date         = u["date"].toString();
        upg.firm         = u["firm"].toString();
        upg.to_grade     = u["to_grade"].toString();
        upg.from_grade   = u["from_grade"].toString();
        upg.action       = u["action"].toString();
        upg.price_target = num_or_nan(u["price_target"]);
        upg.prior_target = num_or_nan(u["prior_target"]);
        data_.upgrades_downgrades.append(upg);
    }

    // ── Officers ──────────────────────────────────────────────────────────
    for (const auto& v : root["officers"].toArray()) {
        QJsonObject o = v.toObject();
        CompanyOfficer off;
        off.name      = o["name"].toString();
        off.title     = o["title"].toString();
        off.total_pay = o["total_pay"].toInt();
        off.year_born = o["year_born"].toInt();
        if (!off.name.isEmpty())
            data_.officers.append(off);
    }

    // ── Calendar ──────────────────────────────────────────────────────────
    QJsonObject cal = root["calendar"].toObject();
    data_.calendar.earnings_date    = cal["earnings_date"].toString();
    data_.calendar.earnings_avg     = num_or_nan(cal["earnings_avg"]);
    data_.calendar.earnings_low     = num_or_nan(cal["earnings_low"]);
    data_.calendar.earnings_high    = num_or_nan(cal["earnings_high"]);
    data_.calendar.revenue_avg      = num_or_nan(cal["revenue_avg"]);
    data_.calendar.revenue_low      = num_or_nan(cal["revenue_low"]);
    data_.calendar.revenue_high     = num_or_nan(cal["revenue_high"]);
    data_.calendar.ex_dividend_date = cal["ex_dividend_date"].toString();
    data_.calendar.dividend_date    = cal["dividend_date"].toString();

    // ── Institutional Holders ─────────────────────────────────────────────
    for (const auto& v : root["institutional_holders"].toArray()) {
        QJsonObject h = v.toObject();
        InstitutionalHolder holder;
        holder.name           = h["name"].toString();
        holder.shares         = num_or_nan(h["shares"]);
        holder.value          = num_or_nan(h["value"]);
        holder.percentage     = num_or_nan(h["percentage"]);
        holder.change_percent = num_or_nan(h["change_percent"]);
        holder.fund_family    = h["fund_family"].toString();
        holder.type           = "institutional";
        if (!holder.name.isEmpty())
            data_.institutional_holders.append(holder);
    }

    // ── Mutual Fund Holders ───────────────────────────────────────────────
    for (const auto& v : root["mutualfund_holders"].toArray()) {
        QJsonObject h = v.toObject();
        InstitutionalHolder holder;
        holder.name           = h["name"].toString();
        holder.shares         = num_or_nan(h["shares"]);
        holder.value          = num_or_nan(h["value"]);
        holder.percentage     = num_or_nan(h["percentage"]);
        holder.change_percent = num_or_nan(h["change_percent"]);
        holder.fund_family    = h["fund_family"].toString();
        holder.type           = "mutualfund";
        if (!holder.name.isEmpty())
            data_.mutualfund_holders.append(holder);
    }

    // ── Insider Holders ───────────────────────────────────────────────────
    for (const auto& v : root["insider_holders"].toArray()) {
        QJsonObject h = v.toObject();
        InsiderHolder insider;
        insider.name             = h["name"].toString();
        insider.title            = h["title"].toString();
        insider.shares           = num_or_nan(h["shares"]);
        insider.percentage       = num_or_nan(h["percentage"]);
        insider.last_transaction = h["last_transaction"].toString();
        if (!insider.name.isEmpty())
            data_.insider_holders.append(insider);
    }

    // ── Peers ─────────────────────────────────────────────────────────────
    for (const auto& v : root["peers"].toArray()) {
        QJsonObject p = v.toObject();
        PeerCompany peer;
        peer.ticker          = p["ticker"].toString();
        peer.name            = p["name"].toString();
        peer.market_cap      = num_or_nan(p["market_cap"]);
        peer.pe_ratio        = num_or_nan(p["pe_ratio"]);
        peer.forward_pe      = num_or_nan(p["forward_pe"]);
        peer.roe             = num_or_nan(p["roe"]);
        peer.revenue_growth  = num_or_nan(p["revenue_growth"]);
        peer.profit_margins  = num_or_nan(p["profit_margins"]);
        peer.gross_margins   = num_or_nan(p["gross_margins"]);
        peer.current_price   = num_or_nan(p["current_price"]);
        peer.sector          = p["sector"].toString();
        peer.beta            = num_or_nan(p["beta"]);
        peer.ev_to_ebitda    = num_or_nan(p["ev_to_ebitda"]);
        peer.price_to_book   = num_or_nan(p["price_to_book"]);
        peer.week52_change   = num_or_nan(p["week52_change"]);
        peer.recommendation  = p["recommendation"].toString();
        if (!peer.ticker.isEmpty())
            data_.peers.append(peer);
    }

    // ── Valuation Signal ──────────────────────────────────────────────────
    data_.valuation = compute_valuation(data_.company, data_.peers);

    data_.data_quality = root["data_quality"].toInt();
    data_.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    emit progress_changed(100, "Complete");
    emit data_ready(data_);

    if (hub_registered_ && !data_.company.ticker.isEmpty()) {
        fincept::datahub::DataHub::instance().publish(
            QStringLiteral("geopolitics:relationship_graph:") + data_.company.ticker,
            QVariant::fromValue(data_));
    }

    LOG_INFO("RelMapService", QString("Loaded %1: %2 inst, %3 insiders, %4 peers, quality=%5%")
                                  .arg(data_.company.ticker)
                                  .arg(data_.institutional_holders.size())
                                  .arg(data_.insider_holders.size())
                                  .arg(data_.peers.size())
                                  .arg(data_.data_quality));
}

ValuationSignal RelationshipMapService::compute_valuation(const CompanyInfo& co, const QVector<PeerCompany>& peers) {
    ValuationSignal sig;
    // No real peer set (none is sourced today) or no positive P/E -> no
    // signal at all. Don't emit a default "HOLD" that reads as a call.
    // NaN-safe: !(x > 0) also catches a missing (NaN) P/E.
    if (peers.isEmpty() || !(co.pe_ratio > 0)) {
        sig.status = "INSUFFICIENT DATA";
        sig.action = QString();
        sig.score = 0;
        return sig;
    }

    // Compute peer median PE
    QVector<double> peer_pes;
    for (const auto& p : peers) {
        if (p.pe_ratio > 0)
            peer_pes.append(p.pe_ratio);
    }
    if (peer_pes.isEmpty()) {
        sig.status = "INSUFFICIENT DATA";
        sig.action = QString();
        sig.score = 0;
        return sig;
    }

    std::sort(peer_pes.begin(), peer_pes.end());
    double median_pe = peer_pes[peer_pes.size() / 2];
    double pe_ratio_vs_peers = co.pe_ratio / median_pe;

    double score = 0;
    // PE undervalued if < 0.8x peers
    if (pe_ratio_vs_peers < 0.7)
        score += 2;
    else if (pe_ratio_vs_peers < 0.85)
        score += 1;
    else if (pe_ratio_vs_peers > 1.3)
        score -= 2;
    else if (pe_ratio_vs_peers > 1.15)
        score -= 1;

    // Growth premium
    if (co.revenue_growth > 0.2)
        score += 1;
    else if (co.revenue_growth < -0.05)
        score -= 1;

    // Profitability
    if (co.profit_margins > 0.2)
        score += 0.5;

    sig.score = std::max(-3.0, std::min(3.0, score));

    if (sig.score >= 1.5) {
        sig.status = "UNDERVALUED";
        sig.action = "BUY";
    } else if (sig.score >= 0.5) {
        sig.status = "POTENTIALLY UNDERVALUED";
        sig.action = "BUY";
    } else if (sig.score <= -1.5) {
        sig.status = "OVERVALUED";
        sig.action = "SELL";
    } else if (sig.score <= -0.5) {
        sig.status = "POTENTIALLY OVERVALUED";
        sig.action = "SELL";
    } else {
        sig.status = "FAIRLY VALUED";
        sig.action = "HOLD";
    }

    return sig;
}

// ═══════════════════════════════════════════════════════════════════════════════
// DATAHUB PRODUCER — geopolitics:relationship_graph:<ticker>
// ═══════════════════════════════════════════════════════════════════════════════

QStringList RelationshipMapService::topic_patterns() const {
    return {QStringLiteral("geopolitics:relationship_graph:*")};
}

void RelationshipMapService::refresh(const QStringList& topics) {
    for (const auto& topic : topics) {
        const QStringList parts = topic.split(QLatin1Char(':'));
        // geopolitics:relationship_graph:<ticker>
        if (parts.size() != 3) continue;
        fetch(parts[2]);
    }
}

int RelationshipMapService::max_requests_per_sec() const {
    return 1;  // Heavy yfinance aggregation per call; cap at 1/sec.
}

void RelationshipMapService::ensure_registered_with_hub() {
    if (hub_registered_) return;
    auto& hub = fincept::datahub::DataHub::instance();
    hub.register_producer(this);

    // Matches kRelMapTtlSec (10 min). Min 2 min between refreshes — the Python
    // fetch is slow (~5-15 s) and consumers are dashboards, not tickers.
    fincept::datahub::TopicPolicy policy;
    policy.ttl_ms = kRelMapTtlSec * 1000;
    policy.min_interval_ms = 2 * 60 * 1000;
    hub.set_policy_pattern(QStringLiteral("geopolitics:relationship_graph:*"), policy);

    hub_registered_ = true;
    LOG_INFO("RelMapService", "Registered with DataHub (geopolitics:relationship_graph:*)");
}

} // namespace fincept::services
