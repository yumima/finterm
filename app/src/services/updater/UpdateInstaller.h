#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

/// How an update is put in place — the platform side of UpdateService.
///
/// A release build that can replace itself stages the verified download and,
/// when the app quits (or the user picks Restart Now), starts the embedded
/// helper (scripts/apply-update.sh / .ps1) detached: it waits for this process
/// to exit, keeps the previous version, puts the new one in place, optionally
/// relaunches, and leaves a status file the next launch reports.
///
///   Linux    the AppImage ($APPIMAGE) is replaced in one rename; the update is
///            staged beside it (same filesystem).
///   macOS    the .app bundle is swapped; the DMG is staged in the app data.
///   Windows  the IFW install is removed with its maintenance tool and the new
///            installer runs unattended into the same folder.
///
/// A version that fails to start twice after an update is rolled back: the
/// app records the update before handing over (pending boot), counts its own
/// starts early in main(), and confirms once its window is up.
namespace finterm::services::update_install {

struct Target {
    QString kind;        ///< "appimage" | "macapp" | "ifw"
    QString target;      ///< the AppImage / the .app bundle / the install folder
    QString previous;    ///< where the version being replaced is kept
    QString staging_dir; ///< where the verified download waits
};

/// Where this build can update itself, or nullopt (not an AppImage, a bundle
/// in a folder the user can't write, a non-IFW install…): then the update is
/// the installer the user runs, as before.
std::optional<Target> in_place_target();

/// The staged download's path for a version (in the target's staging dir).
QString staged_path(const Target& t, const QString& version, const QString& download_url);

/// What is installed at the target now (size + time of its main file): it
/// changes when another instance has applied an update there.
QString target_stamp(const Target& t);

/// target_stamp as it was when this process started (boot_check); empty when a
/// helper was at work then.
QString startup_stamp();

/// The helper (an update or a rollback) is at work.
bool helper_in_flight();

/// Remove downloads left staged by a session that was killed, or replaced by
/// a newer one (100+ MB each) — not while another instance or the helper may
/// be using one, and never the running or the kept previous version. (One
/// previous version is kept after it is replaced: releases are purged online,
/// so it is the only way back.) `keep`: the stored offer's staged file, which
/// a session that ended without quitting left to be used.
void sweep_stale(const Target& t, const QString& keep);

/// Start the helper detached. `relaunch`: start the new version once applied.
/// Records the pending boot (for the rollback) when it applies an update.
bool launch_helper(const Target& t, const QString& staged, const QString& from, const QString& to, bool relaunch,
                   bool rollback = false);

/// The helper's report of the last apply / rollback (consumed: read once, by
/// the profile that applied it).
std::optional<QJsonObject> take_status();

/// Another finterm (another profile, any build) runs from this install: an
/// update must wait — it would replace files under a running app.
bool other_instance_running();

/// Early in main(): count this start against a just-applied version; after two
/// starts that never confirmed, roll back and relaunch the previous version.
/// Returns true when the caller must exit now (the rollback took over).
bool boot_check();

/// The app is up (its window shown for a while, or quit cleanly): the applied
/// version is good.
void confirm_boot();

} // namespace finterm::services::update_install
