#pragma once
#include "storage/repositories/BaseRepository.h"

#include <optional>

namespace fincept {

struct ChatSession {
    QString id;
    QString title;
    QString provider;
    QString model;
    int message_count = 0;
    QString created_at;
    QString updated_at;
    QString persona_id;  // chat persona bound to this conversation (ChatPersonas.h); default 'general'
    QString context_tag; // which embedded pane owns this thread ("symbol:NVDA",
                         // "portfolio:<id>"); '' = global / untagged
};

struct ChatMessage {
    QString id;
    QString session_id;
    QString role; // "user", "assistant", "system"
    QString content;
    QString provider;
    QString model;
    int tokens_used = 0;
    QString timestamp;
};

class ChatRepository : public BaseRepository<ChatSession> {
  public:
    static ChatRepository& instance();

    // Sessions
    Result<ChatSession> create_session(const QString& title, const QString& provider = {}, const QString& model = {},
                                       const QString& persona_id = QStringLiteral("general"),
                                       const QString& context_tag = {});
    Result<ChatSession> get_session(const QString& id);
    /// The most recently updated session carrying `context_tag`, if any —
    /// how an embedded pane (per-stock, per-portfolio) finds its own thread.
    Result<std::optional<ChatSession>> latest_session_for_context(const QString& context_tag);
    Result<QVector<ChatSession>> list_sessions();
    Result<void> update_session_title(const QString& id, const QString& title);
    Result<void> update_session_persona(const QString& id, const QString& persona_id);
    Result<void> delete_session(const QString& id);

    // Messages
    Result<void> add_message(const QString& session_id, const QString& role, const QString& content,
                             const QString& provider = {}, const QString& model = {}, int tokens = 0);
    Result<QVector<ChatMessage>> get_messages(const QString& session_id);
    Result<void> delete_messages(const QString& session_id);

  private:
    ChatRepository() = default;
    static ChatSession map_session(QSqlQuery& q);
    static ChatMessage map_message(QSqlQuery& q);
};

} // namespace fincept
