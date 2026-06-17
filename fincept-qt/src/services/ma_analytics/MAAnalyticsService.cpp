// src/services/ma_analytics/MAAnalyticsService.cpp
#include "services/ma_analytics/MAAnalyticsService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "services/util/DiskCache.h"
#include "storage/cache/CacheManager.h"

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>

namespace fincept::services::ma {

namespace {
inline void publish_ma_result(bool hub_registered, const QString& context, const QJsonObject& obj) {
    if (!hub_registered) return;
    fincept::datahub::DataHub::instance().publish(QStringLiteral("ma:") + context, QVariant(obj));
}

// Persistent on-disk cache for the M&A analytics screen. Only the slow
// "by-context" payloads (the deal-database EDGAR walk: scan_filings,
// all_deals, search_deals, parse_filing) get persisted — these cost 30-90s
// cold and are the same data set per session. Per-input results (DCF /
// LBO / comps) used to also persist under `<context>_<hash>.json`, but
// the constructor couldn't replay them (filename only carried an MD5 of
// the params, no way to reconstruct the cache key), so the writes were
// pure disk bloat. The CacheManager 5-min TTL covers same-session reuse;
// cross-session reuse for per-input models is rare enough that the cost
// isn't worth the complexity.
fincept::services::util::DiskCache& disk_cache() {
    static fincept::services::util::DiskCache c(QStringLiteral("ma_analytics"));
    return c;
}

// Sanitize the context for use as a filename basename.
QString sanitize_context(const QString& ctx) {
    QString s;
    s.reserve(ctx.size());
    for (const QChar c : ctx) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            s.append(c);
        else
            s.append(QLatin1Char('_'));
    }
    return s;
}

QString context_filename(const QString& ctx) {
    return sanitize_context(ctx) + QStringLiteral(".json");
}

}  // namespace

// ── Singleton ────────────────────────────────────────────────────────────────
MAAnalyticsService& MAAnalyticsService::instance() {
    static MAAnalyticsService inst;
    return inst;
}

MAAnalyticsService::MAAnalyticsService(QObject* parent) : QObject(parent) {
    // One-time housekeeping for installs that still have old per-params
    // files on disk (we used to write them; commit removed the save path).
    // Walk the dir, replay by-context payloads, delete any orphan hashed
    // files so they don't keep occupying disk forever. Deferred to the next
    // event-loop tick so the file I/O stays off the startup path (the replayed
    // result_ready signals still reach no listeners until panels connect).
    QTimer::singleShot(0, this, [this]() {
    const QStringList files = disk_cache().files();
    for (const QString& fname : files) {
        // Detect old per-params files by the "_<8-hex>.json" tail and unlink.
        QString stem = fname;
        if (stem.endsWith(QStringLiteral(".json"))) stem.chop(5);
        const int last_us = stem.lastIndexOf(QLatin1Char('_'));
        const QString tail = last_us > 0 ? stem.mid(last_us + 1) : QString();
        const bool tail_is_hash =
            tail.size() == 8 && std::all_of(tail.begin(), tail.end(), [](QChar c) {
                return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                    || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
            });
        if (tail_is_hash) {
            disk_cache().remove(fname);
            continue;
        }
        // By-context payload — replay through result_ready. No listeners
        // yet (panels connect later); they re-emit via panel-driven fetch.
        const QJsonDocument doc = disk_cache().load(fname);
        if (!doc.isObject()) continue;
        emit result_ready(stem, doc.object());
    }
    });
}

// ── Python helpers ───────────────────────────────────────────────────────────
void MAAnalyticsService::run_python(const QString& script, const QStringList& args, const QString& context) {
    QPointer<MAAnalyticsService> self = this;
    python::PythonRunner::instance().run(script, args, [self, context](python::PythonResult result) {
        if (!self)
            return;
        if (!result.success) {
            LOG_ERROR("MAAnalytics", QString("Python call failed [%1]: %2").arg(context, result.error));
            emit self->error_occurred(context, result.error);
            return;
        }
        auto json_str = python::extract_json(result.output);
        auto doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isNull()) {
            LOG_ERROR("MAAnalytics", QString("Invalid JSON from [%1]").arg(context));
            emit self->error_occurred(context, "Invalid JSON response from Python");
            return;
        }
        auto obj = doc.object();
        // Persist by-context — these are the slow paths (deal-database EDGAR
        // walk especially) so the next launch hydrates instantly.
        disk_cache().save(context_filename(context), doc);
        LOG_INFO("MAAnalytics", QString("Result ready [%1]").arg(context));
        emit self->result_ready(context, obj);
        publish_ma_result(self->hub_registered_, context, obj);
    });
}

