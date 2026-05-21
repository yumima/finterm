#include "mcp/McpHttpClient.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace fincept::mcp {

namespace {
constexpr const char* TAG = "McpHttpClient";
}

McpHttpClient::McpHttpClient(const McpServerConfig& config, QObject* parent)
    : McpClientBase(parent), config_(config) {}

McpHttpClient::~McpHttpClient() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────

Result<void> McpHttpClient::start() {
    if (running_.load())
        return Result<void>::ok();

    if (config_.base_url.isEmpty()) {
        return Result<void>::err("HTTP MCP server missing base_url");
    }
    QUrl url(config_.base_url);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return Result<void>::err("HTTP MCP server has an invalid base_url: " +
                                 config_.base_url.toStdString());
    }
    if (config_.auth_scheme == QStringLiteral("oauth")) {
        return Result<void>::err(
            "auth_scheme='oauth' not supported in this build — Track 4 #14b "
            "lands OAuth+DCR");
    }

    if (!nam_)
        nam_ = new QNetworkAccessManager(this);
    running_.store(true);
    append_log(QStringLiteral("started (base_url=%1, auth=%2)")
                   .arg(config_.base_url, config_.auth_scheme));
    LOG_INFO(TAG, QString("HTTP MCP client started: %1 (%2)")
                      .arg(config_.name, config_.base_url));
    return Result<void>::ok();
}

void McpHttpClient::stop() {
    if (!running_.exchange(false))
        return;
    if (nam_) {
        nam_->deleteLater();
        nam_ = nullptr;
    }
    append_log(QStringLiteral("stopped"));
    LOG_INFO(TAG, QString("HTTP MCP client stopped: %1").arg(config_.name));
}

bool McpHttpClient::is_running() const {
    return running_.load();
}

// ─────────────────────────────────────────────────────────────────────
// MCP protocol methods
// ─────────────────────────────────────────────────────────────────────

Result<QJsonObject> McpHttpClient::initialize() {
    QJsonObject params;
    params["protocolVersion"] = QStringLiteral("2025-03-26");
    QJsonObject caps;
    caps["tools"] = QJsonObject{};
    params["capabilities"] = caps;
    QJsonObject info;
    info["name"] = QStringLiteral("finterm");
    info["version"] = QStringLiteral("0.1.0");
    params["clientInfo"] = info;
    return send_request(QStringLiteral("initialize"), params);
}

Result<std::vector<ExternalTool>> McpHttpClient::list_tools() {
    auto r = send_request(QStringLiteral("tools/list"), {});
    if (r.is_err())
        return Result<std::vector<ExternalTool>>::err(r.error());

    const QJsonObject result = r.value();
    const QJsonArray arr = result.value("tools").toArray();
    std::vector<ExternalTool> tools;
    tools.reserve(static_cast<std::size_t>(arr.size()));
    for (const auto& v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject obj = v.toObject();
        ExternalTool t;
        t.server_id = config_.id;
        t.name = obj.value("name").toString();
        t.description = obj.value("description").toString();
        t.input_schema = obj.value("inputSchema").toObject();
        tools.push_back(std::move(t));
    }
    return Result<std::vector<ExternalTool>>::ok(std::move(tools));
}

Result<QJsonObject> McpHttpClient::call_tool(const QString& name, const QJsonObject& args) {
    QJsonObject params;
    params["name"] = name;
    params["arguments"] = args;
    return send_request(QStringLiteral("tools/call"), params);
}

