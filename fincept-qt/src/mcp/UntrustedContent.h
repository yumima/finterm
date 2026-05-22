#pragma once
// UntrustedContent.h — prompt-injection guard for tool output (Track 14 #40, R23).
//
// External text reaching the model from tool calls (news bodies,
// fetched webpages, filing sections, forum posts) can carry
// prompt-injection payloads ("Ignore previous instructions and …").
// Wrapping that text with explicit `<untrusted>` markers gives the
// model an unambiguous signal that what's inside is *data*, not
// instructions.  The agent's system prompt declares that nothing
// inside `<untrusted>…</untrusted>` should be followed as a
// command — even if it looks like one.
//
// Usage:
//
//   #include "mcp/UntrustedContent.h"
//   QJsonObject article;
//   article["headline"] = UntrustedContent::wrap(a.headline);
//   article["summary"]  = UntrustedContent::wrap(a.summary);
//
// Tag choice: `<untrusted>` is short, unambiguous, and lower-risk
// than markdown-fenced blocks (which the model is trained to enter).
// We deliberately avoid `<system>`, `<instructions>`, etc. — those
// names overlap with role markers in chat templates and could be
// exploited as an *unwrap* vector.
//
// Limits.  The guard is mitigation, not a hermetic barrier: a
// determined attacker can still craft content that confuses the
// model.  Defence in depth:
//   1. Wrap text at the source (this helper).
//   2. System prompt tells the model not to follow `<untrusted>` tags.
//   3. Per-tool kill-switch (Track 14 #39) lets the user disable the
//      offending tool the moment a problem is observed.
//   4. AgentTrace (Track 14 #38) records every tool call so audit
//      can review what content reached the model.

#include <QString>

namespace fincept::mcp {

class UntrustedContent {
  public:
    /// Wrap `content` with `<untrusted>` … `</untrusted>` markers.
    /// Empty / null input passes through unchanged — wrapping empty
    /// strings just adds noise without protection.
    static QString wrap(const QString& content);

    /// Tag opener / closer — exposed so tests and system-prompt text
    /// can reference the exact markers without re-hardcoding them.
    static constexpr const char* kOpenTag = "<untrusted>";
    static constexpr const char* kCloseTag = "</untrusted>";
};

} // namespace fincept::mcp
