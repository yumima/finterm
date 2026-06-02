// McpClient.cpp — JSON-RPC 2.0 over stdio for external MCP servers (Qt port)

#include "mcp/McpClient.h"

#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcessEnvironment>

namespace fincept::mcp {

static constexpr const char* TAG = "McpClient";

McpClient::McpClient(const McpServerConfig& config, QObject* parent) : McpClientBase(parent), config_(config) {}

McpClient::~McpClient() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

Result<void> McpClient::start() {
    if (running_)
        return Result<void>::ok();

    // Create a dedicated worker thread with its own event loop so that
    // QProcess signals are delivered there. start() is always called from a
    // background thread (never the UI thread), so blocking here is safe.
    worker_thread_ = new QThread;
    worker_thread_->setObjectName("mcp-" + config_.id);

    process_ = new QProcess; // no parent — we'll move it to worker thread

    // Merge environment
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = config_.env.constBegin(); it != config_.env.constEnd(); ++it)
        env.insert(it.key(), it.value());
    process_->setProcessEnvironment(env);

    // Wire signals — they'll fire on worker_thread_ once process_ is moved
    connect(process_, &QProcess::readyReadStandardOutput, this, &McpClient::on_ready_read, Qt::DirectConnection);
    connect(
        process_, &QProcess::readyReadStandardError, this,
        [this]() {
            while (process_ && process_->canReadLine()) {
                const QString line = QString::fromUtf8(process_->readLine()).trimmed();
                if (!line.isEmpty())
                    append_log("[stderr] " + line);
            }
        },
        Qt::DirectConnection);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &McpClient::on_finished,
            Qt::DirectConnection);

    // Route uvx through the app's bundled uv
    QString command = config_.command;
    QStringList args = config_.args;
    if (command == "uvx") {
        const QString uv = python::PythonSetupManager::instance().uv_path();
        if (QFileInfo::exists(uv)) {
            command = uv;
            args.prepend("run");
            args.prepend("tool"); // becomes: uv tool run <package> <args>
            LOG_INFO(TAG, "Routing uvx through bundled uv: " + uv);
        }
    }

#ifdef _WIN32
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif

    // Move QProcess to worker thread and start it there
    process_->moveToThread(worker_thread_);
    worker_thread_->start();

    // BlockingQueuedConnection is safe here — start() is always called from a
    // background thread (McpService auto-start thread), never from the UI thread.
    bool started_ok = false;
    QMetaObject::invokeMethod(
        process_,
        [this, command, args, &started_ok]() {
            process_->start(command, args);
            started_ok = process_->waitForStarted(60000); // 60s for first-time uv downloads
        },
        Qt::BlockingQueuedConnection);

    if (!started_ok) {
        const QString err = process_->errorString();
        LOG_ERROR(TAG, "Failed to start MCP server: " + config_.name + " — " + err);
        worker_thread_->quit();
        worker_thread_->wait(3000);
        cleanup_process();
        delete worker_thread_;
        worker_thread_ = nullptr;
        return Result<void>::err("Failed to start: " + config_.name.toStdString() + " — " + err.toStdString());
    }

    running_ = true;
    LOG_INFO(TAG, "Started MCP server process: " + config_.name);
    return Result<void>::ok();
}

void McpClient::cleanup_process() {
    if (process_) {
        delete process_;
        process_ = nullptr;
    }
}

void McpClient::stop() {
    if (!running_)
        return;
    running_ = false;

    // Wake any pending requests with an error
    {
        QMutexLocker lock(&rpc_mutex_);
        for (auto* req : pending_) {
            req->error = "Server stopped";
            req->completed = true;
        }
        rpc_cond_.wakeAll();
    }

    if (process_) {
        // Ask the worker thread to terminate the process, then quit its event loop.
        // We use QueuedConnection so the worker thread handles terminate() in its
        // own event loop — no BlockingQueuedConnection deadlock risk.
        // After quit(), wait() drains the thread and the process dies with it.
        QMetaObject::invokeMethod(process_, [this]() { process_->terminate(); }, Qt::QueuedConnection);
    }

    if (worker_thread_) {
        worker_thread_->quit();
        // Wait up to 4s for the worker to drain terminate() and exit cleanly.
        // If it hangs, kill the process directly before giving up.
        if (!worker_thread_->wait(4000) && process_) {
            process_->kill();
            worker_thread_->wait(1000);
        }
        delete worker_thread_;
        worker_thread_ = nullptr;
    }

    cleanup_process();
    LOG_INFO(TAG, "Stopped MCP server: " + config_.name);
}

