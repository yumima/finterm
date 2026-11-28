#pragma once
// DataTransfer — a profile's personal data as one file, to keep or to take to
// another computer, and back.
//
//   export   user/ (every account's database, files, workspaces, feature data,
//            agent memory) into <name>.tar.gz with a manifest: each database
//            copied with VACUUM INTO (consistent while the app runs), WAL and
//            lock files left out. It holds the accounts with their sign-in
//            details (auth.db: PIN checks), so it is as private as the data
//            folder. Keyring entries (service API keys, passwords) are not in
//            it: they stay in this computer's keyring.
//   import   asked for in Settings (or on the command line), done at the next
//            start before anything opens a database: the archive is checked
//            (only user/ and the manifest, no path leaving it, no links, no
//            database newer than this build), the profile's current personal
//            data is moved — never deleted — to <backups>/pre-import-<time>/,
//            and the archive's takes its place. The startup data upgrade then
//            brings it up to date.
//
// Uses the system's tar (Windows 10 1803+, macOS, Linux all have one).

#include <QString>
#include <QStringList>

namespace finterm::data_transfer {

struct Where {
    QString profile_root; ///< <root> or <root>/profiles/<name>
    QString data_dir;     ///< AppPaths::data(): <profile>/user/data, or an older layout's <profile>/data
    QString backup_root;  ///< start_fresh::backup_root()
    QString profile;      ///< "default" or the profile's name
    QString app_version;
    int known_version = 0; ///< the newest user-database schema this build reads
};

struct Result {
    bool ok = false;
    QString error;      ///< why not
    QString file;       ///< export: the archive written
    QString backup_dir; ///< import: where the replaced data went
    int databases = 0;
};

/// Write the profile's personal data to `file` (.tar.gz).
Result export_to(const Where& w, const QString& file);

/// Check an archive without changing anything — its listing and manifest
/// (not unpacked: the import checks the databases themselves): "" when it
/// can be imported.
QString check_archive(const Where& w, const QString& file);

/// Ask for `file` to be imported at the next start of this profile (check it
/// first with check_archive; the import checks it again). "" or why.
QString schedule_import(const Where& w, const QString& file);

/// Import now (nothing may have the profile's data open).
Result import_now(const Where& w, const QString& file);

struct Outcome {
    bool ran = false;
    Result result;
};

/// At startup, beside start_fresh::run_if_scheduled(): an import asked for in
/// the previous run.
Outcome run_if_scheduled(const Where& w);

} // namespace finterm::data_transfer
