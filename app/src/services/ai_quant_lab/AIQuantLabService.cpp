// src/services/ai_quant_lab/AIQuantLabService.cpp
#include "services/ai_quant_lab/AIQuantLabService.h"

#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "services/agents/AgentService.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/LlmProfileRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>

namespace finterm::services::quant {

static QString compact(const QJsonObject& o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// ── Singleton ────────────────────────────────────────────────────────────────
AIQuantLabService& AIQuantLabService::instance() {
    static AIQuantLabService inst;
    return inst;
}

AIQuantLabService::AIQuantLabService(QObject* parent) : QObject(parent) {
    // Build script map from module definitions
    for (const auto& mod : all_quant_modules())
        script_map_[mod.id] = mod.script;
}

// ── Python helper ────────────────────────────────────────────────────────────
// Quant-lab scripts report failure in-band ({"success": false, "error": ...}
// or a bare {"error": ...}), frequently on stdout with exit 1 and an empty
// stderr. Returns that message, or empty when the payload is a real result.
static QString aiq_in_band_error(const QJsonObject& o) {
    const QJsonValue ev = o.value("error");
    const bool has_error = !ev.isUndefined() && !ev.isNull() && !(ev.isString() && ev.toString().isEmpty()) &&
                           !(ev.isBool() && !ev.toBool());
    if (!has_error && o.value("success").toBool(true))
        return {};
    if (ev.isString())
        return ev.toString();
    if (o.value("message").isString())
        return o.value("message").toString();
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).left(500);
}

static QString aiq_failure_message(const python::PythonResult& result) {
    const auto doc = QJsonDocument::fromJson(python::extract_json(result.output).toUtf8());
    QString msg = doc.isObject() ? aiq_in_band_error(doc.object()) : QString();
    if (msg.isEmpty())
        msg = result.error.trimmed();
    if (msg.isEmpty())
        msg = QString("Script failed (exit code %1)").arg(result.exit_code);
    return msg;
}
// What takes minutes goes in a job's slot; the rest is asked for by a reader
// who is waiting, and must not queue behind a training.
static bool is_long_job(const QString& module_id, const QString& command) {
    static const QSet<QString> commands = {
        QStringLiteral("train"),        QStringLiteral("train_model"),        QStringLiteral("retrain"),
        QStringLiteral("run_backtest"), QStringLiteral("backtest"),           QStringLiteral("optimize_portfolio"),
        QStringLiteral("execute_task"), QStringLiteral("run_selection"),      QStringLiteral("tune_hyperparameters"),
        QStringLiteral("create_ensemble"), QStringLiteral("generate_evaluation_report"),
    };
    if (command == QLatin1String("evaluate"))
        return module_id == QLatin1String("rl_trading"); // plays whole episodes
    // Forecasting wrappers fit a model to answer ("forecast", "forecast_deepar").
    return commands.contains(command) || command.startsWith(QLatin1String("forecast")) ||
           (module_id == QLatin1String("gluonts") && !command.contains(QLatin1String("status")));
}

void AIQuantLabService::run_python(const QString& script, const QStringList& args, const QString& module_id,
                                   const QString& command) {
    QPointer<AIQuantLabService> self = this;
    auto done = [self, module_id, command](python::PythonResult result) {
        if (!self)
            return;
        if (!result.success) {
            const QString msg = aiq_failure_message(result);
            LOG_ERROR("AIQuantLab", QString("[%1/%2] Failed: %3").arg(module_id, command, msg));
            emit self->error_occurred(module_id, msg);
            return;
        }
        auto json_str = python::extract_json(result.output);
        auto doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isNull()) {
            LOG_ERROR("AIQuantLab", QString("[%1/%2] Invalid JSON").arg(module_id, command));
            emit self->error_occurred(module_id, "Invalid JSON response");
            return;
        }
        if (const QString msg = aiq_in_band_error(doc.object()); !msg.isEmpty()) {
            // In-band error: report it as a failure and never cache it as a result.
            LOG_ERROR("AIQuantLab", QString("[%1/%2] Script reported error: %3").arg(module_id, command, msg));
            emit self->error_occurred(module_id, msg);
            return;
        }
        LOG_INFO("AIQuantLab", QString("[%1/%2] Result ready").arg(module_id, command));
        emit self->result_ready(module_id, command, doc.object());
    };
    auto& runner = python::PythonRunner::instance();
    if (is_long_job(module_id, command))
        runner.run_job(script, args, std::move(done));
    else
        runner.run(script, args, std::move(done));
}