bool McpClient::is_running() const {
    return running_ && process_ && process_->state() == QProcess::Running;
}

// ============================================================================
// MCP Protocol Methods
// ============================================================================

Result<QJsonObject> McpClient::initialize() {
    QJsonObject params;
    params["protocolVersion"] = "2024-11-05";

    QJsonObject client_info;
    client_info["name"] = "finterm";
    client_info["version"] = "4.0.2";
    params["clientInfo"] = client_info;

    QJsonObject capabilities;
    params["capabilities"] = capabilities;

    return send_request("initialize", params, 120000); // 120s for slow servers
}

Result<std::vector<ExternalTool>> McpClient::list_tools() {
    auto result = send_request("tools/list", {});
    if (result.is_err())
        return Result<std::vector<ExternalTool>>::err(result.error());

    const QJsonObject& data = result.value();
    QJsonArray tools_json = data["tools"].toArray();

    std::vector<ExternalTool> tools;
    tools.reserve(static_cast<std::size_t>(tools_json.size()));

    for (const auto& item : tools_json) {
        QJsonObject t = item.toObject();
        ExternalTool et;
        et.server_id = config_.id;
        et.server_name = config_.name;
        et.name = t["name"].toString();
        et.description = t["description"].toString();
        et.input_schema = t["inputSchema"].toObject();
        tools.push_back(std::move(et));
    }

    return Result<std::vector<ExternalTool>>::ok(std::move(tools));
}

Result<std::vector<ExternalResource>> McpClient::list_resources() {
    auto result = send_request("resources/list", {});
    if (result.is_err())
        return Result<std::vector<ExternalResource>>::err(result.error());

    const QJsonObject& data = result.value();
    const QJsonArray arr = data["resources"].toArray();

    std::vector<ExternalResource> out;
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (const auto& item : arr) {
        const QJsonObject r = item.toObject();
        ExternalResource er;
        er.server_id = config_.id;
        er.uri = r["uri"].toString();
        er.name = r["name"].toString();
        er.description = r["description"].toString();
        // MCP spec uses mimeType (camelCase)
        er.mime_type = r["mimeType"].toString();
        out.push_back(std::move(er));
    }
    return Result<std::vector<ExternalResource>>::ok(std::move(out));
}

Result<ResourceContent> McpClient::read_resource(const QString& uri) {
    QJsonObject params;
    params["uri"] = uri;
    auto result = send_request("resources/read", params);
    if (result.is_err()) {
        ResourceContent rc;
        rc.uri = uri;
        rc.error = QString::fromStdString(result.error());
        return Result<ResourceContent>::ok(rc);
    }

    // MCP spec returns { contents: [{uri, mimeType, text|blob}] } — one
    // resource per uri but the array is allowed to carry multiple
    // representations.  We surface the first text-bearing block; if
    // none, the first blob; else an error.
    const QJsonObject& data = result.value();
    const QJsonArray contents = data["contents"].toArray();
    if (contents.isEmpty()) {
        ResourceContent rc;
        rc.uri = uri;
        rc.error = "server returned empty contents";
        return Result<ResourceContent>::ok(rc);
    }

    for (const auto& c : contents) {
        const QJsonObject obj = c.toObject();
        if (obj.contains("text")) {
            ResourceContent rc;
            rc.uri = obj.value("uri").toString(uri);
            rc.mime_type = obj.value("mimeType").toString();
            rc.text = obj.value("text").toString();
            return Result<ResourceContent>::ok(rc);
        }
    }
    for (const auto& c : contents) {
        const QJsonObject obj = c.toObject();
        if (obj.contains("blob")) {
            ResourceContent rc;
            rc.uri = obj.value("uri").toString(uri);
            rc.mime_type = obj.value("mimeType").toString();
            rc.blob = QByteArray::fromBase64(obj.value("blob").toString().toUtf8());
            return Result<ResourceContent>::ok(rc);
        }
    }
    ResourceContent rc;
    rc.uri = uri;
    rc.error = "server returned contents with no text or blob fields";
    return Result<ResourceContent>::ok(rc);
}

