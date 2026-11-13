#include "core/config/AppPaths.h"

#include "core/config/AppIdentity.h"
#include "core/config/ProfileManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <cstdio>

namespace finterm {

namespace {

// The per-user application-data folder the root lives in.
QString data_base() {
#ifdef _WIN32
    // GenericDataLocation = %LOCALAPPDATA% on Windows (Qt docs: QStandardPaths),
    // avoiding the fragile double-cdUp() from AppLocalDataLocation.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#elif defined(__APPLE__)
    return QDir::homePath() + "/Library/Application Support";
#else
    return QDir::homePath() + "/.local/share";
#endif
}

QString current_root() {
    return data_base() + "/" + AppIdentity::kBundleId;
}

QString legacy_root() {
    return data_base() + "/" + AppIdentity::kBundleIdLegacy;
}

void say(const QString& msg) {
    // Runs before the logger exists; the launching terminal is the channel.
    std::fprintf(stdout, "[migrate] %s\n", qUtf8Printable(msg));
    std::fflush(stdout);
}

// Database locks a running instance holds, by the names old and new builds use.
const char* const kDbLockNames[] = {"fincept.db.lock", "finterm.db.lock"};

// True when any profile under `root` has a live instance: renaming a folder
// out from under a running app splits its SQLite journals from the databases.
bool root_in_use(const QString& root) {
    QStringList profile_roots{root};
    const QDir profiles(root + "/profiles");
    for (const QString& name : profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        profile_roots << profiles.filePath(name);
    for (const QString& proot : profile_roots)
        for (const QString& d : {proot + "/data", proot + "/user/data"})
            if (AppPaths::data_dir_in_use(d))
                return true;
#if defined(Q_OS_LINUX)
    // Processes that run from the folder without holding a database lock: the
    // yfinance daemon and other Python children (their interpreter lives in the
    // root's venvs, their socket under its runtime/), or an instance that has
    // not reached its lock yet. Windows refuses to rename a folder with open
    // files, so only Linux needs this.
    const QByteArray needle = (root + "/").toUtf8();
    const QDir proc(QStringLiteral("/proc"));
    for (const QString& pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (pid.isEmpty() || !pid.at(0).isDigit())
            continue;
        if (QFileInfo(proc.filePath(pid) + "/exe").symLinkTarget().toUtf8().startsWith(needle))
            return true;
        QFile cmd(proc.filePath(pid) + "/cmdline");
        if (cmd.open(QIODevice::ReadOnly) && cmd.readAll().contains(needle))
            return true;
    }
#endif
    return false;
}

// A venv names its interpreter by absolute path — the bin/python symlink,
// pyvenv.cfg's `home`, every console script's shebang — and all of those point
// into the root. Repoint them, or every venv-* is dead after the move and
// PythonSetupManager would re-download gigabytes of packages.
// Idempotent: anything already repointed no longer contains the old path.
void relocate_venvs(const QString& from, const QString& to) {
    // Both spellings: Windows venvs write backslash paths (pyvenv.cfg `home`,
    // Scripts/*.bat), Qt hands us forward slashes.
    const QList<QPair<QByteArray, QByteArray>> spellings = {
        {from.toUtf8(), to.toUtf8()},
        {QDir::toNativeSeparators(from).toUtf8(), QDir::toNativeSeparators(to).toUtf8()},
    };
    int fixed = 0;
    const QDir root(to);
    for (const QString& venv : root.entryList({"venv-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString vdir = root.filePath(venv);
        QStringList files{vdir + "/pyvenv.cfg"};
        for (const char* bin : {"/bin", "/Scripts"}) {
            const QDir b(vdir + bin);
            for (const QString& f : b.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot))
                files << b.filePath(f);
        }
        for (const QString& path : files) {
            const QFileInfo fi(path);
            if (fi.isSymLink()) {
                const QString target = fi.symLinkTarget();
                if (!target.startsWith(from + "/"))
                    continue;
                QFile::remove(path);
                if (QFile::link(to + target.mid(from.size()), path))
                    ++fixed;
                continue;
            }
            if (!fi.isFile() || fi.size() > 1024 * 1024)
                continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QByteArray body = f.readAll();
            f.close();
            // Text only: a binary launcher (Windows Scripts/*.exe) embeds the
            // path in a layout a length-changing replace would corrupt. finterm
            // never runs those — only the interpreter, which pyvenv.cfg drives.
            if (body.contains('\0'))
                continue;
            bool hit = false;
            for (const auto& [f_b, t_b] : spellings) {
                if (body.contains(f_b)) {
                    body.replace(f_b, t_b);
                    hit = true;
                }
            }
            if (!hit)
                continue;
            // Write-then-rename: a crash mid-write must not leave a truncated
            // file that no longer contains the old path (the re-run would skip it).
            const auto perms = f.permissions();
            QSaveFile out(path);
            if (out.open(QIODevice::WriteOnly) && out.write(body) == body.size() && out.commit()) {
                QFile::setPermissions(path, perms);
                ++fixed;
            }
        }
    }
    if (fixed > 0)
        say(QString("python: repointed %1 venv file(s) at the new data folder").arg(fixed));
}

// Whether `dir` holds finterm's data, as opposed to merely existing: other
// tools share the folder name (hearth installs voice models under
// <data>/finterm/), so existence alone proves nothing.
bool is_data_root(const QString& dir) {
    return QFileInfo::exists(dir + "/user") || QFileInfo::exists(dir + "/profiles.json");
}

// Markers in the current root. kMovePending: entries are being moved in from
// the legacy folder (resumed on the next launch until done). kMovedMarker: the
// move is complete, and the legacy folder is never touched again — if it
// reappears it belongs to another install of the older app.
const char* const kMovePending = "/.legacy-move-pending";
const char* const kMovedMarker = "/.legacy-moved";

void touch(const QString& path) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.close();
}

QString resolve_root() {
    const QString cur = current_root();
    if (QFileInfo::exists(cur + kMovedMarker) ||
        (is_data_root(cur) && !QFileInfo::exists(cur + kMovePending)))
        return cur;
    const QString old = legacy_root();
    return QFileInfo(old).isDir() ? old : cur;
}

// Fixed by migrate_root() for the life of the process: until it runs, root()
// resolves on every call.
QString g_root;

// Move every entry of the legacy folder into the current one, each with its
// own rename. The current folder may already hold things that are not
// finterm's data (hearth keeps voice models in it, and may have them open), so
// it is never itself renamed. Resumable: the pending marker is written first,
// and an interrupted move finishes on the next launch; the app does not run on
// a half-moved folder (see main.cpp).
bool move_legacy_root() {
    const QString cur = current_root();
    const QString old = legacy_root();
    const bool resuming = QFileInfo::exists(cur + kMovePending);

    if (!resuming) {
        if (QFileInfo::exists(cur + kMovedMarker) || !QFileInfo(old).isDir())
            return true; // moved already, or nothing to move — the common case
        // A current folder that already holds finterm's data is an install of
        // its own; the legacy folder is not ours to take.
        if (is_data_root(cur))
            return true;
    }
    if (QFileInfo(old).isDir()) {
        if (root_in_use(old)) {
            say(QString("data: %1 is in use by a running instance — it moves on a later launch").arg(old));
            return false;
        }
        if (!QDir().mkpath(cur)) {
            say(QString("data: could not create %1").arg(cur));
            return false;
        }
        touch(cur + kMovePending);
        QStringList stuck;
        const QDir src(old);
        const auto entries = src.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QString& name : entries) {
            if (name == QLatin1String(kMovePending + 1))
                continue; // our own marker, should one have been left there
            QString dst = cur + "/" + name;
            // A name already taken: by a resumed move's earlier half that an
            // older build re-created since, or by something else's file. Keep
            // both — the legacy copy goes under imported/.
            if (QFileInfo::exists(dst)) {
                QDir().mkpath(cur + "/imported/legacy");
                dst = cur + "/imported/legacy/" + name;
                for (int i = 1; QFileInfo::exists(dst); ++i)
                    dst = cur + "/imported/legacy/" + name + "." + QString::number(i);
                say(QString("data: %1 exists in both folders — the older copy goes to %2").arg(name, dst));
            }
            if (!QDir().rename(src.filePath(name), dst))
                stuck << name;
        }
        if (!stuck.isEmpty()) {
            say(QString("data: could not move %1 from %2 into %3 — the move finishes on a later launch")
                    .arg(stuck.join(", "), old, cur));
            return false;
        }
        QDir().rmdir(old);
        say(QString("data: moved %1 into %2").arg(old, cur));
    }
    relocate_venvs(old, cur); // idempotent, so a resumed move re-runs it safely
    touch(cur + kMovedMarker);
    QFile::remove(cur + kMovePending);
    return true;
}

} // namespace

QString AppPaths::root() {
    return g_root.isEmpty() ? resolve_root() : g_root;
}

bool AppPaths::migrate_root() {
    // One launch at a time: two starting together (a double-click) must not
    // both move entries. A second launch waits for the first to finish.
    QDir().mkpath(data_base());
    QLockFile guard(data_base() + "/." + AppIdentity::kBundleId + "-migration.lock");
    guard.setStaleLockTime(0);
    guard.lock();
    const bool ok = move_legacy_root();
    g_root = resolve_root();
    return ok;
}

bool AppPaths::data_dir_in_use(const QString& data_dir) {
    if (!QFileInfo::exists(data_dir))
        return false;
    for (const char* name : kDbLockNames) {
        QLockFile live(data_dir + "/" + name);
        live.setStaleLockTime(0); // a held lock is never stale by age (see main.cpp)
        if (live.tryLock(0)) {
            live.unlock();
            continue;
        }
        // Only a lock someone holds means a live instance. A folder we cannot
        // write a lock into (read-only, full disk) is not one.
        if (live.error() == QLockFile::LockFailedError)
            return true;
    }
    return false;
}

QString AppPaths::current_root_path() {
    return current_root();
}

QString AppPaths::legacy_root_path() {
    return legacy_root();
}

QString AppPaths::user() {
    return ProfileManager::instance().profile_root() + "/user";
}

namespace {
// data/, files/ and workspaces/ each resolve on their own: the new
// user/<sub> once LayoutMigrations v3 has moved it, the old <profile>/<sub>
// while that still holds the user's things — so a move that only half
// completed can never hide the half that did not move. For data/ the test is
// the account database itself (auth.db): a stray file in user/data must not
// switch the app onto an empty account while the real one sits in data/.
QString pick_layout(const char* sub) {
    const QString proot = ProfileManager::instance().profile_root();
    const QString old_path = proot + "/" + sub;
    const QString new_path = proot + "/user/" + sub;
    if (qstrcmp(sub, "data") == 0) {
        const bool old_has = QFileInfo::exists(old_path + "/auth.db");
        const bool new_has = QFileInfo::exists(new_path + "/auth.db");
        return (old_has && !new_has) ? old_path : new_path;
    }
    const auto entries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden;
    const QDir old_dir(old_path), new_dir(new_path);
    if (old_dir.exists() && !old_dir.isEmpty(entries) && (!new_dir.exists() || new_dir.isEmpty(entries)))
        return old_path;
    return new_path;
}
} // namespace

QString AppPaths::data() {
    return pick_layout("data");
}

QString AppPaths::features() {
    return user() + "/features";
}

QString AppPaths::agents() {
    return user() + "/agents";
}

QString AppPaths::market_data() {
    return root() + "/market_data";
}

QString AppPaths::logs() {
    return ProfileManager::instance().profile_root() + "/logs";
}

QString AppPaths::files() {
    return pick_layout("files");
}

QString AppPaths::cache() {
    return ProfileManager::instance().profile_root() + "/cache";
}

QString AppPaths::models() {
    return root() + "/models";
}

QString AppPaths::runtime() {
    return root() + "/runtime";
}

QString AppPaths::workspaces() {
    return pick_layout("workspaces");
}

QString AppPaths::crashdumps() {
    return ProfileManager::instance().profile_root() + "/crashdumps";
}

void AppPaths::ensure_all() {
    // Use fprintf to stderr (not qWarning) — main.cpp installs a Qt message
    // handler that routes qWarning into Logger, but Logger's file isn't open
    // yet when ensure_all() runs during startup. Raw stderr is the safe channel.
    const auto try_mkpath = [](const QString& p) {
        if (p.isEmpty()) {
            fprintf(stderr, "[AppPaths] empty path — skipping mkpath\n");
            return;
        }
        if (QDir().mkpath(p))
            return;
        const QFileInfo info(p);
        fprintf(stderr, "[AppPaths] mkpath failed: %s exists=%d isDir=%d writable=%d\n", qUtf8Printable(p),
                info.exists(), info.isDir(), info.isWritable());
    };
    try_mkpath(root());
    try_mkpath(data());
    try_mkpath(logs());
    try_mkpath(files());
    try_mkpath(cache());
    try_mkpath(models());
    try_mkpath(runtime());
    try_mkpath(workspaces());
    try_mkpath(features());
    try_mkpath(agents());
    try_mkpath(market_data());
    try_mkpath(crashdumps());

    // A plain map at the root, so the split is obvious from a file browser.
    // Generic text only — nothing about the user goes in it.
    QFile readme(root() + "/README.txt");
    if (!readme.exists() && readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        readme.write("finterm data folder\n"
                     "\n"
                     "user/         YOUR data - databases, files, workspaces, feature and agent data.\n"
                     "              Back up or erase this folder to keep or remove everything personal.\n"
                     "              (A non-default profile has its own: profiles/<name>/user/)\n"
                     "\n"
                     "Everything else is the app's own and holds nothing personal:\n"
                     "cache/ (safe to delete), market_data/ (public SEC/FINRA indexes),\n"
                     "models/, logs/, crashdumps/, runtime/, python/, venv-*/, uv/.\n"
                     "\n"
                     "API keys and passwords are not stored here: they are in the OS keyring.\n");
    }
}

} // namespace finterm
