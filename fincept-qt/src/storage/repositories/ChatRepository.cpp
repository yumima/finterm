#include "storage/repositories/ChatRepository.h"

#include <QUuid>

namespace fincept {

ChatRepository& ChatRepository::instance() {
    static ChatRepository s;
    return s;
}

ChatSession ChatRepository::map_session(QSqlQuery& q) {
    return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
            q.value(3).toString(), q.value(4).toInt(),    q.value(5).toString(),
            q.value(6).toString(), q.value(7).toString(), q.value(8).toString()};
}

ChatMessage ChatRepository::map_message(QSqlQuery& q) {
    return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
            q.value(4).toString(), q.value(5).toString(), q.value(6).toInt(),    q.value(7).toString()};
}

Result<ChatSession> ChatRepository::create_session(const QString& title, const QString& provider,
                                                   const QString& model, const QString& persona_id,
                                                   const QString& context_tag) {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto r = exec_write("INSERT INTO chat_sessions (id, title, provider, model, persona_id, context_tag) "
                        "VALUES (?, ?, ?, ?, ?, ?)",
                        {id, title, provider, model, persona_id, context_tag});
    if (r.is_err())
        return Result<ChatSession>::err(r.error());
    return get_session(id);
}

static const char* kSessionCols =
    "id, title, provider, model, message_count, created_at, updated_at, persona_id, context_tag";

Result<ChatSession> ChatRepository::get_session(const QString& id) {
    return query_one(QStringLiteral("SELECT %1 FROM chat_sessions WHERE id = ?").arg(kSessionCols), {id},
                     map_session);
}

Result<std::optional<ChatSession>> ChatRepository::latest_session_for_context(const QString& context_tag) {
    auto r = query_list(QStringLiteral("SELECT %1 FROM chat_sessions WHERE context_tag = ? "
                                       "ORDER BY updated_at DESC LIMIT 1")
                            .arg(kSessionCols),
                        {context_tag}, map_session);
    if (r.is_err())
        return Result<std::optional<ChatSession>>::err(r.error());
    if (r.value().isEmpty())
        return Result<std::optional<ChatSession>>::ok(std::nullopt);
    return Result<std::optional<ChatSession>>::ok(r.value().first());
}

Result<QVector<ChatSession>> ChatRepository::list_sessions() {
    return query_list(QStringLiteral("SELECT %1 FROM chat_sessions ORDER BY updated_at DESC").arg(kSessionCols), {},
                      map_session);
}

Result<void> ChatRepository::update_session_title(const QString& id, const QString& title) {
    return exec_write("UPDATE chat_sessions SET title = ?, updated_at = datetime('now') WHERE id = ?", {title, id});
}

Result<void> ChatRepository::update_session_persona(const QString& id, const QString& persona_id) {
    return exec_write("UPDATE chat_sessions SET persona_id = ?, updated_at = datetime('now') WHERE id = ?",
                      {persona_id, id});
}

Result<void> ChatRepository::delete_session(const QString& id) {
    return exec_write("DELETE FROM chat_sessions WHERE id = ?", {id});
}

Result<void> ChatRepository::add_message(const QString& session_id, const QString& role, const QString& content,
                                         const QString& provider, const QString& model, int tokens) {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto r = exec_write("INSERT INTO chat_messages (id, session_id, role, content, provider, model, tokens_used) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?)",
                        {id, session_id, role, content, provider, model, tokens});
    if (r.is_err())
        return r;

    // Update message count
    return exec_write("UPDATE chat_sessions SET message_count = message_count + 1, "
                      "updated_at = datetime('now') WHERE id = ?",
                      {session_id});
}

Result<QVector<ChatMessage>> ChatRepository::get_messages(const QString& session_id) {
    auto r = db().execute("SELECT id, session_id, role, content, provider, model, tokens_used, timestamp "
                          "FROM chat_messages WHERE session_id = ? ORDER BY timestamp ASC",
                          {session_id});
    if (r.is_err())
        return Result<QVector<ChatMessage>>::err(r.error());
    QVector<ChatMessage> result;
    auto& q = r.value();
    while (q.next())
        result.append(map_message(q));
    return Result<QVector<ChatMessage>>::ok(std::move(result));
}

Result<void> ChatRepository::delete_messages(const QString& session_id) {
    return exec_write("DELETE FROM chat_messages WHERE session_id = ?", {session_id});
}

} // namespace fincept
