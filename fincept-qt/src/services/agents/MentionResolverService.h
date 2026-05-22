#pragma once
// MentionResolverService.h — chat-composer @-mention resolution.
//
// Recognises a small set of `@<name>` references in user input and
// resolves each one to an MCP resource read.  AiChatScreen calls
// `resolve()` before dispatching the user message; the returned
// context block is prepended (under a clearly-labelled header) so
// the agent can read the referenced state without spending a tool
// call.
//
// Pattern: `@<word>` where word matches `[A-Za-z][A-Za-z0-9_\-]*`.
//
// V1 mentions (resource-style — read via McpProvider::read_resource):
//
//   @portfolio  → finterm://portfolio/snapshot
//   @watchlist  → finterm://watchlist/all
//   @news       → finterm://news/digest
//   @thesis     → finterm://notes/active_thesis
//
// Ticker mentions (@AAPL, @MSFT) would call the `get_quote` tool;
// they need an async dispatch path so they're a follow-up.
//
// Pure resolver — no QObject, no signals.  AiChatScreen calls it
// inline on the UI thread; reads complete in microseconds since
// resources are local SQLite + in-process state.

#include <QHash>
#include <QString>
#include <QStringList>

namespace fincept::services {

struct ResolvedMentions {
    /// True if any @-mention was found AND resolved.
    bool any_resolved = false;
    /// The list of mention tokens we recognised (lowercase, no `@`).
    QStringList recognised;
    /// The list of @-mentions in the input we didn't have a resolver
    /// for (presented to the user as a soft hint, not an error).
    QStringList unrecognised;
    /// Markdown block to prepend to the user's message before LLM
    /// dispatch.  Empty when no mentions resolved.
    QString context_block;
};

class MentionResolverService {
  public:
    static MentionResolverService& instance();

    /// Walk the input for `@<word>` tokens, resolve each one we know,
    /// build the context block.  Idempotent + side-effect-free apart
    /// from the resource reads.
    ResolvedMentions resolve(const QString& input);

    /// Registry inspection — useful for the chat-composer's eventual
    /// autocomplete pop-up.
    QStringList known_mentions() const;

    MentionResolverService(const MentionResolverService&) = delete;
    MentionResolverService& operator=(const MentionResolverService&) = delete;

  private:
    MentionResolverService();
    /// mention (lowercase, no `@`) → MCP resource URI
    QHash<QString, QString> registry_;
};

} // namespace fincept::services
