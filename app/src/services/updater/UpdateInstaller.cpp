#include "services/updater/UpdateInstaller.h"

#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QUrl>

namespace finterm::services::update_install {

namespace {

QString updates_dir() {
    const QString d = QDir(AppPaths::root()).filePath(QStringLiteral("updates"));
    QDir().mkpath(d);
    return d;
}
QString status_path() {
    return QDir(updates_dir()).filePath(QStringLiteral("status.json"));
}
QString pending_path() {
    return QDir(updates_dir()).filePath(QStringLiteral("pending.json"));
}

bool dir_writable(const QString& dir) {
    // QFileInfo::isWritable reads the mode bits; a probe file is the truth
    // (ACLs, read-only mounts, /Applications for a standard user).
    QFile probe(QDir(dir).filePath(QStringLiteral(".finterm-write-probe")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

std::optional<QJsonObject> read_json(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return std::nullopt;
    return doc.object();
}

void write_json(const QString& path, const QJsonObject& o) {
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

/// The helper script from the resources, written where it can run.
QString write_helper(const QString& kind) {
    const bool ps = kind == QLatin1String("ifw");
    const QString name = ps ? QStringLiteral("apply-update.ps1") : QStringLiteral("apply-update.sh");
    QFile res(QStringLiteral(":/updater/") + name);
    if (!res.open(QIODevice::ReadOnly)) {
        LOG_ERROR("UpdateInstaller", "The update helper is missing from the build: " + name);
        return {};
    }
    const QString out = QDir(updates_dir()).filePath(name);
    QSaveFile f(out);
    if (!f.open(QIODevice::WriteOnly) || f.write(res.readAll()) < 0 || !f.commit())
        return {};
    QFile::setPermissions(out, QFile::permissions(out) | QFileDevice::ExeOwner | QFileDevice::ReadOwner);
    return out;
}

/// The environment for the relaunched app: an AppImage's runtime puts its own
/// mount (APPDIR …) into the environment — the old version's mount, gone once
/// it exits. Path lists (LD_LIBRARY_PATH, QT_PLUGIN_PATH, PATH, XDG_DATA_DIRS…)
/// lose only their entries under it; the user's own entries stay.
QProcessEnvironment clean_environment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appdir = env.value(QStringLiteral("APPDIR"));
    for (const QString& k : {QStringLiteral("APPDIR"), QStringLiteral("APPIMAGE"), QStringLiteral("ARGV0"),
                             QStringLiteral("OWD")})
        env.remove(k);
    if (appdir.isEmpty())
        return env;
    for (const QString& k : env.keys()) {
        const QString v = env.value(k);
        if (!v.contains(appdir))
            continue;
        QStringList kept;
        for (const QString& part : v.split(QLatin1Char(':')))
            if (!part.contains(appdir))
                kept << part;
        if (kept.isEmpty() || (kept.size() == 1 && kept.first().isEmpty()))
            env.remove(k);
        else
            env.insert(k, kept.join(QLatin1Char(':')));
    }
    return env;
}

/// The profile this process runs ("" = the default) — the relaunch
/// (finterm --profile NAME) and whose status / boot count a file is.
QString running_profile() {
    const QString p = ProfileManager::instance().active();
    return p == QLatin1String("default") ? QString() : p;
}

} // namespace

std::optional<Target> in_place_target() {
#if defined(Q_OS_LINUX)
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (appimage.isEmpty() || !QFileInfo(appimage).isFile())
        return std::nullopt; // a dev tree or a distro package: not ours to replace
    const QString dir = QFileInfo(appimage).absolutePath();
    if (!dir_writable(dir))
        return std::nullopt;
    return Target{QStringLiteral("appimage"), QFileInfo(appimage).absoluteFilePath(),
                  QDir(dir).filePath(QStringLiteral(".finterm-previous.AppImage")), dir};
#elif defined(Q_OS_MACOS)
    // …/finterm.app/Contents/MacOS/finterm
    QDir d(QCoreApplication::applicationDirPath());
    if (!d.cdUp() || !d.cdUp() || !d.absolutePath().endsWith(QLatin1String(".app")))
        return std::nullopt;
    const QString bundle = d.absolutePath();
    const QString parent = QFileInfo(bundle).absolutePath();
    if (!dir_writable(parent))
        return std::nullopt;
    return Target{QStringLiteral("macapp"), bundle, QDir(parent).filePath(QStringLiteral(".finterm-previous.app")),
                  updates_dir()};
#elif defined(Q_OS_WIN)
    const QString dir = QCoreApplication::applicationDirPath();
    // The previous version is kept beside the install (<dir>.previous): its
    // parent must be writable too.
    if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("FintermMaintenanceTool.exe"))) || !dir_writable(dir) ||
        !dir_writable(QFileInfo(QDir::cleanPath(dir)).absolutePath()))
        return std::nullopt; // not an installer's install (a zip, a dev tree), or not ours to change
    return Target{QStringLiteral("ifw"), QDir::toNativeSeparators(dir),
                  QDir::toNativeSeparators(QDir::cleanPath(dir) + QStringLiteral(".previous")), updates_dir()};
#else
    return std::nullopt;
#endif
}

QString staged_path(const Target& t, const QString& version, const QString& download_url) {
    if (t.kind == QLatin1String("appimage"))
        return QDir(t.staging_dir).filePath(QStringLiteral(".finterm-%1.AppImage").arg(version));
    QString name = QFileInfo(QUrl(download_url).path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("finterm-%1-update").arg(version);
    return QDir(t.staging_dir).filePath(name);
}

bool launch_helper(const Target& t, const QString& staged, const QString& from, const QString& to, bool relaunch,
                   bool rollback) {
    const QString script = write_helper(t.kind);
    if (script.isEmpty())
        return false;
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const QString profile = running_profile();
    QString program;
    QStringList args;
    if (t.kind == QLatin1String("ifw")) {
        program = QStringLiteral("powershell.exe");
        args = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),       QStringLiteral("-File"),
                QDir::toNativeSeparators(script), QStringLiteral("-AppPid"),     pid,
                QStringLiteral("-InstallDir"),  t.target,                        QStringLiteral("-Previous"),
                t.previous,                     QStringLiteral("-Status"),       QDir::toNativeSeparators(status_path()),
                QStringLiteral("-From"),        from,                            QStringLiteral("-To"),
                to};
        if (!rollback)
            args << QStringLiteral("-Installer") << QDir::toNativeSeparators(staged);
        if (relaunch)
            args << QStringLiteral("-Relaunch");
        if (rollback)
            args << QStringLiteral("-Rollback");
        if (!profile.isEmpty())
            args << QStringLiteral("-AppProfile") << profile;
    } else {
        program = QStringLiteral("/bin/sh");
        args = {script,
                QStringLiteral("--pid"),
                pid,
                QStringLiteral("--kind"),
                rollback ? t.kind + QStringLiteral("-rollback") : t.kind,
                QStringLiteral("--target"),
                t.target,
                QStringLiteral("--previous"),
                t.previous,
                QStringLiteral("--status"),
                status_path(),
                QStringLiteral("--from"),
                from,
                QStringLiteral("--to"),
                to};
        if (!rollback)
            args << QStringLiteral("--staged") << staged;
        if (relaunch)
            args << QStringLiteral("--relaunch");
        if (!profile.isEmpty())
            args << QStringLiteral("--profile") << profile;
    }

    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessEnvironment(clean_environment());
    p.setWorkingDirectory(QDir::homePath()); // not inside the version being replaced
    qint64 helper_pid = 0;
    if (!p.startDetached(&helper_pid)) {
        LOG_ERROR("UpdateInstaller", QString("Could not start the update helper (%1)").arg(program));
        return false;
    }
    LOG_INFO("UpdateInstaller", QString("Update helper started (pid %1): %2 %3 → %4%5")
                                    .arg(helper_pid)
                                    .arg(rollback ? QStringLiteral("rollback") : t.kind, from, to,
                                         relaunch ? QStringLiteral(", relaunch") : QString()));
    if (!rollback) {
        // For the rollback: the version about to start must confirm itself.
        write_json(pending_path(), QJsonObject{{"kind", t.kind},
                                               {"target", t.target},
                                               {"previous", t.previous},
                                               {"from", from},
                                               {"to", to},
                                               {"profile", profile},
                                               {"starts", 0}});
    }
    return true;
}

