// src/ai_chat/ElicitPrompter.h
#pragma once
// The process-wide elicitation surface. MCP tools (and the authorization gate
// for destructive tools) ask the user questions through ElicitBridge; this
// object is the UI side of that bridge: it subscribes once, renders each
// request as a modal shaped by the request's JSON-Schema, and resolves the
// bridge with the answer.
//
// It exists because elicitation must not depend on any particular screen
// being open. It used to live inside the AI chat screen, so a tool prompt
// fired from the equity chat, the bubble or an agent run errored with "open
// the chat screen" unless that screen happened to be mounted. One always-on
// QObject owned by MainWindow removes the coupling.

#include "mcp/McpTypes.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

namespace fincept::ai_chat {

class ElicitPrompter : public QObject {
    Q_OBJECT
  public:
    /// `dialog_parent` anchors the modals (the main window). The prompter
    /// subscribes to ElicitBridge in the constructor and stays subscribed for
    /// the life of the application.
    explicit ElicitPrompter(QWidget* dialog_parent, QObject* parent = nullptr);

  private slots:
    void on_request(const QString& token, const fincept::mcp::ElicitRequest& req);

  private:
    QPointer<QWidget> dialog_parent_;
};

} // namespace fincept::ai_chat
