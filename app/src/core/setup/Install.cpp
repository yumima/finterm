#include "core/setup/Install.h"

#include "core/config/AppIdentity.h"
#include "core/config/BuildInfo.h"
#include "core/logging/Logger.h"
#include "core/util/ChildEnvironment.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>
#include <tuple>

#ifdef Q_OS_UNIX
#    include <unistd.h>
#endif

namespace finterm::install {

namespace {

constexpr const char* kMarker = "X-Finterm-Installer";
constexpr const char* kVersionKey = "X-Finterm-Version";
constexpr const char* kFavouriteKey = "setup/favourite_added";
constexpr const char* kDontAskKey = "setup/dont_ask";
constexpr const char* kBuiltKey = "X-Finterm-Built";
constexpr const char* kInstalledKey = "setup/installed"; ///< [path, version, built] of the install

/// One per thread (QSettings is reentrant, not thread-safe): the install
/// runs off the GUI thread. They share the store underneath. Every path that
/// writes ends in sync(): a pool thread's copy may outlive the app.
QSettings& settings() {
    thread_local QSettings s(AppIdentity::kOrg, AppIdentity::kApp);
    return s;
}

/// "4.2.3" from "4.2.3", "4.2.3-dev", "finterm 4.2.3"; false when none.
bool parse(const QString& text, int& a, int& b, int& c) {
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return false;
    a = m.captured(1).toInt();
    b = m.captured(2).toInt();
    c = m.captured(3).toInt();
    return true;
}

/// The installed version (and build) against this one. A version is
/// released again from a newer commit at times: then the commit times decide.
Found compare(const QString& installed, const QString& mine, const QString& installed_built = {},
              const QString& mine_built = {}) {
    int a1 = 0, b1 = 0, c1 = 0, a2 = 0, b2 = 0, c2 = 0;
    if (!parse(installed, a1, b1, c1) || !parse(mine, a2, b2, c2))
        return Found::Unknown;
    const auto x = std::make_tuple(a1, b1, c1), y = std::make_tuple(a2, b2, c2);
    if (x != y)
        return x < y ? Found::Older : Found::Newer;
    if (!installed_built.isEmpty() && !mine_built.isEmpty() && installed_built != mine_built)
        return installed_built < mine_built ? Found::Older : Found::Newer; // ISO UTC sorts
    return Found::Same;
}

bool same_file(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
    const QFileInfo fa(a), fb(b);
    if (fa.exists() && fb.exists())
        return fa.canonicalFilePath() == fb.canonicalFilePath();
    return QDir::cleanPath(fa.absoluteFilePath()) == QDir::cleanPath(fb.absoluteFilePath());
}

/// Replace `to` with `from` in one step (POSIX rename replaces; QFile::rename
/// refuses an existing destination).
bool replace_file(const QString& from, const QString& to) {
#ifdef Q_OS_UNIX
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#else
    QFile::remove(to);
    return QFile::rename(from, to);
#endif
}

QString run(const QString& program, const QStringList& args, int timeout_ms = 15000, bool* ok = nullptr) {
    QProcess p;
    p.setProcessEnvironment(clean_environment());
    p.start(program, args);
    const bool done = p.waitForFinished(timeout_ms);
    if (!done)
        p.kill();
    if (ok)
        *ok = done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// ── Linux ────────────────────────────────────────────────────────────────────

QString desktop_path(const Env& env) {
    return QDir(env.data_home).filePath(QStringLiteral("applications/finterm.desktop"));
}

QString icon_path(const Env& env) {
    return QDir(env.data_home).filePath(QStringLiteral("icons/hicolor/256x256/apps/finterm.png"));
}

QString command_path(const Env& env) {
    return QDir(env.home).filePath(QStringLiteral(".local/bin/finterm"));
}

QString previous_path(const QString& installed) {
#if defined(Q_OS_MACOS)
    return QDir(QFileInfo(installed).absolutePath()).filePath(QStringLiteral(".finterm-previous.app"));
#else
    // The updater's name for the kept version (UpdateInstaller), so its
    // rollback and its sweep know it.
    return QDir(QFileInfo(installed).absolutePath()).filePath(QStringLiteral(".finterm-previous.AppImage"));
#endif
}

QString read_text(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool write_text(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString part = path + QStringLiteral(".part");
    QFile f(part);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(text.toUtf8());
    f.close();
    return replace_file(part, path);
}

bool ours(const QString& desktop_text) {
    return desktop_text.contains(QLatin1String(kMarker) + QStringLiteral("=1"));
}

QString desktop_value(const QString& text, const char* key) {
    const QRegularExpression re(QStringLiteral("^%1=(.*)$").arg(QRegularExpression::escape(QLatin1String(key))),
                                QRegularExpression::MultilineOption);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

/// A string value of a desktop entry: \\ escaped (Desktop Entry spec).
QString desktop_string(const QString& value) {
    return QString(value).replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
}

/// One argument of an Exec line (Desktop Entry spec): quoted when it holds
/// anything special, with " ` $ \\ escaped inside the quotes; % is a field
/// code there, so a literal one is %%; then escaped as any string value.
QString exec_arg(const QString& arg) {
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./:+=@%-]+$"));
    QString out = arg;
    out.replace(QLatin1Char('%'), QStringLiteral("%%"));
    if (!plain.match(arg).hasMatch()) {
        out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        for (const char* ch : {"\"", "`", "$"})
            out.replace(QLatin1String(ch), QStringLiteral("\\") + QLatin1String(ch));
        out = QLatin1Char('"') + out + QLatin1Char('"');
    }
    return desktop_string(out);
}

bool gsettings_favourites(QStringList& out) {
    if (QStandardPaths::findExecutable(QStringLiteral("gsettings")).isEmpty())
        return false;
    bool ok = false;
    const QString value = run(QStringLiteral("gsettings"), {QStringLiteral("get"), QStringLiteral("org.gnome.shell"),
                                                            QStringLiteral("favorite-apps")},
                              5000, &ok);
    if (!ok)
        return false;
    out.clear();
    static const QRegularExpression item(QStringLiteral("'([^']*)'"));
    for (auto it = item.globalMatch(value); it.hasNext();)
        out << it.next().captured(1);
    return true;
}

bool set_gsettings_favourites(const QStringList& list) {
    QStringList quoted;
    for (const QString& s : list)
        quoted << QLatin1Char('\'') + QString(s).remove(QLatin1Char('\'')) + QLatin1Char('\'');
    bool ok = false;
    run(QStringLiteral("gsettings"), {QStringLiteral("set"), QStringLiteral("org.gnome.shell"),
                                      QStringLiteral("favorite-apps"), QLatin1Char('[') + quoted.join(QStringLiteral(", ")) + QLatin1Char(']')},
        5000, &ok);
    return ok;
}

void refresh_desktop_caches(const Env& env) {
    if (!QStandardPaths::findExecutable(QStringLiteral("update-desktop-database")).isEmpty())
        run(QStringLiteral("update-desktop-database"), {QDir(env.data_home).filePath(QStringLiteral("applications"))});
    if (!QStandardPaths::findExecutable(QStringLiteral("gtk-update-icon-cache")).isEmpty())
        run(QStringLiteral("gtk-update-icon-cache"),
            {QStringLiteral("-q"), QStringLiteral("-t"), QDir(env.data_home).filePath(QStringLiteral("icons/hicolor"))});
}

/// The small script the menu entry and the `finterm` command start, beside
/// the image. It asks the image to mount itself (--appimage-mount: the
/// AppImage runtime prints the mount point, or fails at once without FUSE),
/// stops that test mount and what it started (its own process group;
/// bounded: TERM, then KILL), then execs the image —
/// mounted when the test worked, unpacked (always works) when it didn't or
/// gave no answer in 10 s; why is written to logs/launch.log. finterm is the
/// started process itself (exec): its signals and output are its own. An
/// interrupted launcher ends its test mount (trap).
QString launch_script_path(const QString& installed) {
    return QDir(QFileInfo(installed).absolutePath()).filePath(QStringLiteral(".finterm-launch"));
}

/// The script for `installed`: its path written in (the command starts the
/// script through a link, so the script's own location says nothing).
QString launch_script_text(const QString& installed) {
    QString quoted = installed;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral(
        "#!/bin/sh\n"
        "# finterm's launcher, written by its installer: the image mounted through FUSE\n"
        "# where a test mount works, else unpacked (which always works).\n"
        "app='%1'\n"
        "[ -n \"$APPIMAGE_EXTRACT_AND_RUN\" ] && exec \"$app\" \"$@\"\n"
        "out=$(mktemp) || APPIMAGE_EXTRACT_AND_RUN=1 exec \"$app\" \"$@\"\n"
        "# The test mount in a process group of its own (setsid), so stopping it\n"
        "# stops whatever it started too.\n"
        "if command -v setsid >/dev/null 2>&1; then\n"
        "    setsid \"$app\" --appimage-mount >\"$out\" 2>\"$out.err\" &\n"
        "else\n"
        "    \"$app\" --appimage-mount >\"$out\" 2>\"$out.err\" &\n"
        "fi\n"
        "probe=$!\n"
        "# (No \"--\" before the group: dash's kill doesn't take it.)\n"
        "signal() { kill \"$1\" \"-$probe\" 2>/dev/null || kill \"$1\" \"$probe\" 2>/dev/null; }\n"
        "stop_probe() {\n"
        "    signal -TERM\n"
        "    n=0\n"
        "    while kill -0 \"$probe\" 2>/dev/null && [ \"$n\" -lt 20 ]; do sleep 0.05; n=$((n + 1)); done\n"
        "    signal -KILL\n"
        "}\n"
        "trap 'stop_probe; rm -f \"$out\" \"$out.err\"; exit 130' INT TERM HUP\n"
        "i=0\n"
        "while [ \"$i\" -lt 200 ] && [ ! -s \"$out\" ] && kill -0 \"$probe\" 2>/dev/null; do\n"
        "    sleep 0.05; i=$((i + 1))\n"
        "done\n"
        "mounted=no\n"
        "[ -s \"$out\" ] && [ -d \"$(head -n 1 \"$out\")\" ] && mounted=yes\n"
        "stop_probe\n"
        "trap - INT TERM HUP\n"
        "if [ \"$mounted\" = yes ]; then\n"
        "    rm -f \"$out\" \"$out.err\"\n"
        "    exec \"$app\" \"$@\"\n"
        "fi\n"
        "log=\"${XDG_DATA_HOME:-$HOME/.local/share}/finterm/logs/launch.log\"\n"
        "if mkdir -p \"$(dirname \"$log\")\" 2>/dev/null; then\n"
        "    { date; echo 'FUSE mount failed or did not answer: starting unpacked.'; cat \"$out.err\"; } >>\"$log\" 2>/dev/null\n"
        "fi\n"
        "rm -f \"$out\" \"$out.err\"\n"
        "APPIMAGE_EXTRACT_AND_RUN=1 exec \"$app\" \"$@\"\n")
        .arg(quoted);
}

bool write_launch_script(const QString& installed) {
    const QString path = launch_script_path(installed);
    if (read_text(path) != launch_script_text(installed) && !write_text(path, launch_script_text(installed)))
        return false;
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                          QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                          QFileDevice::ExeOther);
}

/// The launcher, icon and command for `installed`. Lines for the report.
/// `add_command`: create the `finterm` command when there is none (an install
/// does; a refresh only keeps one of ours current, never puts back one the
/// user removed).
QStringList write_launcher(const Env& env, const QString& installed, bool add_command, bool* ok = nullptr) {
    QStringList done;
    if (ok)
        *ok = true;
    // The launch script first, before anything else is touched: the menu
    // entry and the command start it, so without it they are left as they
    // are (an earlier launcher keeps working) and the caller is told.
    if (!write_launch_script(installed)) {
        if (ok)
            *ok = false;
        done << QStringLiteral("could not write the launch script ") + launch_script_path(installed);
        return done;
    }
    done << QStringLiteral("launch script: ") + launch_script_path(installed);
    if (!env.package_icon.isEmpty() && QFileInfo(env.package_icon).isFile()) {
        QDir().mkpath(QFileInfo(icon_path(env)).absolutePath());
        QFile::remove(icon_path(env));
        if (QFile::copy(env.package_icon, icon_path(env)))
            done << QStringLiteral("icon: ") + icon_path(env);
    }
    const QString desktop = desktop_path(env);
    const QString existing = read_text(desktop);
    if (QFileInfo::exists(desktop) && !ours(existing)) {
        // Someone else's (finterm.sh install from a source tree): kept.
        QString bak = desktop + QStringLiteral(".bak");
        for (int i = 2; QFileInfo::exists(bak); ++i)
            bak = desktop + QStringLiteral(".bak") + QString::number(i);
        if (QFile::rename(desktop, bak))
            done << QStringLiteral("kept the previous launcher as ") + bak;
    }
    if (write_text(desktop, desktop_entry(env, installed)))
        done << QStringLiteral("launcher: ") + desktop;
    // The command: a link we own, never someone's own file of that name.
    const QString cmd = command_path(env);
    const QFileInfo ci(cmd);
    const bool our_link = ci.isSymLink() && (same_file(ci.symLinkTarget(), launch_script_path(installed)) ||
                                             same_file(ci.symLinkTarget(), installed) ||
                                             same_file(ci.symLinkTarget(), previous_path(installed)));
    if (our_link || (add_command && !ci.exists() && !ci.isSymLink())) {
        QDir().mkpath(ci.absolutePath());
        QFile::remove(cmd);
        if (QFile::link(launch_script_path(installed), cmd))
            done << QStringLiteral("command: ") + cmd;
    }
    refresh_desktop_caches(env);
    return done;
}

/// The installed image's version, from the launcher we wrote with it. An
/// image we did not install (copied there by hand) is not run to ask — an
/// old build would open its whole window: its version is unknown.
QString linux_installed_version(const Env& env, const QString& path, QString* built = nullptr) {
    const QString text = read_text(desktop_path(env));
    if (ours(text) && desktop_value(text, "TryExec") == desktop_string(path)) {
        if (built)
            *built = desktop_value(text, kBuiltKey);
        return desktop_value(text, kVersionKey);
    }
    return {};
}

// ── macOS ────────────────────────────────────────────────────────────────────
#if defined(Q_OS_MACOS)

QString mac_bundle_version(const QString& bundle) {
    QSettings plist(QDir(bundle).filePath(QStringLiteral("Contents/Info.plist")), QSettings::NativeFormat);
    return plist.value(QStringLiteral("CFBundleShortVersionString")).toString();
}

QString dock_tile_url(const QString& bundle) {
    return QStringLiteral("file://") + QDir::cleanPath(bundle) + QLatin1Char('/');
}

bool mac_dock_has(const QString& bundle) {
    return run(QStringLiteral("defaults"), {QStringLiteral("read"), QStringLiteral("com.apple.dock"),
                                            QStringLiteral("persistent-apps")})
        .contains(QDir::cleanPath(bundle));
}

bool mac_dock_add(const QString& bundle) {
    const QString tile =
        QStringLiteral("<dict><key>tile-data</key><dict><key>file-data</key><dict><key>_CFURLString</key><string>%1"
                       "</string><key>_CFURLStringType</key><integer>15</integer></dict></dict></dict>")
            .arg(dock_tile_url(bundle).toHtmlEscaped());
    bool ok = false;
    run(QStringLiteral("defaults"), {QStringLiteral("write"), QStringLiteral("com.apple.dock"),
                                     QStringLiteral("persistent-apps"), QStringLiteral("-array-add"), tile},
        10000, &ok);
    if (ok)
        run(QStringLiteral("killall"), {QStringLiteral("Dock")});
    return ok;
}

/// Remove our tile: the Dock's list without entries for `bundle`, written
/// back through `defaults import` (cfprefsd sees it; a plist edited on disk
/// it would overwrite).
bool mac_dock_remove(const QString& bundle) {
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return false;
    const QString plist = tmp.filePath(QStringLiteral("dock.plist"));
    bool ok = false;
    run(QStringLiteral("defaults"), {QStringLiteral("export"), QStringLiteral("com.apple.dock"), plist}, 10000, &ok);
    if (!ok)
        return false;
    QStringList kept;
    bool removed = false;
    {
        QSettings s(plist, QSettings::NativeFormat);
        QVariantList apps = s.value(QStringLiteral("persistent-apps")).toList();
        for (int i = apps.size() - 1; i >= 0; --i) {
            const QVariantMap file = apps[i].toMap().value(QStringLiteral("tile-data")).toMap()
                                         .value(QStringLiteral("file-data")).toMap();
            if (file.value(QStringLiteral("_CFURLString")).toString().contains(QDir::cleanPath(bundle))) {
                apps.removeAt(i);
                removed = true;
            }
        }
        if (!removed)
            return true;
        s.setValue(QStringLiteral("persistent-apps"), apps);
        s.sync();
    }
    run(QStringLiteral("defaults"), {QStringLiteral("import"), QStringLiteral("com.apple.dock"), plist}, 10000, &ok);
    if (ok)
        run(QStringLiteral("killall"), {QStringLiteral("Dock")});
    return ok;
}

/// Copy a bundle (ditto keeps its signature, links and attributes).
bool copy_bundle(const QString& from, const QString& to) {
    bool ok = false;
    run(QStringLiteral("/usr/bin/ditto"), {from, to}, 300000, &ok);
    return ok;
}
#endif

bool remove_path(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink())
        return true;
    if (fi.isDir() && !fi.isSymLink())
        return QDir(path).removeRecursively();
    return QFile::remove(path);
}

} // namespace

QString desktop_entry(const Env& env, const QString& installed) {
    const QString exec = exec_arg(launch_script_path(installed));
    QString icon = QStringLiteral("finterm");
    if (!env.package_icon.isEmpty())
        icon = icon_path(env);
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Version=1.0\n"
                          "Name=finterm\n"
                          "GenericName=Financial Research Terminal\n"
                          "Comment=Local-first financial research terminal\n"
                          "Exec=%1\n"
                          "TryExec=%2\n"
                          "Icon=%3\n"
                          "Terminal=false\n"
                          "Categories=Office;Finance;\n"
                          "Keywords=finance;trading;stocks;portfolio;markets;\n"
                          "StartupWMClass=finterm\n"
                          "StartupNotify=true\n"
                          "%4=1\n"
                          "%5=%6\n"
                          "%7=%8\n")
        .arg(exec, desktop_string(installed), desktop_string(icon), QLatin1String(kMarker), QLatin1String(kVersionKey),
             env.version, QLatin1String(kBuiltKey), env.built);
}