void AIQuantLabService::run_python_cached(const QString& script, const QStringList& args, const QString& module_id,
                                          const QString& command, int ttl_sec) {
    const QString cache_key = "aiquant:" + module_id + ":" + command;
    const QVariant cached = finterm::CacheManager::instance().get(cache_key);
    if (!cached.isNull()) {
        auto doc = QJsonDocument::fromJson(cached.toString().toUtf8());
        if (!doc.isNull()) {
            LOG_DEBUG("AIQuantLab", QString("[%1/%2] Cache hit").arg(module_id, command));
            emit result_ready(module_id, command, doc.object());
            return;
        }
    }

    QPointer<AIQuantLabService> self = this;
    python::PythonRunner::instance().run(
        script, args, [self, module_id, command, cache_key, ttl_sec](python::PythonResult result) {
            if (!self)
                return;
            if (!result.success) {
                const QString msg = aiq_failure_message(result);
                LOG_ERROR("AIQuantLab", QString("[%1/%2] Failed: %3").arg(module_id, command, msg));
                emit self->error_occurred(module_id, msg);
                return;
            }
            auto json_str = python::extract_json(result.output);
            auto doc = QJsonDocument::fromJson(json_str.toUtf8());
            if (doc.isNull()) {
                LOG_ERROR("AIQuantLab", QString("[%1/%2] Invalid JSON").arg(module_id, command));
                emit self->error_occurred(module_id, "Invalid JSON response");
                return;
            }
            if (const QString msg = aiq_in_band_error(doc.object()); !msg.isEmpty()) {
                // In-band error: report it as a failure and never cache it as a result.
                LOG_ERROR("AIQuantLab", QString("[%1/%2] Script reported error: %3").arg(module_id, command, msg));
                emit self->error_occurred(module_id, msg);
                return;
            }
            finterm::CacheManager::instance().put(
                cache_key, QVariant(QString::fromUtf8(QJsonDocument(doc.object()).toJson(QJsonDocument::Compact))),
                ttl_sec, "ai_quant_lab");
            LOG_INFO("AIQuantLab", QString("[%1/%2] Result ready").arg(module_id, command));
            emit self->result_ready(module_id, command, doc.object());
        });
}

// ── Generic module execution ─────────────────────────────────────────────────
void AIQuantLabService::run_module(const QString& module_id, const QString& command, const QJsonObject& params) {
    // The Deep Agent module is two programs: its script is RD-Agent's.
    // Its script's commands only read; the rest are processes this service keeps.
    if (module_id == QLatin1String("deep_agent")) {
        QJsonObject p = params;
        p.remove("config"); // asked for by an agent: the model is the app's to choose
        const QString task = p["task_id"].toString();
        if (command == QLatin1String("execute_task"))
            return run_deep_agent(p);
        if (command == QLatin1String("start_factor_mining"))
            return rd_agent_start_factor_mining(p);
        if (command == QLatin1String("start_model_optimization"))
            return rd_agent_start_model_optimization(p);
        if (command == QLatin1String("start_quant_research"))
            return rd_agent_start_quant_research(p);
        if (command == QLatin1String("resume_task"))
            return rd_agent_resume_task(task);
        if (command == QLatin1String("stop_task"))
            return rd_agent_stop_task(task);
        if (command == QLatin1String("start_ui"))
            return rd_agent_start_ui();
        if (command == QLatin1String("start_mcp_server"))
            return rd_agent_start_mcp_server(p["port"].toInt(kMcpPort));
        if (command == QLatin1String("mcp_status"))
            return rd_agent_mcp_status();
    }
    auto it = script_map_.find(module_id);
    if (it == script_map_.end()) {
        emit error_occurred(module_id, "Unknown module: " + module_id);
        return;
    }
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python(*it, {command, json}, module_id, command);
}

// ── Qlib core ────────────────────────────────────────────────────────────────
void AIQuantLabService::list_models() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"list_models"}, "model_library", "list_models", kListTtlSec);
}

void AIQuantLabService::get_factor_library() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"get_factor_library"}, "factor_discovery", "get_factor_library",
                      kListTtlSec);
}

void AIQuantLabService::train_model(const QJsonObject& params) {
    run_module("model_library", "train_model", params);
}

void AIQuantLabService::run_backtest(const QJsonObject& params) {
    run_module("backtesting", "run_backtest", params);
}

