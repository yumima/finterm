#include "services/social/SocialBackend.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace finterm::services::social_backend {

namespace {
constexpr const char* kScript = "social_app.py";

/// The interpreter for social_app.py. The backend reads the posting
/// credentials through libsecret's PyGObject binding (finterm_keyring.py),
/// which exists only as the distro's python3-gi, compiled for the distro's
/// CPython. The app's own UV venv is isolated and a different CPython, so
/// under PythonRunner every keyring read failed ("libsecret not available")
/// and no channel could ever read as connected. The backend needs nothing
/// else outside the standard library.
QString backend_python() {
#if defined(Q_OS_LINUX)
    static const QString system_python =
        QStandardPaths::findExecutable(QStringLiteral("python3"), {QStringLiteral("/usr/bin"), QStringLiteral("/bin")});
    if (!system_python.isEmpty())
        return system_python;
#endif
    return python::PythonRunner::instance().python_path();
}
} // namespace

/// One backend call, in a process of its own rather than PythonRunner's
/// queue: a confirmed post must not wait behind scrapers holding every
/// slot. The process is not owned by @p ctx — closing the screen or a
/// dialog must not kill a post mid-flight — and @p done is dropped once
/// @p ctx is gone.
///
/// The payload goes to the process's stdin, never its command line: argv is
/// readable by every local user (ps, /proc/<pid>/cmdline) and payloads carry
/// PINs, codes and passwords. social_app.py refuses those actions' payloads
/// from argv.
void call_backend(QObject* ctx, const QString& action, const QJsonObject& payload, int timeout_ms, Done done) {
    QPointer<QObject> guard(ctx);
    auto& runner = python::PythonRunner::instance();
    const QString python = backend_python();
    const QString script = QDir(runner.scripts_dir()).filePath(QString::fromLatin1(kScript));
    auto not_started = [guard, action, done](const QString& why) {
        LOG_WARN("Social", action + QStringLiteral(": ") + why);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [guard, done, why]() {
                if (guard)
                    done({}, why, Outcome::NotStarted);
            },
            Qt::QueuedConnection);
    };
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        not_started(python.isEmpty() ? QStringLiteral("no Python interpreter found")
                                     : QStringLiteral("%1 is missing from the app's scripts").arg(kScript));
        return;
    }

    auto* proc = new QProcess(QCoreApplication::instance());
    proc->setProcessEnvironment(runner.build_python_env());
    proc->setWorkingDirectory(runner.scripts_dir());
    auto* timer = new QTimer(proc);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, proc, [proc, action, timeout_ms]() {
        LOG_WARN("Social", QStringLiteral("%1 timed out after %2s — killing it").arg(action).arg(timeout_ms / 1000));
        proc->kill();
    });
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, not_started](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return; // the rest end in finished()
        not_started(QStringLiteral("could not start Python (%1)").arg(proc->errorString()));
        proc->deleteLater();
    });
    QObject::connect(
        proc, &QProcess::finished, proc, [proc, timer, guard, action, done](int exit_code, QProcess::ExitStatus) {
            timer->stop();
            proc->deleteLater();
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            const QString errout = QString::fromUtf8(proc->readAllStandardError());
            if (!guard)
                return;
            const QJsonDocument doc = QJsonDocument::fromJson(python::extract_json(out).toUtf8());
            const QJsonObject o = doc.object();
            const bool answered = doc.isObject() && !o.isEmpty();
            QString err = o.value(QLatin1String("error")).toString();
            if (err.isEmpty() && !answered) {
                // The tail of stderr is the part that says what happened.
                const QString tail = errout.trimmed().section(QLatin1Char('\n'), -1).left(200);
                err = tail.isEmpty() ? QStringLiteral("the social backend gave no answer (exit %1)").arg(exit_code)
                                     : tail;
            }
            if (!err.isEmpty())
                LOG_WARN("Social", action + QStringLiteral(": ") + err);
            done(o, err, answered ? Outcome::Answered : Outcome::NoAnswer);
        });
    const QByteArray input = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QObject::connect(proc, &QProcess::started, proc, [proc, input]() {
        proc->write(input);
        proc->closeWriteChannel(); // EOF once written: the backend reads to the end
    });
    proc->start(python, {script, action, QStringLiteral("-")});
    if (timeout_ms > 0)
        timer->start(timeout_ms);
}

} // namespace finterm::services::social_backend
