#include "storage/repositories/AgentFeedbackRepository.h"

#include "storage/sqlite/Database.h"

#include <QSqlQuery>

namespace fincept {

namespace {

constexpr int kMaxListLimit = 500;

AgentFeedbackRow map_row(QSqlQuery& q) {
    AgentFeedbackRow r;
    r.id = q.value(0).toLongLong();
    r.request_id = q.value(1).toString();
    r.verdict = q.value(2).toString();
    r.note = q.value(3).toString();
    r.created_at = q.value(4).toString();
    return r;
}

constexpr const char* kCols = "id, request_id, verdict, note, created_at";

} // anonymous namespace

AgentFeedbackRepository& AgentFeedbackRepository::instance() {
    static AgentFeedbackRepository r;
    return r;
}

Result<void> AgentFeedbackRepository::create(const QString& request_id,
                                              const QString& verdict,
                                              const QString& note) {
    if (request_id.isEmpty())
        return Result<void>::err("AgentFeedbackRepository::create: empty request_id");
    if (verdict.isEmpty())
        return Result<void>::err("AgentFeedbackRepository::create: empty verdict");
    auto r = Database::instance().execute(
        QStringLiteral("INSERT INTO agent_feedback (request_id, verdict, note) "
                       "VALUES (?, ?, ?)"),
        {request_id, verdict, note});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<QVector<AgentFeedbackRow>> AgentFeedbackRepository::list_recent(int limit) {
    const int n = std::clamp(limit, 1, kMaxListLimit);
    auto r = Database::instance().execute(
        QStringLiteral("SELECT %1 FROM agent_feedback "
                       "ORDER BY created_at DESC LIMIT ?").arg(kCols),
        {n});
    if (r.is_err())
        return Result<QVector<AgentFeedbackRow>>::err(r.error());
    QVector<AgentFeedbackRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<AgentFeedbackRow>>::ok(std::move(rows));
}

Result<QVector<AgentFeedbackRow>> AgentFeedbackRepository::list_by_request(const QString& request_id) {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT %1 FROM agent_feedback "
                       "WHERE request_id = ? ORDER BY created_at DESC").arg(kCols),
        {request_id});
    if (r.is_err())
        return Result<QVector<AgentFeedbackRow>>::err(r.error());
    QVector<AgentFeedbackRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<AgentFeedbackRow>>::ok(std::move(rows));
}

Result<int> AgentFeedbackRepository::count_by_verdict(const QString& verdict) {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT COUNT(*) FROM agent_feedback WHERE verdict = ?"),
        {verdict});
    if (r.is_err())
        return Result<int>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<int>::ok(q.value(0).toInt());
    return Result<int>::ok(0);
}

} // namespace fincept