void AIQuantLabService::optimize_portfolio(const QJsonObject& params) {
    run_module("backtesting", "optimize_portfolio", params);
}

// ── RL Trading ───────────────────────────────────────────────────────────────
void AIQuantLabService::train_rl_agent(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    QPointer<AIQuantLabService> self = this;

    // Called per stdout/stderr line while the process runs — for live UX only.
    auto on_line = [self](QString line, bool is_stderr) {
        if (!self)
            return;
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith('{')) {
            auto doc = QJsonDocument::fromJson(trimmed.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                auto obj = doc.object();
                const auto event = obj.value("event").toString();
                if (event == "progress") {
                    emit self->training_progress("rl_trading", obj.value("step").toInt(), obj.value("total").toInt(),
                                                 obj.value("reward_mean").toDouble(), obj.value("loss").toDouble());
                    return;
                }
                if (event == "log") {
                    emit self->training_log("rl_trading", obj.value("msg").toString(), is_stderr);
                    return;
                }
                // "result" events are handled authoritatively in the finished callback.
                if (event == "result")
                    return;
            }
        }
        // Non-JSON or untagged line → raw log entry (SB3 warnings, stack traces, etc).
        emit self->training_log("rl_trading", line, is_stderr);
    };

    // Called once when the process ends — authoritative parse for the final result.
    auto on_finished = [self](python::PythonResult result) {
        if (!self)
            return;
        QJsonObject final_result;
        bool found_result = false;
        const auto lines = result.output.split('\n', Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const auto trimmed = line.trimmed();
            if (!trimmed.startsWith('{'))
                continue;
            auto doc = QJsonDocument::fromJson(trimmed.toUtf8());
            if (doc.isNull() || !doc.isObject())
                continue;
            auto obj = doc.object();
            if (obj.value("event").toString() == "result") {
                obj.remove("event");
                final_result = obj;
                found_result = true;
            }
        }
        // Only trust a result event if the process also exited cleanly. A crash
        // after the script emitted a result line would otherwise surface stale
        // data as success — unsafe for a finance-critical training path.
        if (found_result && result.success) {
            LOG_INFO("AIQuantLab", "[rl_trading/train] Result ready");
            emit self->result_ready("rl_trading", "train", final_result);
        } else {
            QString msg;
            if (found_result && !result.success) {
                msg =
                    QString("Training process exited abnormally (exit=%1) after emitting result").arg(result.exit_code);
                if (!result.error.isEmpty())
                    msg += QStringLiteral(": ") + result.error;
            } else if (!result.error.isEmpty()) {
                msg = result.error;
            } else {
                msg = QStringLiteral("Training ended without result");
            }
            LOG_ERROR("AIQuantLab", QString("[rl_trading/train] Failed: %1").arg(msg));
            emit self->error_occurred("rl_trading", msg);
        }
    };

    python::PythonRunner::instance().run_job("ai_quant_lab/qlib_rl.py", {"train", json}, on_finished, on_line);
}

// ── GS Quant ─────────────────────────────────────────────────────────────────
void AIQuantLabService::gs_risk_metrics(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"risk_metrics", json}, "gs_quant", "risk_metrics");
}

void AIQuantLabService::gs_portfolio_analytics(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"portfolio_analytics", json}, "gs_quant",
               "portfolio_analytics");
}

void AIQuantLabService::gs_greeks(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"greeks", json}, "gs_quant", "greeks");
}

void AIQuantLabService::gs_var_analysis(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"var_analysis", json}, "gs_quant", "var_analysis");
}

void AIQuantLabService::gs_stress_test(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"stress_test", json}, "gs_quant", "stress_test");
}

void AIQuantLabService::gs_backtest(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"backtest", json}, "gs_quant", "backtest");
}

void AIQuantLabService::gs_statistics(const QJsonObject& params) {
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    run_python("Analytics/gs_quant_wrapper/gs_quant_service.py", {"statistics", json}, "gs_quant", "statistics");
}

void AIQuantLabService::cfa_stationarity_test(const QJsonObject& params) {
    run_module("cfa_quant", "stationarity_test", params);
}

void AIQuantLabService::train_advanced_model(const QJsonObject& params) {
    run_module("advanced_models", "train_model", params);
}

