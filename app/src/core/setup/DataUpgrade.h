#pragma once
// DataUpgrade — bring a profile's data up to what this build expects, before
// anything uses it, and never lose the way back.
//
// The users' databases (<data>/users/<name>.db, <data> = AppPaths::data())
// carry the schema the versioned migrations build (storage/sqlite/migrations).
// Without this, each is migrated only when its user logs in, in place and with
// no copy. At startup instead, for the active profile:
//
//   inventory  every user database and its schema version, against the
//              highest this build has
//   newer      a database written by a newer finterm: nothing is opened or
//              changed; main() says so (and offers a backup to go back to)
//   upgrade    only when a step is pending — copy auth.db and every user
//              database (VACUUM INTO: consistent with WAL files present) to
//              <backups>/pre-upgrade-<from>-to-<to>-<time>[-<profile>]/
//              (owner-only, with a manifest), migrate every database, check
//              each one; a database whose migration or check fails is put
//              back from that copy. The last three such backups are kept.
//   stamp      <profile root>/data-version.json: the build that last
//              upgraded the data and its backup — what a rollback restores.
//
// Other profiles are upgraded when they are started (each is its own
// instance): their keyring entries are scoped to the running profile.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace finterm::data_upgrade {

struct DbInfo {
    QString path;
    int version = 0; ///< highest schema_version row; 0 when there is none
    bool readable = true;
    bool set_aside = false; ///< a placeholder a restore left (the real one is in a backup)
};

struct Inventory {
    QVector<DbInfo> user_dbs;
    int known_version = 0; ///< the highest migration this build has

    QVector<DbInfo> newer() const; ///< databases above known_version (placeholders aside)
};

/// The user databases under `data_dir` and their schema versions.
Inventory inventory(const QString& data_dir, int known_version);

/// The highest migration registered in this build (MigrationRunner).
int known_version();

struct Plan {
    QString profile_root; ///< where the stamp lives
    QString data_dir;     ///< the profile's data folder (AppPaths::data()): auth.db, users/
    QString backup_root;  ///< start_fresh::backup_root() in the app
    QString profile;      ///< "default" or the profile's name
    QString app_version;  ///< this build, e.g. "4.2.3"
    QString app_build;    ///< this build as the updater names it ("4.2.3+<commit>")
    int known_version = 0;
};

/// A database is below this build and not one this build already tried and
/// left as it was: upgrade() has work to do. A database whose pending steps
/// reach outside it (Migration::external — the keyring, cache.db) is left to
/// its user's login, as before: run for every user at once, one user's keys
/// would land under another's.
bool needs_upgrade(const Plan& plan);

struct Outcome {
    bool ran = false;        ///< a migration was pending
    bool ok = true;          ///< every database is at known_version and checked
    QString backup_dir;      ///< the pre-upgrade copy ("" when none was made)
    QStringList migrated;    ///< databases brought up to date
    QStringList problems;    ///< for the user: what failed, and that it was put back
};

/// Back up, migrate and check every pending user database of the profile.
/// Refuses (ok=false, nothing changed) when the backup can't be made.
Outcome upgrade(const Plan& plan);

/// Files the app itself left in the data folder that nothing reads any more,
/// moved (never deleted) to <backup_root>/tidied-<time>[-<profile>]/ with a
/// note saying what each was. Each is a named rule, not a sweep: a file the
/// rules don't name may be the user's own and is left where it is. Returns
/// the files moved.
QStringList tidy(const Plan& plan);

/// <profile_root>/data-version.json, or {} when there is none.
QJsonObject read_stamp(const QString& profile_root);

/// The newest pre-upgrade backup of `profile` under `backup_root` whose every
/// database is at or below `known_version` — what a build that finds newer
/// data can go back to. "" when there is none.
QString restorable_backup(const QString& backup_root, const QString& profile, int known_version);

/// Put the user databases of a pre-upgrade backup back. The ones they replace
/// — and any user database newer than this build (plan.known_version), which
/// it could not open — are moved (never deleted) to
/// <backup_root>/rolled-back-<time>[-<profile>]/; `set_aside` names those
/// users, whose login is then refused (a placeholder holds their place). auth.db is not put back (no migration changes it): users added since
/// keep their accounts. Nothing may have the databases open. "" on success,
/// else why — and then nothing was changed.
QString restore(const Plan& plan, const QString& backup_dir, QStringList* set_aside = nullptr);

/// For an update being rolled back by the build that failed to start
/// (plan.app_build): when it upgraded the data, put back each database as
/// its first backup by that build had it, so the previous version finds the
/// data it left. "" when there was
/// nothing to do or it was done; else why.
QString restore_for_rollback(const Plan& plan);

} // namespace finterm::data_upgrade
