// The data upgrade at startup (core/setup/DataUpgrade): a backup before any
// change, every user database brought up to date and checked, a failed one put
// back, newer data recognised and the way back to it, the rollback's restore.

#include "core/setup/DataUpgrade.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace finterm;
namespace du = finterm::data_upgrade;

namespace {

constexpr int kKnown = 3;

/// Run SQL on a database file with a connection of its own.
QVariant sql(const QString& path, const QString& statement) {
    const QString name = QUuid::createUuid().toString();
    QVariant v;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(statement) && q.next())
                v = q.value(0);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return v;
}

int version_of(const QString& path) {
    return sql(path, QStringLiteral("SELECT MAX(version) FROM schema_version")).toInt();
}

/// A user database at schema `version` (rows as the runner writes them).
void make_db(const QString& path, int version, bool fail_next = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    sql(path, QStringLiteral("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, name TEXT NOT NULL, "
                             "applied_at TEXT)"));
    sql(path, QStringLiteral("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT)"));
    sql(path, QStringLiteral("INSERT INTO notes (body) VALUES ('mine')"));
    for (int v = 1; v <= version; ++v)
        sql(path, QStringLiteral("INSERT INTO schema_version (version, name) VALUES (%1, 'm%1')").arg(v));
    if (fail_next)
        sql(path, QStringLiteral("CREATE TABLE fail_me (x)"));
}

