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
    QString supersedes_id;           ///< v039: explicit predecessor link (empty/NULL for standalone or legacy rows)
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

    /// v039: explicit predecessor link.  Sets supersedes_id on the
    /// given artefact row.  Used by the re-run dispatch path to
    /// link the freshly-emitted artefact back to the row the user
    /// asked to re-run, so the lineage view doesn't have to infer
    /// the chain from dispatch identity.
    Result<void> set_supersedes(const QString& artefact_id, const QString& predecessor_id);

    /// Find the most recently-created artefact for a given
    /// agent_traces.request_id.  Used by the dispatch-finish
    /// callback to wire supersedes_id when re-running.  Returns
    /// nullopt if no artefacts were emitted during that request.
    ///
    /// Caveat: if the agent emits MULTIPLE artefacts in one turn
    /// (current emit_artefact tool doesn't forbid this), only the
    /// most-recent one is linked back to the predecessor — the
    /// others end up unlinked.  Document the assumption rather than
    /// trying to map each one; per-artefact predecessor tracking
    /// would require the tool API to carry the link directly.
    Result<std::optional<ChatArtefactRow>> latest_for_request(const QString& request_id);

    Result<QVector<ChatArtefactRow>> list_recent(int limit = 50);
    Result<QVector<ChatArtefactRow>> list_by_request(const QString& request_id);
    Result<std::optional<ChatArtefactRow>> get(const QString& id);

    /// Returns the lineage chain for an artefact — every chat_artefact
    /// row sharing the same (source_agent_id, source_skill, source_args_json)
    /// triple, ordered by created_at DESC.  The first row is the most
    /// recent run; predecessors (typically marked `superseded`) follow.
    ///
    /// Identity is derived from the dispatch identity, not from an
    /// explicit predecessor column — `emit_artefact` doesn't carry a
    /// predecessor pointer, so chronological grouping by the
    /// re-runnable handle is the available signal.  Artefacts created
    /// without a source_skill (e.g. one-off MCP tool dumps) return
    /// just the artefact itself.
    Result<QVector<ChatArtefactRow>> list_lineage_for(const QString& artefact_id);

    ChatArtefactRepository(const ChatArtefactRepository&) = delete;
    ChatArtefactRepository& operator=(const ChatArtefactRepository&) = delete;

  private:
    ChatArtefactRepository() = default;
};

} // namespace fincept
