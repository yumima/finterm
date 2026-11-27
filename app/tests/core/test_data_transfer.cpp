// A profile's personal data as one file (core/setup/DataTransfer): exported
// whole (an older layout's folders too), imported with the current data moved
// aside, and refused when the file is not finterm's, reaches outside user/,
// holds a link, or was made by a newer finterm.

#include "core/setup/DataTransfer.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace finterm;
namespace dt = finterm::data_transfer;

namespace {

void make_db(const QString& path, int version) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString name = QUuid::createUuid().toString();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        q.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT)");
        for (int v = 1; v <= version; ++v)
            q.exec(QString("INSERT INTO schema_version (version, name) VALUES (%1, 'm')").arg(v));
        q.exec("CREATE TABLE notes (body TEXT)");
        q.exec("INSERT INTO notes VALUES ('mine')");
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

void write(const QString& path, const QByteArray& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

/// The tar the module uses (Windows' own bsdtar: Git's GNU tar reads C:\ as a host).
QString tar_exe() {
#ifdef Q_OS_WIN
    return qEnvironmentVariable("SystemRoot") + "\\System32\\tar.exe";
#else
    return QStandardPaths::findExecutable("tar");
#endif
}

QByteArray read(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestDataTransfer : public QObject {
    Q_OBJECT

    QTemporaryDir tmp_;
    int n_ = 0;

    dt::Where where(const QString& name) {
        dt::Where w;
        w.profile_root = tmp_.filePath(name + "/finterm");
        w.data_dir = w.profile_root + "/user/data";
        w.backup_root = tmp_.filePath(name + "/finterm-backups");
        w.profile = "default";
        w.app_version = "4.2.3";
        w.known_version = 5;
        return w;
    }

  private slots:
    void initTestCase() {
        if (!QFileInfo::exists(tar_exe()))
            QSKIP("no tar here");
    }

    void a_profile_goes_to_another_computer() {
        const dt::Where a = where("a");
        make_db(a.data_dir + "/users/ann.db", 5);
        make_db(a.data_dir + "/auth.db", 0);
        write(a.profile_root + "/user/files/report.md", "draft");
#ifndef Q_OS_WIN // (NTFS has no ':' in names)
        write(a.profile_root + "/user/files/notes 10:30.md", "colon"); // a ':' in a name is fine
#endif

        write(a.data_dir + "/users/ann.db-wal", "x"); // left out (VACUUM INTO holds it)
        const QString file = tmp_.filePath("ann.tar.gz");
        const auto r = dt::export_to(a, file);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.databases, 1);

        const dt::Where b = where("b");
        write(b.data_dir + "/auth.db", "theirs"); // b had data: it is moved aside
        write(b.profile_root + "/data-version.json", "{}"); // and its upgrade record with it
        QVERIFY(dt::check_archive(b, file).isEmpty());
        const auto i = dt::import_now(b, file);
        QVERIFY2(i.ok, qPrintable(i.error));
        QVERIFY(QFileInfo::exists(b.data_dir + "/users/ann.db"));
        QVERIFY(!QFileInfo::exists(b.data_dir + "/users/ann.db-wal"));
        QCOMPARE(read(b.profile_root + "/user/files/report.md"), QByteArray("draft"));
#ifndef Q_OS_WIN
        QCOMPARE(read(b.profile_root + "/user/files/notes 10:30.md"), QByteArray("colon"));
#endif
        QCOMPARE(i.databases, 1);
        QCOMPARE(read(i.backup_dir + "/user/data/auth.db"), QByteArray("theirs"));
        QVERIFY(!QFileInfo::exists(b.profile_root + "/data-version.json"));
        QVERIFY(QFileInfo::exists(i.backup_dir + "/data-version.json"));
    }

    void an_older_layout_is_exported_as_the_new_one() {
        dt::Where a = where("legacy");
        a.data_dir = a.profile_root + "/data"; // what AppPaths uses while the accounts are there
        make_db(a.profile_root + "/data/users/old.db", 3);
        write(a.profile_root + "/user/data/stray.log", "x"); // not the data folder in use
        write(a.profile_root + "/workspaces/main.fwsp", "ws");
        const QString file = tmp_.filePath("legacy.tar.gz");
        QVERIFY(dt::export_to(a, file).ok);
        const dt::Where b = where("legacy-to");
        QVERIFY(dt::import_now(b, file).ok);
        QVERIFY(QFileInfo::exists(b.profile_root + "/user/data/users/old.db"));
        QVERIFY(!QFileInfo::exists(b.profile_root + "/user/data/stray.log"));
        QCOMPARE(read(b.profile_root + "/user/workspaces/main.fwsp"), QByteArray("ws"));
    }

    void a_scheduled_import_runs_once() {
        const dt::Where a = where("s");
        make_db(a.data_dir + "/users/ann.db", 2);
        const QString file = tmp_.filePath("s.tar.gz");
        QVERIFY(dt::export_to(a, file).ok);
        const dt::Where b = where("s-to");
        QVERIFY(dt::schedule_import(b, file).isEmpty());
        QVERIFY(dt::run_if_scheduled(b).ran);
        QVERIFY(QFileInfo::exists(b.data_dir + "/users/ann.db"));
        QVERIFY(!dt::run_if_scheduled(b).ran);
    }

    void files_that_are_not_finterm_data_are_refused() {
        const dt::Where b = where("r");
        write(b.data_dir + "/auth.db", "theirs");
        // Not an archive.
        write(tmp_.filePath("plain.txt"), "hello");
        QVERIFY(!dt::check_archive(b, tmp_.filePath("plain.txt")).isEmpty());
        // Something outside user/.
        const QString src = tmp_.filePath("evil");
        write(src + "/etc/x", "x");
        QCOMPARE(QProcess::execute(tar_exe(), {"-czf", tmp_.filePath("evil.tar.gz"), "-C", src, "etc"}), 0);
        QVERIFY(dt::check_archive(b, tmp_.filePath("evil.tar.gz")).contains("other than finterm data"));
#ifndef Q_OS_WIN // (a link there is a .lnk file, not a symbolic link)
        // A link inside user/.
        const QString lsrc = tmp_.filePath("link");
        write(lsrc + "/manifest.json", R"({"kind":"finterm-data","format":1,"databases":[]})");
        QDir().mkpath(lsrc + "/user");
        QVERIFY(QFile::link("/etc/passwd", lsrc + "/user/passwd"));
        QCOMPARE(QProcess::execute(tar_exe(), {"-czf", tmp_.filePath("link.tar.gz"), "-C", lsrc, "manifest.json", "user"}), 0);
        QVERIFY(dt::check_archive(b, tmp_.filePath("link.tar.gz")).contains("other than files and folders"));
        // A FIFO (opening it would hang the app) is no file either.
        const QString fsrc = tmp_.filePath("fifo");
        write(fsrc + "/manifest.json", R"({"kind":"finterm-data","format":1,"databases":[]})");
        QDir().mkpath(fsrc + "/user/data/users");
        QCOMPARE(QProcess::execute("mkfifo", {fsrc + "/user/data/users/alice.db"}), 0);
        QCOMPARE(QProcess::execute(tar_exe(), {"-czf", tmp_.filePath("fifo.tar.gz"), "-C", fsrc, "manifest.json", "user"}), 0);
        QVERIFY(dt::check_archive(b, tmp_.filePath("fifo.tar.gz")).contains("user/data/users/alice.db"));
#endif
        // Made by a newer finterm.
        const dt::Where a = where("newer");
        make_db(a.data_dir + "/users/ann.db", 9);
        QVERIFY(dt::export_to(a, tmp_.filePath("newer.tar.gz")).ok);
        QVERIFY(dt::check_archive(b, tmp_.filePath("newer.tar.gz")).contains("newer finterm"));
        // Nothing changed by any of them.
        QCOMPARE(read(b.data_dir + "/auth.db"), QByteArray("theirs"));
        QVERIFY(!QFileInfo::exists(b.backup_root));
    }
};

QTEST_GUILESS_MAIN(TestDataTransfer)
#include "test_data_transfer.moc"