QJsonObject read_json(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace

class TestDataUpgrade : public QObject {
    Q_OBJECT

    QTemporaryDir tmp_;
    QString root_, backups_;

    du::Plan plan(const QString& build = QStringLiteral("4.2.3+abc")) const {
        du::Plan p;
        p.profile_root = root_;
        p.data_dir = root_ + "/user/data";
        p.backup_root = backups_;
        p.profile = QStringLiteral("default");
        p.app_version = QStringLiteral("4.2.3");
        p.app_build = build;
        p.known_version = kKnown;
        return p;
    }
    QString user_db(const QString& name) const { return root_ + "/user/data/users/" + name + ".db"; }
    QStringList backups(const QString& prefix) const {
        return QDir(backups_).entryList({prefix + "*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    }

  private slots:
    void initTestCase() {
        // Three steps; the third fails on a database holding a fail_me table.
        // Step one reaches outside the database (as v022 does the keyring).
        MigrationRunner::register_migration({1, "one", [](QSqlDatabase&) { return Result<void>::ok(); }, true});
        MigrationRunner::register_migration({2, "two", [](QSqlDatabase& db) {
                                                 QSqlQuery q(db);
                                                 q.exec("ALTER TABLE notes ADD COLUMN tag TEXT");
                                                 return Result<void>::ok();
                                             }});
        MigrationRunner::register_migration({3, "three", [](QSqlDatabase& db) {
                                                 QSqlQuery q(db);
                                                 q.exec("SELECT 1 FROM sqlite_master WHERE name='fail_me'");
                                                 if (q.next())
                                                     return Result<void>::err("step three refused");
                                                 q.exec("CREATE TABLE tags (id INTEGER PRIMARY KEY)");
                                                 return Result<void>::ok();
                                             }});
        QCOMPARE(du::known_version(), kKnown);
    }

    void init() {
        const QString base = tmp_.path() + "/" + QUuid::createUuid().toString(QUuid::Id128);
        root_ = base + "/finterm";
        backups_ = base + "/finterm-backups";
        QDir().mkpath(root_ + "/user/data/users");
    }

    void nothing_to_do_makes_no_backup() {
        make_db(user_db("ann"), kKnown);
        const auto out = du::upgrade(plan());
        QVERIFY(!out.ran);
        QVERIFY(out.ok);
        QVERIFY(!QFileInfo::exists(backups_));
        QVERIFY(du::read_stamp(root_).isEmpty());
    }

    void older_data_is_backed_up_then_upgraded() {
        make_db(user_db("ann"), 1);
        make_db(user_db("bob"), 2);
        make_db(root_ + "/user/data/auth.db", 0);
        const auto out = du::upgrade(plan());
        QVERIFY(out.ran);
        QVERIFY2(out.ok, qPrintable(out.problems.join("; ")));
        QCOMPARE(out.migrated.size(), 2);
        QCOMPARE(version_of(user_db("ann")), kKnown);
        QCOMPARE(version_of(user_db("bob")), kKnown);
        QCOMPARE(sql(user_db("ann"), "SELECT body FROM notes").toString(), QStringLiteral("mine"));

        // The copy is the data as it was, owner-only, with its manifest.
        QVERIFY(QFileInfo(out.backup_dir).fileName().startsWith("pre-upgrade-older-to-4.2.3-"));
        QCOMPARE(version_of(out.backup_dir + "/data/users/ann.db"), 1);
        QCOMPARE(version_of(out.backup_dir + "/data/users/bob.db"), 2);
        QVERIFY(QFileInfo::exists(out.backup_dir + "/data/auth.db"));
#ifndef Q_OS_WIN
        const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QCOMPARE(QFile::permissions(out.backup_dir) & others, QFileDevice::Permissions());
        QCOMPARE(QFile::permissions(backups_) & others, QFileDevice::Permissions());
#endif
        const QJsonObject m = read_json(out.backup_dir + "/manifest.json");
        QCOMPARE(m.value("profile").toString(), QStringLiteral("default"));
        QCOMPARE(m.value("databases").toArray().size(), 3);

        const QJsonObject stamp = du::read_stamp(root_);
        QCOMPARE(stamp.value("version").toString(), QStringLiteral("4.2.3"));
        QCOMPARE(stamp.value("build").toString(), QStringLiteral("4.2.3+abc"));
        QCOMPARE(stamp.value("backups").toArray().last().toString(), out.backup_dir);

        // Up to date now: the next start does nothing.
        QVERIFY(!du::upgrade(plan()).ran);
        QCOMPARE(backups("pre-upgrade-").size(), 1);
    }

    void a_failed_migration_is_put_back_and_not_retried() {
        make_db(user_db("ann"), 1);
        make_db(user_db("bad"), 2, /*fail_next=*/true);
        const auto out = du::upgrade(plan());
        QVERIFY(out.ran);
        QVERIFY(!out.ok);
        QCOMPARE(out.problems.size(), 1);
        QVERIFY(out.problems.first().contains("\"bad\""));
        QCOMPARE(version_of(user_db("ann")), kKnown); // the others still go ahead
        QCOMPARE(version_of(user_db("bad")), 2);      // as it was, step two not left half-applied
        QCOMPARE(sql(user_db("bad"), "SELECT body FROM notes").toString(), QStringLiteral("mine"));

        // Not again at every start (with a backup each time)…
        QVERIFY(!du::needs_upgrade(plan()));
        QVERIFY(!du::upgrade(plan()).ran);
        QCOMPARE(backups("pre-upgrade-").size(), 1);
        // A later pass of the same build (another database) still leaves it.
        make_db(user_db("new"), 1);
        QVERIFY(du::upgrade(plan()).ran);
        QVERIFY(!du::needs_upgrade(plan()));
        QCOMPARE(version_of(user_db("bad")), 2);
        // …but another build of the same version (a fix) tries again.
        QVERIFY(du::needs_upgrade(plan(QStringLiteral("4.2.3+fix"))));
    }

    void the_old_data_layout_is_upgraded_where_it_is() {
        // A profile whose move to user/ never happened keeps <profile>/data.
        QDir(root_ + "/user").removeRecursively();
        make_db(root_ + "/data/users/ann.db", 1);
        du::Plan p = plan();
        p.data_dir = root_ + "/data";
        const auto out = du::upgrade(p);
        QVERIFY(out.ran && out.ok);
        QCOMPARE(version_of(root_ + "/data/users/ann.db"), kKnown);
        QCOMPARE(version_of(out.backup_dir + "/data/users/ann.db"), 1);
    }

    void unreadable_files_are_left_alone() {
        QFile f(user_db("junk"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();
        const auto inv = du::inventory(root_ + "/user/data", kKnown);
        QCOMPARE(inv.user_dbs.size(), 1);
        QVERIFY(!inv.user_dbs.first().readable);
        QVERIFY(!du::needs_upgrade(plan()));
        QVERIFY(!du::upgrade(plan()).ran);
    }

    void newer_data_is_recognised_and_refused_by_the_runner() {
        make_db(user_db("ann"), kKnown + 2);
        const auto inv = du::inventory(root_ + "/user/data", kKnown);
        QCOMPARE(inv.newer().size(), 1);
        QVERIFY(!du::needs_upgrade(plan()));

        const QString name = QUuid::createUuid().toString();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(user_db("ann"));
            QVERIFY(db.open());
            MigrationRunner runner(db);
            const auto r = runner.run();
            QVERIFY(r.is_err());
            QVERIFY(QString::fromStdString(r.error()).contains("newer version of finterm"));
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        QCOMPARE(version_of(user_db("ann")), kKnown + 2); // untouched
    }

    void a_rollback_restores_only_this_builds_upgrade() {
        make_db(user_db("ann"), 1);
        QVERIFY(du::upgrade(plan()).ok);
        QCOMPARE(version_of(user_db("ann")), kKnown);

        // Another build rolling back: not its upgrade, nothing changes.
        QVERIFY(du::restore_for_rollback(plan(QStringLiteral("4.2.3+other"))).isEmpty());
        QCOMPARE(version_of(user_db("ann")), kKnown);

        QVERIFY(du::restore_for_rollback(plan()).isEmpty());
        QCOMPARE(version_of(user_db("ann")), 1);
        // Done once: the stamp no longer names this build.
        QVERIFY(du::read_stamp(root_).value("build").toString().isEmpty());
        QVERIFY(du::restore_for_rollback(plan()).isEmpty());
        QCOMPARE(backups("rolled-back-").size(), 1);
    }

#ifndef Q_OS_WIN
    void a_restore_that_fails_changes_nothing() {
        make_db(user_db("aaa"), 1);
        make_db(user_db("ann"), 1);
        const auto out = du::upgrade(plan());
        QVERIFY(out.ok);
        QFile::remove(user_db("aaa")); // deleted since: the restore puts it back first…
        // …then ann's copy can't be read, so the restore stops there.
        const QString copy = out.backup_dir + "/data/users/ann.db";
        QFile::setPermissions(copy, QFileDevice::Permissions());
        const QString err = du::restore_for_rollback(plan());
        QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QVERIFY(!err.isEmpty());
        QCOMPARE(version_of(user_db("ann")), kKnown);
        QVERIFY(!QFileInfo::exists(user_db("aaa")));
        QCOMPARE(du::read_stamp(root_).value("build").toString(), QStringLiteral("4.2.3+abc"));
        QVERIFY(backups("rolled-back-").isEmpty());
    }
#endif

    void a_rollback_leaves_other_users_where_they_are() {
        make_db(user_db("ann"), 1);
        QVERIFY(du::upgrade(plan()).ok);
        make_db(user_db("zed"), kKnown + 1); // would be "newer" on the go-back path
        QVERIFY(du::restore_for_rollback(plan()).isEmpty());
        QCOMPARE(version_of(user_db("ann")), 1);
        QCOMPARE(version_of(user_db("zed")), kKnown + 1); // left where it is, no placeholder
    }

    void a_rollback_after_two_passes_takes_each_from_its_first_backup() {
        make_db(user_db("ann"), 1);
        QVERIFY(du::upgrade(plan()).ok);
        make_db(user_db("bob"), 2); // appears later; the same build upgrades again
        QVERIFY(du::upgrade(plan()).ran);
        QCOMPARE(version_of(user_db("ann")), kKnown);
        QCOMPARE(version_of(user_db("bob")), kKnown);
        QCOMPARE(du::read_stamp(root_).value("backups").toArray().size(), 2);
        QVERIFY(du::restore_for_rollback(plan()).isEmpty());
        QCOMPARE(version_of(user_db("ann")), 1); // from the first backup, not the second (v3)
        QCOMPARE(version_of(user_db("bob")), 2);
    }

    void steps_reaching_outside_wait_for_the_users_login() {
        make_db(user_db("ancient"), 0); // step one (external) still to run
        make_db(user_db("ann"), 1);
        const auto out = du::upgrade(plan());
        QVERIFY(out.ok);
        QCOMPARE(out.migrated, QStringList{user_db("ann")});
        QCOMPARE(version_of(user_db("ancient")), 0);
        // …but it is in the backup: its login migrates it in place.
        QVERIFY(QFileInfo::exists(out.backup_dir + "/data/users/ancient.db"));
        QVERIFY(!du::needs_upgrade(plan()));
        // Alone, it is still backed up once (and only once).
        QFile::remove(user_db("ann"));
        QVERIFY(!du::upgrade(plan()).ran);
    }

    void the_last_three_backups_are_kept() {
        for (int i = 0; i < 5; ++i) {
            QFile::remove(user_db("ann"));
            make_db(user_db("ann"), 1);
            QVERIFY(du::upgrade(plan(QStringLiteral("4.2.3+b%1").arg(i))).ran);
        }
        QCOMPARE(backups("pre-upgrade-").size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestDataUpgrade)
#include "test_data_upgrade.moc"
