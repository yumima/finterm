#pragma once
// Install — the downloaded package puts itself in place and adds its launcher.
//
//   Linux    the AppImage is copied to ~/Applications/finterm.AppImage (no
//            version in the name, so the launcher, pins and the in-app updater
//            keep one target); a launcher (finterm.desktop, marked as ours),
//            its icon, a `finterm` command in ~/.local/bin; on GNOME, once, a
//            favourite. The launcher and the command start a small script
//            beside the image: mounted through FUSE, or unpacked (always
//            works) when the runtime can't mount it.
//   macOS    the bundle is copied to /Applications (or ~/Applications when
//            that can't be written), quarantine cleared; once, a Dock tile.
//   Windows  the QtIFW installer does it; nothing here.
//
// An existing install is replaced in one rename and the version it replaces is
// kept beside it, under the name the updater's rollback uses. A pin or
// favourite the user removed is never put back: it is added on the first
// install only, and recorded.

#include <QString>
#include <QStringList>

namespace finterm::install {

/// Where things go. From the environment in the app; a test builds its own.
struct Env {
    QString home;           ///< $HOME
    QString data_home;      ///< $XDG_DATA_HOME, or ~/.local/share
    QString package;        ///< what is running: the AppImage file / the .app bundle; "" when not a package
    QString package_icon;   ///< Linux: an icon inside the running image, "" when none
    QString version;        ///< this build's version
    QString built;          ///< this build's commit time (ISO, UTC); "" for a dev build — tells two builds of one version apart
    bool system_apps_writable = true; ///< macOS: /Applications can be written
};

Env current_env();

/// Where this platform installs to (the file or bundle), given what may
/// already be installed. "" on Windows.
QString install_path(const Env& env);

enum class Found { Nothing, Older, Same, Newer, Unknown };

struct State {
    QString path;            ///< install_path()
    bool installed = false;  ///< something is there
    QString version;         ///< its version, "" when it can't be told
    QString built;           ///< its commit time when recorded (see Env::built), else ""
    Found found = Found::Nothing;
    bool running_installed = false; ///< this process is that install
    bool foreign_launcher = false;  ///< Linux: a finterm.desktop that isn't ours (e.g. finterm.sh install)
};

State state(const Env& env);

/// Whether the running package should offer to install itself: a package,
/// not run from its install location, and not marked "don't ask" for this
/// file.
bool should_offer(const Env& env, const State& st);

struct Options {
    bool favourite = true; ///< GNOME favourite / Dock tile — on a first install only
    /// The user was asked (and said no when !favourite): recorded, never asked
    /// again. False when no choice was shown (an update).
    bool favourite_offered = true;
    /// Replace an install whose version can't be told (copied there by hand):
    /// it may be newer. Only when the user said so.
    bool replace_unknown = false;
};

struct Result {
    bool ok = false;
    QString installed;   ///< the installed file / bundle
    QStringList done;    ///< one line per step, for the log and --install's report
    QString error;       ///< why it stopped (nothing half-done is left behind)
    QString warning;     ///< installed, but this is missing (the menu entry, the command)
};

/// Put the running package in place (replacing an older or same version),
/// then the launcher. Not with a newer version there, nor one of unknown
/// version unless Options::replace_unknown: those are refused.
Result install(const Env& env, const Options& opt);

/// The launcher of an install that is running from its place: rewritten when
/// it is ours and out of date (another path, version, FUSE fallback); never
/// created again when the user removed it. Quick no-op otherwise.
void refresh_launcher(const Env& env);

/// Remove the install, the previous version kept beside it, the launcher,
/// icon, command and the favourite we added. The data is never touched: the
/// user deletes its folder, or uses Start fresh first.
Result uninstall(const Env& env);

/// Start the installed copy once this process has exited (it becomes the
/// primary instance). `args` go to it.
bool launch_installed(const QString& installed, const QStringList& args);

/// The favourite / Dock tile was offered on an earlier install (it is never
/// offered again, even after an uninstall).
bool favourite_offered();

/// "Don't ask again" for this package file.
void set_dont_ask(const QString& package);

/// The install this process runs as: note its version and build (the in-app
/// updater replaces it without the installer), so a later download compares
/// against what is really there.
void record_running_install(const Env& env);

/// Testing seams: the launcher text that install() writes.
QString desktop_entry(const Env& env, const QString& installed);

} // namespace finterm::install
