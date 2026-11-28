#include "services/video/YtDlpUpdate.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"
#include "services/video/YtDlp.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

namespace finterm::services::video {

void refresh_ytdlp() {
    auto& mgr = python::PythonSetupManager::instance();
    const QString python = mgr.python_path(QStringLiteral("venv-numpy2"));
    if (!QFileInfo::exists(python) || !QFileInfo::exists(mgr.uv_path()))
        return; // setup has not run: it installs yt-dlp itself
    // Only the copy this environment owns: one next to the binary or on PATH
    // is the user's (or the build's) to keep.
    if (!ytdlp_program().startsWith(AppPaths::root()))
        return;
    QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    const QDateTime last = s.value(QStringLiteral("video/ytdlp_refreshed_at")).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 86400)
        return;
    s.setValue(QStringLiteral("video/ytdlp_refreshed_at"), QDateTime::currentDateTimeUtc());
    s.sync();

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& kv : mgr.uv_env_extra())
        env.insert(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
    proc.setProcessEnvironment(env);
#ifdef _WIN32
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    // These two only (--upgrade-package): their dependencies move only if they
    // must, so the versions requirements-numpy2.txt pins stay.
    proc.setProgram(mgr.uv_path());
    proc.setArguments({QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--python"), python,
                       QStringLiteral("--upgrade-package"), QStringLiteral("yt-dlp"),
                       QStringLiteral("--upgrade-package"), QStringLiteral("yt-dlp-ejs"),
                       QStringLiteral("yt-dlp[default]")});
    if (proc.startDetached())
        LOG_INFO("YtDlp", "Upgrading yt-dlp (in the background)");
    else
        LOG_WARN("YtDlp", "Could not start the yt-dlp upgrade");
}

} // namespace finterm::services::video