Env current_env() {
    Env env;
    env.home = QDir::homePath();
    env.data_home = qEnvironmentVariable("XDG_DATA_HOME");
    if (env.data_home.isEmpty() || !QDir::isAbsolutePath(env.data_home))
        env.data_home = QDir(env.home).filePath(QStringLiteral(".local/share"));
    env.version = QCoreApplication::applicationVersion();
    if (const QDateTime t = build_info::commit_time(); t.isValid() && !build_info::commit().isEmpty())
        env.built = t.toUTC().toString(Qt::ISODate);
    // Only a release is a package to install: a dev build ("-dev") runs from
    // its build tree. FINTERM_SETUP_DEV=1 lets a dev build try it out.
    const bool release = !env.version.contains(QLatin1Char('-')) || qEnvironmentVariableIntValue("FINTERM_SETUP_DEV") == 1;
#if defined(Q_OS_LINUX)
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (release && !appimage.isEmpty() && QFileInfo(appimage).isFile())
        env.package = QFileInfo(appimage).absoluteFilePath();
    const QString appdir = qEnvironmentVariable("APPDIR");
    for (const QString& rel : {QStringLiteral("usr/share/icons/hicolor/256x256/apps/finterm.png"),
                               QStringLiteral("finterm.png")})
        if (!appdir.isEmpty() && QFileInfo(QDir(appdir).filePath(rel)).isFile()) {
            env.package_icon = QDir(appdir).filePath(rel);
            break;
        }
#elif defined(Q_OS_MACOS)
    QDir d(QCoreApplication::applicationDirPath());
    if (release && d.cdUp() && d.cdUp() && d.absolutePath().endsWith(QLatin1String(".app")))
        env.package = d.absolutePath();
    env.system_apps_writable = QFileInfo(QStringLiteral("/Applications")).isWritable();
#endif
    return env;
}

