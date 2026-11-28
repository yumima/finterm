// The package installing itself (core/setup/Install), Linux: the image put in
// place in one step with the replaced version kept, the launcher written and
// kept current, someone else's launcher kept, never a downgrade, and an
// uninstall that removes only what it added. Favourites are not touched here
// (they would be the desktop's real ones).

#include "core/config/AppIdentity.h"
#include "core/setup/Install.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace finterm;

class TestInstall : public QObject {
    Q_OBJECT

    QTemporaryDir tmp_;
    install::Env env_;

    QString write_package(const QString& path, const QString& version) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        f.write(QStringLiteral("#!/bin/sh\necho \"finterm %1\"\n").arg(version).toUtf8());
        f.close();
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner);
        return path;
    }
    static QString read(const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    QString desktop() const { return env_.data_home + "/applications/finterm.desktop"; }
    QString installed() const { return env_.home + "/Applications/finterm.AppImage"; }

  private slots:
    void initTestCase() {
#ifndef Q_OS_LINUX
        QSKIP("Linux install only");
#endif
        // The settings the installer records go to the temporary folder.
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp_.filePath("settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp_.filePath("settings"));
    }

    void init() {
        const QString base = tmp_.filePath(QString::number(QDateTime::currentMSecsSinceEpoch()) +
                                           QString::number(qrand_seed_++));
        env_ = {};
        env_.home = base + "/home";
        env_.data_home = env_.home + "/.local/share";
        env_.version = QStringLiteral("4.2.3");
        env_.package = write_package(base + "/Downloads/finterm-4.2.3-linux-x64-setup.run", "4.2.3");
        env_.fuse2 = true;
        QDir().mkpath(env_.home);
        // Each test starts with no installer records.
        QSettings rec(AppIdentity::kOrg, AppIdentity::kApp);
        rec.clear();
        rec.sync();
    }

    void a_first_install_puts_the_image_and_launcher_in_place() {
        auto st = install::state(env_);
        QCOMPARE(st.found, install::Found::Nothing);
        QVERIFY(install::should_offer(env_, st));
        const auto r = install::install(env_, {/*favourite=*/false});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.installed, installed());
        QVERIFY(QFileInfo(installed()).isExecutable());
        const QString text = read(desktop());
        QVERIFY(text.contains("Exec=" + installed() + "\n"));
        QVERIFY(text.contains("X-Finterm-Installer=1"));
        QVERIFY(text.contains("X-Finterm-Version=4.2.3"));
        QVERIFY(text.contains("StartupWMClass=finterm"));
        QCOMPARE(QFileInfo(env_.home + "/.local/bin/finterm").symLinkTarget(), installed());

        // Running from there now: nothing to offer.
        env_.package = installed();
        st = install::state(env_);
        QVERIFY(st.running_installed);
        QVERIFY(!install::should_offer(env_, st));
    }

    void an_update_replaces_and_keeps_the_previous() {
        env_.version = "4.2.1";
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/old.run", "4.2.1");
        QVERIFY(install::install(env_, {false}).ok);

        env_.version = "4.2.3";
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/new.run", "4.2.3");
        const auto st = install::state(env_);
        QCOMPARE(st.version, QStringLiteral("4.2.1")); // read from our launcher
        QCOMPARE(st.found, install::Found::Older);
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(read(installed()).contains("finterm 4.2.3"));
        QVERIFY(read(env_.home + "/Applications/.finterm-previous.AppImage").contains("finterm 4.2.1"));
        QVERIFY(read(desktop()).contains("X-Finterm-Version=4.2.3"));
        QVERIFY(!QFileInfo::exists(env_.home + "/Applications/.finterm.AppImage.part"));

        // The same version again: the kept previous (4.2.1, what a rollback
        // restores) is not replaced by a copy of 4.2.3.
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/same.run", "4.2.3");
        QCOMPARE(install::state(env_).found, install::Found::Same);
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(read(env_.home + "/Applications/.finterm-previous.AppImage").contains("finterm 4.2.1"));
    }

    void a_release_built_again_is_told_apart() {
        // The same version, released again from a newer commit.
        env_.built = "2026-11-20T10:00:00Z";
        QVERIFY(install::install(env_, {false}).ok);
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/rebuilt.run", "4.2.3");
        env_.built = "2026-11-27T10:00:00Z";
        QCOMPARE(install::state(env_).found, install::Found::Older);
        env_.built = "2026-11-01T10:00:00Z"; // an older build of it: not over the newer
        QCOMPARE(install::state(env_).found, install::Found::Newer);
        env_.built = "2026-11-20T10:00:00Z";
        QCOMPARE(install::state(env_).found, install::Found::Same);
    }

    void the_favourite_is_offered_once() {
        // An uninstall with nothing installed does not count as an offer.
        QVERIFY(install::uninstall(env_).ok);
        QVERIFY(!install::favourite_offered());
        // Unticked at the install: offered all the same, never again.
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(install::favourite_offered());
    }

    void a_newer_install_is_never_replaced() {
        env_.version = "4.3.0";
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/newer.run", "4.3.0");
        QVERIFY(install::install(env_, {false}).ok);
        env_.version = "4.2.3";
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/older.run", "4.2.3");
        QCOMPARE(install::state(env_).found, install::Found::Newer);
        const auto r = install::install(env_, {false});
        QVERIFY(!r.ok);
        QVERIFY(read(installed()).contains("finterm 4.3.0"));
    }

    void someone_elses_launcher_is_kept() {
        QDir().mkpath(QFileInfo(desktop()).absolutePath());
        QFile f(desktop());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Desktop Entry]\nName=finterm\nExec=/home/me/src/finterm/finterm.sh\n");
        f.close();
        QVERIFY(install::state(env_).foreign_launcher);
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(read(desktop() + ".bak").contains("finterm.sh"));
        QVERIFY(read(desktop()).contains("X-Finterm-Installer=1"));
        QVERIFY(!install::state(env_).foreign_launcher);
    }

    void the_launcher_follows_the_install_but_is_not_recreated() {
        QVERIFY(install::install(env_, {false}).ok);
        env_.package = installed();
        // FUSE 2 went away: the launcher starts the image unpacked.
        env_.fuse2 = false;
        install::refresh_launcher(env_);
        QVERIFY(read(desktop()).contains("Exec=env APPIMAGE_EXTRACT_AND_RUN=1 " + installed()));
        // FUSE unknown this run (the test mount gave no answer in time): the
        // launcher's form stays as it is.
        const QString before = read(desktop());
        env_.fuse2 = true;
        env_.fuse_known = false;
        install::refresh_launcher(env_);
        QCOMPARE(read(desktop()), before);
        env_.fuse2 = false;
        env_.fuse_known = true;
        // The command the user removed is not put back by a refresh.
        QFile::remove(env_.home + "/.local/bin/finterm");
        env_.version = "4.2.4"; // the launcher is out of date: rewritten
        install::refresh_launcher(env_);
        QVERIFY(read(desktop()).contains("X-Finterm-Version=4.2.4"));
        QVERIFY(!QFileInfo::exists(env_.home + "/.local/bin/finterm"));
        // Removed by the user: stays removed.
        QFile::remove(desktop());
        install::refresh_launcher(env_);
        QVERIFY(!QFileInfo::exists(desktop()));
    }

    void exec_paths_are_quoted() {
        const QString odd = env_.home + "/My Apps/fin$term.AppImage";
        const QString text = install::desktop_entry(env_, odd);
        QVERIFY2(text.contains("Exec=\"" + env_.home + "/My Apps/fin\\\\$term.AppImage\"\n"), qPrintable(text));
        // % is a field code in Exec: a literal one is doubled.
        QVERIFY(install::desktop_entry(env_, "/home/a%20b/finterm.AppImage").contains("Exec=/home/a%%20b/finterm.AppImage\n"));
    }

    void an_image_we_did_not_install_is_not_run() {
        // Copied there by hand, no launcher of ours: its version is unknown —
        // and the file is never executed to ask (an old one opens a window).
        QDir().mkpath(env_.home + "/Applications");
        QFile f(installed());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\ntouch \"$0.ran\"\n");
        f.close();
        QFile::setPermissions(installed(), QFile::permissions(installed()) | QFileDevice::ExeOwner);
        const auto st = install::state(env_);
        QCOMPARE(st.found, install::Found::Unknown);
        QVERIFY(!QFileInfo::exists(installed() + ".ran"));
        // It may be newer: replaced only when the user says so.
        QVERIFY(!install::install(env_, {false}).ok);
        install::Options yes;
        yes.favourite = false;
        yes.replace_unknown = true;
        QVERIFY(install::install(env_, yes).ok);
        QVERIFY(!QFileInfo::exists(installed() + ".ran"));
    }

    void uninstall_removes_only_what_it_added() {
        QVERIFY(install::install(env_, {false}).ok);
        // The user's own file named like the command is not ours.
        QFile::remove(env_.home + "/.local/bin/finterm");
        write_package(env_.home + "/.local/bin/finterm", "mine");
        const auto r = install::uninstall(env_);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!QFileInfo::exists(installed()));
        QVERIFY(!QFileInfo::exists(desktop()));
        QVERIFY(read(env_.home + "/.local/bin/finterm").contains("mine"));
    }

    void a_command_link_of_the_users_own_is_left_alone() {
        QDir().mkpath(env_.home + "/.local/bin");
        QVERIFY(QFile::link(env_.home + "/src/finterm/build/finterm", env_.home + "/.local/bin/finterm"));
        QVERIFY(install::install(env_, {false}).ok);
        QCOMPARE(QFileInfo(env_.home + "/.local/bin/finterm").symLinkTarget(), env_.home + "/src/finterm/build/finterm");
        QVERIFY(install::uninstall(env_).ok);
        QVERIFY(QFileInfo(env_.home + "/.local/bin/finterm").isSymLink()); // still there
    }

    void dont_ask_is_per_file() {
        QVERIFY(install::should_offer(env_, install::state(env_)));
        install::set_dont_ask(env_.package);
        QVERIFY(!install::should_offer(env_, install::state(env_)));
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/another.run", "4.2.3");
        QVERIFY(install::should_offer(env_, install::state(env_)));
    }

  private:
    int qrand_seed_ = 0;
};

QTEST_GUILESS_MAIN(TestInstall)
#include "test_install.moc"