// ── Factor Discovery ─────────────────────────────────────────────────────────
void AIQuantLabService::factor_get_library() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"get_factor_library"}, "factor_discovery", "get_factor_library",
                      kListTtlSec);
}
void AIQuantLabService::factor_get_data(const QJsonObject& params) {
    run_module("factor_discovery", "get_data", params);
}
void AIQuantLabService::factor_get_calendar(const QJsonObject& params) {
    run_module("factor_discovery", "get_calendar", params);
}
void AIQuantLabService::factor_get_instruments() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"get_instruments"}, "factor_discovery", "get_instruments",
                      kListTtlSec);
}

// ── Model Library ─────────────────────────────────────────────────────────────
void AIQuantLabService::model_list() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"list_models"}, "model_library", "list_models", kListTtlSec);
}
void AIQuantLabService::model_train(const QJsonObject& params) {
    run_module("model_library", "train_model", params);
}
void AIQuantLabService::model_backtest(const QJsonObject& params) {
    run_module("model_library", "run_backtest", params);
}
void AIQuantLabService::model_check_status() {
    run_python_cached("ai_quant_lab/qlib_service.py", {"check_status"}, "model_library", "check_status", kListTtlSec);
}

// ── Live Signals ──────────────────────────────────────────────────────────────
void AIQuantLabService::signals_get_data(const QJsonObject& params) {
    run_module("live_signals", "get_data", params);
}
void AIQuantLabService::signals_get_factor_analysis(const QJsonObject& params) {
    run_module("live_signals", "get_factor_analysis", params);
}
void AIQuantLabService::signals_get_feature_importance(const QJsonObject& params) {
    run_module("live_signals", "get_feature_importance", params);
}

// ── HFT ──────────────────────────────────────────────────────────────────────
void AIQuantLabService::hft_create_orderbook(const QJsonObject& params) {
    run_module("hft", "fetch_orderbook", params); // live CCXT fetch
}
void AIQuantLabService::hft_snapshot(const QJsonObject& params) {
    run_module("hft", "analyze", params); // full analysis: book + trades + all metrics
}
void AIQuantLabService::hft_market_making_quotes(const QJsonObject& params) {
    run_module("hft", "market_making", params); // live book → A-S quotes
}
void AIQuantLabService::hft_detect_toxic(const QJsonObject& params) {
    run_module("hft", "toxic_flow", params); // live trades → PIN score
}
void AIQuantLabService::hft_execute_order(const QJsonObject& params) {
    run_module("hft", "slippage", params); // order book walk → slippage estimate
}

// ── Meta Learning ────────────────────────────────────────────────────────────
void AIQuantLabService::meta_list_models() {
    run_python_cached("ai_quant_lab/qlib_meta_learning.py", {"list_models"}, "meta_learning", "list_models",
                      kListTtlSec);
}
void AIQuantLabService::meta_run_selection(const QJsonObject& params) {
    run_module("meta_learning", "run_selection", params);
}
void AIQuantLabService::meta_create_ensemble(const QJsonObject& params) {
    run_module("meta_learning", "create_ensemble", params);
}
void AIQuantLabService::meta_tune_hyperparameters(const QJsonObject& params) {
    run_module("meta_learning", "tune_hyperparameters", params);
}
void AIQuantLabService::meta_get_results() {
    run_python_cached("ai_quant_lab/qlib_meta_learning.py", {"get_results"}, "meta_learning", "get_results",
                      kListTtlSec);
}

// ── Online Learning ──────────────────────────────────────────────────────────
void AIQuantLabService::online_list_models() {
    run_python_cached("ai_quant_lab/qlib_online_learning.py", {"list_models"}, "online_learning", "list_models",
                      kListTtlSec);
}
void AIQuantLabService::online_create_model(const QJsonObject& params) {
    run_module("online_learning", "create_model", params);
}
void AIQuantLabService::online_train(const QJsonObject& params) {
    run_module("online_learning", "train", params);
}
void AIQuantLabService::online_predict(const QJsonObject& params) {
    run_module("online_learning", "predict", params);
}
void AIQuantLabService::online_performance(const QJsonObject& params) {
    run_module("online_learning", "performance", params);
}

