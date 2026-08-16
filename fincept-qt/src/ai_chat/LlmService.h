#pragma once
// LlmService.h — Multi-provider LLM API client (Qt port)
// Supports OpenAI-compatible APIs (OpenAI, Groq, DeepSeek, OpenRouter, Ollama),
// Anthropic, Google Gemini, and Fincept's own endpoint.
// Streaming via QNetworkReply::readyRead + SSE parsing.

#include "core/result/Result.h"
#include "storage/repositories/LlmProfileRepository.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace fincept::ai_chat {

// ── Provider helpers ──────────────────────────────────────────────────────────

/// True for the local gateway, under either name.
///
/// "ollama" was the original id and is wrong: hearth is the provider — the
/// OpenAI-compatible gateway on 127.0.0.1:11435 — and Ollama is one engine it
/// drives behind that. Calling the provider "ollama" made the Models tab read
/// as though finterm talked to Ollama directly, and cost a real bug when a
/// model field was filled in with "hearth" because that is what the provider
/// actually is.
///
/// "hearth" is canonical from here; "ollama" stays accepted so existing config
/// rows, saved profiles and any path still using the old name keep resolving.
inline bool is_hearth_provider(const QString& provider) {
    const QString p = provider.toLower();
    return p == QStringLiteral("hearth") || p == QStringLiteral("ollama");
}

/// Canonical id for storage and display.
inline QString canonical_provider(const QString& provider) {
    return is_hearth_provider(provider) ? QStringLiteral("hearth") : provider.toLower();
}

inline bool provider_supports_streaming(const QString& provider) {
    return provider == "openai" || provider == "anthropic" || provider == "gemini" || provider == "google" ||
           provider == "groq" || provider == "deepseek" || provider == "openrouter" || provider == "minimax" ||
           provider == "kimi" || is_hearth_provider(provider) || provider == "xai" || provider == "fincept";
}

inline bool provider_requires_api_key(const QString& provider) {
    return !is_hearth_provider(provider) && provider != "fincept";
}

/// Map a provider string to its target agent runtime (R1, R3).
///   "anthropic" → "anthropic"     (Claude Agent SDK path)
///   "hearth"/"ollama" → "local"         (minimal OpenAI-compatible loop)
///   anything else → "external"    (dormant adapters per R2; legacy
///                                  config rows continue to load but
///                                  no agent dispatch is wired)
inline QString runtime_for_provider(const QString& provider) {
    const QString p = provider.toLower();
    if (p == "anthropic")
        return QStringLiteral("anthropic");
    if (is_hearth_provider(p))
        return QStringLiteral("local");
    return QStringLiteral("external");
}

// ── Data types ────────────────────────────────────────────────────────────────

struct ConversationMessage {
    QString role; // "system", "user", "assistant"
    QString content;
};

// Per-request persona scope. Carried through the chat path so each conversation
// (and each chat pane) uses its own persona without touching the global
// active_persona_* singleton state. `valid == false` (the default) means "fall
// back to the global set_persona() snapshot" — keeps every existing caller
// (inline completion, news one-shots, agent bridge) working unchanged.
struct PersonaScope {
    QString prompt;          // resolved persona system prompt
    QStringList tool_globs;  // resolved persona tool allow-list (globs)
    bool valid = false;
    // When false, ask a local reasoning model (hearth/Ollama qwen3) to skip its
    // chain-of-thought via the `think:false` request extension — a large latency
    // win for short structured one-shots. Ignored for cloud providers (an API
    // key is set), which would reject the unknown field.
    bool think = true;
    // ── Per-call target override (role binding) ──────────────────────────────
    //
    // Empty = use the service's configured provider/model. Non-empty = this
    // call goes somewhere else entirely.
    //
    // Providers are NOT mutually exclusive: hearth on loopback and Gemini in
    // the cloud are both reachable at the same time, so "news on a local model
    // while chat is on Gemini" is a normal thing to want. These fields carry
    // the whole target rather than just a model id, because a model name alone
    // cannot say which endpoint or key to use.
    //
    // Fill via LlmService::scope_for_role(). Anything left empty falls back to
    // the configured value, so a partial override is safe.
    QString provider = {};
    QString model = {};
    QString api_key = {};
    QString base_url = {};
    /// Per-call output cap, 0 = the configured max_tokens.
    ///
    /// A short structured one-shot does not need the chat budget. Left at 4096
    /// a model that starts repeating has licence to do it for pages — an
    /// observed news brief collapsed into a multi-thousand-token run of country
    /// and bird names. The cap does not prevent the collapse; it bounds it.
    int max_tokens = 0;
};