void MAAnalyticsService::run_python_json(const QString& script, const QString& command, const QJsonObject& params,
                                         const QString& context) {
    auto params_json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    const QString cache_key = "ma:" + context + "_" + command + ":" + QString::fromUtf8(params_json);

    const QVariant cached = fincept::CacheManager::instance().get(cache_key);
    if (!cached.isNull()) {
        auto doc = QJsonDocument::fromJson(cached.toString().toUtf8());
        if (!doc.isNull()) {
            LOG_DEBUG("MAAnalytics", QString("Cache hit [%1]").arg(context));
            const auto obj = doc.object();
            emit result_ready(context, obj);
            publish_ma_result(hub_registered_, context, obj);
            return;
        }
    }

    QStringList args = {command, QString::fromUtf8(params_json)};

    QPointer<MAAnalyticsService> self = this;
    const QByteArray params_for_hash = params_json;
    python::PythonRunner::instance().run(
        script, args,
        [self, context, cache_key, params_for_hash](python::PythonResult result) {
            if (!self)
                return;
            if (!result.success) {
                LOG_ERROR("MAAnalytics", QString("Python call failed [%1]: %2").arg(context, result.error));
                emit self->error_occurred(context, result.error);
                return;
            }
            auto json_str = python::extract_json(result.output);
            auto doc = QJsonDocument::fromJson(json_str.toUtf8());
            if (doc.isNull()) {
                LOG_ERROR("MAAnalytics", QString("Invalid JSON from [%1]").arg(context));
                emit self->error_occurred(context, "Invalid JSON response");
                return;
            }
            auto obj = doc.object();
            fincept::CacheManager::instance().put(
                cache_key,
                QVariant(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))),
                kResultTtlSec, "ma_analytics");
            // Per-input results (DCF/LBO/comps) intentionally do NOT persist
            // to disk: the constructor can't replay them anyway (filename
            // only carries an MD5 of the params, not the original params or
            // context), so writing them just bloated the cache directory
            // without any cold-start benefit. The CacheManager 5-min TTL
            // covers same-session reuse; cross-session reuse requires the
            // user to re-enter the same inputs, which is rare for ad-hoc
            // valuation work.
            //
            // params_for_hash is intentionally unused now; left in the
            // signature so a future by-input rehydrator (storing context +
            // raw params inside the JSON) can pick this back up.
            (void)params_for_hash;
            LOG_INFO("MAAnalytics", QString("Result ready [%1]").arg(context));
            emit self->result_ready(context, obj);
            publish_ma_result(self->hub_registered_, context, obj);
        });
}

// ── Valuation ────────────────────────────────────────────────────────────────
void MAAnalyticsService::calculate_dcf(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/valuation/dcf_model.py", "calculate", params, "dcf");
}

void MAAnalyticsService::calculate_lbo_returns(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/lbo/returns_calculator.py", "calculate", params, "lbo_returns");
}

void MAAnalyticsService::build_lbo_model(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/lbo/lbo_model.py", "build", params, "lbo_model");
}

void MAAnalyticsService::analyze_lbo_debt_schedule(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/lbo/debt_schedule.py", "analyze", params, "lbo_debt_schedule");
}

void MAAnalyticsService::calculate_lbo_sensitivity(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/lbo/lbo_model.py", "sensitivity", params, "lbo_sensitivity");
}

void MAAnalyticsService::calculate_trading_comps(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/valuation/trading_comps.py", "calculate", params, "trading_comps");
}

void MAAnalyticsService::calculate_precedent_transactions(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/valuation/precedent_transactions.py", "calculate", params,
                    "precedent_txns");
}

