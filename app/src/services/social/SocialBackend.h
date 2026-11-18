#pragma once
// SocialBackend.h — one call to app/scripts/social_app.py, the owner of the
// Social screen's posting, drafts and channel state; read by the Social
// screen and its BRIEFS panel.

#include <QJsonObject>
#include <QString>

#include <functional>

class QObject;

namespace finterm::services::social_backend {

/// How a backend call ended. For a post the difference matters: a process
/// that never started posted nothing; one that ran and gave no answer
/// (killed, crashed) may have posted before it could say so.
enum class Outcome { Answered, NoAnswer, NotStarted };

/// @p done gets the answer object, an error (the backend's own {error}, or
/// why there was none) and how the call ended.
using Done = std::function<void(const QJsonObject& answer, const QString& error, Outcome outcome)>;

/// One call in a process of its own; the payload goes to stdin (it may carry
/// secrets — never argv). @p done is dropped once @p ctx is gone; the
/// process is not owned by it (closing a dialog must not kill a publish).
void call_backend(QObject* ctx, const QString& action, const QJsonObject& payload, int timeout_ms, Done done);

} // namespace finterm::services::social_backend