struct LlmResponse {
    QString content;
    QString error;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    bool success = false;
};

// chunk_text, is_done
using StreamCallback = std::function<void(const QString&, bool)>;

// Final structured response, delivered on the UI thread and scoped to THIS
// call. Prefer this over the finished_streaming() signal for per-widget
// finalization: that signal is a singleton broadcast, so with multiple chat
// panes every pane receives every response (cross-talk). When on_done is set,
// chat_streaming delivers the result only to the caller and skips the signal.
using CompletionCallback = std::function<void(LlmResponse)>;

// ── LlmService ────────────────────────────────────────────────────────────────

class LlmService : public QObject {
    Q_OBJECT
  public:
    static LlmService& instance();

    // Non-streaming (blocking — call from background thread via QtConcurrent::run)
    // use_tools: when false, disables MCP tool execution for this request
    //            (use for the floating bubble to prevent unintended navigation)
    // persona: per-request persona scope (default {} → global set_persona snapshot)
    LlmResponse chat(const QString& user_message, const std::vector<ConversationMessage>& history,
                     bool use_tools = true, const PersonaScope& persona = {});

    // Streaming — launches background thread; on_chunk called on that thread.
    // Emit finished_streaming(response) when done to get result on UI thread.
    // use_tools: when false, disables MCP tool execution for this request
    void chat_streaming(const QString& user_message, const std::vector<ConversationMessage>& history,
                        StreamCallback on_chunk, bool use_tools = true, const PersonaScope& persona = {},
                        CompletionCallback on_done = {});

    // Reload config from DB (call after user changes LLM settings)
    void reload_config();

    // ── Active config accessors (AI Chat context) ─────────────────────────────
    QString active_provider() const;
    QString active_model() const;
    QString active_api_key() const;
    QString active_base_url() const;
    double active_temperature() const;
    int active_max_tokens() const;
    bool tools_enabled() const;
    bool is_configured() const;

/// Build a PersonaScope target for a role, so the call goes wherever that
    /// role is bound — provider, model, key and base URL together.
    ///
    /// Order:
    ///   1. The role's bound profile (Settings → AI Config → Roles). Any
    ///      provider: hearth and a cloud API are both reachable at once, so
    ///      news can sit on a local model while chat is on Gemini.
    ///   2. ``local_fallback`` as the model, applied only when the CONFIGURED
    ///      provider is local — it carries a hearth role alias ("fast_chat")
    ///      that a cloud API would reject.
    ///   3. All-empty: the caller's configured provider and model.
    ///
    /// Roles are listed in AiRoles.h. An unbound role resolves to the chat
    /// model, so binding one is always an override, never a prerequisite.
    PersonaScope scope_for_role(const QString& role, const QString& local_fallback = {}) const;

    // ── Chat persona ──────────────────────────────────────────────────────────
    // Scope subsequent chat()/chat_streaming() calls to a persona (focused
    // system prompt + curated tool allow-list). See ChatPersonas.h. Unknown id
    // → General (all tools).
    void set_persona(const QString& persona_id);
    QString active_persona() const;

    // ── Profile-aware resolution ──────────────────────────────────────────────
    // Returns the resolved LLM profile for a given context.
    // context_type: "ai_chat" | "agent" | "agent_default" |
    //               "team" | "team_default" | "team_coordinator"
    // context_id:   agent/team id, or empty for type-level queries.
    ResolvedLlmProfile resolve_profile(const QString& context_type, const QString& context_id = {}) const;

    // Convenience: build a QJsonObject suitable for embedding in AgentService
    // payloads (provider, model_id, api_key, base_url, temperature, max_tokens).
    static QJsonObject profile_to_json(const ResolvedLlmProfile& p);

    // Fetch available models for a provider (async — emits models_fetched)
    void fetch_models(const QString& provider, const QString& api_key, const QString& base_url = {});

    LlmService(const LlmService&) = delete;
    LlmService& operator=(const LlmService&) = delete;

