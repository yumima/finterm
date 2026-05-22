#pragma once
// McpManager.h — External MCP server lifecycle management (Qt port)
// Manages multiple external MCP server instances with health checking.

#include "core/result/Result.h"
#include "mcp/McpClient.h"
#include "mcp/McpClientBase.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace fincept::mcp {

class McpManager : public QObject {
    Q_OBJECT
  public:
    static McpManager& instance();

    // ── Configuration ──────────────────────────────────────────────────

    /// Load server configs from the McpServerRepository
    void initialize();

    Result<void> save_server(const McpServerConfig& config);
    Result<void> remove_server(const QString& id);
    std::vector<McpServerConfig> get_servers() const;

    // ── Lifecycle ──────────────────────────────────────────────────────

    Result<void> start_server(const QString& id);
    Result<void> stop_server(const QString& id);
    Result<void> restart_server(const QString& id);
    void start_auto_servers();
    void stop_all();
    void shutdown();

    // ── Health Check ───────────────────────────────────────────────────

    void start_health_check(int interval_seconds = 60);
    void stop_health_check();

    // ── Tool Aggregation ───────────────────────────────────────────────

    std::vector<ExternalTool> get_all_external_tools();
    Result<QJsonObject> call_external_tool(const QString& server_id, const QString& tool_name, const QJsonObject& args);

    /// Returns captured log lines for a running server (empty if not running).
    QStringList get_logs(const QString& id) const;

    /// Install a process-wide default server-request handler that
    /// every freshly-started McpClient inherits.  Routes server-side
    /// `sampling/createMessage` and `elicitation/create` requests to
    /// whichever callbacks the caller wires up.  Called once from
    /// `AgentService::install_default_tool_handlers`.
    void set_default_server_request_handler(McpClientBase::ServerRequestHandler handler);

    McpManager(const McpManager&) = delete;
    McpManager& operator=(const McpManager&) = delete;

  signals:
    /// Emitted whenever server list or status changes (save/remove/start/stop).
    void servers_changed();

  private:
    McpManager();

    /// Per-transport client instances.  Each entry is either an
    /// McpClient (stdio) or an McpHttpClient (HTTP) — McpManager
    /// picks the concrete type at start_server() based on the
    /// transport_type field on the config.
    QHash<QString, std::shared_ptr<McpClientBase>> clients_;
    QHash<QString, McpServerConfig> configs_;
    QHash<QString, std::vector<ExternalTool>> tool_cache_;
    QHash<QString, int> restart_attempts_;
    McpClientBase::ServerRequestHandler default_server_request_handler_;

    QTimer* health_timer_ = nullptr;
    mutable QMutex mutex_;

    static constexpr int MAX_RESTART_ATTEMPTS = 3;

    McpClientBase* get_client(const QString& id) const;
    void refresh_tools_for(const QString& id);

  private slots:
    void do_health_check();
};

} // namespace fincept::mcp
