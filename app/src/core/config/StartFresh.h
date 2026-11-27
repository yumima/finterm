#pragma once
// StartFresh — set finterm up again from the beginning, from inside the app:
// the way out of a forgotten PIN, a confused setup, or a state the user just
// wants gone, without hunting down folders by hand.
//
// What it does to the active profile, at the next start, before anything
// opens a database:
//   moved to <root>-backups/pre-reset-<time>/ — user/ (every account, the
//     portfolios, watchlists, notes, workspaces), and the settings file where
//     settings are a file. Nothing personal is deleted: the user can copy it
//     back, or delete the backup when sure.
//   removed — every credential this profile keeps in the OS keyring, the
//     cache, and (when asked) the Python runtime and downloaded models, which
//     the next start downloads again.
//   kept — logs (what to look at if something goes wrong), the public SEC
//     indexes, the voice models hearth shares with this folder, other profiles.

#include <QString>
#include <QStringList>

namespace finterm::start_fresh {

struct Options {
    /// Also remove the Python runtime, its environments and the downloaded
    /// models (gigabytes; the next start sets them up again, which takes a
    /// while and needs the network).
    bool remove_runtime = false;
};

/// Ask for a fresh start of the active profile at its next launch. "" or why
/// it could not be asked.
QString schedule(const Options& options);

/// Start this app again once this process has exited (the fresh start runs
/// in the new process). False when it could not be started.
bool relaunch();

struct Outcome {
    bool ran = false; ///< a fresh start was scheduled and attempted
    bool ok = false;
    QString backup_dir;  ///< where the previous data went
    QStringList problems; ///< what could not be done (shown to the user)
};

/// At startup — after the profile is chosen and the identity migrations ran,
/// before AppPaths::ensure_all() and anything that opens a database.
Outcome run_if_scheduled();

/// "--wait-for-pid N" (relaunch() passes it): wait, up to 30 s, for the
/// process that started this one to exit, so this one becomes the primary
/// instance. Call first thing in main().
void wait_for_parent(int argc, char* argv[]);

/// Where backups go: a sibling of the data folder, never inside it.
QString backup_root();

} // namespace finterm::start_fresh