void MAAnalyticsService::calculate_dcf_sensitivity(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/valuation/dcf_model.py", "sensitivity", params, "dcf_sensitivity");
}

void MAAnalyticsService::generate_football_field(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/valuation/football_field.py", "generate", params, "football_field");
}

// ── Merger Analysis ──────────────────────────────────────────────────────────
void MAAnalyticsService::build_merger_model(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/merger_models/merger_model.py", "build", params, "merger_model");
}

void MAAnalyticsService::calculate_accretion_dilution(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/merger_models/merger_model.py", "accretion_dilution", params,
                    "accretion_dilution");
}

void MAAnalyticsService::build_pro_forma(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/merger_models/merger_model.py", "pro_forma", params, "pro_forma");
}

void MAAnalyticsService::calculate_sources_uses(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/merger_models/sources_uses.py", "calculate", params, "sources_uses");
}

void MAAnalyticsService::analyze_contribution(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/merger_models/contribution_analysis.py", "analyze", params,
                    "contribution");
}

void MAAnalyticsService::calculate_revenue_synergies(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/synergies/revenue_synergies.py", "calculate", params,
                    "revenue_synergies");
}

void MAAnalyticsService::calculate_cost_synergies(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/synergies/cost_synergies.py", "calculate", params, "cost_synergies");
}

void MAAnalyticsService::value_synergies_dcf(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/synergies/synergy_valuation.py", "value", params, "synergy_dcf");
}

// ── Deal Structure ───────────────────────────────────────────────────────────
void MAAnalyticsService::analyze_payment_structure(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_structure/payment_structure.py", "analyze", params,
                    "payment_structure");
}

void MAAnalyticsService::value_earnout(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_structure/earnout_calculator.py", "calculate", params, "earnout");
}

void MAAnalyticsService::calculate_exchange_ratio(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_structure/exchange_ratio.py", "calculate", params,
                    "exchange_ratio");
}

void MAAnalyticsService::analyze_collar_mechanism(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_structure/collar_mechanisms.py", "analyze", params, "collar");
}

void MAAnalyticsService::value_cvr(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_structure/cvr_valuation.py", "calculate", params, "cvr");
}

// ── Deal Database ────────────────────────────────────────────────────────────
void MAAnalyticsService::scan_filings(int days_back) {
    run_python("Analytics/corporateFinance/deal_database/deal_tracker.py", {"scan", QString::number(days_back)},
               "scan_filings");
}

void MAAnalyticsService::get_all_deals() {
    run_python("Analytics/corporateFinance/deal_database/deal_tracker.py", {"list"}, "all_deals");
}

void MAAnalyticsService::search_deals(const QString& query) {
    run_python("Analytics/corporateFinance/deal_database/deal_tracker.py", {"search", query}, "search_deals");
}

void MAAnalyticsService::create_deal(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_database/database_schema.py", "create", params, "create_deal");
}

void MAAnalyticsService::update_deal(const QString& deal_id, const QJsonObject& updates) {
    QJsonObject params = updates;
    params["deal_id"] = deal_id;
    run_python_json("Analytics/corporateFinance/deal_database/database_schema.py", "update", params, "update_deal");
}

void MAAnalyticsService::parse_filing(const QString& filing_url, const QString& filing_type) {
    run_python("Analytics/corporateFinance/deal_database/deal_parser.py", {"parse", filing_url, filing_type},
               "parse_filing");
}

void MAAnalyticsService::estimate_integration_costs(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/synergies/integration_costs.py", "estimate", params,
                    "integration_costs");
}

// ── Startup Valuation ────────────────────────────────────────────────────────
void MAAnalyticsService::calculate_berkus(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/berkus_method.py", "calculate", params, "berkus");
}

void MAAnalyticsService::calculate_scorecard(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/scorecard_method.py", "calculate", params,
                    "scorecard");
}

void MAAnalyticsService::calculate_vc_method(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/vc_method.py", "calculate", params, "vc_method");
}

void MAAnalyticsService::calculate_first_chicago(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/first_chicago_method.py", "calculate", params,
                    "first_chicago");
}

