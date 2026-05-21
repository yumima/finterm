#pragma once
// McpProvider.h — Internal tool registry and executor (Qt port)
// Singleton managing all built-in terminal tools.
// Tools register handlers invoked by AI chat, agents, or node editor.

#include "mcp/McpTypes.h"

#include <QFuture>
#include <QHash>
#include <QMutex>
#include <QSet>

#include <functional>
#include <vector>

namespace fincept::mcp {

class McpProvider {
  public:
    static McpProvider& instance();

    // ── Tool Registration ──────────────────────────────────────────────────

    void register_tool(ToolDef tool);
    void register_tools(std::vector<ToolDef> tools);
    void unregister_tool(const QString& name);

    // ── Tool Enable/Disable ────────────────────────────────────────────────

    void set_tool_enabled(const QString& name, bool enabled);
    bool is_tool_enabled(const QString& name) const;

    // ── Tool Discovery ─────────────────────────────────────────────────────

    /// List all enabled tools as UnifiedTool (for LLM consumption)
    std::vector<UnifiedTool> list_tools() const;

    /// List ALL tools including disabled (for management UI)
    std::vector<UnifiedTool> list_all_tools() const;

    std::size_t tool_count() const;
    bool has_tool(const QString& name) const;

    // ── Resource Registration (MCP spec resources) ─────────────────────────

    /// Register a typed read-only resource.  Resources let the agent read
    /// state by uri (e.g. "finterm://portfolio/snapshot") without spending
    /// a tool-call turn on every fetch.  See plans/ai-stack-free-local.md
    /// R8 / Track 5.
    void register_resource(Resource resource);
    void unregister_resource(const QString& uri);

    /// All registered resources, ordered by uri.
    std::vector<Resource> list_resources() const;

    /// Synchronously invoke the resource handler.  Returns a
    /// ResourceContent whose `error` is set when the uri is unknown or the
    /// handler throws.  Safe to call from any thread provided the handler
    /// is itself thread-safe (handlers are tool-family-supplied closures
    /// that typically read repository singletons — same constraints as
    /// tool handlers).
    ResourceContent read_resource(const QString& uri);

    bool has_resource(const QString& uri) const;

    // ── Tool Execution ─────────────────────────────────────────────────────

    /// Synchronous entry. Dispatches to whichever shape the ToolDef has set
    /// (async_handler wins if both are present). For async tools, internally
    /// dispatches via call_tool_async and waits — the caller is expected to
    /// already be on a background thread (every existing call site is) so
    /// blocking here is safe.
    ToolResult call_tool(const QString& name, const QJsonObject& args);

    /// Asynchronous entry. Returns a QFuture that resolves with the
    /// ToolResult. Sync handlers are wrapped in an immediately-resolved
    /// future. Async handlers run with a watchdog timer (per-tool
    /// default_timeout_ms unless overridden via the ToolContext).
    QFuture<ToolResult> call_tool_async(const QString& name, const QJsonObject& args, ToolContext ctx = {});

    // ── Authorization hook ─────────────────────────────────────────────────

    /// Caller-supplied predicate that returns true iff the call should
    /// proceed. The hook receives the tool's required AuthLevel and
    /// is_destructive flag; it is responsible for checking the active
    /// session and prompting the user. Hook lives in the app layer to
    /// avoid pulling auth/UI headers into McpTypes.h.
    ///
    /// When unset, tools with auth_required >= Verified fail closed.
    /// Tools tagged AuthLevel::None pass through; Authenticated + is_destructive
    /// pass through with a log line until the modal lands.
    using AuthChecker = std::function<bool(AuthLevel required, bool is_destructive)>;
    void set_auth_checker(AuthChecker checker);

    // ── LLM Integration ────────────────────────────────────────────────────

    /// Format all enabled tools for OpenAI function calling
    QJsonArray format_tools_for_openai() const;

    /// Parse "serverId__toolName" → { server_id, tool_name }
    static QPair<QString, QString> parse_openai_function_name(const QString& fn_name);

    // ── Lifecycle ──────────────────────────────────────────────────────────

    void clear();

    // ── Generation Counter ─────────────────────────────────────────────────

    /// Monotonically increasing — incremented on any mutation.
    /// McpService uses this to detect stale cache.
    quint64 generation() const;

    McpProvider(const McpProvider&) = delete;
    McpProvider& operator=(const McpProvider&) = delete;

  private:
    McpProvider() = default;

    mutable QMutex mutex_;
    QHash<QString, ToolDef> tools_;
    QSet<QString> disabled_tools_;
    QHash<QString, Resource> resources_;
    quint64 generation_ = 0;
    AuthChecker auth_checker_;
};

} // namespace fincept::mcp
