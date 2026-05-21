#pragma once
// McpHttpClient.h — JSON-RPC 2.0 over HTTP for external MCP servers.
//
// Track 4 #14a.  Sibling of the stdio McpClient.  Each protocol
// method (initialize, list_tools, call_tool, ping) issues one POST
// to config.base_url with the JSON-RPC envelope; the response body
// carries the JSON-RPC result or error.  Synchronous via QEventLoop
// (matches the existing finterm sync-HTTP pattern in LlmService);
// no streaming SSE in this revision — many simple HTTP MCP servers
// respond synchronously, and the Streamable HTTP spec's SSE channel
// can layer in later without breaking this interface.
//
// Static auth schemes (Track 4 #14a):
//   none    no auth header
//   bearer  Authorization: Bearer <token>
//   api_key <header>: <key>             (header defaults to X-API-Key
//                                        when config.auth_header blank)
//   oauth   rejected — Track 4 #14b lands OAuth+DCR
//
// The auth value (token / key) is pulled from SecureStorage at
// send-time under id 'mcp.auth.<server_id>'.  Not stored in DB.

#include "mcp/McpClientBase.h"

#include <QMutex>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>

#include <atomic>

namespace fincept::mcp {

class McpHttpClient : public McpClientBase {
    Q_OBJECT
  public:
    explicit McpHttpClient(const McpServerConfig& config, QObject* parent = nullptr);
    ~McpHttpClient() override;

    // Lifecycle
    Result<void> start() override;
    void stop() override;
    bool is_running() const override;

    // MCP protocol methods (blocking; must NOT be called from UI thread)
    Result<QJsonObject> initialize() override;
    Result<std::vector<ExternalTool>> list_tools() override;
    Result<QJsonObject> call_tool(const QString& name, const QJsonObject& args) override;
    Result<void> ping() override;

    const McpServerConfig& config() const override { return config_; }

    /// Returns recent request/response log lines (last 500).
    QStringList get_logs() const override;

    /// SecureStorage key id for this server's auth value.
    /// Format: "mcp.auth.<server_id>" — matches the LlmSecureKeys pattern.
    static QString secure_key_for_server(const QString& server_id);

  private:
    Result<QJsonObject> send_request(const QString& method,
                                     const QJsonObject& params,
                                     int timeout_ms = 30000);

    /// Adds Authorization / custom-header auth based on config_.auth_scheme.
    /// Returns false (and logs) when the scheme is unsupported (oauth in
    /// 14a) or required key is missing from SecureStorage.
    bool apply_auth(class QNetworkRequest& req);

    void append_log(const QString& line);

    McpServerConfig config_;
    QNetworkAccessManager* nam_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<int> next_id_{1};

    static constexpr int MAX_LOG_LINES = 500;
    mutable QMutex log_mutex_;
    QStringList log_lines_;
};

} // namespace fincept::mcp
