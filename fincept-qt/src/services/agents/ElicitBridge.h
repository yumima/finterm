#pragma once
// ElicitBridge.h — cross-thread bridge between MCP elicitation
// (worker thread) and the chat UI (main thread).
//
// Tool handlers call `ctx.elicit(req)` from McpProvider's worker
// pool.  The default handler installed by AgentService routes to
// `ElicitBridge::instance().request(req)`, which:
//
//   1. Records the in-flight request under a token.
//   2. Emits `request_received(token, req)` on the main thread
//      (Qt::QueuedConnection from the worker call).
//   3. Blocks the worker on a wait condition.
//   4. When the UI calls `resolve(token, response)`, unblocks the
//      worker and returns the response from `request()`.
//
// AiChatScreen connects to `request_received` on construction and
// shows a modal that calls `resolve` with the user's answer (or a
// "declined" response when the user dismisses).
//
// When no UI is attached, `request()` returns immediately with an
// error — tools degrade with a clear message instead of hanging.
//
// One concurrent request at a time in v1.  A second request while
// the first is open returns the error "elicitation already in
// progress" — tools handle that gracefully.  Multi-request
// queueing can come later.

#include "mcp/McpTypes.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

namespace fincept::services {

class ElicitBridge : public QObject {
    Q_OBJECT
  public:
    static ElicitBridge& instance();

    /// Worker-thread side.  Blocks until UI resolves or the wait
    /// times out (default 5 minutes — agents shouldn't wait forever).
    /// Thread-safe.
    mcp::ElicitResponse request(const mcp::ElicitRequest& req, int timeout_ms = 5 * 60 * 1000);

    /// True when at least one subscriber is connected to
    /// request_received.  AgentService uses this to decide whether
    /// the bridge or the error-stub should be the default handler.
    bool has_subscriber() const;

    ElicitBridge(const ElicitBridge&) = delete;
    ElicitBridge& operator=(const ElicitBridge&) = delete;

  public slots:
    /// UI-thread side.  Called after the user answers the modal.
    /// `token` matches the value passed to `request_received`.
    /// `response.declined` should be true when the user dismissed
    /// without answering.
    void resolve(const QString& token, const mcp::ElicitResponse& response);

  signals:
    /// Emitted on the main thread when a worker calls `request()`.
    /// Subscribers show a modal and call `resolve(token, …)` when
    /// the user is done.
    void request_received(const QString& token, const mcp::ElicitRequest& req);

  private:
    ElicitBridge();

    mutable QMutex mutex_;
    QWaitCondition cv_;
    QString pending_token_;
    mcp::ElicitResponse pending_response_;
    bool response_ready_ = false;
};

} // namespace fincept::services