QString install_path(const Env& env) {
#if defined(Q_OS_LINUX)
    return QDir(env.home).filePath(QStringLiteral("Applications/finterm.AppImage"));
#elif defined(Q_OS_MACOS)
    // A real bundle where one is; a symbolic link there (finterm.sh install
    // links ~/Applications to a source checkout's build) is not an install.
    const QString system = QStringLiteral("/Applications/finterm.app");
    const QString user = QDir(env.home).filePath(QStringLiteral("Applications/finterm.app"));
    const auto bundle = [](const QString& p) { return QFileInfo(p).isDir() && !QFileInfo(p).isSymLink(); };
    if (bundle(system))
        return system;
    if (bundle(user))
        return user;
    return env.system_apps_writable ? system : user;
#else
    Q_UNUSED(env);
    return {};
#endif
}

State state(const Env& env) {
    State st;
    st.path = install_path(env);
    if (st.path.isEmpty())
        return st;
    st.running_installed = same_file(env.package, st.path);
#if defined(Q_OS_LINUX)
    st.installed = QFileInfo(st.path).isFile();
    const QString desktop = desktop_path(env);
    st.foreign_launcher = QFileInfo::exists(desktop) && !ours(read_text(desktop));
    if (st.installed) {
        if (st.running_installed) {
            st.version = env.version;
            st.built = env.built;
        } else {
            st.version = linux_installed_version(env, st.path, &st.built);
            // What the installed copy said of itself when it last ran (the
            // in-app updater replaces it without touching the launcher).
            const QStringList rec = settings().value(QLatin1String(kInstalledKey)).toStringList();
            if (rec.size() == 3 && same_file(rec.at(0), st.path) && !rec.at(1).isEmpty()) {
                st.version = rec.at(1);
                st.built = rec.at(2);
            }
        }
    }
#elif defined(Q_OS_MACOS)
    // A link in its place (a source checkout's): not an install — replaced,
    // never kept as the version to roll back to.
    st.foreign_launcher = QFileInfo(st.path).isSymLink();
    st.installed = QFileInfo(st.path).isDir() && !st.foreign_launcher;
    if (st.installed) {
        st.version = mac_bundle_version(st.path);
        const QStringList rec = settings().value(QLatin1String(kInstalledKey)).toStringList();
        if (rec.size() == 3 && same_file(rec.at(0), st.path) && rec.at(1) == st.version)
            st.built = rec.at(2);
        if (st.running_installed)
            st.built = env.built;
    }
#endif
    st.found = !st.installed ? Found::Nothing : compare(st.version, env.version, st.built, env.built);
    return st;
}