  signals:
    // LEGACY — no in-app consumers. Streaming completion is delivered per-call
    // via the on_done CompletionCallback (see chat_streaming), NOT this signal.
    // Do NOT connect chat panes here: it is a singleton broadcast, so every
    // connected pane would receive every pane's response (the multi-pane
    // cross-talk bug). Kept only for backward compatibility.
    void finished_streaming(LlmResponse response);
    void config_changed(); // emitted after reload_config() — UI can react
    void models_fetched(const QString& provider, const QStringList& models, const QString& error);

  private:
    LlmService();

    mutable QMutex mutex_;

    // Active config (reloaded lazily — all mutable so ensure_config() can be called from const accessors)
    mutable QString provider_;
    mutable QString api_key_;
    mutable QString base_url_;
    mutable QString model_;
    mutable double temperature_ = 0.7;
    mutable int max_tokens_ = 4096;
    mutable QString system_prompt_;
    mutable bool tools_enabled_ = true;
    mutable bool config_loaded_ = false;

    // Active chat persona — a focused system-prompt addition + a tool allow-list
    // (glob patterns; empty = all tools / General). Guarded by its OWN mutex,
    // not mutex_: the streaming / tool-loop build path reads the persona WITHOUT
    // holding mutex_, so all reads go through the locked snapshot accessors
    // below (returning by value) to avoid a COW data race with set_persona().
    mutable QMutex persona_mutex_;
    QString active_persona_id_;
    QString active_persona_prompt_;
    QStringList active_persona_tools_;
    QStringList persona_tools() const;  // locked snapshot — safe to read off-thread
    QString persona_prompt() const;     // locked snapshot — safe to read off-thread

    void ensure_config() const;

    // Per-request system additions: persona instructions + ambient app context
    // (current symbol / active portfolio). Injected after the base system prompt.
    QString ambient_context() const;
    QString dynamic_system_suffix() const;
    // Per-request overloads: use the PersonaScope's prompt/globs when valid,
    // else fall back to the global persona snapshot.
    QString dynamic_system_suffix(const PersonaScope& persona) const;
    QStringList resolve_tool_globs(const PersonaScope& persona) const;

    // Request builders → QJsonObject
    QJsonObject build_openai_request(const QString& user_message, const std::vector<ConversationMessage>& history,
                                     bool stream, bool with_tools = true, const PersonaScope& persona = {});
    QJsonObject build_anthropic_request(const QString& user_message, const std::vector<ConversationMessage>& history,
                                        bool stream, const PersonaScope& persona = {});
    /// Make a tool's JSON Schema safe to send. Gemini validates
    /// function_declarations strictly and rejects the WHOLE request when any
    /// one is malformed, so a single bad tool takes down every call — which is
    /// how one array property with no "items" broke the news brief. OpenAI
    /// tolerates the same schema, so the fault stayed invisible until a second
    /// provider was configured.
    static QJsonObject sanitize_tool_schema(QJsonObject schema);

    /// True when a response failed because the provider's quota ran out, as
    /// opposed to any other error. Only this warrants moving to another model:
    /// a 400 or a bad key will fail identically on the next one.
    static bool is_quota_exhausted(const LlmResponse& r);

    /// The next target to try after `current` ran out of quota, or an empty
    /// scope when there is nowhere left to go.
    ///
    /// Order: a smaller model on the SAME provider first (same key, same
    /// endpoint, usually a separate allowance), then the first configured
    /// LOCAL provider, which has no quota at all and is therefore the floor.
    PersonaScope next_quota_fallback(const PersonaScope& current) const;

    QJsonObject build_gemini_request(bool use_tools, const QString& user_message, const std::vector<ConversationMessage>& history,
                                     const PersonaScope& persona = {});
    QJsonObject build_fincept_request(const QString& user_message, const std::vector<ConversationMessage>& history,
                                      bool with_tools);

    QString get_endpoint_url(const PersonaScope& persona = {}) const;

    // Effective request target: the per-call override when the role binding set
    // one, else the service's configured value. Every request-path member read
    // goes through these so a bound role cannot half-apply (right model, wrong
    // key) — the failure mode that makes a cross-provider binding look like an
    // auth error.
    QString eff_provider(const PersonaScope& p) const { return p.provider.isEmpty() ? provider_ : p.provider; }
    QString eff_model(const PersonaScope& p) const { return p.model.isEmpty() ? model_ : p.model; }
    QString eff_api_key(const PersonaScope& p) const { return p.provider.isEmpty() ? api_key_ : p.api_key; }
    QString eff_base_url(const PersonaScope& p) const { return p.provider.isEmpty() ? base_url_ : p.base_url; }
    QMap<QString, QString> get_headers(const PersonaScope& persona = {}) const;