void MAAnalyticsService::calculate_risk_factor(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/risk_factor_summation.py", "calculate", params,
                    "risk_factor");
}

void MAAnalyticsService::calculate_comprehensive_startup(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/startup_valuation/startup_summary.py", "comprehensive", params,
                    "startup_comprehensive");
}

// ── Fairness Opinion ─────────────────────────────────────────────────────────
void MAAnalyticsService::generate_fairness_opinion(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/fairness_opinion/valuation_framework.py", "generate", params,
                    "fairness_opinion");
}

void MAAnalyticsService::analyze_premium(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/fairness_opinion/premium_analysis.py", "analyze", params,
                    "premium_analysis");
}

void MAAnalyticsService::assess_process_quality(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/fairness_opinion/process_quality.py", "assess", params,
                    "process_quality");
}

// ── Industry Metrics ─────────────────────────────────────────────────────────
void MAAnalyticsService::calculate_tech_metrics(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/industry_metrics/technology.py", "calculate", params, "tech_metrics");
}

void MAAnalyticsService::calculate_healthcare_metrics(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/industry_metrics/healthcare.py", "calculate", params,
                    "healthcare_metrics");
}

void MAAnalyticsService::calculate_financial_services_metrics(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/industry_metrics/financial_services.py", "calculate", params,
                    "finserv_metrics");
}

// ── Advanced Analytics ───────────────────────────────────────────────────────
void MAAnalyticsService::run_monte_carlo(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/advanced_analytics/monte_carlo_valuation.py", "run", params,
                    "monte_carlo");
}

void MAAnalyticsService::run_regression(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/advanced_analytics/regression_analysis.py", "run", params,
                    "regression");
}

// ── Deal Comparison ──────────────────────────────────────────────────────────
void MAAnalyticsService::compare_deals(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_comparison/deal_comparator.py", "compare", params,
                    "compare_deals");
}

void MAAnalyticsService::rank_deals(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_comparison/deal_comparator.py", "rank", params, "rank_deals");
}

void MAAnalyticsService::benchmark_deal_premium(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_comparison/deal_comparator.py", "benchmark", params,
                    "benchmark_premium");
}

void MAAnalyticsService::analyze_payment_structures(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_comparison/deal_comparator.py", "payment_structures", params,
                    "payment_structures");
}

void MAAnalyticsService::analyze_industry_deals(const QJsonObject& params) {
    run_python_json("Analytics/corporateFinance/deal_comparison/deal_comparator.py", "industry", params,
                    "industry_deals");
}

// ═══════════════════════════════════════════════════════════════════════════════
// DATAHUB PRODUCER — ma:*
// ═══════════════════════════════════════════════════════════════════════════════
//
// All M&A methods funnel through run_python / run_python_json which emit
// `result_ready(context, obj)` and publish to `ma:<context>` in parallel.
// Because every analytic takes a caller-supplied `params` payload, the hub
// can't schedule a refresh without knowing the parameters — so topics are
// push-only. The hub still caches the most recent result for subscribers
// who join late, but won't drive new fetches.

QStringList MAAnalyticsService::topic_patterns() const {
    return {QStringLiteral("ma:*")};
}

void MAAnalyticsService::refresh(const QStringList& topics) {
    // All ma:* topics are push-only (params-dependent). Log advisory only.
    Q_UNUSED(topics);
}

int MAAnalyticsService::max_requests_per_sec() const {
    return 1;  // Heavy Python analytics; callers drive their own cadence.
}

void MAAnalyticsService::ensure_registered_with_hub() {
    if (hub_registered_) return;
    auto& hub = fincept::datahub::DataHub::instance();
    hub.register_producer(this);

    fincept::datahub::TopicPolicy policy;
    policy.push_only = true;
    policy.ttl_ms = kResultTtlSec * 1000;  // 2 min cache for latecomers.
    hub.set_policy_pattern(QStringLiteral("ma:*"), policy);

    hub_registered_ = true;
    LOG_INFO("MAAnalyticsService", "Registered with DataHub (ma:*)");
}

} // namespace fincept::services::ma
