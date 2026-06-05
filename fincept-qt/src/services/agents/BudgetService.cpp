#include "services/agents/BudgetService.h"

#include "core/logging/Logger.h"
#include "storage/repositories/AgentConfigRepository.h"
#include "storage/sqlite/Database.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace fincept::services {

namespace {
constexpr const char* kBudgetTag = "BudgetService";

/// Read `agent_configs.config_json.budget.max_usd_per_day` for an
/// agent.  Returns 0.0 (no cap) on parse error / missing field —
/// fail-open: a malformed config shouldn't refuse every dispatch.
double max_usd_per_day_for(const QString& agent_id) {
    if (agent_id.isEmpty())
        return 0.0;
    auto r = AgentConfigRepository::instance().get(agent_id);
    if (r.is_err())
        return 0.0;
    QJsonParseError perr;
    const QJsonDocument doc =
        QJsonDocument::fromJson(r.value().config_json.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return 0.0;
    const QJsonObject budget = doc.object().value(QStringLiteral("budget")).toObject();
    const QJsonValue cap = budget.value(QStringLiteral("max_usd_per_day"));
    if (!cap.isDouble())  // also rejects null / undefined / non-numeric
        return 0.0;
    const double v = cap.toDouble();
    return v > 0.0 ? v : 0.0;
}

QString start_of_local_day_utc() {
    // The user thinks in local days ("midnight when I went to bed"),
    // but agent_traces.started_at is SQLite's datetime('now') — UTC,
    // formatted "YYYY-MM-DD HH:MM:SS".  Build the comparand to match
    // that format exactly:
    //   1. Local midnight as QDateTime.
    //   2. Converted to UTC.
    //   3. Serialised with a literal space separator (NOT 'T'); the
    //      Qt::ISODate format uses 'T' and would compare as STRICTLY
    //      GREATER than every space-separated string from datetime()
    //      ('T' = 0x54 > ' ' = 0x20), making the WHERE clause exclude
    //      every same-day trace and report $0 spent.
    const QDate today = QDate::currentDate();
    const QDateTime local_midnight(today, QTime(0, 0), Qt::LocalTime);
    return local_midnight.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // anonymous namespace

BudgetService& BudgetService::instance() {
    static BudgetService s;
    return s;
}

Result<double> BudgetService::spend_today(const QString& agent_id) {
    if (agent_id.isEmpty())
        return Result<double>::ok(0.0);
    auto r = Database::instance().execute(
        QStringLiteral("SELECT COALESCE(SUM(cost_usd), 0.0) FROM agent_traces "
                       "WHERE agent_id = ? AND started_at >= ?"),
        {agent_id, start_of_local_day_utc()});
    if (r.is_err())
        return Result<double>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<double>::ok(q.value(0).toDouble());
    return Result<double>::ok(0.0);
}

Result<double> BudgetService::spend_today_total() {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT COALESCE(SUM(cost_usd), 0.0) FROM agent_traces "
                       "WHERE started_at >= ?"),
        {start_of_local_day_utc()});
    if (r.is_err())
        return Result<double>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<double>::ok(q.value(0).toDouble());
    return Result<double>::ok(0.0);
}

Result<void> BudgetService::check_dispatch(const QString& agent_id) {
    const double cap = max_usd_per_day_for(agent_id);
    if (cap <= 0.0)
        return Result<void>::ok();  // no cap configured

    auto spent_r = spend_today(agent_id);
    if (spent_r.is_err()) {
        // Fail-open on a SQL read error — same posture as the
        // kill-switch.  A transient DB hiccup shouldn't block dispatch.
        LOG_WARN(kBudgetTag, QString("budget check for %1 failed to read spend: %2 (allowing dispatch)")
                          .arg(agent_id, QString::fromStdString(spent_r.error())));
        return Result<void>::ok();
    }
    const double spent = spent_r.value();
    if (spent >= cap) {
        return Result<void>::err(
            QString("daily budget exceeded for agent '%1': $%2 spent today vs cap $%3")
                .arg(agent_id, QString::number(spent, 'f', 2), QString::number(cap, 'f', 2))
                .toStdString());
    }
    return Result<void>::ok();
}

} // namespace fincept::services
