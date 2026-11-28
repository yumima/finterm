#include "core/setup/DataTransfer.h"

#include "core/logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>

namespace finterm::data_transfer {

namespace {

constexpr const char* kKind = "finterm-data";
constexpr int kFormat = 1;
constexpr const char* kMarker = "import.json";

QString tar_program() {
#ifdef Q_OS_WIN
    // Windows' own bsdtar, not one a PATH entry (Git's GNU tar) puts first:
    // that one reads C:\ as a remote host.
    const QString sys = qEnvironmentVariable("SystemRoot") + QStringLiteral("\\System32\\tar.exe");
    if (QFileInfo::exists(sys))
        return sys;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("tar"));
}

/// Run tar; "" on success, else what it said.
QString tar(const QStringList& args, QString* out = nullptr) {
    const QString program = tar_program();
    if (program.isEmpty())
        return QStringLiteral("this computer has no tar program");
    QProcess p;
    // macOS tar adds an AppleDouble "._name" entry for every file with extended
    // attributes: none of those in an export.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("COPYFILE_DISABLE"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    p.start(program, args);
    if (!p.waitForFinished(30 * 60 * 1000)) {
        p.kill();
        return QStringLiteral("tar did not finish");
    }
    if (out)
        *out = QString::fromUtf8(p.readAllStandardOutput());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QStringLiteral("tar failed: %1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed().left(300));
    return {};
}

int schema_version_of(const QString& path) {
    const QString name = QStringLiteral("finterm_transfer_") + QUuid::createUuid().toString(QUuid::Id128);
    int v = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'")))
                v = q.next() && q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) && q.next()
                        ? q.value(0).toInt()
                        : 0;
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return v;
}

/// A consistent copy of a database, even while the app writes to it.
bool vacuum_into(const QString& src, const QString& dest) {
    const QString name = QStringLiteral("finterm_transfer_") + QUuid::createUuid().toString(QUuid::Id128);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(src);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            q.exec(QStringLiteral("PRAGMA busy_timeout = 10000"));
            q.prepare(QStringLiteral("VACUUM INTO ?"));
            q.addBindValue(dest);
            ok = q.exec();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool skipped(const QString& name) {
    // A deleted account's database its removal could not finish deleting.
    if (name.contains(QLatin1String(".db.deleted")))
        return true;
    return name.endsWith(QLatin1String("-wal")) || name.endsWith(QLatin1String("-shm")) ||
           name.endsWith(QLatin1String(".lock")) || name.endsWith(QLatin1String("-journal"));
}

/// Copy `from` (a folder) to `to`, databases through VACUUM INTO. Adds each
/// user database (users/<name>.db) to `dbs`. "" or why.
QString stage_tree(const QString& from, const QString& to, const QString& rel_base, QJsonArray& dbs,
                   const QString& skip_dir = {}) {
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QFileInfo fi(src);
        if (fi.isSymLink() || skipped(fi.fileName()))
            continue;
        const QString rel = QDir(from).relativeFilePath(src);
        if (!skip_dir.isEmpty() && (rel == skip_dir || rel.startsWith(skip_dir + QLatin1Char('/'))))
            continue;
        const QString dest = QDir(to).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        const bool db = fi.suffix() == QLatin1String("db");
        if (!(db && vacuum_into(src, dest))) {
            // Not a database, or one VACUUM INTO could not read: copied as it
            // is — with its WAL, which holds committed changes the file may
            // not have yet (a failed VACUUM may have left a partial copy).
            QFile::remove(dest);
            if (!QFile::copy(src, dest))
                return QStringLiteral("could not copy %1").arg(QDir::toNativeSeparators(src));
            if (db)
                for (const char* side : {"-wal", "-shm"})
                    if (QFileInfo::exists(src + side) && !QFile::copy(src + side, dest + side))
                        return QStringLiteral("could not copy %1").arg(QDir::toNativeSeparators(src + side));
        }
        if (db && QFileInfo(rel_base + rel).path() == QLatin1String("user/data/users"))
            dbs.append(QJsonObject{{QStringLiteral("file"), rel_base + rel},
                                   {QStringLiteral("version"), schema_version_of(dest)}});
    }
    return {};
}

QJsonObject read_json(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool write_json(const QString& path, const QJsonObject& o) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

/// Only "manifest.json" and paths under "user/", none climbing out.
bool safe_entry(QString name) {
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.startsWith(QLatin1String("./")))
        name = name.mid(2);
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("manifest.json") ||
        name == QLatin1String("user") || name == QLatin1String("user/"))
        return true;
    // An absolute path or a drive letter never; a ':' elsewhere in a file name
    // ("notes 10:30.md") is fine.
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:"));
    if (name.startsWith(QLatin1Char('/')) || drive.match(name).hasMatch())
        return false;
    // An AppleDouble twin a Mac's tar added beside a file (under user/, or of
    // the manifest): harmless, removed after unpacking.
    if (name == QLatin1String("._manifest.json"))
        return true;
    if (!name.startsWith(QLatin1String("user/")))
        return false;
    for (const QString& part : name.split(QLatin1Char('/')))
        if (part == QLatin1String(".."))
            return false;
    return true;
}

