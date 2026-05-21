#include "storage/repositories/AgentTraceRepository.h"

#include "core/logging/Logger.h"
#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QMetaType>
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

// Wrap an optional<T> into a QVariant that SQL sees as NULL when
// the optional is empty.  We must use the typed-null QVariant form
// because bare `QVariant()` is QVariant::Invalid, which Qt's SQL
// driver may treat as 0 rather than NULL for integer columns.
template <typename T>
QVariant null_or(const std::optional<T>& v, QMetaType::Type type) {
    return v ? QVariant(*v) : QVariant(QMetaType(type));
}

} // anonymous namespace

AgentTraceRepository& AgentTraceRepository::instance() {
    static AgentTraceRepository r;
    return r;
}

Result<void> AgentTraceRepository::create(const AgentTraceCreate& c) {
    if (c.request_id.isEmpty())
        return Result<void>::err("AgentTraceRepository::create: empty request_id");

    // INSERT OR IGNORE — a duplicate request_id is silently dropped.
    // Real dispatcher uses UUIDs so collisions can't happen; the
    // OR IGNORE is defensive against a buggy caller that retries on
    // dispatch failure with the same id.
    const QString sql = QStringLiteral(
        "INSERT OR IGNORE INTO agent_traces "
        "(request_id, agent_id, runtime, source, query, config_json, status) "
        "VALUES (?, ?, ?, ?, ?, ?, 'in_progress')");
    const QVariantList params = {
        c.request_id,
        c.agent_id,
        c.runtime,
        c.source,
        c.query,
        QString::fromUtf8(QJsonDocument(c.config).toJson(QJsonDocument::Compact)),
    };
    auto r = Database::instance().execute(sql, params);
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<void> AgentTraceRepository::finish(const AgentTraceFinish& f) {
    if (f.request_id.isEmpty())
        return Result<void>::err("AgentTraceRepository::finish: empty request_id");

    const QString sql = QStringLiteral(
        "UPDATE agent_traces SET "
        "  finished_at = datetime('now'), "
        "  status = ?, "
        "  response = COALESCE(NULLIF(?, ''), response), "
        "  error = COALESCE(NULLIF(?, ''), error), "
        "  latency_ms = COALESCE(?, latency_ms), "
        "  tokens_in = COALESCE(?, tokens_in), "
        "  tokens_out = COALESCE(?, tokens_out), "
        "  cost_usd = COALESCE(?, cost_usd) "
        "WHERE request_id = ?");
    const QVariantList params = {
        f.success ? QStringLiteral("success") : QStringLiteral("error"),
        f.response,
        f.error,
        null_or(f.latency_ms, QMetaType::Int),
        null_or(f.tokens_in, QMetaType::Int),
        null_or(f.tokens_out, QMetaType::Int),
        null_or(f.cost_usd, QMetaType::Double),
        f.request_id,
    };
    auto r = Database::instance().execute(sql, params);
    if (r.is_err())
        return Result<void>::err(r.error());

    if (r.value().numRowsAffected() == 0) {
        // No matching trace row — either the dispatcher forgot to
        // call create(), or this finish() came from a different
        // pipeline.  Warn rather than silently dropping.
        LOG_WARN(TAG, QString("finish() found no row for request_id=%1").arg(f.request_id));
    }
    return Result<void>::ok();
}

Result<QVector<AgentTraceRow>> AgentTraceRepository::list_recent(int limit) {
    const int n = std::clamp(limit, 1, kMaxListLimit);
    const QString sql = QStringLiteral(
        "SELECT %1 FROM agent_traces ORDER BY started_at DESC LIMIT ?").arg(kCols);
    auto r = Database::instance().execute(sql, {n});
    if (r.is_err())
        return Result<QVector<AgentTraceRow>>::err(r.error());
    QVector<AgentTraceRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<AgentTraceRow>>::ok(std::move(rows));
}

Result<std::optional<AgentTraceRow>> AgentTraceRepository::get(const QString& request_id) {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM agent_traces WHERE request_id = ?").arg(kCols);
    auto r = Database::instance().execute(sql, {request_id});
    if (r.is_err())
        return Result<std::optional<AgentTraceRow>>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<std::optional<AgentTraceRow>>::ok(map_row(q));
    return Result<std::optional<AgentTraceRow>>::ok(std::nullopt);
}

} // namespace fincept
