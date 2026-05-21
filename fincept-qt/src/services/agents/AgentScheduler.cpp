#include "services/agents/AgentScheduler.h"

#include "core/logging/Logger.h"
#include "services/agents/AgentService.h"
#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace fincept::services {

namespace {
constexpr const char* TAG = "AgentScheduler";
constexpr int kTickIntervalMs = 60 * 1000;
} // namespace

// ─────────────────────────────────────────────────────────────────────
// Cron parser — pure, testable
// ─────────────────────────────────────────────────────────────────────

std::optional<ScheduleSpec> parse_cron(const QString& cron) {
    const QString s = cron.trimmed();

    if (s.startsWith(QStringLiteral("@daily "))) {
        const QString rest = s.mid(7).trimmed();
        const QStringList parts = rest.split(':');
        if (parts.size() != 2)
            return std::nullopt;
        bool h_ok = false, m_ok = false;
        const int hour = parts[0].toInt(&h_ok);
        const int min = parts[1].toInt(&m_ok);
        if (!h_ok || !m_ok || hour < 0 || hour > 23 || min < 0 || min > 59)
            return std::nullopt;
        ScheduleSpec spec;
        spec.kind = ScheduleSpec::Daily;
        spec.hour = hour;
        spec.minute = min;
        return spec;
    }

    if (s.startsWith(QStringLiteral("@every "))) {
        const QString rest = s.mid(7).trimmed();
        if (rest.length() < 2)
            return std::nullopt;
        const QChar unit = rest.back();
        bool ok = false;
        const int n = rest.left(rest.length() - 1).toInt(&ok);
        if (!ok)
            return std::nullopt;
        ScheduleSpec spec;
        if (unit == QLatin1Char('m')) {
            if (n < 1 || n > 1440)
                return std::nullopt;
            spec.kind = ScheduleSpec::EveryMinutes;
            spec.interval = n;
            return spec;
        }
        if (unit == QLatin1Char('h')) {
            if (n < 1 || n > 168)
                return std::nullopt;
            spec.kind = ScheduleSpec::EveryHours;
            spec.interval = n;
            return spec;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

bool is_due(const ScheduleSpec& spec, const QDateTime& last_run, const QDateTime& now) {
    switch (spec.kind) {
        case ScheduleSpec::Daily: {
            // Anacron-style: fire when local time is at or past HH:MM and
            // we haven't already fired today.  This catches the "morning
            // brief" use-case when the user opens the app after the
            // scheduled minute had already passed — strict HH:MM matching
            // would silently skip the day.
            const QTime now_t = now.time();
            const QTime spec_t(spec.hour, spec.minute);
            if (now_t < spec_t)
                return false;
            if (!last_run.isValid())
                return true;
            return last_run.date() < now.date();
        }
        case ScheduleSpec::EveryMinutes: {
            if (!last_run.isValid())
                return true;
            return last_run.secsTo(now) >= spec.interval * 60;
        }
        case ScheduleSpec::EveryHours: {
            if (!last_run.isValid())
                return true;
            return last_run.secsTo(now) >= spec.interval * 3600;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────
// AgentScheduler
// ─────────────────────────────────────────────────────────────────────

AgentScheduler& AgentScheduler::instance() {
    static AgentScheduler s;
    return s;
}

AgentScheduler::AgentScheduler() {
    tick_.setInterval(kTickIntervalMs);
    QObject::connect(&tick_, &QTimer::timeout, this, &AgentScheduler::on_tick);
}

void AgentScheduler::start() {
    if (tick_.isActive())
        return;
    tick_.start();
    LOG_INFO(TAG, QString("AgentScheduler started (tick %1 ms)").arg(kTickIntervalMs));
    // Fire one tick at startup so a freshly-due daily catches the boot
    // window (otherwise the user might miss it on a restart at HH:MM).
    on_tick();
}

void AgentScheduler::stop() {
    tick_.stop();
    LOG_INFO(TAG, "AgentScheduler stopped");
}

void AgentScheduler::on_tick() {
    const int n = tick_now();
    if (n > 0)
        LOG_INFO(TAG, QString("tick: fired %1 entry(ies)").arg(n));
}

int AgentScheduler::tick_now() {
    auto entries = list_entries();
    if (entries.is_err()) {
        LOG_WARN(TAG, QString("tick: failed to load schedule: %1")
                          .arg(QString::fromStdString(entries.error())));
        return 0;
    }
    const QDateTime now = QDateTime::currentDateTime();
    int fired = 0;
    for (auto& e : entries.value()) {
        if (!e.enabled)
            continue;
        const auto spec = parse_cron(e.cron);
        if (!spec) {
            LOG_WARN(TAG, QString("entry %1: invalid cron '%2'").arg(e.id, e.cron));
            continue;
        }
        const QDateTime last = e.last_run_at.isEmpty()
                                   ? QDateTime{}
                                   : QDateTime::fromString(e.last_run_at, Qt::ISODate);
        if (!is_due(*spec, last, now))
            continue;
        run_entry(e);
        ++fired;
    }
    return fired;
}

void AgentScheduler::run_entry(ScheduleEntry& e) {
    LOG_INFO(TAG, QString("firing entry %1 (agent=%2, skill=%3)")
                      .arg(e.id, e.agent_id, e.skill));

    QJsonObject config;
    if (!e.agent_id.isEmpty())
        config["agent_id"] = e.agent_id;
    if (!e.args_json.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(e.args_json.toUtf8());
        if (doc.isObject())
            config["args"] = doc.object();
    }
    // Query string is the skill name today.  Track 7's slash dispatch
    // will replace this with proper (agent, skill, args) resolution.
    const QString query = e.skill;

    QString status;
    try {
        auto& svc = AgentService::instance();
        const QString request_id = svc.run_agent(query, config);
        status = QStringLiteral("ok:") + request_id;
        emit entry_fired(e.id, e.agent_id, e.skill);
    } catch (const std::exception& ex) {
        status = QStringLiteral("error: ") + QString::fromUtf8(ex.what());
        emit entry_failed(e.id, status);
        LOG_WARN(TAG, QString("entry %1 dispatch failed: %2").arg(e.id, status));
    }

    // Persist the new last_run / last_status.  Failures here are
    // logged but not fatal — the next tick will see the same entry
    // due again and retry.
    QSqlQuery q(fincept::Database::instance().raw_db());
    q.prepare("UPDATE agent_schedule "
              "SET last_run_at = datetime('now'), last_status = ?, "
              "    updated_at = datetime('now') "
              "WHERE id = ?");
    q.bindValue(0, status);
    q.bindValue(1, e.id);
    if (!q.exec()) {
        LOG_WARN(TAG, QString("failed to write last_run for entry %1: %2")
                          .arg(e.id, q.lastError().text()));
    }
}

// ─────────────────────────────────────────────────────────────────────
// CRUD
// ─────────────────────────────────────────────────────────────────────

namespace {

ScheduleEntry map_row(QSqlQuery& q) {
    ScheduleEntry e;
    e.id = q.value(0).toString();
    e.agent_id = q.value(1).toString();
    e.skill = q.value(2).toString();
    e.args_json = q.value(3).toString();
    e.cron = q.value(4).toString();
    e.enabled = q.value(5).toBool();
    e.last_run_at = q.value(6).toString();
    e.last_status = q.value(7).toString();
    e.created_at = q.value(8).toString();
    e.updated_at = q.value(9).toString();
    return e;
}

constexpr const char* kCols =
    "id, agent_id, skill, args_json, cron, enabled, "
    "last_run_at, last_status, created_at, updated_at";

} // anonymous namespace

Result<QVector<ScheduleEntry>> AgentScheduler::list_entries() {
    QSqlQuery q(fincept::Database::instance().raw_db());
    if (!q.exec(QStringLiteral("SELECT %1 FROM agent_schedule ORDER BY id").arg(kCols))) {
        return Result<QVector<ScheduleEntry>>::err(q.lastError().text().toStdString());
    }
    QVector<ScheduleEntry> rows;
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<ScheduleEntry>>::ok(std::move(rows));
}

Result<void> AgentScheduler::save_entry(const ScheduleEntry& e) {
    if (!parse_cron(e.cron)) {
        return Result<void>::err(
            "invalid cron expression: '" + e.cron.toStdString() +
            "' (expected '@daily HH:MM' or '@every Nm' / '@every Nh')");
    }
    QSqlQuery q(fincept::Database::instance().raw_db());
    // last_run_at / last_status are preserved across edits — callers
    // pass empty strings unless they explicitly want to overwrite (e.g.
    // an admin "reset history" action).  created_at is preserved by the
    // same COALESCE pattern.
    q.prepare("INSERT OR REPLACE INTO agent_schedule "
              "(id, agent_id, skill, args_json, cron, enabled, "
              " last_run_at, last_status, created_at, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?, "
              "  COALESCE(NULLIF(?, ''), (SELECT last_run_at FROM agent_schedule WHERE id = ?)), "
              "  COALESCE(NULLIF(?, ''), (SELECT last_status FROM agent_schedule WHERE id = ?)), "
              "  COALESCE((SELECT created_at FROM agent_schedule WHERE id = ?), datetime('now')), "
              "  datetime('now'))");
    const QString id = e.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : e.id;
    q.bindValue(0, id);
    q.bindValue(1, e.agent_id);
    q.bindValue(2, e.skill);
    q.bindValue(3, e.args_json);
    q.bindValue(4, e.cron);
    q.bindValue(5, e.enabled ? 1 : 0);
    q.bindValue(6, e.last_run_at);
    q.bindValue(7, id);
    q.bindValue(8, e.last_status);
    q.bindValue(9, id);
    q.bindValue(10, id);
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

Result<void> AgentScheduler::remove_entry(const QString& id) {
    QSqlQuery q(fincept::Database::instance().raw_db());
    q.prepare("DELETE FROM agent_schedule WHERE id = ?");
    q.bindValue(0, id);
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

Result<void> AgentScheduler::set_enabled(const QString& id, bool enabled) {
    QSqlQuery q(fincept::Database::instance().raw_db());
    q.prepare("UPDATE agent_schedule SET enabled = ?, updated_at = datetime('now') WHERE id = ?");
    q.bindValue(0, enabled ? 1 : 0);
    q.bindValue(1, id);
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

} // namespace fincept::services
