#pragma once
// McpClientBase.h — abstract interface for MCP transports (Qt port).
//
// Two implementations:
//   - McpClient     JSON-RPC 2.0 over stdio (existing, the "subprocess"
//                   transport — npx / uvx / python-based servers).
//   - McpHttpClient JSON-RPC 2.0 over streamable HTTP (Track 4 #14a).
//
// McpManager picks one or the other per server config based on
// McpServerConfig.transport_type.
//
// McpServerConfig lives here (not in McpClient.h) so both transport
// implementations can include just this header without dragging in the
// stdio-specific subclass.

#include "core/result/Result.h"
#include "mcp/McpTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace fincept::mcp {

/// Configuration for one MCP server row.
///
/// Stdio servers use `command + args + env`.  HTTP servers (Track 4
/// #14a) use the `transport_type / base_url / auth_*` fields added by
/// migration v025.  Fields not relevant to a given transport are
/// ignored by that transport's implementation.
struct McpServerConfig {
    QString id;
    QString name;
    QString description;

    // Stdio transport.
    QString command; // e.g. "npx", "uvx", "python"
    QStringList args;
    QHash<QString, QString> env;

    QString category;
    bool enabled = true;
    bool auto_start = false;
    ServerStatus status = ServerStatus::Stopped;
    QString created_at;
    QString updated_at;
};

// No Q_OBJECT here — McpClientBase is a pure-interface abstract class
// with no signals or slots of its own.  Concrete subclasses (McpClient,
// McpHttpClient) carry Q_OBJECT and any per-transport signals.
// Skipping Q_OBJECT here means no MOC-generated .cpp is needed for the
// base, so it stays header-only.
class McpClientBase : public QObject {
  public:
    explicit McpClientBase(QObject* parent = nullptr) : QObject(parent) {}
    ~McpClientBase() override = default;

    // ── Lifecycle ──────────────────────────────────────────────────────
    // NOTE: start() must only be called from a background thread, never
    // the UI thread.
    virtual Result<void> start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    // ── MCP protocol methods ───────────────────────────────────────────
    // Synchronous, must NOT be called from the UI thread.
    virtual Result<QJsonObject> initialize() = 0;
    virtual Result<std::vector<ExternalTool>> list_tools() = 0;
    virtual Result<QJsonObject> call_tool(const QString& name, const QJsonObject& args) = 0;
    virtual Result<void> ping() = 0;

    virtual const McpServerConfig& config() const = 0;

    /// Returns captured stdout/stderr lines (stdio) or request/response
    /// log lines (http).  Last 500 lines max.
    virtual QStringList get_logs() const = 0;
};

} // namespace fincept::mcp