Result<std::vector<ExternalPrompt>> McpClient::list_prompts() {
    auto result = send_request("prompts/list", {});
    if (result.is_err())
        return Result<std::vector<ExternalPrompt>>::err(result.error());

    const QJsonObject& data = result.value();
    const QJsonArray arr = data["prompts"].toArray();

    std::vector<ExternalPrompt> out;
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (const auto& item : arr) {
        const QJsonObject p = item.toObject();
        ExternalPrompt ep;
        ep.server_id = config_.id;
        ep.name = p["name"].toString();
        ep.description = p["description"].toString();
        for (const auto& a : p["arguments"].toArray()) {
            const QJsonObject obj = a.toObject();
            PromptArgument pa;
            pa.name = obj["name"].toString();
            pa.description = obj["description"].toString();
            pa.required = obj.value("required").toBool(false);
            ep.arguments.push_back(std::move(pa));
        }
        out.push_back(std::move(ep));
    }
    return Result<std::vector<ExternalPrompt>>::ok(std::move(out));
}

Result<PromptResult> McpClient::get_prompt(const QString& name,
                                           const QHash<QString, QString>& args) {
    QJsonObject params;
    params["name"] = name;
    if (!args.isEmpty()) {
        QJsonObject jargs;
        for (auto it = args.constBegin(); it != args.constEnd(); ++it)
            jargs[it.key()] = it.value();
        params["arguments"] = jargs;
    }
    auto result = send_request("prompts/get", params);
    if (result.is_err()) {
        PromptResult pr;
        pr.error = QString::fromStdString(result.error());
        return Result<PromptResult>::ok(pr);
    }

    // MCP spec: { description?, messages: [{role, content: {type, text}}] }
    PromptResult pr;
    const QJsonObject& data = result.value();
    pr.description = data.value("description").toString();
    for (const auto& m : data.value("messages").toArray()) {
        const QJsonObject obj = m.toObject();
        PromptMessage pm;
        pm.role = obj.value("role").toString();
        // Content can be a string (legacy) OR { type: "text", text: "..." }
        const auto content = obj.value("content");
        if (content.isString()) {
            pm.text = content.toString();
        } else if (content.isObject()) {
            const QJsonObject cobj = content.toObject();
            pm.text = cobj.value("text").toString();
        }
        pr.messages.push_back(std::move(pm));
    }
    return Result<PromptResult>::ok(pr);
}

Result<QJsonObject> McpClient::call_tool(const QString& name, const QJsonObject& args) {
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    // 120s timeout for tool execution — external tools like database queries
    // (Postgres, MySQL, etc.) can take significantly longer than 30s,
    // especially for schema introspection or large result sets.
    return send_request("tools/call", params, 120000);
}

