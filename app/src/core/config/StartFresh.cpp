#include "core/config/StartFresh.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/network/ProxySettings.h"
#include "core/util/ChildEnvironment.h"
#include "storage/secure/SecureStorage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QThread>

#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#    include <windows.h>
#else
#    include <csignal>
#    include <sys/types.h>
#endif

namespace finterm::start_fresh {

namespace {

QString marker_path() {
    return QDir(ProfileManager::instance().profile_root()).filePath(QStringLiteral("start-fresh.json"));
}

/// Copy a tree; false on the first file that would not copy. With
/// `keep_existing`, files already at the target are left as they are.
bool copy_tree(const QString& from, const QString& to, bool keep_existing = false) {
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(fi.absoluteFilePath()));
        if (fi.isDir() && !fi.isSymLink()) {
            if (!QDir().mkpath(target))
                return false;
        } else if (!(keep_existing && QFileInfo::exists(target)) && !QFile::copy(fi.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

/// Move `from` (a folder) to `to`: one rename where it can, else a copy
/// then a delete. False, with `from` still there, when it could not.
bool move_tree(const QString& from, const QString& to) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QDir().rename(from, to))
        return true;
    if (!copy_tree(from, to)) {
        QDir(to).removeRecursively();
        return false;
    }
    if (QDir(from).removeRecursively())
        return true;
    // Partly deleted (a file held open): put back what went, from the copy —
    // the folder is whole again, and the copy stays as a backup.
    copy_tree(to, from, /*keep_existing=*/true);
    return false;
}

bool pid_running(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h)
        return false;
    const bool running = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return running;
#else
    return ::kill(pid_t(pid), 0) == 0;
#endif
}

} // namespace

QString backup_root() {
    return QDir::cleanPath(AppPaths::root()) + QStringLiteral("-backups");
}

