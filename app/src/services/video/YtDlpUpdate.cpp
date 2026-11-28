#include "services/video/YtDlpUpdate.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"
#include "services/video/YtDlp.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

namespace finterm::services::video {

void refresh_ytdlp(bool after_failure) {
    auto& mgr = python::PythonSetupManager::instance();
    const QString python = mgr.python_path(QStringLiteral("venv-numpy2"));
    if (!QFileInfo::exists(python) || !QFileInfo::exists(mgr.uv_path()))
        return; // setup has not run: it installs yt-dlp itself
    // Only the copy this environment owns: one next to the binary or on PATH
    // is the user's (or the build's) to keep.
    if (!ytdlp_program().startsWith(AppPaths::root()))
        return;
    static bool running = false;
    if (running)
        return;
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    const QDateTime last = s.value(QStringLiteral("video/ytdlp_refreshed_at")).toDateTime();
    const qint64 age = last.isValid() ? last.secsTo(QDateTime::currentDateTimeUtc()) : -1;
    if (age >= 0 && age < (after_failure ? 3600 : 86400))
        return;
    s.setValue(QStringLiteral("video/ytdlp_refreshed_at"), QDateTime::currentDateTimeUtc());
    s.sync();

    running = true;
    auto* proc = new QProcess(qApp);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("UV_PYTHON_INSTALL_DIR"), mgr.install_dir() + QStringLiteral("/python"));
    proc->setProcessEnvironment(env);
    QObject::connect(proc, &QProcess::finished, proc, [proc](int code, QProcess::ExitStatus st) {
        running = false;
        if (st == QProcess::NormalExit && code == 0)
            LOG_INFO("YtDlp", "yt-dlp is up to date");
        else
            LOG_WARN("YtDlp", "Could not upgrade yt-dlp: " +
                                  QString::fromUtf8(proc->readAllStandardError()).trimmed().right(300));
        proc->deleteLater();
    });
    QObject::connect(proc, &QProcess::errorOccurred, proc, [proc](QProcess::ProcessError) {
        if (proc->state() == QProcess::NotRunning) {
            running = false;
            proc->deleteLater();
        }
    });
    LOG_INFO("YtDlp", after_failure ? "A stream could not be resolved: upgrading yt-dlp" : "Upgrading yt-dlp");
    proc->start(mgr.uv_path(), {QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--python"), python,
                                QStringLiteral("--upgrade"), QStringLiteral("yt-dlp[default]")});
}

} // namespace finterm::services::video