Result<void> McpClient::ping() {
    auto r = send_request("ping", {}, 5000);
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

// ============================================================================
// JSON-RPC internals
// ============================================================================

Result<QJsonObject> McpClient::send_request(const QString& method, const QJsonObject& params, int timeout_ms) {
    if (!is_running())
        return Result<QJsonObject>::err("Server not running: " + config_.name.toStdString());

    int id;
    PendingRequest pending;

    {
        QMutexLocker lock(&rpc_mutex_);
        id = next_id_++;
        pending_[id] = &pending;
    }

    // Build JSON-RPC 2.0 request
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = id;
    req["method"] = method;
    if (!params.isEmpty())
        req["params"] = params;

    LOG_INFO(TAG, "RPC → " + method + " (id=" + QString::number(id) + ")");

    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";

    // Write to process on its worker thread
    QMetaObject::invokeMethod(process_, [this, line]() { process_->write(line); }, Qt::QueuedConnection);

    // Wait for response via QWaitCondition. QProcess lives on its own
    // worker thread with an event loop, so readyRead signals fire there
    // and handle_line() wakes us up via rpc_cond_.
    QElapsedTimer timer;
    timer.start();

    {
        QMutexLocker lock(&rpc_mutex_);
        if (!pending.completed) {
            rpc_cond_.wait(&rpc_mutex_, static_cast<unsigned long>(timeout_ms));
        }
    }

    {
        QMutexLocker lock(&rpc_mutex_);
        pending_.remove(id);
    }

    if (!pending.error.isEmpty())
        return Result<QJsonObject>::err(pending.error.toStdString());
    if (!pending.completed)
        return Result<QJsonObject>::err("Timeout waiting for: " + method.toStdString());

    LOG_INFO(TAG, "RPC ← " + method + " OK (" + QString::number(timer.elapsed()) + "ms)");
    return Result<QJsonObject>::ok(pending.response);
}

void McpClient::handle_line(const QByteArray& line) {
    if (line.trimmed().isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return;

    QJsonObject obj = doc.object();
    if (!obj.contains("id")) {
        // JSON-RPC notification — no `id`, no response expected.  Route
        // to the registered handler if any.  Common methods:
        //   `notifications/message`  — server-side structured log
        //   `notifications/resources/list_changed`
        //   `notifications/tools/list_changed`
        //   `notifications/prompts/list_changed`
        //   `notifications/progress`  — progress updates for in-flight calls
        if (notification_handler_) {
            const QString method = obj.value("method").toString();
            const QJsonObject params = obj.value("params").toObject();
            notification_handler_(method, params);
        }
        return;
    }

    // JSON-RPC request from the server (has `id` + `method`) — distinct
    // from responses (which have `id` + `result` / `error`).  Route
    // through the handler, send the response back on the same pipe.
    // Common methods:
    //   `sampling/createMessage`  — server wants client to invoke LLM
    //   `elicitation/create`      — server wants client to ask user
    //
    // Threading caveat: the handler runs synchronously on the QProcess
    // read-loop thread.  A slow handler (e.g. LlmService::chat doing
    // a multi-second LLM round-trip) blocks parsing of subsequent
    // messages from this server for the duration.  In practice the
    // server is also blocked waiting for our response, so this isn't
    // a deadlock — just a latency cost on side-channel notifications
    // (progress, log lines) from the same server.  If that latency
    // becomes a problem, dispatch the handler via QtConcurrent and
    // ferry the response back via QueuedConnection.
    if (obj.contains("method")) {
        const QString method = obj.value("method").toString();
        const QJsonObject params = obj.value("params").toObject();
        const QJsonValue req_id = obj.value("id");

        QJsonObject response;
        response["jsonrpc"] = QStringLiteral("2.0");
        response["id"] = req_id;
        if (server_request_handler_) {
            auto r = server_request_handler_(method, params);
            if (r.is_ok()) {
                response["result"] = r.value();
            } else {
                QJsonObject err;
                err["code"] = -32603;  // Internal error
                err["message"] = QString::fromStdString(r.error());
                response["error"] = err;
            }
        } else {
            QJsonObject err;
            err["code"] = -32601;  // Method not found
            err["message"] = QString("no server-request handler registered for %1").arg(method);
            response["error"] = err;
        }
        const QByteArray line = QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n";
        QMetaObject::invokeMethod(process_, [this, line]() { process_->write(line); }, Qt::QueuedConnection);
        return;
    }

    int id = obj["id"].toInt(-1);
    if (id < 0)
        return;

    QMutexLocker lock(&rpc_mutex_);
    PendingRequest* req = pending_.value(id, nullptr);
    if (!req)
        return;

    if (obj.contains("error")) {
        req->error = obj["error"].toObject()["message"].toString("RPC error");
    } else {
        req->response = obj["result"].toObject();
    }
    req->completed = true;
    rpc_cond_.wakeAll();
}

void McpClient::on_ready_read() {
    while (process_ && process_->canReadLine()) {
        QByteArray line = process_->readLine();
        append_log("[stdout] " + QString::fromUtf8(line).trimmed());
        handle_line(line);
    }
}

QStringList McpClient::get_logs() const {
    QMutexLocker lock(&log_mutex_);
    return log_lines_;
}

void McpClient::append_log(const QString& line) {
    QMutexLocker lock(&log_mutex_);
    log_lines_.append(line);
    if (log_lines_.size() > MAX_LOG_LINES)
        log_lines_.removeFirst();
}

void McpClient::on_finished(int exit_code, QProcess::ExitStatus) {
    running_ = false;
    LOG_WARN(TAG, QString("MCP server '%1' exited with code %2").arg(config_.name).arg(exit_code));

    // Wake pending requests
    QMutexLocker lock(&rpc_mutex_);
    for (auto* req : pending_) {
        req->error = "Server process exited";
        req->completed = true;
    }
    rpc_cond_.wakeAll();
}

} // namespace fincept::mcp