    /// Resolve the max output tokens for a request. Order:
    ///   1. the per-call ceiling from the role binding (persona.max_tokens),
    ///      else the user-set global max_tokens_ → use it, but clamp to the
    ///      model's published cap so we don't get a 400 from the API.
    ///   2. model's published cap from ModelCatalog.
    ///   3. conservative fallback (8192).
    ///
    /// The cap is looked up for the model the request actually targets, which
    /// is the persona's when a role binding overrode it. Called with mutex_
    /// held. The default-constructed scope resolves to the global settings, so
    /// persona-less callers (config logging, the legacy fincept endpoint) keep
    /// their previous behaviour.
    int resolved_max_tokens(const PersonaScope& persona = {}) const;

    /// Write the output-token cap into an OpenAI-shaped request body under the
    /// key that provider accepts.
    ///
    /// OpenAI and xAI deprecated `max_tokens`; gpt-5 and the o-series reject a
    /// body that uses it. build_openai_request() knew that, but the three
    /// tool-loop follow-up bodies wrote `max_tokens` unconditionally — so an
    /// agentic chat on a gpt-5 profile 400'd on its first tool round while the
    /// no-tools path worked. One helper so the next follow-up body cannot
    /// reintroduce it. Called with mutex_ held.
    void set_openai_max_tokens(QJsonObject& body, const PersonaScope& persona) const;

    // Synchronous HTTP helpers (use QNetworkAccessManager + QEventLoop)
    // Must be called from a background thread.
    LlmResponse do_request(const QString& user_message, const std::vector<ConversationMessage>& history,
                           bool use_tools = true, const PersonaScope& persona = {});
    LlmResponse do_streaming_request(const QString& user_message, const std::vector<ConversationMessage>& history,
                                     StreamCallback on_chunk, const PersonaScope& persona = {});

    // Tool-call follow-up loop (OpenAI-compatible)
    LlmResponse do_tool_loop(QJsonArray loop_messages, const QString& url, const QMap<QString, QString>& headers,
                             const PersonaScope& persona = {});

    // Detect and execute tool calls embedded as text/XML in the response content.
    // Returns std::nullopt if no text-based tool calls were found.
    // Takes the persona so its follow-up request targets the SAME model this
    // turn was sent to. Without it a role-bound call posts the configured
    // model to the override's endpoint — a 404 that looks like a tool bug.
    std::optional<LlmResponse> try_extract_and_execute_text_tool_calls(const QString& content,
                                                                       const QString& user_message, const QString& url,
                                                                       const QMap<QString, QString>& headers,
                                                                       const PersonaScope& persona = {});

    // Models-list helpers
    static QString get_models_url(const QString& provider, const QString& api_key, const QString& base_url);
    static QMap<QString, QString> get_models_headers(const QString& provider, const QString& api_key);
    static QStringList parse_models_response(const QString& provider, const QByteArray& body);

    // Parse SSE data line → extracted text chunk
    static QString parse_sse_chunk(const QString& data, const QString& provider);

    // Parse token usage from response JSON
    static void parse_usage(LlmResponse& resp, const QJsonObject& rj, const QString& provider);

    // Synchronous POST helper (blocks calling thread via QEventLoop)
    struct HttpResult {
        bool success = false;
        int status = 0;
        QByteArray body;
        QString error;
    };
    static HttpResult blocking_post(const QString& url, const QJsonObject& body, const QMap<QString, QString>& headers,
                                    int timeout_ms = 120000);
    static HttpResult blocking_get(const QString& url, const QMap<QString, QString>& headers, int timeout_ms = 30000);

    // QEventLoop-based HTTP for Cloudflare-protected endpoints (Fincept)
    static HttpResult eventloop_request(const QString& method, const QString& url, const QByteArray& body,
                                        const QMap<QString, QString>& headers, int timeout_ms = 30000);

    // Fincept async path: POST /research/llm/async → poll /research/llm/status/{id}
    LlmResponse fincept_async_request(const QString& user_message, const std::vector<ConversationMessage>& history);
};

} // namespace fincept::ai_chat
