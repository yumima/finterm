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
/// MCP-spec logging levels (RFC 5424 syslog severities — the spec calls
/// them out by name).  Tool handlers emit structured log events via
/// ToolContext::on_log; the dispatch forwards to both the app-internal
/// core Logger and (Track 5 commit H follow-up) the MCP-spec
/// `notifications/message` channel for the agent runtime.
enum class LogLevel { Debug, Info, Notice, Warning, Error, Critical, Alert, Emergency };

// ============================================================================
// Sampling — MCP-spec server-side LLM call (sampling/createMessage)
// ============================================================================
//
// A tool whose work needs the model — e.g. an Edgar filing summarizer
// that wants the LLM to compress a 50-page 10-K before returning —
// calls ctx.sample(req).  The runtime fans the request out to the
// active LLM profile, awaits the response, returns it to the tool
// handler.  Cost flows through the user's profile (same as a normal
// chat turn).
//
// Currently sync from the handler's POV: the runtime is expected to
// run on its own thread and the handler blocks awaiting the response.
// When McpProvider grows async-tool support beyond promise-resolution,
// revisit.

struct SampleRequest {
    QString prompt;          // user-message content
    QString system_prompt;   // optional system-message preamble
    int max_tokens = 512;
    double temperature = 0.7;
};

struct SampleResponse {
    QString text;
    QString model;           // model id that produced the response (best-effort)
    QString error;           // empty on success
};

// ============================================================================
// Elicitation — MCP-spec server-side structured user prompt
// ============================================================================
//
// A tool that needs disambiguation or a confirmation mid-call asks the
// user via ctx.elicit(req).  The runtime surfaces the request to the
// chat UI (modal / inline form), awaits the user's response, returns
// it to the handler.  The schema is JSON Schema describing the expected
// answer shape.
//
// Common shapes:
//   - boolean   "Confirm trade?"           {type: "boolean"}
//   - enum      "Which file did you mean?" {type: "string", enum: [...]}
//   - object    structured form            {type: "object", properties: ...}

struct ElicitRequest {
    QString prompt;          // user-visible question
    QJsonObject schema;      // JSON Schema for the answer
};

struct ElicitResponse {
    QJsonValue value;        // user's answer matching the schema
    bool declined = false;   // true if the user explicitly declined
    QString error;           // transport / wiring errors only
};

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

    /// Optional structured-log emitter (MCP spec `notifications/message`).
    /// Distinct from core Logger which is app-internal — on_log events
    /// can surface to the agent runtime and UI.  May be null; the helpers
    /// below short-circuit when so.
    std::function<void(LogLevel, const QString&)> on_log;

    /// Optional server-side LLM sampling (MCP spec `sampling/createMessage`).
    /// Tool handlers call ctx.sample(req) to ask the runtime to invoke
    /// the active LLM profile; blocks until the response arrives.  May
    /// be null — `sample()` below returns an error in that case.
    std::function<SampleResponse(const SampleRequest&)> on_sample;

    /// Optional server-side structured user prompt (MCP spec
    /// `elicitation/create`).  Tool handlers call ctx.elicit(req) to
    /// ask the user mid-call; blocks until the user responds or
    /// declines.  May be null — `elicit()` below returns an error.
    std::function<ElicitResponse(const ElicitRequest&)> on_elicit;

    /// Hard timeout in milliseconds. McpProvider arms a timer that
    /// resolves the promise with a timeout error if the handler hasn't
    /// finished by then. Default: 30s; per-tool override via
    /// ToolDef::default_timeout_ms; per-call override via _meta.timeout_ms.
    int timeout_ms = 30000;

    /// Convenience — true if cancellation hook is set AND signalled.
    bool cancelled() const { return is_cancelled && is_cancelled(); }

    // Logging convenience.  Branchless when on_log is null.
    void log_debug(const QString& msg) const { if (on_log) on_log(LogLevel::Debug, msg); }
    void log_info(const QString& msg) const { if (on_log) on_log(LogLevel::Info, msg); }
    void log_warn(const QString& msg) const { if (on_log) on_log(LogLevel::Warning, msg); }
    void log_error(const QString& msg) const { if (on_log) on_log(LogLevel::Error, msg); }

    // Sampling / elicitation convenience.  Return an error response
    // when the callback isn't wired so the handler can surface the
    // failure to the model rather than crashing.
    SampleResponse sample(const SampleRequest& req) const {
        if (on_sample)
            return on_sample(req);
        SampleResponse r;
        r.error = QStringLiteral("sampling not wired into this runtime");
        return r;
    }
    ElicitResponse elicit(const ElicitRequest& req) const {
        if (on_elicit)
            return on_elicit(req);
        ElicitResponse r;
        r.error = QStringLiteral("elicitation not wired into this runtime");
        return r;
    }
};

