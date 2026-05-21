// McpProvider.cpp — Internal tool registry and executor (Qt port)

#include "mcp/McpProvider.h"

#include "core/logging/Logger.h"
#include "mcp/SchemaValidator.h"

#include <QApplication>
#include <QMetaObject>
#include <QPromise>

#include <algorithm>
#include <QTimer>

#include <atomic>
#include <memory>

namespace fincept::mcp {

static constexpr const char* TAG = "McpProvider";

McpProvider& McpProvider::instance() {
    static McpProvider s;
    return s;
}

// ============================================================================
// Registration
// ============================================================================

void McpProvider::register_tool(ToolDef tool) {
    QMutexLocker lock(&mutex_);
    QString name = tool.name;
    tools_.insert(name, std::move(tool));
    ++generation_;
}

void McpProvider::register_tools(std::vector<ToolDef> tools) {
    QMutexLocker lock(&mutex_);
    for (auto& t : tools) {
        QString name = t.name;
        tools_.insert(name, std::move(t));
    }
    ++generation_;
}

void McpProvider::unregister_tool(const QString& name) {
    QMutexLocker lock(&mutex_);
    tools_.remove(name);
    ++generation_;
}

// ============================================================================
// Resources (MCP spec resources)
// ============================================================================

void McpProvider::register_resource(Resource resource) {
    QMutexLocker lock(&mutex_);
    QString uri = resource.uri;
    resources_.insert(uri, std::move(resource));
    ++generation_;
}

void McpProvider::unregister_resource(const QString& uri) {
    QMutexLocker lock(&mutex_);
    resources_.remove(uri);
    ++generation_;
}

std::vector<Resource> McpProvider::list_resources() const {
    QMutexLocker lock(&mutex_);
    std::vector<Resource> result;
    result.reserve(static_cast<std::size_t>(resources_.size()));
    for (auto it = resources_.cbegin(); it != resources_.cend(); ++it)
        result.push_back(it.value());
    std::sort(result.begin(), result.end(),
              [](const Resource& a, const Resource& b) { return a.uri < b.uri; });
    return result;
}

ResourceContent McpProvider::read_resource(const QString& uri) {
    ResourceHandler handler;
    QString default_mime;
    {
        QMutexLocker lock(&mutex_);
        auto it = resources_.constFind(uri);
        if (it == resources_.cend()) {
            ResourceContent r;
            r.uri = uri;
            r.error = "unknown resource: " + uri;
            return r;
        }
        handler = it.value().handler;
        default_mime = it.value().mime_type;
    }
    // Mutex released before invoking the handler — handlers may take
    // their own locks (repository singletons) and we don't want to
    // pin the provider mutex across long calls.
    if (!handler) {
        ResourceContent r;
        r.uri = uri;
        r.error = "resource has no handler: " + uri;
        return r;
    }
    ResourceContent r = handler();
    if (r.uri.isEmpty())
        r.uri = uri;
    if (r.mime_type.isEmpty())
        r.mime_type = default_mime;
    return r;
}

bool McpProvider::has_resource(const QString& uri) const {
    QMutexLocker lock(&mutex_);
    return resources_.contains(uri);
}

// ============================================================================
// Enable / Disable
// ============================================================================

void McpProvider::set_tool_enabled(const QString& name, bool enabled) {
    QMutexLocker lock(&mutex_);
    if (enabled)
        disabled_tools_.remove(name);
    else
        disabled_tools_.insert(name);
    ++generation_;
}

bool McpProvider::is_tool_enabled(const QString& name) const {
    QMutexLocker lock(&mutex_);
    return !disabled_tools_.contains(name);
}

// ============================================================================
// Discovery
// ============================================================================

std::vector<UnifiedTool> McpProvider::list_tools() const {
    QMutexLocker lock(&mutex_);
    std::vector<UnifiedTool> result;
    result.reserve(static_cast<std::size_t>(tools_.size()));

    for (auto it = tools_.cbegin(); it != tools_.cend(); ++it) {
        if (disabled_tools_.contains(it.key()))
            continue;
        const auto& t = it.value();
        result.push_back({QString(INTERNAL_SERVER_ID), QString(INTERNAL_SERVER_NAME), t.name, t.description,
                          t.input_schema.to_json(), true});
    }
    return result;
}

std::vector<UnifiedTool> McpProvider::list_all_tools() const {
    QMutexLocker lock(&mutex_);
    std::vector<UnifiedTool> result;
    result.reserve(static_cast<std::size_t>(tools_.size()));

    for (auto it = tools_.cbegin(); it != tools_.cend(); ++it) {
        const auto& t = it.value();
        result.push_back({QString(INTERNAL_SERVER_ID), QString(INTERNAL_SERVER_NAME), t.name, t.description,
                          t.input_schema.to_json(), true});
    }
    return result;
}

std::size_t McpProvider::tool_count() const {
    QMutexLocker lock(&mutex_);
    std::size_t count = 0;
    for (auto it = tools_.cbegin(); it != tools_.cend(); ++it) {
        if (!disabled_tools_.contains(it.key()))
            ++count;
    }
    return count;
}

bool McpProvider::has_tool(const QString& name) const {
    QMutexLocker lock(&mutex_);
    return tools_.contains(name);
}

// ============================================================================
// Execution
// ============================================================================

ToolResult McpProvider::call_tool(const QString& name, const QJsonObject& args) {
    // Sync entry. If the tool is async we still want to give legacy callers
    // (LlmService, agent tooling, workflow nodes) a blocking ToolResult, so
    // we dispatch via call_tool_async and wait. Every existing call site is
    // already on a worker thread so blocking here is safe.
    auto future = call_tool_async(name, args);
    future.waitForFinished();
    if (future.resultCount() == 0)
        return ToolResult::fail("Tool '" + name + "' produced no result");
    return future.result();
}

QFuture<ToolResult> McpProvider::call_tool_async(const QString& name, const QJsonObject& args, ToolContext ctx) {
    ToolHandler sync_handler;
    AsyncToolHandler async_handler;
    ToolSchema schema;
    int default_timeout_ms = 30000;
    AuthLevel auth_required = AuthLevel::None;
    bool is_destructive = false;

    auto fail_now = [](const QString& msg) {
        QPromise<ToolResult> p;
        p.start();
        p.addResult(ToolResult::fail(msg));
        p.finish();
        return p.future();
    };

    {
        QMutexLocker lock(&mutex_);

        if (!tools_.contains(name))
            return fail_now("Tool not found: " + name);
        if (disabled_tools_.contains(name))
            return fail_now("Tool is disabled: " + name);

        const auto& def = tools_[name];
        sync_handler = def.handler;
        async_handler = def.async_handler;
        schema = def.input_schema;
        default_timeout_ms = def.default_timeout_ms;
        auth_required = def.auth_required;
        is_destructive = def.is_destructive;

        if (!async_handler && !sync_handler)
            return fail_now("Tool '" + name + "' has no handler");
    }

    // Authorization gate. The app-layer hook (installed via set_auth_checker)
    // handles the user-visible side. Without a checker we fail closed for
    // Verified+; lesser levels pass with an advisory log.
    if (auth_required != AuthLevel::None || is_destructive) {
        AuthChecker checker;
        {
            QMutexLocker lock(&mutex_);
            checker = auth_checker_;
        }
        if (checker) {
            if (!checker(auth_required, is_destructive)) {
                LOG_WARN(TAG, QString("Tool '%1' blocked by auth checker (required=%2, destructive=%3)")
                                  .arg(name, auth_level_str(auth_required))
                                  .arg(is_destructive ? "true" : "false"));
                return fail_now(
                    QString("Tool '%1' requires user authorization").arg(name));
            }
        } else if (auth_required >= AuthLevel::Verified) {
            LOG_WARN(TAG, QString("Tool '%1' blocked: no AuthChecker installed (required=%2)")
                              .arg(name, auth_level_str(auth_required)));
            return fail_now("Tool requires confirmation but no authorisation hook is installed");
        } else if (is_destructive) {
            LOG_INFO(TAG,
                     QString("Tool '%1' is destructive (advisory; install McpProvider::set_auth_checker to gate)")
                         .arg(name));
        }
    }

    // Validate + default-inject args before handing them to the handler.
    // Tools that don't declare structured schemas see this as a no-op.
    QJsonObject normalized = args;
    auto vr = validate_args(schema, normalized);
    if (vr.is_err()) {
        const QString err = QString::fromStdString(vr.error());
        LOG_WARN(TAG, QString("Tool '%1' rejected: %2").arg(name, err));
        return fail_now(err);
    }

    // Per-call timeout override falls through ctx; fall back to per-tool default.
    if (ctx.timeout_ms == 30000) // ToolContext default — not explicitly set
        ctx.timeout_ms = default_timeout_ms;

    // Async preferred; sync wrapped in a resolved future so call_tool() works
    // uniformly for both shapes.
    if (async_handler) {
        auto promise = std::make_shared<QPromise<ToolResult>>();
        promise->start();

        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        if (!ctx.is_cancelled) {
            ctx.is_cancelled = [cancelled]() { return cancelled->load(); };
        } else {
            auto orig = ctx.is_cancelled;
            ctx.is_cancelled = [orig, cancelled]() { return cancelled->load() || orig(); };
        }

        // One-shot timeout watchdog. Posted onto the application thread so it
        // fires even if the handler never returns control to a Qt event loop.
        auto* watchdog = new QTimer;
        watchdog->setSingleShot(true);
        watchdog->moveToThread(qApp->thread());
        QObject::connect(watchdog, &QTimer::timeout, watchdog,
                         [promise, cancelled, watchdog, name]() {
                             if (!promise->future().isFinished()) {
                                 cancelled->store(true);
                                 LOG_WARN(TAG, QString("Tool '%1' timed out").arg(name));
                                 promise->addResult(ToolResult::fail("Tool '" + name + "' timed out"));
                                 promise->finish();
                             }
                             watchdog->deleteLater();
                         });
        QMetaObject::invokeMethod(
            watchdog, [watchdog, ms = ctx.timeout_ms]() { watchdog->start(ms); }, Qt::QueuedConnection);

        try {
            LOG_DEBUG(TAG, "Calling async tool: " + name);
            async_handler(normalized, ctx, promise);
        } catch (const std::exception& e) {
            LOG_ERROR(TAG, QString("Async tool '%1' threw: %2").arg(name, e.what()));
            if (!promise->future().isFinished()) {
                promise->addResult(ToolResult::fail(QString("Tool execution error: ") + e.what()));
                promise->finish();
            }
        } catch (...) {
            LOG_ERROR(TAG, QString("Async tool '%1' threw unknown exception").arg(name));
            if (!promise->future().isFinished()) {
                promise->addResult(ToolResult::fail("Unknown error during tool execution"));
                promise->finish();
            }
        }
        return promise->future();
    }

    // Legacy sync path — invoke and wrap in a resolved future.
    QPromise<ToolResult> p;
    p.start();
    try {
        LOG_DEBUG(TAG, "Calling sync tool: " + name);
        p.addResult(sync_handler(normalized));
    } catch (const std::exception& e) {
        LOG_ERROR(TAG, QString("Tool '%1' threw exception: %2").arg(name, e.what()));
        p.addResult(ToolResult::fail(QString("Tool execution error: ") + e.what()));
    } catch (...) {
        LOG_ERROR(TAG, QString("Tool '%1' threw unknown exception").arg(name));
        p.addResult(ToolResult::fail("Unknown error during tool execution"));
    }
    p.finish();
    return p.future();
}

void McpProvider::set_auth_checker(AuthChecker checker) {
    QMutexLocker lock(&mutex_);
    auth_checker_ = std::move(checker);
}

// ============================================================================
// LLM Integration
// ============================================================================

QJsonArray McpProvider::format_tools_for_openai() const {
    auto tools = list_tools();
    QJsonArray result;

    for (const auto& tool : tools) {
        QJsonObject schema = tool.input_schema;
        if (schema.isEmpty()) {
            schema["type"] = "object";
            schema["properties"] = QJsonObject();
        }

        QJsonObject fn;
        fn["name"] = QString(INTERNAL_SERVER_ID) + "__" + tool.name;
        fn["description"] = tool.description;
        fn["parameters"] = schema;

        QJsonObject entry;
        entry["type"] = "function";
        entry["function"] = fn;
        result.append(entry);
    }

    return result;
}

QPair<QString, QString> McpProvider::parse_openai_function_name(const QString& fn_name) {
    int pos = fn_name.indexOf("__");
    if (pos <= 0 || pos >= fn_name.length() - 2)
        return {"", ""};
    return {fn_name.left(pos), fn_name.mid(pos + 2)};
}

// ============================================================================
// Lifecycle
// ============================================================================

void McpProvider::clear() {
    QMutexLocker lock(&mutex_);
    tools_.clear();
    disabled_tools_.clear();
    ++generation_;
}

// ============================================================================
// Generation Counter
// ============================================================================

quint64 McpProvider::generation() const {
    QMutexLocker lock(&mutex_);
    return generation_;
}

} // namespace fincept::mcp