/// Unpack and check `file` into `into`; "" or why.
/// Check `file` and unpack it into `into` — all of it, or (`manifest_only`,
/// for a check that must not need the space) just its manifest. "" or why.
QString unpack(const Where& w, const QString& file, const QString& into, bool manifest_only = false) {
    if (!QFileInfo(file).isFile())
        return QStringLiteral("%1 is not a file").arg(QDir::toNativeSeparators(file));
    QString listing;
    if (const QString err = tar({QStringLiteral("-tzf"), file}, &listing); !err.isEmpty())
        return QStringLiteral("not a finterm data file (%1)").arg(err);
    const QStringList names = listing.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : names)
        if (!safe_entry(line.trimmed()))
            return QStringLiteral("the file holds something other than finterm data (%1)").arg(line.trimmed());
    // Only files and folders, before anything is written: no links (symbolic
    // or hard), FIFOs or devices. The verbose listing's mode column, entry by
    // entry in the same order as the names.
    QString detail;
    if (const QString err = tar({QStringLiteral("-tvzf"), file}, &detail); !err.isEmpty())
        return QStringLiteral("not a finterm data file (%1)").arg(err);
    const QStringList modes = detail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < modes.size(); ++i) {
        const QString line = modes.at(i).trimmed();
        // l symbolic link, h hard link, p FIFO, c/b device, s socket.
        if (!line.isEmpty() && QStringLiteral("lhpcbs").contains(line.at(0)))
            return QStringLiteral("the file holds something other than files and folders (%1)")
                .arg(modes.size() == names.size() ? names.at(i).trimmed() : line);
    }
    if (!names.contains(QStringLiteral("manifest.json")) && !names.contains(QStringLiteral("./manifest.json")))
        return QStringLiteral("not a finterm data file (no manifest)");
    if (!names.contains(QStringLiteral("user/")) && !names.contains(QStringLiteral("user")) &&
        std::none_of(names.begin(), names.end(), [](const QString& n) { return n.startsWith(QLatin1String("user/")); }))
        return QStringLiteral("the file holds no data");
    QStringList xargs{QStringLiteral("-xzf"), file, QStringLiteral("-C"), into};
    if (manifest_only)
        xargs << QStringLiteral("manifest.json");
    if (const QString err = tar(xargs); !err.isEmpty())
        return err;
    // AppleDouble twins (see safe_entry) are not data.
    {
        QDirIterator twins(into, {QStringLiteral("._*")}, QDir::Files | QDir::Hidden | QDir::System,
                           QDirIterator::Subdirectories);
        while (twins.hasNext())
            QFile::remove(twins.next());
    }
    // No links: nothing may point out of the data folder once it is in place.
    QDirIterator it(into, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        if (QFileInfo(it.next()).isSymLink())
            return QStringLiteral("the file holds a link (%1)").arg(QDir(into).relativeFilePath(it.filePath()));
    const QJsonObject m = read_json(QDir(into).filePath(QStringLiteral("manifest.json")));
    if (m.value(QStringLiteral("kind")).toString() != QLatin1String(kKind))
        return QStringLiteral("not a finterm data file (no manifest)");
    if (m.value(QStringLiteral("format")).toInt() > kFormat)
        return QStringLiteral("made by a newer finterm (%1): update finterm first")
            .arg(m.value(QStringLiteral("app_version")).toString());
    for (const auto& v : m.value(QStringLiteral("databases")).toArray())
        if (v.toObject().value(QStringLiteral("version")).toInt() > w.known_version)
            return QStringLiteral("made by a newer finterm (%1): update finterm first")
                .arg(m.value(QStringLiteral("app_version")).toString());
    if (manifest_only)
        return {};
    if (!QFileInfo(QDir(into).filePath(QStringLiteral("user"))).isDir())
        return QStringLiteral("the file holds no data");
    // The databases themselves, not only what the manifest says of them.
    const QDir users(QDir(into).filePath(QStringLiteral("user/data/users")));
    for (const QString& name : users.entryList({QStringLiteral("*.db")}, QDir::Files))
        if (schema_version_of(users.filePath(name)) > w.known_version)
            return QStringLiteral("made by a newer finterm (%1): update finterm first")
                .arg(m.value(QStringLiteral("app_version")).toString());
    return {};
}