/// The key "don't ask again" is kept under: the package's path — but a
/// translocated macOS app (Gatekeeper runs a quarantined one from a new random
/// folder each time) is one key for all its runs.
QString dont_ask_key(const QString& package) {
    if (package.contains(QLatin1String("/AppTranslocation/")))
        return QStringLiteral("AppTranslocation/") + QFileInfo(package).fileName();
    return package;
}

bool should_offer(const Env& env, const State& st) {
    if (env.package.isEmpty() || st.path.isEmpty() || st.running_installed)
        return false;
    return !settings().value(QLatin1String(kDontAskKey)).toStringList().contains(dont_ask_key(env.package));
}

bool favourite_offered() {
    return settings().value(QLatin1String(kFavouriteKey)).isValid();
}

void record_running_install(const Env& env) {
    const QString path = install_path(env);
    if (path.isEmpty() || !same_file(env.package, path))
        return;
    const QStringList rec{path, env.version, env.built};
    if (settings().value(QLatin1String(kInstalledKey)).toStringList() != rec) {
        settings().setValue(QLatin1String(kInstalledKey), rec);
        settings().sync();
    }
}

void set_dont_ask(const QString& package) {
    QStringList list = settings().value(QLatin1String(kDontAskKey)).toStringList();
    const QString key = dont_ask_key(package);
    if (!list.contains(key)) {
        list << key;
        settings().setValue(QLatin1String(kDontAskKey), list);
        settings().sync();
    }
}

