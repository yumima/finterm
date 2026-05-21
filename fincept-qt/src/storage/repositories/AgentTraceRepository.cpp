#include "storage/repositories/AgentTraceRepository.h"

#include "core/logging/Logger.h"
#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace fincept {

namespace {
constexpr const char* TAG = "AgentTraceRepository";
constexpr int kMaxListLimit = 1000;

AgentTraceRow map_row(QSqlQuery& q) {
    AgentTraceRow r;
    r.id = q.value(0).toLongLong();
    r.request_id = q.value(1).toString();
    r.agent_id = q.value(2).toString();
    r.runtime = q.value(3).toString();
    r.source = q.value(4).toString();
    r.query = q.value(5).toString();
    r.config_json = q.value(6).toString();
    r.started_at = q.value(7).toString();
    r.finished_at = q.value(8).toString();
    r.status = q.value(9).toString();
    r.response = q.value(10).toString();
    r.error = q.value(11).toString();
    if (!q.value(12).isNull())
        r.latency_ms = q.value(12).toInt();
    if (!q.value(13).isNull())
        r.tokens_in = q.value(13).toInt();
    if (!q.value(14).isNull())
        r.tokens_out = q.value(14).toInt();
    if (!q.value(15).isNull())
        r.cost_usd = q.value(15).toDouble();
    return r;
}

constexpr const char* kCols =
    "id, request_id, agent_id, runtime, source, query, config_json, "
    "started_at, finished_at, status, response, error, "
    "latency_ms, tokens_in, tokens_out, cost_usd";

} // anonymous namespace

AgentTraceRepository& AgentTraceRepository::instance() {
    static AgentTraceRepository r;
    return r;
}

Result<void> AgentTraceRepository::create(const AgentTraceCreate& c) {
    if (c.request_id.isEmpty())
        return Result<void>::err("AgentTraceRepository::create: empty request_id");

    QSqlQuery q(fincept::Database::instance().raw_db());
    // INSERT OR IGNORE — a duplicate request_id is silently dropped.
    // Real dispatcher uses UUIDs so collisions can't happen; the
    // OR IGNORE is defensive against a buggy caller that retries on
    // dispatch failure with the same id.
    q.prepare("INSERT OR IGNORE INTO agent_traces "
              "(request_id, agent_id, runtime, source, query, config_json, status) "
              "VALUES (?, ?, ?, ?, ?, ?, 'in_progress')");
    q.bindValue(0, c.request_id);
    q.bindValue(1, c.agent_id);
    q.bindValue(2, c.runtime);
    q.bindValue(3, c.source);
    q.bindValue(4, c.query);
    q.bindValue(5, QString::fromUtf8(QJsonDocument(c.config).toJson(QJsonDocument::Compact)));
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

Result<void> AgentTraceRepository::finish(const AgentTraceFinish& f) {
    if (f.request_id.isEmpty())
        return Result<void>::err("AgentTraceRepository::finish: empty request_id");

    auto& db = fincept::Database::instance().raw_db();
    QSqlQuery q(db);
    q.prepare("UPDATE agent_traces SET "
              "  finished_at = datetime('now'), "
              "  status = ?, "
              "  response = COALESCE(NULLIF(?, ''), response), "
              "  error = COALESCE(NULLIF(?, ''), error), "
              "  latency_ms = COALESCE(?, latency_ms), "
              "  tokens_in = COALESCE(?, tokens_in), "
              "  tokens_out = COALESCE(?, tokens_out), "
              "  cost_usd = COALESCE(?, cost_usd) "
              "WHERE request_id = ?");
    q.bindValue(0, f.success ? QStringLiteral("success") : QStringLiteral("error"));
    q.bindValue(1, f.response);
    q.bindValue(2, f.error);
    q.bindValue(3, f.latency_ms ? QVariant(*f.latency_ms) : QVariant{QMetaType(QMetaType::Int)});
    q.bindValue(4, f.tokens_in ? QVariant(*f.tokens_in) : QVariant{QMetaType(QMetaType::Int)});
    q.bindValue(5, f.tokens_out ? QVariant(*f.tokens_out) : QVariant{QMetaType(QMetaType::Int)});
    q.bindValue(6, f.cost_usd ? QVariant(*f.cost_usd) : QVariant{QMetaType(QMetaType::Double)});
    q.bindValue(7, f.request_id);
    if (!q.exec())
        return Result<void>::err(q.lastError().text().toStdString());

    if (q.numRowsAffected() == 0) {
        // No matching trace row — either the dispatcher forgot to
        // call create(), or this finish() came from a different
        // pipeline.  Warn rather than silently dropping.
        LOG_WARN(TAG, QString("finish() found no row for request_id=%1").arg(f.request_id));
    }
    return Result<void>::ok();
}

Result<QVector<AgentTraceRow>> AgentTraceRepository::list_recent(int limit) {
    const int n = std::clamp(limit, 1, kMaxListLimit);
    QSqlQuery q(fincept::Database::instance().raw_db());
    q.prepare(QStringLiteral("SELECT %1 FROM agent_traces "
                             "ORDER BY started_at DESC LIMIT ?").arg(kCols));
    q.bindValue(0, n);
    if (!q.exec())
        return Result<QVector<AgentTraceRow>>::err(q.lastError().text().toStdString());
    QVector<AgentTraceRow> rows;
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<AgentTraceRow>>::ok(std::move(rows));
}

Result<std::optional<AgentTraceRow>> AgentTraceRepository::get(const QString& request_id) {
    QSqlQuery q(fincept::Database::instance().raw_db());
    q.prepare(QStringLiteral("SELECT %1 FROM agent_traces WHERE request_id = ?").arg(kCols));
    q.bindValue(0, request_id);
    if (!q.exec())
        return Result<std::optional<AgentTraceRow>>::err(q.lastError().text().toStdString());
    if (q.next())
        return Result<std::optional<AgentTraceRow>>::ok(map_row(q));
    return Result<std::optional<AgentTraceRow>>::ok(std::nullopt);
}

} // namespace fincept
