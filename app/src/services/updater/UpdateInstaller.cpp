#include "services/updater/UpdateInstaller.h"

#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <cstdlib>

#if defined(Q_OS_WIN)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <signal.h>
#    include <sys/types.h>
#endif

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

QString helper_path() {
    return QDir(updates_dir()).filePath(QStringLiteral("helper.json"));
}
QString g_startup_stamp;

/// The helper recorded as `pid` at `at` is still running — that process, not
/// another that got its number since (a restart of the machine).
bool helper_alive(qint64 pid, const QDateTime& at) {
    if (pid <= 0 || !at.isValid())
        return false;
#if defined(Q_OS_WIN)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    DWORD code = 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    if (alive && GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        // 100 ns units since 1601.
        const qint64 ticks = (qint64(created.dwHighDateTime) << 32) | qint64(created.dwLowDateTime);
        const qint64 ms = ticks / 10000 - 11644473600000LL;
        alive = std::abs(ms - at.toMSecsSinceEpoch()) < 60 * 1000;
    }
    CloseHandle(h);
    return alive;
#else
    if (::kill(pid_t(pid), 0) != 0)
        return false;
    QProcess ps;
    ps.start(QStringLiteral("ps"), {QStringLiteral("-o"), QStringLiteral("command="), QStringLiteral("-p"),
                                    QString::number(pid)});
    if (!ps.waitForFinished(2000))
        return true; // can't tell: assume it is
    return ps.readAllStandardOutput().contains("apply-update");
#endif
}

#if defined(Q_OS_WIN)
QString windows_powershell() {
    // By full path — not whatever "powershell" the user's PATH finds (or not).
    return QDir::toNativeSeparators(QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows")))
                                        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")));
}
#endif

/// "finterm is being updated", said by a program outside the install.
void say_updating() {
#if defined(Q_OS_WIN)
    QProcess::startDetached(windows_powershell(),
                            {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                             QStringLiteral("-Command"),
                             QStringLiteral("(New-Object -ComObject WScript.Shell).Popup('finterm is being updated. "
                                            "Start it again in a moment.', 15, 'finterm', 64) | Out-Null")},
                            QDir::homePath()); // not the install folder finterm was started in
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"),
                            {QStringLiteral("-e"), QStringLiteral("display dialog \"finterm is being updated. Start it "
                                                                  "again in a moment.\" with title \"finterm\" buttons "
                                                                  "{\"OK\"} default button 1 giving up after 15")},
                            QDir::homePath());
#endif
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
    // APPIMAGE is inherited: from a terminal inside another AppImage it names
    // that application. Ours only when this binary runs from its mount.
    const QString appdir = QFileInfo(qEnvironmentVariable("APPDIR")).canonicalFilePath();
    const QString exe_dir = QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    if (appdir.isEmpty() || (exe_dir != appdir && !exe_dir.startsWith(appdir + QLatin1Char('/'))))
        return std::nullopt;
    const QString dir = QFileInfo(appimage).absolutePath();
    if (!dir_writable(dir))
        return std::nullopt;
    const Target t{QStringLiteral("appimage"), QFileInfo(appimage).absoluteFilePath(),
                   QDir(dir).filePath(QStringLiteral(".finterm-previous.AppImage")), dir};
    // Run by hand, the kept previous version would be its own rollback copy.
    if (QFileInfo(t.previous) == QFileInfo(t.target))
        return std::nullopt;
    return t;
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
    // The updater's own name (sweep_stale matches it), whatever the asset is
    // called; the suffix is what Windows / hdiutil go by.
    const QString suffix = QFileInfo(QUrl(download_url).path()).suffix();
    return QDir(t.staging_dir)
        .filePath(QStringLiteral("finterm-%1-setup%2")
                      .arg(version, suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix));
}

QString target_stamp(const Target& t) {
    QString file = t.target;
    if (t.kind == QLatin1String("macapp"))
        file = QDir(t.target).filePath(QStringLiteral("Contents/Info.plist"));
    else if (t.kind == QLatin1String("ifw"))
        file = QDir(t.target).filePath(QStringLiteral("finterm.exe"));
    const QFileInfo f(file);
    return f.exists() ? QStringLiteral("%1:%2").arg(f.size()).arg(f.lastModified().toMSecsSinceEpoch()) : QString();
}

bool helper_in_flight() {
    const auto h = read_json(helper_path());
    if (!h)
        return false;
    // Its process, not a timer: a killed helper (logoff, power) is over at
    // once and a slow one is not.
    const auto at = QDateTime::fromString(h->value(QStringLiteral("at")).toString(), Qt::ISODate);
    return helper_alive(qint64(h->value(QStringLiteral("pid")).toDouble()), at);
}