Result install(const Env& env, const Options& opt) {
    Result r;
    const State st = state(env);
    if (env.package.isEmpty() || st.path.isEmpty()) {
        r.error = QStringLiteral("this copy of finterm is not an installable package");
        return r;
    }
    if (st.found == Found::Newer) {
        r.error = QStringLiteral("a newer finterm (%1) is installed in %2").arg(st.version, st.path);
        return r;
    }
    if (st.found == Found::Unknown && !opt.replace_unknown) {
        r.error = QStringLiteral("a finterm of unknown version is installed in %1 (it may be newer); replace it with "
                                 "--install --replace")
                      .arg(st.path);
        return r;
    }
    r.installed = st.path;
    if (st.running_installed) {
        r.ok = true;
        r.done << QStringLiteral("already running from ") + st.path;
        return r;
    }
    const QString dir = QFileInfo(st.path).absolutePath();
    if (!QDir().mkpath(dir)) {
        r.error = QStringLiteral("could not create %1").arg(dir);
        return r;
    }
    const QString previous = previous_path(st.path);

#if defined(Q_OS_LINUX)
    // The new image next to its place (same filesystem), then one rename: a
    // running copy keeps its old file, a crash leaves the old one in place.
    // The launch script first (it needs only the path): one that can't be
    // written stops the install before anything is replaced.
    if (!write_launch_script(st.path)) {
        r.error = QStringLiteral("could not write the launch script %1").arg(launch_script_path(st.path));
        return r;
    }
    const QString part = QDir(dir).filePath(QStringLiteral(".finterm.AppImage.part"));
    QFile::remove(part);
    if (!QFile::copy(env.package, part)) {
        r.error = QStringLiteral("could not copy %1 to %2").arg(env.package, dir);
        return r;
    }
    QFile::setPermissions(part, QFile::permissions(part) | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                                    QFileDevice::ReadOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    // The kept previous version is what a rollback restores: installing the
    // same version again keeps it (it would become a copy of this one).
    if (st.installed && st.found != Found::Same) {
        QFile::remove(previous);
        if (::link(QFile::encodeName(st.path).constData(), QFile::encodeName(previous).constData()) == 0)
            r.done << QStringLiteral("kept the version it replaces as ") + previous;
    }
    if (!replace_file(part, st.path)) {
        QFile::remove(part);
        r.error = QStringLiteral("could not put the new version in place at %1").arg(st.path);
        return r;
    }
    r.done << QStringLiteral("installed: ") + st.path;
    bool launcher_ok = true;
    r.done << write_launcher(env, st.path, /*add_command=*/true, &launcher_ok);
    if (!launcher_ok) {
        // (Written above already: only a failure after that lands here.)
        r.error = QStringLiteral("finterm is in %1, but its launcher could not be written (%2)")
                      .arg(st.path, launch_script_path(st.path));
        return r;
    }
    // Offered once: recorded whether it was taken or not (unticked), so it is
    // never offered again — not after an uninstall either. Only when it was
    // offered (an update shows no such choice).
    if (opt.favourite_offered && !opt.favourite && !settings().value(QLatin1String(kFavouriteKey)).isValid()) {
        settings().setValue(QLatin1String(kFavouriteKey), false);
    } else if (opt.favourite && !settings().value(QLatin1String(kFavouriteKey)).isValid()) {
        QStringList favs;
        if (gsettings_favourites(favs)) {
            // Asked once, at the first install; never again, even if removed.
            settings().setValue(QLatin1String(kFavouriteKey), false);
            if (!favs.contains(QStringLiteral("finterm.desktop"))) {
                favs << QStringLiteral("finterm.desktop");
                if (set_gsettings_favourites(favs)) {
                    settings().setValue(QLatin1String(kFavouriteKey), true);
                    r.done << QStringLiteral("added to favourites");
                }
            }
        }
    }
#elif defined(Q_OS_MACOS)
    const QString part = QDir(dir).filePath(QStringLiteral(".finterm-installing.app"));
    remove_path(part);
    if (!copy_bundle(env.package, part)) {
        remove_path(part);
        r.error = QStringLiteral("could not copy finterm to %1").arg(dir);
        return r;
    }
    run(QStringLiteral("/usr/bin/xattr"), {QStringLiteral("-dr"), QStringLiteral("com.apple.quarantine"), part});
    if (st.foreign_launcher) {
        // A link to a source checkout's build: replaced, not kept as a version.
        QFile::remove(st.path);
        r.done << QStringLiteral("replaced the link ") + st.path;
    }
    if (st.installed) {
        // Installing the same version again: the kept previous stays (see Linux).
        const QString aside = st.found == Found::Same ? QDir(dir).filePath(QStringLiteral(".finterm-replaced.app")) : previous;
        remove_path(aside);
        if (!QFile::rename(st.path, aside)) {
            remove_path(part);
            r.error = QStringLiteral("could not move the installed finterm aside (is it running?)");
            return r;
        }
        if (aside == previous)
            r.done << QStringLiteral("kept the version it replaces as ") + previous;
    }
    if (!QFile::rename(part, st.path)) {
        if (st.installed)
            QFile::rename(st.found == Found::Same ? QDir(dir).filePath(QStringLiteral(".finterm-replaced.app")) : previous,
                          st.path);
        remove_path(part);
        r.error = QStringLiteral("could not put finterm in %1").arg(dir);
        return r;
    }
    remove_path(QDir(dir).filePath(QStringLiteral(".finterm-replaced.app")));
    r.done << QStringLiteral("installed: ") + st.path;
    if ((opt.favourite || opt.favourite_offered) && !settings().value(QLatin1String(kFavouriteKey)).isValid()) {
        settings().setValue(QLatin1String(kFavouriteKey), false); // offered once (see Linux)
        if (opt.favourite && !mac_dock_has(st.path) && mac_dock_add(st.path)) {
            settings().setValue(QLatin1String(kFavouriteKey), true);
            r.done << QStringLiteral("added to the Dock");
        }
    }
#endif
    settings().setValue(QLatin1String(kInstalledKey), QStringList{st.path, env.version, env.built});
    settings().sync();
    LOG_INFO("Install", r.done.join(QStringLiteral("; ")));
    r.ok = true;
    return r;
}

void refresh_launcher(const Env& env) {
#if defined(Q_OS_LINUX)
    const State st = state(env);
    if (!st.running_installed)
        return;
    const QString desktop = desktop_path(env);
    const QString text = read_text(desktop);
    if (!ours(text))
        return; // none (the user removed it), or someone else's
    if (text != desktop_entry(env, st.path) || read_text(launch_script_path(st.path)) != launch_script_text(st.path) ||
        (!env.package_icon.isEmpty() && !QFileInfo::exists(icon_path(env))))
    {
        bool ok = true;
        const QStringList done = write_launcher(env, st.path, /*add_command=*/false, &ok);
        if (ok)
            LOG_INFO("Install", "Launcher brought up to date: " + done.join(QStringLiteral("; ")));
        else
            LOG_WARN("Install", "Launcher not brought up to date: " + done.join(QStringLiteral("; ")));
    }
#else
    Q_UNUSED(env);
#endif
}

Result uninstall(const Env& env) {
    Result r;
    const QString path = install_path(env);
    if (path.isEmpty()) {
        r.error = QStringLiteral("on Windows, uninstall finterm from Settings › Apps");
        return r;
    }
    r.installed = path;
    // Only an install: on macOS a link in its place is a source checkout's
    // (finterm.sh install), not ours to remove.
    const QFileInfo fi(path);
    const bool installed = fi.exists() && !fi.isSymLink();
    const QString previous = previous_path(path);
    QStringList gone = installed ? QStringList{path, previous} : QStringList{previous};
#if defined(Q_OS_LINUX)
    gone << launch_script_path(path);
#endif
    for (const QString& p : gone) {
        if (!remove_path(p)) {
            r.error = QStringLiteral("could not remove %1").arg(p);
            return r;
        }
    }
    r.done << (installed ? QStringLiteral("removed ") + path : QStringLiteral("nothing installed in ") + path);
#if defined(Q_OS_LINUX)
    if (ours(read_text(desktop_path(env)))) {
        QFile::remove(desktop_path(env));
        QFile::remove(icon_path(env));
        r.done << QStringLiteral("removed the launcher");
        refresh_desktop_caches(env);
    }
    const QFileInfo cmd(command_path(env));
    if (cmd.isSymLink() && (same_file(cmd.symLinkTarget(), launch_script_path(path)) ||
                            same_file(cmd.symLinkTarget(), path) || same_file(cmd.symLinkTarget(), previous))) {
        QFile::remove(command_path(env));
        r.done << QStringLiteral("removed the finterm command");
    }
    if (settings().value(QLatin1String(kFavouriteKey)).toBool()) {
        QStringList favs;
        if (gsettings_favourites(favs) && favs.removeAll(QStringLiteral("finterm.desktop")) > 0 &&
            set_gsettings_favourites(favs))
            r.done << QStringLiteral("removed from favourites");
    }
#elif defined(Q_OS_MACOS)
    if (settings().value(QLatin1String(kFavouriteKey)).toBool() && mac_dock_remove(path))
        r.done << QStringLiteral("removed from the Dock");
#endif
    // The record stays: a reinstall does not put back a favourite that was
    // offered once (the user may have removed it on purpose). Only one we
    // added is marked removed; never offered stays never offered.
    if (settings().value(QLatin1String(kFavouriteKey)).toBool())
        settings().setValue(QLatin1String(kFavouriteKey), false);
    settings().remove(QLatin1String(kInstalledKey));
    settings().sync();
    r.ok = true;
    return r;
}

bool launch_installed(const QString& installed, const QStringList& args) {
    QStringList all{QStringLiteral("--wait-for-pid"), QString::number(QCoreApplication::applicationPid())};
    all << args;
#if defined(Q_OS_MACOS)
    QStringList open_args{QStringLiteral("-n"), QStringLiteral("-a"), installed, QStringLiteral("--args")};
    open_args << all;
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), open_args, QDir::homePath());
#else
    // Through the launch script (mounted, or unpacked where that fails), as
    // the menu entry starts it — written only if it isn't there (an existing
    // one is the installed build's own, kept current by its refresh).
    QProcess p;
    const QString script = launch_script_path(installed);
    const bool have_script = QFileInfo(script).isExecutable() || write_launch_script(installed);
    p.setProgram(have_script ? script : installed);
    p.setArguments(all);
    p.setProcessEnvironment(clean_environment());
    p.setWorkingDirectory(QDir::homePath());
    return p.startDetached();
#endif
}

} // namespace finterm::install