Result<void> McpHttpClient::ping() {
    auto r = send_request(QStringLiteral("ping"), {}, 5000);
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

// ─────────────────────────────────────────────────────────────────────
// HTTP plumbing
// ─────────────────────────────────────────────────────────────────────

Result<QJsonObject> McpHttpClient::send_request(const QString& method,
                                                const QJsonObject& params,
                                                int timeout_ms) {
    if (!running_.load() || !nam_) {
        return Result<QJsonObject>::err("HTTP MCP client not started");
    }

    QJsonObject envelope;
    envelope["jsonrpc"] = QStringLiteral("2.0");
    envelope["id"] = next_id_.fetch_add(1);
    envelope["method"] = method;
    if (!params.isEmpty())
        envelope["params"] = params;
    const QByteArray body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);

    QNetworkRequest req(QUrl(config_.base_url));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    if (!apply_auth(req)) {
        return Result<QJsonObject>::err("auth setup failed (see logs)");
    }

    append_log(QStringLiteral("→ %1 %2").arg(method, QString::fromUtf8(body)).left(MAX_LOG_LINES));

    QNetworkReply* reply = nam_->post(req, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timed_out = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timed_out = true;
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeout_ms);
    loop.exec();

    if (timed_out) {
        reply->abort();
        reply->deleteLater();
        const QString msg = QStringLiteral("timeout after %1 ms on %2").arg(timeout_ms).arg(method);
        append_log(msg);
        return Result<QJsonObject>::err(msg.toStdString());
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray response_body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        const QString msg = QStringLiteral("network error %1 (status=%2): %3")
                                .arg(static_cast<int>(err))
                                .arg(status)
                                .arg(QString::fromUtf8(response_body).left(200));
        append_log(msg);
        return Result<QJsonObject>::err(msg.toStdString());
    }

    QJsonParseError parse_err;
    const QJsonDocument doc = QJsonDocument::fromJson(response_body, &parse_err);
    if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString msg = QStringLiteral("invalid JSON response: %1").arg(parse_err.errorString());
        append_log(msg);
        return Result<QJsonObject>::err(msg.toStdString());
    }
    const QJsonObject obj = doc.object();
    append_log(QStringLiteral("← %1 (status=%2, %3 bytes)")
                   .arg(method)
                   .arg(status)
                   .arg(response_body.size()));

    if (obj.contains("error")) {
        const QJsonObject ej = obj.value("error").toObject();
        const QString msg = QStringLiteral("server error %1: %2")
                                .arg(ej.value("code").toInt())
                                .arg(ej.value("message").toString());
        return Result<QJsonObject>::err(msg.toStdString());
    }
    return Result<QJsonObject>::ok(obj.value("result").toObject());
}

bool McpHttpClient::apply_auth(QNetworkRequest& req) {
    const QString scheme = config_.auth_scheme.isEmpty() ? QStringLiteral("none")
                                                         : config_.auth_scheme;
    if (scheme == QStringLiteral("none"))
        return true;

    if (scheme == QStringLiteral("oauth")) {
        append_log(QStringLiteral("oauth scheme requested but not supported in 14a"));
        return false;
    }

    // Pull the auth value from SecureStorage at request time.
    const QString key = secure_key_for_server(config_.id);
    auto r = SecureStorage::instance().retrieve(key);
    if (r.is_err() || r.value().isEmpty()) {
        append_log(QStringLiteral("auth value missing in SecureStorage: %1").arg(key));
        return false;
    }
    const QString value = r.value();

    if (scheme == QStringLiteral("bearer")) {
        req.setRawHeader("Authorization", ("Bearer " + value).toUtf8());
        return true;
    }
    if (scheme == QStringLiteral("api_key")) {
        const QString header_name = config_.auth_header.isEmpty()
                                        ? QStringLiteral("X-API-Key")
                                        : config_.auth_header;
        req.setRawHeader(header_name.toUtf8(), value.toUtf8());
        return true;
    }
    append_log(QStringLiteral("unknown auth_scheme: %1").arg(scheme));
    return false;
}

// ─────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────

QString McpHttpClient::secure_key_for_server(const QString& server_id) {
    return QStringLiteral("mcp.auth.") + server_id;
}

QStringList McpHttpClient::get_logs() const {
    QMutexLocker lock(&log_mutex_);
    return log_lines_;
}

void McpHttpClient::append_log(const QString& line) {
    const QString stamped = QDateTime::currentDateTime().toString(Qt::ISODate) + " " + line;
    QMutexLocker lock(&log_mutex_);
    log_lines_.append(stamped);
    if (log_lines_.size() > MAX_LOG_LINES)
        log_lines_.removeFirst();
}

} // namespace fincept::mcp
