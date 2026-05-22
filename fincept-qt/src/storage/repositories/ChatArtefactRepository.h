#pragma once
// ChatArtefactRepository.h — CRUD over the chat_artefacts table (v034).
//
// Used by:
//   - the `emit_artefact` internal MCP tool (agents create artefacts
//     via tool call instead of dumping into chat bubbles)
//   - the Workbench Artefacts panel (list / view / re-run / export)
//   - the trace drill-down dialog (artefacts created by a turn)

#include "core/result/Result.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <optional>

namespace fincept {

struct ChatArtefactRow {
    QString id;
    QString kind;
    QString title;
    QString payload_json;            ///< Structured content; schema depends on kind.
    QString source_request_id;       ///< AgentTraceRow.request_id when produced via agent dispatch.
    QString source_agent_id;
    QString source_skill;
    QString source_args_json;
    QString status;                  ///< "final" | "draft" | "superseded"
    QString created_at;
    QString updated_at;
};

struct ChatArtefactCreate {
    QString kind;
    QString title;
    QJsonObject payload;             ///< Serialised compact-JSON into payload_json.
    QString source_request_id;
    QString source_agent_id;
    QString source_skill;
    QJsonObject source_args;
    QString status = "final";
};

class ChatArtefactRepository {
  public:
    static ChatArtefactRepository& instance();

    /// Returns the generated UUID on success.  Caller doesn't supply
    /// an id; we own that.
    Result<QString> create(const ChatArtefactCreate& c);

    /// Replace payload + bump updated_at.  Used by re-run.
    Result<void> update_payload(const QString& id, const QJsonObject& payload);

    /// Soft-delete: mark status='superseded'.  Re-run records create
    /// a new artefact + supersede the predecessor so history stays
    /// auditable.
    Result<void> mark_superseded(const QString& id);

    Result<QVector<ChatArtefactRow>> list_recent(int limit = 50);
    Result<QVector<ChatArtefactRow>> list_by_request(const QString& request_id);
    Result<std::optional<ChatArtefactRow>> get(const QString& id);

    ChatArtefactRepository(const ChatArtefactRepository&) = delete;
    ChatArtefactRepository& operator=(const ChatArtefactRepository&) = delete;

  private:
    ChatArtefactRepository() = default;
};

} // namespace fincept
