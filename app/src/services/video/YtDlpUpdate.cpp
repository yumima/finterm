#include "services/video/YtDlpUpdate.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"
#include "services/video/YtDlp.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
    // The pins stay: the requirements constrain the upgrade, so one that would
    // move a pinned package fails instead. (Its own constraints file: the
    // requirements without yt-dlp's line — a constraint can't carry extras.)
    const QString requirements = mgr.find_requirements_file(QStringLiteral("requirements-numpy2.txt"));
    QFile in(requirements);
    if (requirements.isEmpty() || !in.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QStringList pins;
    for (const QString& raw : QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'))) {
        const QString line = raw.section(QLatin1Char('#'), 0, 0).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1String("yt-dlp")) && !line.startsWith(QLatin1Char('-')))
            pins << line;
    }
    const QString constraints = AppPaths::root() + QStringLiteral("/ytdlp-constraints.txt");
    QSaveFile out(constraints);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    out.write((pins.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (!out.commit())
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
    // These two only (--upgrade-package), within the pins (-c).
    proc.setProgram(mgr.uv_path());
    proc.setArguments({QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("--python"), python,
                       QStringLiteral("-c"), constraints, QStringLiteral("--upgrade-package"), QStringLiteral("yt-dlp"),
                       QStringLiteral("--upgrade-package"), QStringLiteral("yt-dlp-ejs"),
                       QStringLiteral("yt-dlp[default]")});
    // Out of the install folder (an update renames it), its own log (what
    // went wrong, if it does), not finterm's terminal.
    proc.setWorkingDirectory(AppPaths::root());
    const QString log = AppPaths::logs() + QStringLiteral("/ytdlp-upgrade.log");
    proc.setStandardInputFile(QProcess::nullDevice());
    proc.setStandardOutputFile(log, QIODevice::Truncate);
    proc.setStandardErrorFile(log, QIODevice::Append);
    if (proc.startDetached())
        LOG_INFO("YtDlp", "Upgrading yt-dlp in the background (" + log + ")");
    else
        LOG_WARN("YtDlp", "Could not start the yt-dlp upgrade");
}

} // namespace finterm::services::video
