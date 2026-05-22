#include "storage/repositories/ChatArtefactRepository.h"

#include "storage/sqlite/Database.h"

#include <QJsonDocument>
#include <QSqlQuery>
#include <QUuid>

namespace fincept {

namespace {

constexpr int kMaxListLimit = 500;

ChatArtefactRow map_row(QSqlQuery& q) {
    ChatArtefactRow r;
    r.id = q.value(0).toString();
    r.kind = q.value(1).toString();
    r.title = q.value(2).toString();
    r.payload_json = q.value(3).toString();
    r.source_request_id = q.value(4).toString();
    r.source_agent_id = q.value(5).toString();
    r.source_skill = q.value(6).toString();
    r.source_args_json = q.value(7).toString();
    r.status = q.value(8).toString();
    r.created_at = q.value(9).toString();
    r.updated_at = q.value(10).toString();
    return r;
}

constexpr const char* kCols =
    "id, kind, title, payload_json, "
    "source_request_id, source_agent_id, source_skill, source_args_json, "
    "status, created_at, updated_at";

QString serialise_json(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // anonymous namespace

ChatArtefactRepository& ChatArtefactRepository::instance() {
    static ChatArtefactRepository r;
    return r;
}

Result<QString> ChatArtefactRepository::create(const ChatArtefactCreate& c) {
    if (c.kind.isEmpty() || c.title.isEmpty())
        return Result<QString>::err(
            "ChatArtefactRepository::create: kind and title are required");

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString sql = QStringLiteral(
        "INSERT INTO chat_artefacts "
        "(id, kind, title, payload_json, source_request_id, source_agent_id, "
        " source_skill, source_args_json, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    auto r = Database::instance().execute(sql, {
        id, c.kind, c.title, serialise_json(c.payload),
        c.source_request_id, c.source_agent_id, c.source_skill,
        serialise_json(c.source_args),
        c.status.isEmpty() ? QStringLiteral("final") : c.status,
    });
    if (r.is_err())
        return Result<QString>::err(r.error());
    return Result<QString>::ok(id);
}

Result<void> ChatArtefactRepository::update_payload(const QString& id, const QJsonObject& payload) {
    if (id.isEmpty())
        return Result<void>::err("update_payload: empty id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE chat_artefacts "
                       "SET payload_json = ?, updated_at = datetime('now') "
                       "WHERE id = ?"),
        {serialise_json(payload), id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<void> ChatArtefactRepository::mark_superseded(const QString& id) {
    if (id.isEmpty())
        return Result<void>::err("mark_superseded: empty id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE chat_artefacts "
                       "SET status = 'superseded', updated_at = datetime('now') "
                       "WHERE id = ?"),
        {id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<QVector<ChatArtefactRow>> ChatArtefactRepository::list_recent(int limit) {
    const int n = std::clamp(limit, 1, kMaxListLimit);
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts "
        "ORDER BY created_at DESC LIMIT ?").arg(kCols);
    auto r = Database::instance().execute(sql, {n});
    if (r.is_err())
        return Result<QVector<ChatArtefactRow>>::err(r.error());
    QVector<ChatArtefactRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<ChatArtefactRow>>::ok(std::move(rows));
}

Result<QVector<ChatArtefactRow>> ChatArtefactRepository::list_by_request(const QString& request_id) {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts WHERE source_request_id = ? "
        "ORDER BY created_at DESC").arg(kCols);
    auto r = Database::instance().execute(sql, {request_id});
    if (r.is_err())
        return Result<QVector<ChatArtefactRow>>::err(r.error());
    QVector<ChatArtefactRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    return Result<QVector<ChatArtefactRow>>::ok(std::move(rows));
}

Result<std::optional<ChatArtefactRow>> ChatArtefactRepository::get(const QString& id) {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM chat_artefacts WHERE id = ?").arg(kCols);
    auto r = Database::instance().execute(sql, {id});
    if (r.is_err())
        return Result<std::optional<ChatArtefactRow>>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<std::optional<ChatArtefactRow>>::ok(map_row(q));
    return Result<std::optional<ChatArtefactRow>>::ok(std::nullopt);
}

} // namespace fincept
