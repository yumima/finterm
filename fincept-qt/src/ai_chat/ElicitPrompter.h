// src/ai_chat/ElicitPrompter.h
#pragma once
// The process-wide elicitation surface. MCP tools (and the authorization gate
// for destructive tools) ask the user questions through ElicitBridge; this
// object is the UI side of that bridge: it subscribes once, renders each
// request as a modal shaped by the request's JSON-Schema, and resolves the
// bridge with the answer.
//
// It exists because elicitation must not depend on any particular screen —
// or window — being open. It used to live inside the AI chat screen, so a
// tool prompt fired from the equity chat, the bubble or an agent run errored
// with "open the chat screen" unless that screen happened to be mounted. One
// qApp-owned singleton removes the coupling: it outlives every MainWindow
// (closing the first window must not kill elicitation for the second), and
// each modal anchors to whichever window is active when the request lands.

#include "mcp/McpTypes.h"

#include <QObject>

namespace fincept::ai_chat {

class ElicitPrompter : public QObject {
    Q_OBJECT
  public:
    /// Mount the process's one prompter (idempotent). Called by every
    /// MainWindow constructor; only the first call creates it, and the
    /// instance is parented to qApp, not to the calling window.
    static void ensure_mounted();

  private slots:
    void on_request(const QString& token, const fincept::mcp::ElicitRequest& req);

  private:
    ElicitPrompter();
};

} // namespace fincept::ai_chat