/// Render an MCP LogLevel as the canonical lowercase string the spec
/// uses for `notifications/message.level` (e.g. "debug", "info",
/// "warning", "error", …).
inline const char* log_level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Notice: return "notice";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
        case LogLevel::Critical: return "critical";
        case LogLevel::Alert: return "alert";
        case LogLevel::Emergency: return "emergency";
    }
    return "info";
}

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

/// Wire-shape Resource — what we get back from `resources/list` against
/// an external MCP server.  Differs from the internal `Resource` struct
/// below: no handler closure, just metadata.  Content is fetched via
/// `read_resource(uri)` which returns a `ResourceContent`.
struct ExternalResource {
    QString server_id;
    QString uri;
    QString name;
    QString description;
    QString mime_type;
};

// ============================================================================
// Resource — typed, addressable read-only content (MCP spec)
// ============================================================================
//
// Resources let an agent read finterm state (portfolio snapshot, active
// watchlist, current news digest, …) without spending a tool-call turn
// on every fetch.  The model addresses a resource by its `uri`; reading
// it returns the current snapshot via the resource's handler.
//
// Dynamic resources (e.g., portfolio_snapshot) build the content fresh
// on each read; static resources can cache.

// ============================================================================
// Prompts — MCP spec templated prompts surfaced in the slash menu
// ============================================================================
//
// A Prompt is a named, user-invokable template that produces one or
// more chat messages.  The model never sees prompts directly — the
// user picks a prompt from the slash menu (`/daily-brief`), supplies
// the named arguments, the server expands the template, and finterm
// injects the resulting messages into the next turn.
//
// In finterm: matches Track 5 / Track 7 — slash commands resolve to
// `(agent, skill, args)` tuples; prompts are the MCP-spec channel
// for surfacing those resolutions to the model.

struct PromptArgument {
    QString name;        // e.g. "date", "ticker"
    QString description;
    bool required = false;
};

/// Message emitted by a prompt expansion.  Role is "system" | "user" |
/// "assistant" (matches MCP spec).  Content is plain text in the v1
/// surface; richer content types (image, tool-result) deferred.
struct PromptMessage {
    QString role;        // "system" | "user" | "assistant"
    QString text;
};

/// Result of expanding a prompt template — list of messages to feed
/// into the next chat turn.
struct PromptResult {
    QString description;          // optional human-readable description
    std::vector<PromptMessage> messages;
    QString error;                // empty on success
};

/// Handler invoked by McpProvider::get_prompt.  Receives the user-
/// supplied argument map (string→string).  Synchronous; expected to
/// be fast (template expansion + maybe a repository read).
using PromptHandler = std::function<PromptResult(const QHash<QString, QString>&)>;

struct Prompt {
    QString name;        // user-visible identifier (slash-command stem)
    QString description;
    std::vector<PromptArgument> arguments;
    PromptHandler handler;
};

/// Result returned by a resource handler.
struct ResourceContent {
    QString uri;
    QString mime_type;     // "application/json", "text/plain", etc.
    QString text;          // for text-based resources (JSON, markdown, …)
    QByteArray blob;       // for binary resources (image, pdf, …)
    QString error;         // empty on success
};

/// Handler invoked on read_resource(uri).  Synchronous; expected to be
/// quick (the agent reads resources eagerly).  Long-running fetches
/// should be tools, not resources.
using ResourceHandler = std::function<ResourceContent()>;

struct Resource {
    QString uri;           // e.g., "finterm://portfolio/snapshot"
    QString name;          // display name
    QString description;
    QString mime_type;     // default content type when handler doesn't override
    ResourceHandler handler;
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
