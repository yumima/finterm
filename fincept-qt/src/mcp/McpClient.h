#pragma once
// McpClient.h — JSON-RPC 2.0 over stdio for external MCP servers (Qt port)
// Spawns an external MCP server process and communicates via stdin/stdout.
//
// Inherits from McpClientBase so McpManager can hold heterogeneous
// transports (stdio + HTTP) in a single shared_ptr<McpClientBase> map.
// McpServerConfig now lives in McpClientBase.h.

#include "mcp/McpClientBase.h"

#include <QByteArray>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

namespace fincept::mcp {

class McpClient : public McpClientBase {
    Q_OBJECT
  public:
    explicit McpClient(const McpServerConfig& config, QObject* parent = nullptr);
    ~McpClient() override;

    // Lifecycle
    // NOTE: start() must only be called from a background thread, never the UI thread.
    Result<void> start() override;
    void stop() override;
    bool is_running() const override;

    // MCP protocol methods (blocking, must NOT be called from UI thread)
    Result<QJsonObject> initialize() override;
    Result<std::vector<ExternalTool>> list_tools() override;
    Result<QJsonObject> call_tool(const QString& name, const QJsonObject& args) override;
    Result<void> ping() override;

    // MCP resources (Track 5 commit D)
    Result<std::vector<ExternalResource>> list_resources() override;
    Result<ResourceContent> read_resource(const QString& uri) override;

    // MCP prompts
    Result<std::vector<ExternalPrompt>> list_prompts() override;
    Result<PromptResult> get_prompt(const QString& name,
                                    const QHash<QString, QString>& args) override;

    const McpServerConfig& config() const override { return config_; }

    /// Returns captured stdout/stderr lines (last 500 lines).
    QStringList get_logs() const override;

  private:
    struct PendingRequest {
        QJsonObject response;
        bool completed = false;
        QString error;
    };

    McpServerConfig config_;
    QThread* worker_thread_ = nullptr;
    QProcess* process_ = nullptr;
    std::atomic<bool> running_{false};

    // Request tracking — guarded by rpc_mutex_
    mutable QMutex rpc_mutex_;
    QWaitCondition rpc_cond_;
    QHash<int, PendingRequest*> pending_;
    int next_id_ = 1;

    Result<QJsonObject> send_request(const QString& method, const QJsonObject& params, int timeout_ms = 30000);
    void handle_line(const QByteArray& line);
    void append_log(const QString& line);
    void cleanup_process(); // deletes process_ safely (call only after thread is stopped/not started)

    static constexpr int MAX_LOG_LINES = 500;
    mutable QMutex log_mutex_;
    QStringList log_lines_;

  private slots:
    void on_ready_read();
    void on_finished(int exit_code, QProcess::ExitStatus status);
};

} // namespace fincept::mcp