// ── Rolling Retraining ───────────────────────────────────────────────────────
void AIQuantLabService::rolling_create_schedule(const QJsonObject& params) {
    run_module("rolling_retraining", "create", params);
}
void AIQuantLabService::rolling_execute_retrain(const QJsonObject& params) {
    // Retrain emits multiple JSON lines (streaming progress events).
    // We parse each line individually and emit result_ready per event.
    auto json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    QPointer<AIQuantLabService> self = this;
    auto done = [self](const python::PythonResult& result) {
        if (!self)
            return;
        // Every event it got to, then — when it did not end well — why.
        for (const auto& line : result.output.split('\n', Qt::SkipEmptyParts)) {
            const auto doc = QJsonDocument::fromJson(line.trimmed().toUtf8());
            if (doc.isObject())
                emit self->result_ready("rolling_retraining", "retrain", doc.object());
        }
        if (!result.success)
            emit self->error_occurred("rolling_retraining", result.error.trimmed().isEmpty()
                                                                ? QStringLiteral("The retrain ended without a result")
                                                                : result.error.trimmed());
    };
    python::PythonRunner::instance().run_job("ai_quant_lab/qlib_rolling_retraining.py", {"retrain", json}, done);
}
void AIQuantLabService::rolling_list_schedules() {
    run_python_cached("ai_quant_lab/qlib_rolling_retraining.py", {"list"}, "rolling_retraining", "list", kListTtlSec);
}
void AIQuantLabService::rolling_preview_tasks(const QJsonObject& params) {
    run_module("rolling_retraining", "preview", params);
}
void AIQuantLabService::rolling_delete_schedule(const QJsonObject& params) {
    run_module("rolling_retraining", "delete", params);
}

// ── Deep Agent (LangGraph multi-agent) ───────────────────────────────────────
QJsonObject AIQuantLabService::llm_config(const QString& profile_id) {
    QString provider, model, key, base;
    int max_tokens = 0;
    const auto take = [&](const auto& p) {
        provider = p.provider, model = p.model_id, key = p.api_key, base = p.base_url, max_tokens = p.max_tokens;
    };
    if (profile_id.isEmpty())
        take(LlmProfileRepository::instance().resolve_for_context("agent"));
    else if (const auto r = LlmProfileRepository::instance().get_profile(profile_id); r.is_ok())
        take(r.value());
    if (provider.isEmpty() || model.isEmpty())
        return {};
    return {{"llm_provider", provider},
            {"llm_model", model},
            {"llm_api_key", key},
            {"llm_base_url", ai_chat::LlmService::provider_api_root(provider, base)},
            {"llm_max_tokens", max_tokens}};
}

void AIQuantLabService::run_deep_agent(const QJsonObject& params) {
    QJsonObject config = params["config"].toObject();
    if (!config.contains("llm_provider"))
        config = llm_config();
    if (config.isEmpty()) {
        emit error_occurred("deep_agent", "No LLM profile: set one up in Settings > LLM Config");
        return;
    }
    QJsonObject payload{{"task", params["task"]}};
    payload["agent_type"] = params["agent_type"].toString(QStringLiteral("general"));
    if (params.contains("thread_id"))
        payload["thread_id"] = params["thread_id"];
    QJsonObject clean{{"llm_provider", config["llm_provider"]},
                      {"llm_model", config["llm_model"]},
                      {"llm_api_key", config["llm_api_key"]},
                      {"llm_base_url", config["llm_base_url"]},
                      {"llm_max_tokens", config["llm_max_tokens"]}};
    AgentService::attach_terminal_tools(clean);
    payload["config"] = clean;
    const auto json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    run_python("agents/deepagents/cli.py", {"execute_task", json}, "deep_agent", "execute_task");
}

// ── RD-Agent (autonomous factor/model research) ──────────────────────────────
void AIQuantLabService::rd_agent_check_status() {
    run_python("agents/rdagents/cli.py", {"check_status"}, "deep_agent", "check_status");
}

void AIQuantLabService::rd_agent_start_factor_mining(const QJsonObject& params) {
    start_rd_job("start_factor_mining", "factor_mining", params);
}

void AIQuantLabService::rd_agent_start_model_optimization(const QJsonObject& params) {
    start_rd_job("start_model_optimization", "model_optimization", params);
}

void AIQuantLabService::rd_agent_start_quant_research(const QJsonObject& params) {
    start_rd_job("start_quant_research", "quant_research", params);
}

void AIQuantLabService::rd_agent_get_discovered_factors(const QString& task_id) {
    run_python("agents/rdagents/cli.py", {"get_discovered_factors", compact({{"task_id", task_id}})}, "deep_agent",
               "get_discovered_factors");
}

