#pragma once
// AiRoles.h — the single source of truth for finterm's AI roles.
//
// A "role" is a place in the product that asks a model for something: the chat
// pane, the news brief, equity research, inline completion. Each is bound to a
// profile (provider + model + params) through LlmProfileRepository, using the
// role id as `context_type`. That column is free text, so adding a role here
// needs no migration.
//
// Why a registry rather than string literals at each call site: before this,
// callers hardcoded their model. NewsService pinned itself to the hearth role
// name "fast_chat" (kBriefModelRole), which is meaningless to any non-hearth
// provider — so the moment the active provider became Gemini, the news brief
// had no way to express "use something cheap and fast" and silently fell back
// to the chat model. A role the user can bind is the fix; a literal is not.
//
// Resolution for an unbound role falls through LlmProfileRepository's existing
// chain (entity → type default → global default → active provider), so every
// role works out of the box and binding one is an override, never a
// prerequisite.

#include <QString>
#include <QVector>

namespace fincept::ai_chat {

struct AiRole {
    QString id;     ///< context_type stored in llm_profile_assignments
    QString label;  ///< shown in Settings → AI Config → Roles
    QString hint;   ///< one line describing what this role is asked to do
};

/// Every bindable role, in the order they should appear in the UI.
/// Ordered by how often a user reasonably wants to change one, not
/// alphabetically — chat first, background jobs after.
inline const QVector<AiRole>& ai_roles() {
    static const QVector<AiRole> kRoles = {
        {QStringLiteral("ai_chat"), QStringLiteral("Chat"),
         QStringLiteral("The main AI chat pane — long conversations, tool use.")},
        {QStringLiteral("news"), QStringLiteral("News briefs"),
         QStringLiteral("TL;DR headline summaries. Short structured one-shots; "
                        "favour a fast model over a strong one.")},
        {QStringLiteral("equity_research"), QStringLiteral("Equity research"),
         QStringLiteral("Company analysis in the Equity Research screen. "
                        "Favours reasoning quality over latency.")},
        {QStringLiteral("portfolio"), QStringLiteral("Portfolio insights"),
         QStringLiteral("Commentary on holdings and performance.")},
        {QStringLiteral("knowledge"), QStringLiteral("AI tutor"),
         QStringLiteral("Explanations in the Knowledge screen.")},
        {QStringLiteral("ai_quant_lab"), QStringLiteral("Quant lab"),
         QStringLiteral("Factor models, backtests, attribution commentary.")},
        {QStringLiteral("completion"), QStringLiteral("Inline completion"),
         QStringLiteral("Fires as you type — latency matters more than depth.")},
        {QStringLiteral("agent_default"), QStringLiteral("Agents (default)"),
         QStringLiteral("Default for agents with no profile of their own.")},
        {QStringLiteral("team_default"), QStringLiteral("Teams (default)"),
         QStringLiteral("Default for teams with no profile of their own.")},
    };
    return kRoles;
}

/// Human label for a role id, or the id itself when it isn't a registered role
/// (agent/team rows carry per-entity context types that never appear above).
inline QString ai_role_label(const QString& id) {
    for (const auto& r : ai_roles())
        if (r.id == id)
            return r.label;
    return id;
}

} // namespace fincept::ai_chat
