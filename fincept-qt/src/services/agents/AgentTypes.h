// src/services/agents/AgentTypes.h
#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::services {

// ── View modes for the Agent Config screen ──────────────────────────────────

enum class AgentViewMode { Agents, Create, Teams, Workflows, Planner, Tools, Chat, System };

// ── Agent discovery ─────────────────────────────────────────────────────────

struct AgentInfo {
    QString id;
    QString name;
    QString description;
    QString category;
    QStringList capabilities;
    QString provider;
    QString version;
    QJsonObject config; // full config payload
};

struct AgentCategory {
    QString name;
    int count = 0;
};

// ── Execution results ───────────────────────────────────────────────────────

struct AgentExecutionResult {
    bool success = false;
    QString response;
    QString error;
    int execution_time_ms = 0;
    QString request_id; // set by AgentService; panels guard on this to avoid cross-contamination
};

// ── Routing ─────────────────────────────────────────────────────────────────

struct RoutingResult {
    bool success = false;
    QString agent_id;
    QString intent;
    /// Routing match score — NOT a calibrated confidence.  `score_basis`
    /// says what it is: "keyword_match" (0.3 per keyword + 0.5 per pattern,
    /// capped at 1) or "llm_self_reported" (the router model's own number).
    /// has_match_score is false when no score exists (e.g. LLM gave none).
    double match_score = 0.0;
    bool has_match_score = false;
    QString score_basis;
    QStringList matched_keywords;
    QJsonObject config;
    QString request_id; // matches the run_agent call that triggered this routing
};

/// Human label for a routing score, e.g. "keyword match score 0.60" or
/// "router's self-rated score 0.80".  Never rendered as a "confidence %".
inline QString routing_score_label(const RoutingResult& r) {
    if (!r.has_match_score)
        return QStringLiteral("no match score");
    const QString n = QString::number(r.match_score, 'f', 2);
    if (r.score_basis == QLatin1String("llm_self_reported"))
        return QStringLiteral("router's self-rated score %1").arg(n);
    return QStringLiteral("keyword match score %1").arg(n);
}

// ── System info ─────────────────────────────────────────────────────────────

struct AgentSystemInfo {
    QString version;
    QString framework;
    QJsonObject capabilities; // providers, tools, vectordbs, embedders, output_models
    QStringList features;
};

// ── Tools ───────────────────────────────────────────────────────────────────

struct AgentToolsInfo {
    QJsonObject tools; // category -> [tool names]
    QStringList categories;
    int total_count = 0;
};

// ── Models ──────────────────────────────────────────────────────────────────

struct AgentModelsInfo {
    QStringList providers;
    int count = 0;
};

// ── Team configuration ──────────────────────────────────────────────────────

struct TeamMember {
    QString agent_id;
    QString name;
    QString role;
    QJsonObject model_config;
    QStringList tools;
    QString instructions;
};

struct TeamConfig {
    QString name;
    QString mode; // "coordinate", "route", "collaborate"
    QVector<TeamMember> members;
    QJsonObject coordinator_model;
    bool show_member_responses = false;
    int leader_index = 0;
};

// ── Execution plan ──────────────────────────────────────────────────────────

struct PlanStep {
    QString id;
    QString name;
    QString step_type;
    QJsonObject config;
    QStringList dependencies;
    QString status; // "pending", "running", "completed", "failed"
    QString result;
    QString error;
};

struct ExecutionPlan {
    QString id;
    QString name;
    QString description;
    QVector<PlanStep> steps;
    QString status;
    bool is_complete = false;
    bool has_failed = false;
    QString request_id;
    /// Planner notices, e.g. tools the LLM named that don't exist and were
    /// dropped from a step.  Shown to the user, never silently discarded.
    QStringList warnings;
};

} // namespace fincept::services