std::optional<QJsonObject> take_status() {
    auto s = read_json(status_path());
    // The profile that applied it reports it (all profiles share this folder).
    if (!s || s->value(QStringLiteral("profile")).toString() != running_profile())
        return std::nullopt;
    QFile::remove(status_path());
    return s;
}

bool other_instance_running() {
    // Every running finterm holds the lock in its profile's data folder; any
    // but our own means another instance (of any build) uses this install.
    const QString ours = QDir::cleanPath(AppPaths::data());
    auto& pm = ProfileManager::instance();
    for (const QString& name : pm.list_profiles()) {
        const QString root = pm.profile_root_for(name);
        for (const QString& sub : {QStringLiteral("/user/data"), QStringLiteral("/data")}) {
            const QString dir = QDir::cleanPath(root + sub);
            if (dir != ours && AppPaths::data_dir_in_use(dir))
                return true;
        }
    }
    return false;
}

bool boot_check() {
    // A self-test run is no start of the user's app (it never shows a window).
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest")))
        return false;
    auto pending = read_json(pending_path());
    if (!pending || pending->value(QStringLiteral("profile")).toString() != running_profile())
        return false;
    const QString running = QCoreApplication::applicationVersion();
    const QString to = pending->value(QStringLiteral("to")).toString();
    if (running == pending->value(QStringLiteral("from")).toString()) {
        QFile::remove(pending_path()); // the update didn't happen (the status says why)
        return false;
    }
    if (running != to)
        return false; // another build (a dev tree): not the one to judge
    const int starts = pending->value(QStringLiteral("starts")).toInt() + 1;
    if (starts < 3) {
        (*pending)[QStringLiteral("starts")] = starts;
        write_json(pending_path(), *pending);
        return false;
    }
    // Two starts of the new version never reached its window: put the
    // previous one back and run it.
    LOG_ERROR("UpdateInstaller", QString("finterm %1 failed to start twice — rolling back to %2")
                                     .arg(to, pending->value(QStringLiteral("from")).toString()));
    const Target t{pending->value(QStringLiteral("kind")).toString(), pending->value(QStringLiteral("target")).toString(),
                   pending->value(QStringLiteral("previous")).toString(), QString()};
    QFile::remove(pending_path());
    return launch_helper(t, QString(), to, pending->value(QStringLiteral("from")).toString(), /*relaunch=*/true,
                         /*rollback=*/true);
}

void confirm_boot() {
    auto pending = read_json(pending_path());
    if (pending && pending->value(QStringLiteral("to")).toString() == QCoreApplication::applicationVersion()) {
        QFile::remove(pending_path());
        LOG_INFO("UpdateInstaller", "Updated version confirmed: " + QCoreApplication::applicationVersion());
    }
}

} // namespace finterm::services::update_install