void AIQuantLabService::rd_agent_get_optimized_model(const QString& task_id) {
    run_python("agents/rdagents/cli.py", {"get_optimized_model", compact({{"task_id", task_id}})}, "deep_agent",
               "get_optimized_model");
}

void AIQuantLabService::rd_agent_list_tasks(const QString& status_filter) {
    run_python("agents/rdagents/cli.py",
               {"list_tasks", status_filter.isEmpty() ? QStringLiteral("{}") : compact({{"status", status_filter}})},
               "deep_agent", "list_tasks");
}

void AIQuantLabService::rd_agent_stop_task(const QString& task_id) {
    const quint64 handle = rd_jobs_.value(task_id);
    if (handle == 0) {
        emit error_occurred("deep_agent", QString("%1 is not running").arg(task_id));
        return;
    }
    python::PythonRunner::instance().stop_background(handle);
    emit result_ready("deep_agent", "stop_task",
                      {{"success", true}, {"task_id", task_id}, {"message", QString("Stopping %1").arg(task_id)}});
}

void AIQuantLabService::rd_agent_resume_task(const QString& task_id, const QJsonObject& config) {
    start_rd_job("resume_task", {}, {{"task_id", task_id}, {"config", config}});
}

// A job is a process of its own, kept here until it ends (worker.py). Its first
// line says whether it started.
void AIQuantLabService::start_rd_job(const QString& command, const QString& type, QJsonObject params) {
    if (!rd_jobs_.isEmpty()) { // each wants the machine: LLM, Docker, hours
        emit error_occurred("deep_agent", QString("%1 is running: one RD-Agent job at a time").arg(rd_jobs_.firstKey()));
        return;
    }
    if (params["config"].toObject().isEmpty())
        params["config"] = llm_config();
    if (!type.isEmpty())
        params["type"] = type;
    // Until it has a name, it holds the place under the command's.
    struct State {
        QString key;
        quint64 handle = 0;
        bool answered = false;
    };
    auto state = std::make_shared<State>();
    state->key = command;
    rd_jobs_.insert(command, 0);
    QPointer<AIQuantLabService> self = this;
    auto on_line = [self, state, command](const QString& line, bool is_stderr) {
        if (!self || is_stderr || state->answered)
            return;
        const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();
        if (!o.contains("success"))
            return;
        state->answered = true;
        if (!o["success"].toBool()) {
            emit self->error_occurred("deep_agent", o["error"].toString());
            return;
        }
        self->rd_jobs_.remove(state->key);
        state->key = o["task_id"].toString();
        self->rd_jobs_.insert(state->key, state->handle);
        emit self->result_ready("deep_agent", command, o);
    };
    auto on_exit = [self, state](const python::PythonResult& r) {
        if (!self)
            return;
        self->rd_jobs_.remove(state->key);
        if (!state->answered)
            emit self->error_occurred("deep_agent", r.error.trimmed().isEmpty() ? QStringLiteral("RD-Agent did not start")
                                                                                : r.error.trimmed().right(500));
        else
            self->rd_agent_list_tasks(); // how it ended
    };
    const quint64 handle = python::PythonRunner::instance().start_background(
        "agents/rdagents/worker.py", {type.isEmpty() ? "resume" : "start", compact(params)}, on_line, on_exit);
    state->handle = handle;
}

void AIQuantLabService::rd_agent_start_ui() {
    start_service("start_ui", "agents/rdagents/log_viewer.py", {}, {});
}

void AIQuantLabService::rd_agent_start_mcp_server(int port) {
    if (services_.contains("start_mcp_server")) {
        emit result_ready("deep_agent", "start_mcp_server", services_["start_mcp_server"].answer);
        return;
    }
    QJsonObject params{{"port", port}};
    AgentService::attach_terminal_tools(params, /*lasting=*/true);
    const QJsonObject tools = params;
    start_service("start_mcp_server", "agents/rdagents/mcp_server.py", {compact(params)},
                  [tools]() { AgentService::release_terminal_tools(tools); });
}

void AIQuantLabService::rd_agent_stop_mcp_server() {
    if (services_.contains("start_mcp_server"))
        python::PythonRunner::instance().stop_background(services_["start_mcp_server"].handle);
}

void AIQuantLabService::rd_agent_mcp_status() {
    const bool on = services_.contains("start_mcp_server");
    QJsonObject o = on ? services_["start_mcp_server"].answer : QJsonObject();
    o["success"] = true;
    o["running"] = on;
    emit result_ready("deep_agent", "mcp_status", o);
}