QString marker_path(const Where& w) {
    return QDir(w.profile_root).filePath(QString::fromLatin1(kMarker));
}

/// `base`, or base-2, base-3… — the first that does not exist yet.
QString unique_dir(const QString& base) {
    QString dir = base;
    for (int i = 2; QFileInfo::exists(dir); ++i)
        dir = base + QStringLiteral("-") + QString::number(i);
    return dir;
}

bool make_private_dir(const QString& root, const QString& dir) {
    if (!QDir().mkpath(dir))
        return false;
    const auto owner_only = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    return QFile::setPermissions(root, owner_only) && QFile::setPermissions(dir, owner_only);
}

} // namespace

Result export_to(const Where& w, const QString& file) {
    Result r;
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        r.error = QStringLiteral("no temporary folder");
        return r;
    }
    const QString staging = tmp.filePath(QStringLiteral("x"));
    QJsonArray dbs;
    // The data folder is the one the app uses (AppPaths::data(): an older
    // layout's <profile>/data while that one has the accounts), whatever else
    // sits in the other; the rest of user/ as it is.
    const QString user = QDir(w.profile_root).filePath(QStringLiteral("user"));
    if (QFileInfo(w.data_dir).isDir())
        if (const QString err = stage_tree(w.data_dir, QDir(staging).filePath(QStringLiteral("user/data")),
                                           QStringLiteral("user/data/"), dbs);
            !err.isEmpty()) {
            r.error = err;
            return r;
        }
    if (QFileInfo(user).isDir())
        if (const QString err = stage_tree(user, QDir(staging).filePath(QStringLiteral("user")), QStringLiteral("user/"),
                                           dbs, QStringLiteral("data"));
            !err.isEmpty()) {
            r.error = err;
            return r;
        }
    // An older layout's folders (data/, files/, workspaces/ beside user/),
    // taken as the new layout has them.
    for (const QString& sub : {QStringLiteral("files"), QStringLiteral("workspaces")}) {
        const QString legacy = QDir(w.profile_root).filePath(sub);
        const QString inside = QDir(staging).filePath(QStringLiteral("user/") + sub);
        if (QFileInfo(legacy).isDir() && !QFileInfo::exists(inside))
            if (const QString err = stage_tree(legacy, inside, QStringLiteral("user/") + sub + QLatin1Char('/'), dbs);
                !err.isEmpty()) {
                r.error = err;
                return r;
            }
    }
    if (dbs.isEmpty()) {
        r.error = QStringLiteral("this profile has no accounts to export");
        return r;
    }
    const QString at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!write_json(QDir(staging).filePath(QStringLiteral("manifest.json")),
                    QJsonObject{{QStringLiteral("kind"), QLatin1String(kKind)},
                                {QStringLiteral("format"), kFormat},
                                {QStringLiteral("app_version"), w.app_version},
                                {QStringLiteral("profile"), w.profile},
                                {QStringLiteral("at"), at},
                                {QStringLiteral("databases"), dbs}})) {
        r.error = QStringLiteral("could not write the manifest");
        return r;
    }
    // Written beside its place first: a failed export leaves no half file.
    const QString part = file + QStringLiteral(".part");
    QFile::remove(part);
    if (const QString err = tar({QStringLiteral("-czf"), part, QStringLiteral("-C"), staging, QStringLiteral("manifest.json"),
                                 QStringLiteral("user")});
        !err.isEmpty()) {
        QFile::remove(part);
        r.error = err;
        return r;
    }
    QFile::setPermissions(part, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::remove(file);
    if (!QFile::rename(part, file)) {
        QFile::remove(part);
        r.error = QStringLiteral("could not write %1").arg(QDir::toNativeSeparators(file));
        return r;
    }
    r.ok = true;
    r.file = file;
    r.databases = dbs.size();
    LOG_INFO("DataTransfer", QString("Exported %1 account(s) to %2").arg(dbs.size()).arg(file));
    return r;
}

QString check_archive(const Where& w, const QString& file) {
    // The listing and the manifest only: the whole archive may not fit in the
    // temporary folder (often a small tmpfs). The import checks the rest.
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return QStringLiteral("no temporary folder");
    return unpack(w, file, tmp.path(), /*manifest_only=*/true);
}

