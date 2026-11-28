// The package installing itself (core/setup/Install), Linux: the image put in
// place in one step with the replaced version kept, the launcher written and
// kept current, someone else's launcher kept, never a downgrade, and an
// uninstall that removes only what it added. Favourites are not touched here
// (they would be the desktop's real ones).

#include "core/config/AppIdentity.h"
#include "core/setup/Install.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
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
#else
        // The settings the installer records go to the temporary folder.
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp_.filePath("settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp_.filePath("settings"));
#endif
    }

    void init() {
        const QString base = tmp_.filePath(QString::number(QDateTime::currentMSecsSinceEpoch()) +
                                           QString::number(qrand_seed_++));
        env_ = {};
        env_.home = base + "/home";
        env_.data_home = env_.home + "/.local/share";
        env_.version = QStringLiteral("4.2.3");
        env_.package = write_package(base + "/Downloads/finterm-4.2.3-linux-x64-setup.run", "4.2.3");
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
        // The menu entry and the command start the launch script beside the
        // image (mounted, or unpacked when the runtime can't mount it).
        const QString script = env_.home + "/Applications/.finterm-launch";
        QVERIFY(text.contains("Exec=" + script + "\n"));
        QVERIFY(text.contains("TryExec=" + installed() + "\n"));
        QVERIFY(QFileInfo(script).isExecutable());
        QVERIFY(read(script).contains("APPIMAGE_EXTRACT_AND_RUN=1 exec"));
        QVERIFY(text.contains("X-Finterm-Installer=1"));
        QVERIFY(text.contains("X-Finterm-Version=4.2.3"));
        QVERIFY(text.contains("StartupWMClass=finterm"));
        QCOMPARE(QFileInfo(env_.home + "/.local/bin/finterm").symLinkTarget(), script);

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
        // The launch script removed (or out of date): a refresh puts it back.
        const QString script = env_.home + "/Applications/.finterm-launch";
        QFile::remove(script);
        install::refresh_launcher(env_);
        QVERIFY(QFileInfo(script).isExecutable());
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

    void the_launch_script_runs_mounted_or_unpacked() {
        // A stand-in image: --appimage-mount prints a mount point and holds it
        // when FAKE_FUSE=1, fails at once otherwise (as the runtime does
        // without FUSE); run, it says how it was started.
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/fuse.run", "4.2.3");
        QFile f(env_.package);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n"
                "if [ \"$1\" = --appimage-mount ]; then\n"
                "  [ \"$FAKE_FUSE\" = hang ] && { trap '' TERM; sleep 31.7; }\n"
                "  [ \"$FAKE_FUSE\" = 1 ] || { echo 'Cannot mount AppImage' >&2; exit 127; }\n"
                "  echo /tmp; exec sleep 31.7\n"
                "fi\n"
                "[ -n \"$APPIMAGE_EXTRACT_AND_RUN\" ] && { echo unpacked \"$@\"; exit 0; }\n"
                "echo mounted \"$@\"; [ \"$1\" = app-fails ] && exit 127; exit 0\n");
        f.close();
        QVERIFY(install::install(env_, {false}).ok);
        const QString script = env_.home + "/Applications/.finterm-launch";
        const auto run = [&](const char* fuse, const QStringList& args, int* code = nullptr) {
            QProcess p;
            QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
            e.remove("APPIMAGE_EXTRACT_AND_RUN"); // the test's own environment must not decide
            e.insert("FAKE_FUSE", QString::fromLatin1(fuse));
            e.insert("HOME", env_.home); // the launch log goes under HOME, not the real one
            p.setProcessEnvironment(e);
            p.start(script, args);
            if (!p.waitForFinished(30000)) {
                p.kill();
                p.waitForFinished();
                return QStringLiteral("(did not finish)");
            }
            if (code)
                *code = p.exitCode();
            return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        };
        QCOMPARE(run("1", {"--version"}), QStringLiteral("mounted --version"));
        QCOMPARE(run("0", {"--version"}), QStringLiteral("unpacked --version"));
        // Why it ran unpacked is in the launch log.
        QVERIFY(read(env_.home + "/.local/share/finterm/logs/launch.log").contains("Cannot mount AppImage"));
        // The app's own exit code (even 127) is its own: no second start.
        int code = 0;
        QCOMPARE(run("1", {"app-fails"}, &code), QStringLiteral("mounted app-fails"));
        QCOMPARE(code, 127);
        // A test mount that never answers and ignores TERM: unpacked after
        // the 10 s wait and a bounded stop — not a hung start.
        QElapsedTimer t;
        t.start();
        QCOMPARE(run("hang", {"--version"}), QStringLiteral("unpacked --version"));
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));

        // Interrupted while the test mount runs: the mount goes with it.
        {
            QProcess p;
            QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
            e.remove("APPIMAGE_EXTRACT_AND_RUN");
            e.insert("FAKE_FUSE", "hang");
            e.insert("HOME", env_.home);
            p.setProcessEnvironment(e);
            p.start(script, {"--version"});
            QVERIFY(p.waitForStarted());
            QTest::qWait(500);
            p.terminate(); // SIGTERM to the launcher
            QVERIFY(p.waitForFinished(5000));
            QTest::qWait(300);
            QProcess ps;
            ps.start("pgrep", {"-f", "^sleep 31\\.7$"});
            QVERIFY2(ps.waitForFinished(5000), "pgrep did not run");
            // pgrep: 0 a match, 1 none — anything else, it didn't look.
            QCOMPARE(ps.exitCode(), 1);
        }
    }

    void exec_paths_are_quoted() {
        // The script sits beside the image: its folder's name is what needs quoting.
        const QString odd = env_.home + "/My Apps $1/finterm.AppImage";
        const QString text = install::desktop_entry(env_, odd);
        QVERIFY2(text.contains("Exec=\"" + env_.home + "/My Apps \\\\$1/.finterm-launch\"\n"), qPrintable(text));
        // % is a field code in Exec: a literal one is doubled.
        QVERIFY(install::desktop_entry(env_, "/home/a%20b/finterm.AppImage").contains("Exec=/home/a%%20b/.finterm-launch\n"));
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

    void a_launcher_that_could_not_be_made_is_added_by_install_again() {
        // The applications folder can't be written at install time: the
        // install succeeds with a warning saying how to add it.
        const QString apps = env_.data_home + "/applications";
        QDir().mkpath(apps);
        QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(apps).isWritable())
            QSKIP("a read-only folder doesn't stop this user (root)");
        const auto r = install::install(env_, {false});
        QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(r.ok);
        QVERIFY(r.error.isEmpty());
        QVERIFY(r.warning.contains("could not write the launcher"));
        QVERIFY(r.warning.contains("'" + env_.home + "/Applications/.finterm-launch' --install"));
        QVERIFY(!QFileInfo::exists(desktop()));
        // A start doesn't make what isn't there (it may be the user's choice)…
        env_.package = installed();
        install::refresh_launcher(env_);
        QVERIFY(!QFileInfo::exists(desktop()));
        // …running --install again from the installed copy does.
        const auto again = install::install(env_, {false});
        QVERIFY(again.ok);
        QVERIFY(again.warning.isEmpty());
        QVERIFY(QFileInfo::exists(desktop()));
    }

    void someone_elses_launcher_stays_when_ours_cant_be_written() {
        QDir().mkpath(QFileInfo(desktop()).absolutePath());
        {
            QFile f(desktop());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Desktop Entry]\nName=finterm\nExec=/home/me/src/finterm/finterm.sh\n");
        }
        // The folder can't be written: ours can't be made.
        const QString apps = QFileInfo(desktop()).absolutePath();
        QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(apps).isWritable())
            QSKIP("a read-only folder doesn't stop this user (root)");
        install::install(env_, {false});
        QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(read(desktop()).contains("finterm.sh")); // still there, not moved aside
        QCOMPARE(QDir(apps).entryList(QDir::Files), QStringList{"finterm.desktop"}); // nothing left behind
    }

    void a_linked_or_read_only_entry_is_replaced_not_written_through() {
        const QString apps = QFileInfo(desktop()).absolutePath();
        QDir().mkpath(apps);
        // The user's entry is a link into their own files: the link is
        // replaced (theirs kept as .bak), the file it points to untouched.
        const QString theirs = env_.home + "/dotfiles/finterm.desktop";
        write_package(theirs, "x"); // any content
        const QByteArray before = read(theirs).toUtf8();
        QVERIFY(QFile::link(theirs, desktop()));
        QVERIFY(install::install(env_, {false}).ok);
        QCOMPARE(read(theirs).toUtf8(), before);
        QVERIFY(!QFileInfo(desktop()).isSymLink());
        QVERIFY(read(desktop()).contains("X-Finterm-Installer=1"));
        QVERIFY(QFileInfo::exists(desktop() + ".bak"));
        // Readable by all (as a menu entry is), the launch script runnable, no
        // temporaries left.
        QVERIFY(QFile::permissions(desktop()) & QFileDevice::ReadOther);
        QVERIFY(QFileInfo(env_.home + "/Applications/.finterm-launch").isExecutable());
        QCOMPARE(QDir(apps).entryList({".finterm.desktop.*"}, QDir::Files | QDir::Hidden), QStringList{});
        QCOMPARE(QDir(env_.home + "/Applications").entryList({"..finterm-launch.*"}, QDir::Files | QDir::Hidden),
                 QStringList{});
        // A read-only entry is replaced as well (the folder is writable).
        QFile::remove(desktop());
        write_package(desktop(), "y");
        QFile::setPermissions(desktop(), QFileDevice::ReadOwner);
        if (QFileInfo(desktop()).isWritable())
            QSKIP("a read-only file doesn't stop this user (root)");
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/again.run", "4.2.3");
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(read(desktop()).contains("X-Finterm-Installer=1"));
    }

    void a_dangling_link_is_replaced_and_hidden_files_kept() {
        const QString apps = QFileInfo(desktop()).absolutePath();
        QDir().mkpath(apps);
        QVERIFY(QFile::link(env_.home + "/deleted-checkout/finterm.desktop", desktop()));
        // A hidden file of the user's that looks like a temporary name.
        write_package(apps + "/.finterm.desktop.backup", "mine");
        const auto r = install::install(env_, {false});
        QVERIFY(r.ok);
        QVERIFY(r.warning.isEmpty());
        QVERIFY(read(desktop()).contains("X-Finterm-Installer=1"));
        QCOMPARE(QFileInfo(desktop() + ".bak").symLinkTarget(), env_.home + "/deleted-checkout/finterm.desktop");
        QVERIFY(read(apps + "/.finterm.desktop.backup").contains("mine"));
    }

    void temporaries_are_swept_only_when_old() {
        const QString apps = QFileInfo(desktop()).absolutePath();
        QDir().mkpath(apps);
        const QString fresh = apps + "/.finterm.desktop.finterm-tmp.AbC123";
        const QString aged = apps + "/.finterm.desktop.finterm-tmp.XyZ789";
        write_package(fresh, "f");
        write_package(aged, "a");
        {
            QFile f(aged);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        }
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(QFileInfo::exists(fresh)); // perhaps another finterm's write in progress
        QVERIFY(!QFileInfo::exists(aged));
        // Beside the launch script too.
        const QString beside = env_.home + "/Applications/..finterm-launch.finterm-tmp.QwE456";
        write_package(beside, "s");
        {
            QFile f(beside);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        }
        // (Swept when that file is next written: here, a script out of date.)
        {
            QFile f(env_.home + "/Applications/.finterm-launch");
            QVERIFY(f.open(QIODevice::Append));
            f.write("# an older text\n");
        }
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/again.run", "4.2.3");
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(!QFileInfo::exists(beside));
        // One dated more than a day ahead (a clock since put right) goes too.
        const QString ahead = apps + "/.finterm.desktop.finterm-tmp.FuT001";
        write_package(ahead, "x");
        {
            QFile f(ahead);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(3), QFileDevice::FileModificationTime));
        }
        env_.version = "4.2.3.1"; // (an install writes the menu entry, which sweeps beside it)
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/later.run", "4.2.3");
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(!QFileInfo::exists(ahead));
    }

    void a_start_sweeps_day_old_leftovers_even_when_all_is_current() {
        QVERIFY(install::install(env_, {false}).ok);
        env_.package = installed();
        const QString apps = QFileInfo(desktop()).absolutePath();
        const auto aged = [&](const QString& path, qint64 secs) {
            write_package(path, "t");
            QFile f(path);
            return f.open(QIODevice::ReadWrite) &&
                   f.setFileTime(QDateTime::currentDateTime().addSecs(-secs), QFileDevice::FileModificationTime);
        };
        QVERIFY(aged(apps + "/.finterm.desktop.finterm-tmp.Old001", 2 * 86400));
        QVERIFY(aged(apps + "/.finterm.desktop.finterm-tmp.New001", 2 * 3600)); // perhaps a write in progress
        QVERIFY(aged(apps + "/.finterm.desktop.finterm-tmp.Fut001", -40 * 86400)); // a clock since put right
        QVERIFY(aged(env_.home + "/Applications/.finterm-launch.finterm-tmp.Fut002", -3 * 86400)); // perhaps skew
        QVERIFY(aged(env_.home + "/Applications/.finterm-launch.part", 2 * 86400));
        QVERIFY(aged(env_.home + "/Applications/.finterm-launch.new", 2 * 3600)); // an older build writing now
        install::refresh_launcher(env_); // nothing stale: no rewrite
        QVERIFY(!QFileInfo::exists(apps + "/.finterm.desktop.finterm-tmp.Old001"));
        QVERIFY(QFileInfo::exists(apps + "/.finterm.desktop.finterm-tmp.New001"));
        QVERIFY(!QFileInfo::exists(apps + "/.finterm.desktop.finterm-tmp.Fut001"));
        QVERIFY(QFileInfo::exists(env_.home + "/Applications/.finterm-launch.finterm-tmp.Fut002"));
        QVERIFY(!QFileInfo::exists(env_.home + "/Applications/.finterm-launch.part"));
        QVERIFY(QFileInfo::exists(env_.home + "/Applications/.finterm-launch.new"));
        // --install from the installed copy sweeps too — beside the launch
        // script, which is current (no rewrite there to sweep it otherwise).
        QVERIFY(aged(env_.home + "/Applications/.finterm-launch.part", 2 * 86400));
        QVERIFY(install::install(env_, {false}).ok);
        QVERIFY(!QFileInfo::exists(env_.home + "/Applications/.finterm-launch.part"));
    }

    void links_are_kept_as_written_and_bak_names_never_collide() {
        const QString apps = QFileInfo(desktop()).absolutePath();
        QDir().mkpath(apps);
        // A relative link: its .bak says the same relative thing.
        QVERIFY(QFile::link("../somewhere/finterm.desktop", desktop()));
        QVERIFY(install::install(env_, {false}).ok);
        QCOMPARE(QFile::symLinkTarget(desktop() + ".bak").isEmpty(), false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        QCOMPARE(QFileInfo(desktop() + ".bak").readSymLink(), QStringLiteral("../somewhere/finterm.desktop"));
#endif
        // Again a dangling link where ours was: the dangling .bak is a taken name.
        QFile::remove(desktop());
        QVERIFY(QFile::link("../gone/finterm.desktop", desktop()));
        env_.package = write_package(QFileInfo(env_.package).absolutePath() + "/again.run", "4.2.3");
        const auto r = install::install(env_, {false});
        QVERIFY(r.ok);
        QVERIFY(r.warning.isEmpty());
        QVERIFY(QFileInfo(desktop() + ".bak2").isSymLink());
    }

    void a_linked_launch_script_is_replaced_by_ours() {
        QVERIFY(install::install(env_, {false}).ok);
        const QString script = env_.home + "/Applications/.finterm-launch";
        const QString theirs = env_.home + "/copy-of-launch";
        QVERIFY(QFile::copy(script, theirs));
        QFile::setPermissions(theirs, QFileDevice::ReadOwner | QFileDevice::WriteOwner); // same text, not runnable
        QFile::remove(script);
        QVERIFY(QFile::link(theirs, script));
        env_.package = installed();
        install::refresh_launcher(env_);
        QVERIFY(!QFileInfo(script).isSymLink());
        QVERIFY(QFileInfo(script).isExecutable());
        QCOMPARE(QFile::permissions(theirs) & QFileDevice::ExeOwner, QFileDevice::Permissions()); // theirs untouched
        // A mode the user narrowed (700) is theirs: kept at a start, and kept
        // when a new text replaces the script.
        const auto owner_only = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        const auto bits = [&] {
            return QFile::permissions(script) & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                                 QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                                                 QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther);
        };
        QFile::setPermissions(script, owner_only);
        install::refresh_launcher(env_);
        QCOMPARE(bits(), owner_only);
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::Append));
            f.write("# an older text\n");
        }
        install::refresh_launcher(env_);
        QVERIFY(!read(script).contains("an older text"));
        QCOMPARE(bits(), owner_only);
        // A wide mode (777) is narrowed by a rewrite: no write bit for others.
        QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                          QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                                          QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther);
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::Append));
            f.write("# an older text\n");
        }
        install::refresh_launcher(env_);
        QCOMPARE(bits() & (QFileDevice::WriteGroup | QFileDevice::WriteOther), QFileDevice::Permissions());
        // Runnable but not readable: sh can't open it — rewritten, readable
        // and runnable by the owner (and nothing more than it had).
        QFile::setPermissions(script, QFileDevice::ExeOwner);
        if (QFileInfo(script).isReadable())
            QSKIP("an unreadable file doesn't stop this user (root)");
        install::refresh_launcher(env_);
        QCOMPARE(bits(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    }

    void their_entry_stays_when_its_copy_cant_be_made() {
        // finterm.desktop is a directory (can't be copied): nothing moves,
        // nothing is left behind, the install says the launcher is missing.
        QDir().mkpath(desktop());
        const auto r = install::install(env_, {false});
        QVERIFY(r.ok);
        QVERIFY(r.warning.contains("could not write the launcher"));
        QVERIFY(QFileInfo(desktop()).isDir());
        QStringList left = QDir(QFileInfo(desktop()).absolutePath()).entryList(QDir::AllEntries | QDir::Hidden |
                                                                               QDir::NoDotAndDotDot);
        QCOMPARE(left, QStringList{"finterm.desktop"});
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
