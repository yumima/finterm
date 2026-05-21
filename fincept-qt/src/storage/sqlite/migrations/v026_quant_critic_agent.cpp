// v026_quant_critic_agent — Seed the quant_critic AgentConfig
// referenced by Track 11 #33.
//
// quant_critic is an opinionated agent identity whose system prompt
// + skills + tool allowlist are tuned for narrating quant outputs.
// It uses the QuantNarratorTools family (commit 32f1927a-onwards)
// plus selected read-only data tools from the existing surfaces.
//
// Skills (earnings-analysis, model-update) come from upstream
// `anthropics/financial-services` and aren't vendored yet (Track 7).
// They're listed in the config so they'll resolve once vendoring
// lands — the skills_loader silently drops unknown names today, so
// pre-Track-7 the agent runs with system_prompt + tools only.
//
// Idempotent: INSERT OR IGNORE on the id primary key.  Re-running
// the migration is a no-op; users can edit config_json by hand
// without fear of being overwritten.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration026";
constexpr const char* kAgentId = "quant_critic";

static QString build_config_json() {
    QJsonObject cfg;
    cfg["system_prompt"] = QStringLiteral(
        "You are quant_critic, finterm's reviewer of quantitative work.  "
        "Your audience is the user looking at their own strategy.  Be "
        "specific with magnitudes — cite Sharpe, drawdown, hit rate, "
        "turnover, regime sensitivity wherever the data supports it.  "
        "Prefer the narrate_backtest_result / propose_param_sweep / "
        "narrate_live_trade tools for commentary tasks; pull underlying "
        "numbers from the existing quant lab + paper trading + portfolio "
        "tools.  Avoid hedging language unless the data justifies it.");

    QJsonArray skills;
    skills.append(QStringLiteral("earnings-analysis"));
    skills.append(QStringLiteral("model-update"));
    cfg["skills"] = skills;

    // allow_tools (Track 8 #24 / R7) — wire-form glob patterns matching
    // McpService::list_tools_for(agent_id).  Internal McpProvider tools
    // surface as int__<name>.  Narrator + quant lab + portfolio reads
    // + paper trading reads.  Explicitly omit destructive surfaces
    // (place_order, etc.) since this agent is read-side only.
    QJsonArray allow;
    allow.append(QStringLiteral("int__narrate_*"));
    allow.append(QStringLiteral("int__propose_*"));
    allow.append(QStringLiteral("int__quant_*"));
    allow.append(QStringLiteral("int__run_quant_module"));
    allow.append(QStringLiteral("int__list_quant_modules"));
    allow.append(QStringLiteral("int__list_quant_module_commands"));
    allow.append(QStringLiteral("int__get_holdings"));
    allow.append(QStringLiteral("int__get_portfolios"));
    allow.append(QStringLiteral("int__get_paper_*"));
    allow.append(QStringLiteral("int__fd_*"));  // financial-datasets reads
    cfg["allow_tools"] = allow;

    return QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
}

static Result<void> apply_v026(QSqlDatabase& db) {
    QSqlQuery exists(db);
    if (!exists.exec(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='agent_configs'")) {
        return Result<void>::err(exists.lastError().text().toStdString());
    }
    if (!exists.next()) {
        LOG_WARN(kTag, "agent_configs table missing — skipping v026");
        return Result<void>::ok();
    }

    const QString config_json = build_config_json();

    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO agent_configs "
              "(id, name, description, config_json, category, is_default, is_active) "
              "VALUES (?, ?, ?, ?, ?, 0, 0)");
    q.bindValue(0, QString::fromUtf8(kAgentId));
    q.bindValue(1, QStringLiteral("Quant Critic"));
    q.bindValue(2, QStringLiteral(
                       "Reviews backtests, proposes parameter sweeps, "
                       "narrates live trades.  Read-side only — no "
                       "destructive tools allowed."));
    q.bindValue(3, config_json);
    q.bindValue(4, QStringLiteral("quant"));
    // is_default=0, is_active=0 — quant_critic doesn't auto-activate.
    // Users invoke it explicitly via slash commands or pick it as the
    // current agent in the workbench.  If another agent_configs row
    // already has is_active=1 from a previous run / migration, that
    // row's activeness is preserved (INSERT OR IGNORE doesn't touch
    // it; new row sits dormant alongside).

    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());

    if (q.numRowsAffected() > 0) {
        LOG_INFO(kTag, QString("Seeded agent_configs row '%1'").arg(kAgentId));
    }
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v026() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({26, "quant_critic_agent", apply_v026});
}

} // namespace fincept
