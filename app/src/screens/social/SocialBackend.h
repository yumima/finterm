#pragma once
// SocialBackend.h — one call to app/scripts/social_app.py, shared by the
// Social screen and its BRIEFS panel. Implemented in SocialScreen.cpp.

#include <QJsonObject>
#include <QString>

#include <functional>

class QObject;

namespace finterm::screens::social_backend {

/// How a backend call ended. For a post the difference matters: a process
/// that never started posted nothing; one that ran and gave no answer
/// (killed, crashed) may have posted before it could say so.
enum class Outcome { Answered, NoAnswer, NotStarted };

/// @p done gets the answer object, an error (the backend's own {error}, or
/// why there was none) and how the call ended.
using Done = std::function<void(const QJsonObject& answer, const QString& error, Outcome outcome)>;

/// The interpreter social_app.py runs under (the system Python on Linux:
/// the keyring binding lives there).
QString backend_python();

/// One call in a process of its own; the payload goes to stdin (it may carry
/// secrets — never argv). @p done is dropped once @p ctx is gone; the
/// process is not owned by it (closing a dialog must not kill a publish).
void call_backend(QObject* ctx, const QString& action, const QJsonObject& payload, int timeout_ms, Done done);

} // namespace finterm::screens::social_backend