// A server of ours: one of each, its answer (the address) kept while it runs.
void AIQuantLabService::start_service(const QString& command, const QString& script, const QStringList& args,
                                      std::function<void()> on_stopped) {
    if (services_.contains(command)) {
        if (!services_[command].answer.isEmpty())
            emit result_ready("deep_agent", command, services_[command].answer);
        return; // else it is starting, and will answer
    }
    services_.insert(command, {});
    QPointer<AIQuantLabService> self = this;
    auto answered = std::make_shared<bool>(false);
    auto on_line = [self, command, answered](const QString& line, bool is_stderr) {
        if (!self || is_stderr || *answered)
            return;
        const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();
        if (!o.contains("success"))
            return;
        *answered = true;
        if (!o["success"].toBool()) {
            emit self->error_occurred("deep_agent", o["error"].toString());
            return;
        }
        if (self->services_.contains(command))
            self->services_[command].answer = o;
        emit self->result_ready("deep_agent", command, o);
    };
    auto on_exit = [self, command, answered, on_stopped](const python::PythonResult& r) {
        if (on_stopped)
            on_stopped();
        if (!self)
            return;
        self->services_.remove(command);
        if (!*answered)
            emit self->error_occurred("deep_agent", r.error.trimmed().isEmpty() ? command + " ended at once"
                                                                                : r.error.trimmed().right(500));
        emit self->service_stopped(command);
    };
    const quint64 handle = python::PythonRunner::instance().start_background(script, args, on_line, on_exit);
    if (services_.contains(command))
        services_[command].handle = handle;
}

// ── Feature Engineering ──────────────────────────────────────────────────────
void AIQuantLabService::feature_compute(const QJsonObject& params) {
    auto cmd = params["indicator"].toString("moving_average");
    run_module("feature_engineering", cmd, params);
}

void AIQuantLabService::feature_select_by_ic(const QJsonObject& params) {
    run_module("feature_engineering", "select_features_by_ic", params);
}

void AIQuantLabService::feature_evaluate_expression(const QJsonObject& params) {
    run_module("feature_engineering", "evaluate_expression", params);
}

// ── Portfolio Optimization ────────────────────────────────────────────────────
void AIQuantLabService::portopt_run(const QString& method, const QJsonObject& params) {
    run_module("portfolio_opt", method, params);
}

// ── Factor Evaluation ─────────────────────────────────────────────────────────
void AIQuantLabService::evaluation_ic(const QJsonObject& params) {
    run_module("factor_evaluation", "calculate_ic", params);
}

void AIQuantLabService::evaluation_report(const QJsonObject& params) {
    run_module("factor_evaluation", "generate_evaluation_report", params);
}

void AIQuantLabService::evaluation_risk_metrics(const QJsonObject& params) {
    run_module("factor_evaluation", "calculate_risk_metrics", params);
}

// ── Strategy Builder ──────────────────────────────────────────────────────────
void AIQuantLabService::strategy_create(const QString& type, const QJsonObject& params) {
    run_module("strategy_builder", type, params);
}

void AIQuantLabService::strategy_portfolio_metrics(const QJsonObject& params) {
    run_module("strategy_builder", "portfolio_metrics", params);
}

// ── Data Processors ───────────────────────────────────────────────────────────
void AIQuantLabService::dataproc_list_processors() {
    auto it = script_map_.find("data_processors");
    if (it == script_map_.end()) {
        emit error_occurred("data_processors", "Unknown module");
        return;
    }
    run_python_cached(*it, {"list_processors", "{}"}, "data_processors", "list_processors", kListTtlSec);
}

void AIQuantLabService::dataproc_create_pipeline(const QJsonObject& params) {
    run_module("data_processors", "create_pipeline", params);
}

void AIQuantLabService::dataproc_process_data(const QJsonObject& params) {
    run_module("data_processors", "process_data", params);
}

// ── Quant Reporting ───────────────────────────────────────────────────────────
void AIQuantLabService::reporting_ic_analysis(const QJsonObject& params) {
    run_module("quant_reporting", "ic_analysis", params);
}

void AIQuantLabService::reporting_model_performance(const QJsonObject& params) {
    run_module("quant_reporting", "model_performance", params);
}

void AIQuantLabService::reporting_cumulative_returns(const QJsonObject& params) {
    run_module("quant_reporting", "cumulative_return_graph", params);
}

} // namespace finterm::services::quant