QString schedule(const Options& options) {
    QFile f(marker_path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Could not write %1: %2").arg(QDir::toNativeSeparators(f.fileName()), f.errorString());
    QJsonObject o;
    o[QStringLiteral("at")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    o[QStringLiteral("remove_runtime")] = options.remove_runtime;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return {};
}

bool relaunch() {
    QStringList args{QStringLiteral("--wait-for-pid"), QString::number(QCoreApplication::applicationPid())};
    const QString profile = ProfileManager::instance().active();
    if (profile != QLatin1String("default"))
        args << QStringLiteral("--profile") << profile;
    // An AppImage runs from a mount that goes away with this process: start
    // the image itself.
    QString program = qEnvironmentVariable("APPIMAGE");
    if (program.isEmpty() || !QFileInfo(program).isFile())
        program = QCoreApplication::applicationFilePath();
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessEnvironment(proxy::with_user_proxy_env(clean_environment()));
    p.setWorkingDirectory(QDir::homePath());
    return p.startDetached();
}

void wait_for_parent(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--wait-for-pid") != 0)
            continue;
        const qint64 pid = QByteArray(argv[i + 1]).toLongLong();
        if (pid <= 0)
            return;
        QElapsedTimer t;
        t.start();
        while (pid_running(pid) && t.elapsed() < 30000)
            QThread::msleep(100);
        return;
    }
}

Outcome run_if_scheduled() {
    Outcome out;
    QFile marker(marker_path());
    if (!marker.exists())
        return out;
    out.ran = true;
    Options options;
    if (marker.open(QIODevice::ReadOnly)) {
        options.remove_runtime =
            QJsonDocument::fromJson(marker.readAll()).object().value(QStringLiteral("remove_runtime")).toBool();
        marker.close();
    }
    // Once, whatever happens below: a fresh start that fails must not repeat
    // at every launch.
    marker.remove();

    if (AppPaths::data_dir_in_use(AppPaths::data())) {
        out.problems << QStringLiteral("Another finterm (or one of its helpers) is still using the data folder, so "
                                       "nothing was changed. Close every finterm window and choose Start fresh again.");
        return out;
    }

    const QString proot = ProfileManager::instance().profile_root();
    const QString profile = ProfileManager::instance().active();
    out.backup_dir = QDir(backup_root()).filePath(
        QStringLiteral("pre-reset-%1%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
                 profile == QLatin1String("default") ? QString() : QStringLiteral("-") + profile));

    // 1. Everything personal: user/, and the folders an older layout kept the
    //    same things in. A move, not a delete — nothing personal is lost.
    bool moved_any = false; // a backup folder holds something
    QStringList moved;      // to put back if a later one fails
    for (const QString& sub : {QStringLiteral("user"), QStringLiteral("data"), QStringLiteral("files"),
                               QStringLiteral("workspaces")}) {
        const QString from = QDir(proot).filePath(sub);
        if (!QFileInfo(from).isDir())
            continue;
        if (!move_tree(from, QDir(out.backup_dir).filePath(sub))) {
            // All or nothing: half the personal data gone would be a reset
            // nobody asked for. Put back what already moved.
            QStringList stuck;
            for (const QString& done : std::as_const(moved))
                if (!move_tree(QDir(out.backup_dir).filePath(done), QDir(proot).filePath(done)))
                    stuck << QDir::toNativeSeparators(QDir(out.backup_dir).filePath(done));
            out.problems << QStringLiteral("%1 could not be moved to the backup folder (a file in it may be open), "
                                           "so nothing was reset.")
                                .arg(QDir::toNativeSeparators(from));
            if (!stuck.isEmpty())
                out.problems << QStringLiteral("These could not be put back — copy them back into %1 by hand: %2")
                                    .arg(QDir::toNativeSeparators(proot), stuck.join(QStringLiteral(", ")));
            // A copy left behind (a partly failed move keeps it): say where.
            if (!QDir(out.backup_dir).exists() ||
                QDir(out.backup_dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
                QDir().rmdir(out.backup_dir);
                out.backup_dir.clear();
            }
            return out;
        }
        moved << sub;
        moved_any = true;
    }

    // 2. Settings — the default profile's (other profiles share the store
    //    but keep their own data). A copy first where settings are a file.
    //    Not the secure fallback store (kAppSecure): it holds every
    //    profile's secrets; this profile's go in step 3.
    if (profile == QLatin1String("default")) {
        QDir().mkpath(out.backup_dir);
        {
            QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
            const QFileInfo file(s.fileName());
            if (file.isAbsolute() && file.isFile())
                QFile::copy(file.absoluteFilePath(), QDir(out.backup_dir).filePath(file.fileName()));
            // Migration bookkeeping stays: those moves are done and must not run again.
            for (const QString& key : s.allKeys())
                if (!key.startsWith(QLatin1String("meta/")) && !key.startsWith(QLatin1String("migration/")))
                    s.remove(key);
            s.sync();
        }
    }

    // 3. Credentials of this profile.
    if (auto r = SecureStorage::instance().remove_all_in_profile(QFileInfo(out.backup_dir).fileName());
        r.is_err())
        out.problems << QStringLiteral("Some saved keys could not be set aside in the system keyring (%1).")
                            .arg(QString::fromStdString(r.error()));

    // 4. What the app regenerates.
    QDir(QDir(proot).filePath(QStringLiteral("cache"))).removeRecursively();
    if (options.remove_runtime) {
        const QString root = AppPaths::root();
        QStringList gone{QStringLiteral("runtime"), QStringLiteral("python"), QStringLiteral("uv"),
                         QStringLiteral("uv-cache"), QStringLiteral("models")};
        for (const QString& v : QDir(root).entryList({QStringLiteral("venv-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
            gone << v;
        for (const QString& sub : gone)
            if (!QDir(QDir(root).filePath(sub)).removeRecursively())
                out.problems << QStringLiteral("%1 could not be removed completely.")
                                    .arg(QDir::toNativeSeparators(QDir(root).filePath(sub)));
        QFile::remove(QDir(root).filePath(QStringLiteral(".setup_complete")));
    }

    // Nothing personal was there to keep: no empty backup folder.
    if (!moved_any && QDir(out.backup_dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        QDir().rmdir(out.backup_dir);
        out.backup_dir.clear();
    }
    out.ok = out.problems.isEmpty();
    return out;
}

} // namespace finterm::start_fresh
