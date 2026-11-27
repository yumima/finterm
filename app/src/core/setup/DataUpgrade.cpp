#include "core/setup/DataUpgrade.h"

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace finterm::data_upgrade {

namespace {

constexpr const char* kStampFile = "data-version.json";
constexpr const char* kManifestFile = "manifest.json";
constexpr const char* kUpgradePrefix = "pre-upgrade-";
constexpr int kKeepUpgradeBackups = 3;

QString users_dir(const QString& data_dir) {
    return QDir(data_dir).filePath(QStringLiteral("users"));
}

QString auth_db(const QString& data_dir) {
    return QDir(data_dir).filePath(QStringLiteral("auth.db"));
}

QString now_tag() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

QString profile_suffix(const QString& profile) {
    return profile.isEmpty() || profile == QLatin1String("default") ? QString() : QStringLiteral("-") + profile;
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

/// A connection of its own to one database file, removed when it goes.
class Conn {
  public:
    explicit Conn(const QString& path, bool read_only)
        : name_(QStringLiteral("finterm_upgrade_") + QUuid::createUuid().toString(QUuid::Id128)) {
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        db_.setDatabaseName(path);
        if (read_only)
            db_.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db_.open()) {
            QSqlQuery q(db_);
            q.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
            if (!read_only)
                q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        }
    }
    ~Conn() {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool ok() const { return db_.isOpen(); }
    QString error() const { return db_.lastError().text(); }
    QSqlDatabase& db() { return db_; }

  private:
    QString name_;
    QSqlDatabase db_;
};

/// The highest schema_version row, 0 when the table is missing or empty, -1
/// when the file can't be read as a database.
int schema_version_of(const QString& path) {
    Conn c(path, /*read_only=*/true);
    if (!c.ok())
        return -1;
    QSqlQuery q(c.db());
    if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'")))
        return -1; // not a database (or damaged past reading its catalogue)
    if (!q.next())
        return 0;
    if (!q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}

/// "" when SQLite's structural check passes, else what it found.
QString check_db(const QString& path) {
    Conn c(path, /*read_only=*/true);
    if (!c.ok())
        return c.error();
    QSqlQuery q(c.db());
    if (!q.exec(QStringLiteral("PRAGMA quick_check")))
        return q.lastError().text();
    QStringList found;
    while (q.next())
        found << q.value(0).toString();
    if (found != QStringList{QStringLiteral("ok")})
        return found.join(QStringLiteral("; ")).left(300);
    return {};
}

/// Rows that refer to a missing row (PRAGMA foreign_key_check), -1 when it
/// can't be read. An upgrade may not add any; ones it found are not its own.
int fk_violations(const QString& path) {
    Conn c(path, /*read_only=*/true);
    if (!c.ok())
        return -1;
    QSqlQuery q(c.db());
    if (!q.exec(QStringLiteral("PRAGMA foreign_key_check")))
        return -1;
    int n = 0;
    while (q.next())
        ++n;
    return n;
}

/// A consistent copy of a database (its WAL included) at `dest`.
QString copy_db(const QString& src, const QString& dest) {
    {
        Conn c(src, /*read_only=*/true);
        if (c.ok()) {
            QSqlQuery q(c.db());
            q.prepare(QStringLiteral("VACUUM INTO ?"));
            q.addBindValue(dest);
            if (q.exec())
                return {};
            QFile::remove(dest);
        }
    }
    // Not readable as a database (it is copied as it is, so nothing is lost):
    // with its WAL, which holds committed changes the file may not have yet.
    if (!QFile::copy(src, dest))
        return QStringLiteral("could not copy %1").arg(QDir::toNativeSeparators(src));
    for (const char* side : {"-wal", "-shm"})
        if (QFileInfo::exists(src + side))
            QFile::copy(src + side, dest + side);
    return {};
}

/// Put `from` (a database file) in place of `to`, with no WAL left behind
/// that would belong to the file it replaces.
bool put_db(const QString& from, const QString& to) {
    for (const char* side : {"", "-wal", "-shm"})
        QFile::remove(to + side);
    if (!QFile::copy(from, to))
        return false;
    for (const char* side : {"-wal", "-shm"})
        if (QFileInfo::exists(from + side))
            QFile::copy(from + side, to + side);
    return true;
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


QString manifest_of(const QString& backup_dir) {
    return QDir(backup_dir).filePath(QString::fromLatin1(kManifestFile));
}

/// The pre-upgrade backups of `profile`, newest first.
QStringList upgrade_backups(const QString& backup_root, const QString& profile) {
    QVector<std::pair<QString, QString>> found; // (at, path)
    const QDir dir(backup_root);
    for (const QString& name : dir.entryList({QString::fromLatin1(kUpgradePrefix) + QLatin1Char('*')},
                                             QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(name);
        const QJsonObject m = read_json(manifest_of(path));
        if (m.value(QStringLiteral("profile")).toString() == profile)
            found.append({m.value(QStringLiteral("at")).toString(), path});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QStringList out;
    for (const auto& f : found)
        out << f.second;
    return out;
}

/// The databases of a backup that are put back, as (path in the backup, path
/// in the data folder): the user databases (auth.db is kept as it is now).
QVector<std::pair<QString, QString>> restorable_dbs(const QString& backup_dir, const QString& data_dir) {
    QVector<std::pair<QString, QString>> out;
    for (const auto& v : read_json(manifest_of(backup_dir)).value(QStringLiteral("databases")).toArray()) {
        const QString rel = v.toObject().value(QStringLiteral("file")).toString();
        // Only users/<name>.db: never a path that climbs out of the folder.
        if (!rel.startsWith(QLatin1String("users/")) || rel.contains(QLatin1String("..")) || rel.count(QLatin1Char('/')) != 1)
            continue;
        out.append({QDir(backup_dir).filePath(QStringLiteral("data/") + rel), QDir(data_dir).filePath(rel)});
    }
    return out;
}

QString key_of(const Plan& plan, const DbInfo& d) {
    return QDir(plan.data_dir).relativeFilePath(d.path) + QLatin1Char('@') + QString::number(d.version);
}

/// The databases a stamp by this build lists under `field`, as
/// "users/<name>.db@<version>": "left_as_is" — tried and left as they were
/// (damaged, or the migration failed), not tried again at every start with a
/// new backup each time; "deferred" — left to the user's login, backed up.
QSet<QString> keys_in(const Plan& plan, const QJsonObject& stamp, const char* field) {
    QSet<QString> out;
    if (plan.app_build.isEmpty() || stamp.value(QStringLiteral("build")).toString() != plan.app_build)
        return out; // another build (a fix of the same version, too) looks again
    for (const auto& v : stamp.value(QLatin1String(field)).toArray()) {
        const QJsonObject o = v.toObject();
        out.insert(o.value(QStringLiteral("file")).toString() + QLatin1Char('@') +
                   QString::number(o.value(QStringLiteral("version")).toInt()));
    }
    return out;
}

/// Below this build, and readable.
bool behind(const Plan& plan, const DbInfo& d) {
    return d.readable && d.version < plan.known_version;
}

/// To migrate now.
QVector<DbInfo> todo(const Plan& plan, const Inventory& inv, const QJsonObject& stamp) {
    const QSet<QString> left = keys_in(plan, stamp, "left_as_is");
    QVector<DbInfo> out;
    for (const auto& d : inv.user_dbs)
        if (behind(plan, d) && !left.contains(key_of(plan, d)) && !MigrationRunner::external_after(d.version))
            out.append(d);
    return out;
}

/// Left to the user's login (a pending step reaches outside the database),
/// and not yet backed up by this build: the login migrates it in place, so
/// the copy is taken now — a rollback needs it as much as any other.
QVector<DbInfo> deferred(const Plan& plan, const Inventory& inv, const QJsonObject& stamp) {
    const QSet<QString> done = keys_in(plan, stamp, "deferred");
    QVector<DbInfo> out;
    for (const auto& d : inv.user_dbs)
        if (behind(plan, d) && MigrationRunner::external_after(d.version) && !done.contains(key_of(plan, d)))
            out.append(d);
    return out;
}

} // namespace

QVector<DbInfo> Inventory::newer() const {
    QVector<DbInfo> out;
    for (const auto& d : user_dbs)
        if (d.version > known_version)
            out.append(d);
    return out;
}

Inventory inventory(const QString& data_dir, int known) {
    Inventory inv;
    inv.known_version = known;
    const QDir dir(users_dir(data_dir));
    for (const QString& name : dir.entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Name)) {
        DbInfo d;
        d.path = dir.filePath(name);
        const int v = schema_version_of(d.path);
        d.readable = v >= 0;
        d.version = std::max(v, 0);
        inv.user_dbs.append(d);
    }
    return inv;
}

int known_version() {
    return MigrationRunner::newest_version();
}

QJsonObject read_stamp(const QString& profile_root) {
    return read_json(QDir(profile_root).filePath(QString::fromLatin1(kStampFile)));
}

bool needs_upgrade(const Plan& plan) {
    const Inventory inv = inventory(plan.data_dir, plan.known_version);
    const QJsonObject stamp = read_stamp(plan.profile_root);
    return !todo(plan, inv, stamp).isEmpty() || !deferred(plan, inv, stamp).isEmpty();
}

Outcome upgrade(const Plan& plan) {
    Outcome out;
    const Inventory inv = inventory(plan.data_dir, plan.known_version);
    const QJsonObject stamp = read_stamp(plan.profile_root);
    const QVector<DbInfo> pending = todo(plan, inv, stamp);
    const QVector<DbInfo> later = deferred(plan, inv, stamp);
    if (pending.isEmpty() && later.isEmpty())
        return out;
    out.ran = true;
    const QDir data(plan.data_dir);

    // 1. The copy, before anything changes. ("from": the version that last
    // upgraded this data — none before 4.2.3 recorded it.)
    const bool same_build = !plan.app_build.isEmpty() && stamp.value(QStringLiteral("build")).toString() == plan.app_build;
    const QString from = same_build ? stamp.value(QStringLiteral("from")).toString()
                                    : stamp.value(QStringLiteral("version")).toString();
    out.backup_dir = unique_dir(QDir(plan.backup_root)
                                    .filePath(QStringLiteral("%1%2-to-%3-%4%5")
                                                  .arg(QString::fromLatin1(kUpgradePrefix),
                                                       from.isEmpty() ? QStringLiteral("older") : from,
                                                       plan.app_version, now_tag(), profile_suffix(plan.profile))));
    const auto give_up = [&out](const QString& why) {
        out.ok = false;
        out.problems << why;
        if (!out.backup_dir.isEmpty() && QFileInfo(out.backup_dir).isDir())
            QDir(out.backup_dir).removeRecursively(); // a partial copy is no backup
        out.backup_dir.clear();
        return out;
    };
    if (!make_private_dir(plan.backup_root, out.backup_dir))
        return give_up(QStringLiteral("Could not create the backup folder %1, so your data was not upgraded.")
                           .arg(QDir::toNativeSeparators(out.backup_dir)));
    QJsonArray databases;
    QStringList sources;
    if (QFileInfo::exists(auth_db(plan.data_dir)))
        sources << auth_db(plan.data_dir);
    for (const auto& d : inv.user_dbs)
        sources << d.path;
    for (const QString& src : sources) {
        const QString rel = data.relativeFilePath(src);
        const QString dest = QDir(out.backup_dir).filePath(QStringLiteral("data/") + rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (const QString err = copy_db(src, dest); !err.isEmpty())
            return give_up(QStringLiteral("Could not back up %1 (%2), so your data was not upgraded.")
                               .arg(QDir::toNativeSeparators(src), err));
        databases.append(QJsonObject{{QStringLiteral("file"), rel}, {QStringLiteral("version"), schema_version_of(dest)}});
    }
    const QString at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!write_json(manifest_of(out.backup_dir), QJsonObject{{QStringLiteral("kind"), QStringLiteral("pre-upgrade")},
                                                             {QStringLiteral("profile"), plan.profile},
                                                             {QStringLiteral("from"), from},
                                                             {QStringLiteral("to"), plan.app_version},
                                                             {QStringLiteral("at"), at},
                                                             {QStringLiteral("databases"), databases}}))
        return give_up(QStringLiteral("Could not write the backup's list of files, so your data was not upgraded."));
    LOG_INFO("DataUpgrade", "Backed up the data before upgrading it: " + out.backup_dir);

    // What the next start, and a rollback, read — written now, before the
    // first migration (a crash in one must still leave the way back), and
    // again when done. Every backup this build made is listed (a second pass
    // — a database that appeared later — adds one; the rollback takes each
    // database from the first that holds it); the databases left to their
    // user's login are those this build has now backed up; those it left as
    // they were (in an earlier pass, too) are not tried again.
    QJsonArray backups_by_build;
    if (same_build)
        for (const auto& v : stamp.value(QStringLiteral("backups")).toArray())
            if (QFileInfo(v.toString()).isDir())
                backups_by_build.append(v);
    backups_by_build.append(out.backup_dir);
    QJsonArray deferred_json = same_build ? stamp.value(QStringLiteral("deferred")).toArray() : QJsonArray();
    for (const auto& d : later)
        deferred_json.append(QJsonObject{{QStringLiteral("file"), data.relativeFilePath(d.path)},
                                         {QStringLiteral("version"), d.version}});
    QJsonArray left = same_build ? stamp.value(QStringLiteral("left_as_is")).toArray() : QJsonArray();
    const QString stamp_path = QDir(plan.profile_root).filePath(QString::fromLatin1(kStampFile));
    const auto write_stamp = [&]() {
        return write_json(stamp_path, QJsonObject{{QStringLiteral("version"), plan.app_version},
                                                  {QStringLiteral("build"), plan.app_build},
                                                  {QStringLiteral("schema"), plan.known_version},
                                                  {QStringLiteral("from"), from},
                                                  {QStringLiteral("at"), at},
                                                  {QStringLiteral("backups"), backups_by_build},
                                                  {QStringLiteral("left_as_is"), left},
                                                  {QStringLiteral("deferred"), deferred_json}});
    };
    if (!write_stamp())
        // Without it a rollback could not find the backup: change nothing.
        return give_up(QStringLiteral("Could not write %1, so your data was not upgraded.")
                           .arg(QDir::toNativeSeparators(stamp_path)));

    // 2. Every pending database, each checked after; one that fails goes back.
    const auto leave = [&](const DbInfo& d) {
        left.append(QJsonObject{{QStringLiteral("file"), data.relativeFilePath(d.path)},
                                {QStringLiteral("version"), d.version}});
    };
    for (const auto& d : pending) {
        const QString name = QFileInfo(d.path).completeBaseName();
        if (const QString before = check_db(d.path); !before.isEmpty()) {
            // Damaged before the upgrade: left exactly as it was (a copy is in
            // the backup); its user's login reports what is wrong, as before.
            out.ok = false;
            leave(d);
            out.problems << QStringLiteral("The data of user \"%1\" was already damaged (%2); it was left as it was.")
                                .arg(name, before);
            continue;
        }
        const int fk_before = fk_violations(d.path);
        QString failed;
        {
            Conn c(d.path, /*read_only=*/false);
            if (!c.ok()) {
                failed = c.error();
            } else {
                MigrationRunner runner(c.db());
                if (auto r = runner.run(); r.is_err())
                    failed = QString::fromStdString(r.error());
            }
        }
        if (failed.isEmpty())
            failed = check_db(d.path);
        if (failed.isEmpty() && fk_before >= 0) {
            const int fk_after = fk_violations(d.path);
            if (fk_after < 0)
                failed = QStringLiteral("its references could not be checked after the upgrade");
            else if (fk_after > fk_before)
                failed = QStringLiteral("the upgrade left rows that refer to missing rows");
        }
        if (failed.isEmpty()) {
            out.migrated << d.path;
            continue;
        }
        out.ok = false;
        leave(d);
        const QString copy = QDir(out.backup_dir).filePath(QStringLiteral("data/") + data.relativeFilePath(d.path));
        const bool back = put_db(copy, d.path);
        LOG_ERROR("DataUpgrade", QString("Upgrading %1 failed: %2 — %3")
                                     .arg(d.path, failed, back ? "put back" : "COULD NOT BE PUT BACK"));
        out.problems << (back ? QStringLiteral("The data of user \"%1\" could not be upgraded (%2). It was put back "
                                               "as it was.")
                                    .arg(name, failed)
                              : QStringLiteral("The data of user \"%1\" could not be upgraded (%2), and putting it "
                                               "back failed. A copy is in %3.")
                                    .arg(name, failed, QDir::toNativeSeparators(out.backup_dir)));
    }

    // 3. The record, with what this pass left as it was.
    if (!write_stamp()) {
        out.ok = false;
        out.problems << QStringLiteral("Could not record the upgrade in %1. A copy of the data from before it is in "
                                       "%2.")
                            .arg(QDir::toNativeSeparators(stamp_path), QDir::toNativeSeparators(out.backup_dir));
    }

    // 4. Keep the last few pre-upgrade backups of this profile, and every
    // one a rollback of this build would need.
    QSet<QString> needed;
    for (const auto& v : backups_by_build)
        needed.insert(v.toString());
    const QStringList all = upgrade_backups(plan.backup_root, plan.profile);
    for (int i = kKeepUpgradeBackups; i < all.size(); ++i)
        if (!needed.contains(all[i]))
            QDir(all[i]).removeRecursively();
    return out;
}

QString restore_for_rollback(const Plan& plan) {
    const QJsonObject stamp = read_stamp(plan.profile_root);
    if (plan.app_build.isEmpty() || stamp.value(QStringLiteral("build")).toString() != plan.app_build)
        return {}; // this build did not upgrade the data: the previous one reads it as it is
    // Every backup this build made, oldest first: each database comes back
    // from the first that holds it — as it was before this build touched it.
    // (No user can have been added since: the build being rolled back never
    // had its window up.)
    QVector<std::pair<QString, QString>> entries; // (copy in a backup, place in the data folder)
    QSet<QString> seen;
    QString first;
    for (const auto& v : stamp.value(QStringLiteral("backups")).toArray()) {
        const QString b = v.toString();
        if (!QFileInfo(b).isDir())
            continue;
        if (first.isEmpty())
            first = b;
        for (const auto& e : restorable_dbs(b, plan.data_dir))
            if (!seen.contains(e.second)) {
                seen.insert(e.second);
                entries.append(e);
            }
    }
    if (entries.isEmpty())
        return {};
    for (const auto& [from, to] : entries)
        if (!QFileInfo::exists(from))
            return QStringLiteral("the backup is missing %1").arg(QDir::toNativeSeparators(from));

    // What they replace is moved, never deleted, to rolled-back-<time>/.
    const QString aside = unique_dir(QDir(plan.backup_root)
                                         .filePath(QStringLiteral("rolled-back-%1%2")
                                                       .arg(now_tag(), profile_suffix(plan.profile))));
    if (!make_private_dir(plan.backup_root, aside))
        return QStringLiteral("could not create %1").arg(QDir::toNativeSeparators(aside));
    const QDir data(plan.data_dir);
    QVector<std::pair<QString, QString>> moved; // (kept, original)
    QStringList created;                         // put where nothing was
    const auto undo = [&]() {
        for (const QString& c : created)
            for (const char* side : {"", "-wal", "-shm"})
                QFile::remove(c + side);
        for (const auto& [kept, original] : moved) {
            // Whatever was copied in goes first: a backup's -wal left beside
            // the original would be replayed onto it.
            for (const char* side : {"", "-wal", "-shm"})
                QFile::remove(original + side);
            for (const char* side : {"", "-wal", "-shm"})
                if (QFileInfo::exists(kept + side))
                    QFile::rename(kept + side, original + side);
        }
        QDir(aside).removeRecursively();
    };
    for (const auto& [from, to] : entries) {
        const bool existed = QFileInfo::exists(to);
        if (existed) {
            const QString kept = QDir(aside).filePath(QStringLiteral("data/") + data.relativeFilePath(to));
            QDir().mkpath(QFileInfo(kept).absolutePath());
            if (!QFile::rename(to, kept)) {
                undo();
                return QStringLiteral("could not move %1 aside").arg(QDir::toNativeSeparators(to));
            }
            moved.append({kept, to});
            for (const char* side : {"-wal", "-shm"})
                if (QFileInfo::exists(to + side))
                    QFile::rename(to + side, kept + side);
        } else {
            created << to;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!put_db(from, to)) {
            undo();
            return QStringLiteral("could not copy %1 back").arg(QDir::toNativeSeparators(from));
        }
    }
    // The record now describes the data as the previous version left it.
    const QString stamp_path = QDir(plan.profile_root).filePath(QString::fromLatin1(kStampFile));
    QFile::rename(stamp_path, QDir(aside).filePath(QString::fromLatin1(kStampFile)));
    const QString was = read_json(manifest_of(first)).value(QStringLiteral("from")).toString();
    if (!was.isEmpty())
        write_json(stamp_path, QJsonObject{{QStringLiteral("version"), was},
                                           {QStringLiteral("at"),
                                            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
    LOG_INFO("DataUpgrade", QString("Rollback: the data is back as it was before %1; what it replaced is in %2")
                                .arg(plan.app_build, aside));
    return {};
}

} // namespace finterm::data_upgrade