void sweep_stale(const Target& t, const QString& keep) {
    if (other_instance_running() || helper_in_flight())
        return; // someone may be using one
    // Only names staged_path gives (and their .part), never the version that
    // runs or the one a rollback puts back.
    static const QRegularExpression staged(
        QStringLiteral("^\\.finterm-\\d+\\.\\d+\\.\\d+\\.AppImage(\\.\\d+\\.part)?$"
                       "|^finterm-\\d+\\.\\d+\\.\\d+-setup(\\.[A-Za-z0-9]+)?(\\.\\d+\\.part)?$"));
    const QDir dir(t.staging_dir);
    for (const QFileInfo& f : dir.entryInfoList(QDir::Files | QDir::Hidden)) {
        if (staged.match(f.fileName()).hasMatch() && f != QFileInfo(t.target) && f != QFileInfo(t.previous) &&
            (keep.isEmpty() || f != QFileInfo(keep)))
            QFile::remove(f.absoluteFilePath());
    }
}

bool launch_helper(const Target& t, const QString& staged, const QString& from, const QString& to, bool relaunch,
                   bool rollback) {
    // One helper at a time. The lock is this process's until it exits — from
    // then the helper's own record says it is at work.
    static QLockFile lock(QDir(updates_dir()).filePath(QStringLiteral("apply.lock")));
    lock.setStaleLockTime(0);
    if (helper_in_flight() || (!lock.isLocked() && !lock.tryLock(0))) {
        LOG_INFO("UpdateInstaller", "Another finterm is already putting an update in");
        return false;
    }
    const QString script = write_helper(t.kind);
    if (script.isEmpty())
        return false;
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const QString profile = running_profile();
    QString program;
    QStringList args;
    if (t.kind == QLatin1String("ifw")) {
#if defined(Q_OS_WIN)
        program = windows_powershell();
#endif
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
        args << QStringLiteral("-Lock") << QDir::toNativeSeparators(helper_path());
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
        args << QStringLiteral("--lock") << helper_path();
    }

    if (!rollback) {
        // For the rollback: the version it puts in must confirm itself.
        QFile::remove(status_path());
        write_json(pending_path(), QJsonObject{{"kind", t.kind},
                                               {"target", t.target},
                                               {"previous", t.previous},
                                               {"from", from},
                                               {"to", to},
                                               {"profile", profile},
                                               {"starts", 0}});
    }

    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessEnvironment(clean_environment());
    p.setWorkingDirectory(QDir::homePath()); // not inside the version being replaced
    qint64 helper_pid = 0;
    if (!p.startDetached(&helper_pid)) {
        LOG_ERROR("UpdateInstaller", QString("Could not start the update helper (%1)").arg(program));
        if (!rollback)
            QFile::remove(pending_path());
        return false;
    }
    // What a finterm started meanwhile goes by (the helper waits for this
    // process first, and removes the record when it is done).
    write_json(helper_path(), QJsonObject{{"pid", double(helper_pid)},
                                          {"kind", t.kind},
                                          {"target", t.target},
                                          {"at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    LOG_INFO("UpdateInstaller", QString("Update helper started (pid %1): %2 %3 → %4%5")
                                    .arg(helper_pid)
                                    .arg(rollback ? QStringLiteral("rollback") : t.kind, from, to,
                                         relaunch ? QStringLiteral(", relaunch") : QString()));
    return true;
}

std::optional<QJsonObject> take_status() {
    auto s = read_json(status_path());
    // The profile that applied it reports it (all profiles share this folder).
    if (!s || s->value(QStringLiteral("profile")).toString() != running_profile())
        return std::nullopt;
    // …from the version it left in place: an AppImage reopened while the
    // helper worked still runs the one before.
    const bool ok = s->value(QStringLiteral("ok")).toBool();
    if (s->value(ok ? QStringLiteral("to") : QStringLiteral("from")).toString() != QCoreApplication::applicationVersion())
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

QString startup_stamp() {
    return g_startup_stamp;
}

bool boot_check() {
    // A self-test run is no start of the user's app (it never shows a window).
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest")))
        return false;
    const auto mine = in_place_target();
    if (helper_in_flight()) {
        // An update or a rollback is being put in — here? (Another finterm, a
        // dev tree or a copy elsewhere, is not touched by it.) An AppImage is
        // replaced in one rename, so running on is safe, and this start isn't
        // counted. A bundle or an install folder is being taken apart: leave
        // at once — staying would hold its files — and let another program
        // say why.
        const auto h = read_json(helper_path());
        const bool here = h && mine && h->value(QStringLiteral("target")).toString() == mine->target;
        if (!here || mine->kind == QLatin1String("appimage"))
            return false;
        say_updating();
        return true;
    }
    // The install as this process found it, before anything else can change
    // it (empty while a helper is at work: whatever that leaves is not ours).
    if (mine)
        g_startup_stamp = target_stamp(*mine);
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
    // The record stays until the restored version starts (running == from,
    // above): a rollback that could not be done is tried once more, then this
    // version runs as it is rather than going round for ever.
    const int rollbacks = pending->value(QStringLiteral("rollbacks")).toInt();
    if (rollbacks >= 2) {
        QFile::remove(pending_path());
        return false;
    }
    (*pending)[QStringLiteral("rollbacks")] = rollbacks + 1;
    write_json(pending_path(), *pending);
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
