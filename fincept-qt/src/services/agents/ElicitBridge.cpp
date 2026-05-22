#include "services/agents/ElicitBridge.h"

#include "core/logging/Logger.h"

#include <QMetaType>
#include <QUuid>

namespace fincept::services {

namespace {
constexpr const char* kElicitTag = "ElicitBridge";

// Register the MCP types so they cross threads via QueuedConnection.
// Idempotent — Qt's qRegisterMetaType handles re-registration.
void ensure_meta_types_registered() {
    static bool done = false;
    if (done)
        return;
    qRegisterMetaType<fincept::mcp::ElicitRequest>("fincept::mcp::ElicitRequest");
    qRegisterMetaType<fincept::mcp::ElicitResponse>("fincept::mcp::ElicitResponse");
    done = true;
}
} // namespace

ElicitBridge& ElicitBridge::instance() {
    static ElicitBridge s;
    return s;
}

ElicitBridge::ElicitBridge() {
    ensure_meta_types_registered();
}

bool ElicitBridge::has_subscriber() const {
    return receivers(SIGNAL(request_received(QString, mcp::ElicitRequest))) > 0;
}

mcp::ElicitResponse ElicitBridge::request(const mcp::ElicitRequest& req, int timeout_ms) {
    mcp::ElicitResponse out;

    if (!has_subscriber()) {
        out.error = "no elicitation surface mounted — open the chat screen "
                    "to enable interactive tool prompts";
        return out;
    }

    QMutexLocker lock(&mutex_);

    if (!pending_token_.isEmpty()) {
        // v1: one at a time.  Worker tool handler propagates the
        // error back to the model, which can retry or give up.
        out.error = "elicitation already in progress";
        return out;
    }

    pending_token_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending_response_ = mcp::ElicitResponse{};
    response_ready_ = false;
    const QString token = pending_token_;

    // QueuedConnection by default since subscribers live on the main
    // thread and we're on a worker thread.  The emit is the only
    // place where we touch the UI side.
    emit request_received(token, req);
    LOG_INFO(kElicitTag, QString("elicit request emitted (token=%1)").arg(token.left(8)));

    // Block until UI resolves or timeout.  Releases mutex during wait
    // so the UI thread can call resolve().
    const bool signalled = cv_.wait(&mutex_, timeout_ms);
    if (!signalled || !response_ready_) {
        out.error = signalled ? "elicit resolve fired without response"
                              : "elicit request timed out";
        pending_token_.clear();
        response_ready_ = false;
        return out;
    }

    out = pending_response_;
    pending_token_.clear();
    pending_response_ = mcp::ElicitResponse{};
    response_ready_ = false;
    return out;
}

void ElicitBridge::resolve(const QString& token, const mcp::ElicitResponse& response) {
    QMutexLocker lock(&mutex_);
    if (pending_token_.isEmpty() || pending_token_ != token) {
        LOG_WARN(kElicitTag, QString("resolve for unknown token %1 (current=%2)")
                          .arg(token.left(8), pending_token_.left(8)));
        return;
    }
    pending_response_ = response;
    response_ready_ = true;
    cv_.wakeAll();
}

} // namespace fincept::services