QString schedule_import(const Where& w, const QString& file) {
    // (The caller has checked the file — check_archive, off the GUI thread;
    // the import checks it again when it runs.)
    QDir().mkpath(w.profile_root);
    if (!write_json(marker_path(w), QJsonObject{{QStringLiteral("file"), QFileInfo(file).absoluteFilePath()},
                                                {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}))
        return QStringLiteral("could not write %1").arg(QDir::toNativeSeparators(marker_path(w)));
    return {};
}

Result import_now(const Where& w, const QString& file) {
    Result r;
    // Unpacked beside the profile (same filesystem: the move below is a rename).
    QDir().mkpath(w.profile_root);
    QTemporaryDir tmp(QDir(w.profile_root).filePath(QStringLiteral(".import-XXXXXX")));
    if (!tmp.isValid()) {
        r.error = QStringLiteral("no room to unpack the file");
        return r;
    }
    if (const QString err = unpack(w, file, tmp.path()); !err.isEmpty()) {
        r.error = err;
        return r;
    }
    // The current personal data goes aside, as Start fresh moves it.
    r.backup_dir = unique_dir(QDir(w.backup_root)
                       .filePath(QStringLiteral("pre-import-%1%2")
                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
                                          w.profile == QLatin1String("default") ? QString()
                                                                                : QStringLiteral("-") + w.profile)));
    QStringList moved;
    const auto put_back = [&]() {
        for (const QString& sub : moved)
            QFile::rename(QDir(r.backup_dir).filePath(sub), QDir(w.profile_root).filePath(sub));
    };
    for (const QString& sub : {QStringLiteral("user"), QStringLiteral("data"), QStringLiteral("files"),
                               QStringLiteral("workspaces")}) {
        const QString from = QDir(w.profile_root).filePath(sub);
        if (!QFileInfo(from).isDir())
            continue;
        if (moved.isEmpty() && !make_private_dir(w.backup_root, r.backup_dir)) {
            r.error = QStringLiteral("could not create the backup folder %1").arg(QDir::toNativeSeparators(r.backup_dir));
            return r;
        }
        if (!QFile::rename(from, QDir(r.backup_dir).filePath(sub))) {
            put_back();
            r.error = QStringLiteral("could not move %1 aside (is another finterm running?)").arg(QDir::toNativeSeparators(from));
            return r;
        }
        moved << sub;
    }
    // The upgrade record describes the data that went: it goes with it (its
    // lists of databases left alone would otherwise apply to the imported ones).
    const QString stamp = QDir(w.profile_root).filePath(QStringLiteral("data-version.json"));
    if (QFileInfo::exists(stamp)) {
        if (moved.isEmpty() && !make_private_dir(w.backup_root, r.backup_dir)) {
            r.error = QStringLiteral("could not create the backup folder %1").arg(QDir::toNativeSeparators(r.backup_dir));
            return r;
        }
        if (!QFile::rename(stamp, QDir(r.backup_dir).filePath(QStringLiteral("data-version.json")))) {
            put_back();
            r.error = QStringLiteral("could not move %1 aside").arg(QDir::toNativeSeparators(stamp));
            return r;
        }
        moved << QStringLiteral("data-version.json");
    }
    if (moved.isEmpty())
        r.backup_dir.clear();
    if (!QFile::rename(tmp.filePath(QStringLiteral("user")), QDir(w.profile_root).filePath(QStringLiteral("user")))) {
        put_back();
        r.error = QStringLiteral("could not put the imported data in place");
        return r;
    }
#ifndef Q_OS_WIN
    QFile::setPermissions(QDir(w.profile_root).filePath(QStringLiteral("user")),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
    r.databases = read_json(tmp.filePath(QStringLiteral("manifest.json"))).value(QStringLiteral("databases")).toArray().size();
    r.ok = true;
    r.file = file;
    LOG_INFO("DataTransfer", QString("Imported %1 account(s) from %2; the data it replaced is in %3")
                                 .arg(r.databases)
                                 .arg(file, r.backup_dir.isEmpty() ? QStringLiteral("(none)") : r.backup_dir));
    return r;
}

Outcome run_if_scheduled(const Where& w) {
    Outcome out;
    const QJsonObject m = read_json(marker_path(w));
    if (m.isEmpty())
        return out;
    QFile::remove(marker_path(w)); // once, whatever happens
    out.ran = true;
    out.result = import_now(w, m.value(QStringLiteral("file")).toString());
    return out;
}

} // namespace finterm::data_transfer
