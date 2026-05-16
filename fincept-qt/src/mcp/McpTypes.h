#pragma once
// McpTypes.h — Core type definitions for the Model Context Protocol system (Qt port)
// Translated from fincept-cpp mcp_types.h — std::string → QString, nlohmann::json → QJsonObject

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPromise>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace fincept::mcp {

// ============================================================================
// Tool Result — returned by tool handlers
// ============================================================================

struct ToolResult {
    bool success = false;
    QJsonValue data; // arbitrary result data
    QString message; // human-readable message
    QString error;   // error message if !success

    QJsonObject to_json() const {
        QJsonObject j;
        j["success"] = success;
        if (!data.isNull() && !data.isUndefined())
            j["data"] = data;
        if (!message.isEmpty())
            j["message"] = message;
        if (!error.isEmpty())
            j["error"] = error;
        return j;
    }

    static ToolResult ok(const QString& msg, const QJsonValue& data = QJsonValue()) {
        ToolResult r;
        r.success = true;
        r.message = msg;
        r.data = data;
        return r;
    }

    static ToolResult ok_data(const QJsonValue& data) {
        ToolResult r;
        r.success = true;
        r.data = data;
        return r;
    }

    static ToolResult fail(const QString& err) {
        ToolResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

// ============================================================================
// Tool Handler — function signatures for tool execution
// ============================================================================
//
// Two handler shapes are supported on the same ToolDef:
//
//   1. SYNC (`ToolHandler`) — returns ToolResult by value. The pre-async
//      shape used by finterm's existing tools. Suitable for handlers that
//      finish in microseconds (registry / cache lookups, EventBus publishes).
//      Long-running sync handlers block the calling thread.
//
//   2. ASYNC (`AsyncToolHandler`) — Phase 4. Receives a ToolContext
//      (cancellation, progress, timeout) and a QPromise<ToolResult> the
//      handler resolves when work completes. Handler returns immediately;
//      the promise is fulfilled later from any thread. Use this for any
//      handler that calls Python, HTTP, or otherwise waits on I/O.
//
// McpProvider::call_tool dispatches to whichever shape is set on the
// ToolDef; if both are set, async wins. McpProvider::call_tool_async
// returns a QFuture<ToolResult> for either shape — sync handlers are
// wrapped in an immediately-resolved future.

using ToolHandler = std::function<ToolResult(const QJsonObject& args)>;

/// Context passed to async handlers. Captured by value into the handler
/// lambda — safe to copy.
struct ToolContext {
    /// Optional progress callback. Handlers call this with progress in [0,1]
    /// and a short status message. Safe to call from any thread.
    /// May be null — check before calling.
    std::function<void(double progress, const QString& message)> on_progress;

    /// Returns true if the caller has signalled cancellation. Handlers
    /// should poll periodically and resolve the promise with
    /// `ToolResult::fail("cancelled")` when set. May be null — treat as
    /// "never cancelled" in that case.
    std::function<bool()> is_cancelled;

    /// Hard timeout in milliseconds. McpProvider arms a timer that
    /// resolves the promise with a timeout error if the handler hasn't
    /// finished by then. Default: 30s; per-tool override via
    /// ToolDef::default_timeout_ms; per-call override via _meta.timeout_ms.
    int timeout_ms = 30000;

    /// Convenience — true if cancellation hook is set AND signalled.
    bool cancelled() const { return is_cancelled && is_cancelled(); }
};

/// Async handler signature. The handler MUST eventually:
///   - call promise->addResult(result) followed by promise->finish(); or
///   - call promise->finish() with no result (caller treats as fail).
///
/// The promise is heap-allocated and shared so the handler can capture it
/// by value into lambdas / network callbacks.
using AsyncToolHandler =
    std::function<void(const QJsonObject& args, ToolContext ctx,
                       std::shared_ptr<QPromise<ToolResult>> promise)>;

// ============================================================================
// Tool Input Schema — JSON Schema 2020-12 subset
// ============================================================================
//
// Two registration shapes are supported and may coexist on the same ToolDef:
//
//   1. LEGACY — set `properties` (QJsonObject) and `required` (QStringList)
//      directly. This is finterm's pre-Phase-3 shape; existing tools all
//      use it. The provider's validator inspects `properties` for type /
//      required.
//
//   2. STRUCTURED — populate `params` via ToolSchemaBuilder (see
//      mcp/ToolSchemaBuilder.h). Each param declares its type, description,
//      defaults, enum, and bounds in a typed C++ struct. `to_json()` merges
//      the two shapes when serialising for the LLM, with `params` taking
//      precedence on key collisions.

struct ToolParam {
    QString type = "string"; // "string" | "integer" | "number" | "boolean" | "array" | "object"
    QString description;
    bool required = false;
    QJsonValue default_value;          // null/undefined if no default
    QStringList enum_values;            // string enums; empty if not enumerated
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<int> min_length;      // for strings
    std::optional<int> max_length;      // for strings
    QString pattern;                    // regex string for strings
    QJsonObject items;                  // schema for array elements (raw JSON)

    QJsonObject to_json() const {
        QJsonObject j;
        j["type"] = type;
        if (!description.isEmpty()) j["description"] = description;
        if (!default_value.isNull() && !default_value.isUndefined())
            j["default"] = default_value;
        if (!enum_values.isEmpty()) {
            QJsonArray arr;
            for (const auto& v : enum_values) arr.append(v);
            j["enum"] = arr;
        }
        if (minimum.has_value())     j["minimum"] = *minimum;
        if (maximum.has_value())     j["maximum"] = *maximum;
        if (min_length.has_value())  j["minLength"] = *min_length;
        if (max_length.has_value())  j["maxLength"] = *max_length;
        if (!pattern.isEmpty())      j["pattern"] = pattern;
        if (!items.isEmpty())        j["items"] = items;
        return j;
    }
};

struct ToolSchema {
    QString type = "object";

    // Legacy shape — raw JSON Schema fragment per param.
    QJsonObject properties; // { "param": { "type": "string", "description": "..." } }

    // Structured shape — typed param declarations via ToolSchemaBuilder.
    QHash<QString, ToolParam> params;

    QStringList required; // legacy callers may set this directly; structured
                          // callers get it derived from params marked required=true.

    QJsonObject to_json() const {
        QJsonObject j;
        j["type"] = type;

        // Merge legacy `properties` with structured `params`. On key conflict,
        // structured wins (it's the migrated source of truth).
        QJsonObject merged_props = properties;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            merged_props[it.key()] = it.value().to_json();
        j["properties"] = merged_props;

        // Required = explicit list ∪ params marked required=true. Dedup.
        QStringList all_required = required;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (it.value().required && !all_required.contains(it.key()))
                all_required.append(it.key());
        }
        if (!all_required.isEmpty()) {
            QJsonArray req;
            for (const auto& r : all_required)
                req.append(r);
            j["required"] = req;
        }
        return j;
    }
};

// ============================================================================
// Authorization — per-tool auth gates
// ============================================================================
//
// Every tool declares an AuthLevel; McpProvider::call_tool checks it via a
// caller-supplied predicate (McpProvider::set_auth_checker) before invoking
// the handler. Defaults to None to keep behaviour unchanged for tools that
// don't opt in.
//
// In finterm (localhost-only, no cloud signup): the default auth checker
// treats the local user as authenticated for any level. `ExplicitConfirm`
// + `is_destructive` still pop a local confirmation dialog so destructive
// ops (place_order, delete_*, run_python_script) require an explicit OK.
//
// AuthLevel.None             — anyone, no gate
// AuthLevel.Authenticated    — logged-in user
// AuthLevel.Verified         — verified email
// AuthLevel.Subscribed       — active subscription
// AuthLevel.ExplicitConfirm  — requires the user to OK each call

enum class AuthLevel {
    None = 0,
    Authenticated = 1,
    Verified = 2,
    Subscribed = 3,
    ExplicitConfirm = 4,
};

inline const char* auth_level_str(AuthLevel a) {
    switch (a) {
        case AuthLevel::None:            return "none";
        case AuthLevel::Authenticated:   return "authenticated";
        case AuthLevel::Verified:        return "verified";
        case AuthLevel::Subscribed:      return "subscribed";
        case AuthLevel::ExplicitConfirm: return "explicit_confirm";
    }
    return "unknown";
}

// ============================================================================
// Tool Definition — a registered MCP tool
// ============================================================================

struct ToolDef {
    QString name;
    QString description;
    QString category; // navigation, trading, portfolio, market-data, analytics, system, exchange
    ToolSchema input_schema;

    // Dual-shape handler. Exactly one of these should be set per tool. If
    // both are set, async wins. If neither is set the tool is unusable and
    // McpProvider::call_tool returns "Tool has no handler".
    ToolHandler handler;            // synchronous (existing finterm shape)
    AsyncToolHandler async_handler; // async — for I/O-bound work

    bool enabled = true;

    /// Hard timeout for async handlers in milliseconds. Sync handlers ignore
    /// this — they block as long as they need. Per-call overrides may flow
    /// via `_meta.timeout_ms` in the args envelope.
    int default_timeout_ms = 30000;

    /// Required auth level — checked by McpProvider::call_tool via the
    /// caller-supplied AuthChecker. Defaults to None (no gate).
    AuthLevel auth_required = AuthLevel::None;

    /// True for state-mutating tools (set_setting, place_order, delete_*,
    /// run_python_script, file ops). Even on localhost, destructive ops
    /// should prompt the user; the AuthChecker is expected to surface a
    /// confirmation dialog when this is set.
    bool is_destructive = false;

    /// Old names this tool used to have. Lets us rename tools without
    /// breaking existing LLM workflows / saved chats — McpProvider's name
    /// resolver tries each alias if the canonical name doesn't match.
    QStringList legacy_aliases;
};

// ============================================================================
// Tool Filter — catalogue subsetting (used by format_tools_for_openai)
// ============================================================================
//
// Returning all enabled tools on every LLM turn is expensive (schema
// bloat + distractor tools confuse the model). ToolFilter lets callers
// scope the catalogue per-call.

struct ToolFilter {
    /// Inclusive — only tools whose category matches one of these are kept.
    /// Empty = no inclusion filter (all categories pass).
    QStringList categories;

    /// Exclusive — drop tools whose category matches one of these. Applied
    /// AFTER `categories` inclusion.
    QStringList exclude_categories;

    /// Regex include filter on tool name. Empty = no include filter.
    QStringList name_patterns;

    /// Regex exclude filter on tool name. Applied after include.
    QStringList exclude_name_patterns;

    /// Hard cap on returned tool count. 0 = no cap. Truncation runs after
    /// all other filters, in registration order.
    int max_tools = 0;
};

// ============================================================================
// Server Status — for external MCP server management
// ============================================================================

enum class ServerStatus { Stopped, Starting, Running, Error };

inline const char* server_status_str(ServerStatus s) {
    switch (s) {
        case ServerStatus::Stopped:
            return "stopped";
        case ServerStatus::Starting:
            return "starting";
        case ServerStatus::Running:
            return "running";
        case ServerStatus::Error:
            return "error";
    }
    return "unknown";
}

// ============================================================================
// External Tool — tool discovered from an external MCP server
// ============================================================================

struct ExternalTool {
    QString server_id;
    QString server_name;
    QString name;
    QString description;
    QJsonObject input_schema;
};

// ============================================================================
// Unified Tool — merged view for consumers (Chat, Agents, Node Editor)
// ============================================================================

struct UnifiedTool {
    QString server_id; // INTERNAL_SERVER_ID for in-process tools
    QString server_name;
    QString name;
    QString description;
    QJsonObject input_schema;
    bool is_internal = false;
    QString category;            // enables ToolFilter category include/exclude
    bool is_destructive = false; // surfaces mutating tools to UI / RAG
};

// ============================================================================
// Constants
// ============================================================================

inline constexpr const char* INTERNAL_SERVER_ID = "fincept-terminal";
inline constexpr const char* INTERNAL_SERVER_NAME = "Fincept Terminal";

} // namespace fincept::mcp
