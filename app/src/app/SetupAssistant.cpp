#include "app/SetupAssistant.h"

#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/config/StartFresh.h"
#include "core/logging/Logger.h"
#include "core/setup/DataTransfer.h"
#include "core/setup/Install.h"
#include "core/setup/SetupPaths.h"
#include "services/updater/UpdateInstaller.h"
#include "screens/settings/DataTransferDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QtConcurrent/QtConcurrentRun>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdio>

#ifdef Q_OS_WIN
#    include <windows.h>
#endif

namespace finterm::setup_assistant {

namespace {

void print_json(const QJsonObject& o) {
#ifdef Q_OS_WIN
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* console = nullptr;
        freopen_s(&console, "CONOUT$", "w", stdout);
    }
#endif
    std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
}

QJsonObject report(const install::Result& r, const char* action) {
    return QJsonObject{{QStringLiteral("action"), QLatin1String(action)},
                       {QStringLiteral("ok"), r.ok},
                       {QStringLiteral("installed"), r.installed},
                       {QStringLiteral("done"), QJsonArray::fromStringList(r.done)},
                       {QStringLiteral("error"), r.error}};
}

/// The arguments the installed copy is started with: this run's profile.
QStringList profile_args() {
    const QString p = ProfileManager::instance().active();
    return p == QLatin1String("default") ? QStringList() : QStringList{QStringLiteral("--profile"), p};
}

/// What the installer did, appended to <logs>/install.log: it runs before the
/// app's log file is set, and hands over to the installed copy right after.
void log_install(const install::Result& r) {
    const QString dir = AppPaths::logs();
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QStringLiteral("install.log")));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    const QString when = QDateTime::currentDateTime().toString(Qt::ISODate);
    f.write(QStringLiteral("%1 %2 %3%4\n")
                .arg(when, r.ok ? QStringLiteral("ok") : QStringLiteral("FAILED"),
                     r.done.join(QStringLiteral("; ")), r.error.isEmpty() ? QString() : QStringLiteral(" — ") + r.error)
                .toUtf8());
}

/// The profile has data (an account database) — where AppPaths keeps it,
/// whichever layout it is on.
bool has_data() {
    return QFileInfo::exists(QDir(AppPaths::data()).filePath(QStringLiteral("auth.db")));
}

QString favourite_label() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("Keep in the Dock");
#else
    return QStringLiteral("Add to favourites (the dock)");
#endif
}

/// A disk image the package runs from goes away once this process has exited.
void eject_disk_image_later(const QString& package) {
#if defined(Q_OS_MACOS)
    if (!package.startsWith(QLatin1String("/Volumes/")))
        return;
    const QString volume = package.section(QLatin1Char('/'), 0, 2);
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), QStringLiteral("sleep 4; /usr/bin/hdiutil detach \"$0\" -quiet"), volume});
#else
    Q_UNUSED(package);
#endif
}

enum class Choice { Install, OpenInstalled, RunThis, Quit };

struct Answer {
    Choice choice = Choice::Quit;
    bool favourite = true;
    bool favourite_offered = false; ///< the checkbox was shown
    bool start_fresh = false;
    bool dont_ask = false;
    QString import_file; ///< an exported data file to start with ("" = none)
};

Answer ask(const install::Env& env, const install::State& st, bool running_now) {
    using install::Found;
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("finterm %1").arg(env.version));
    dlg.setMinimumWidth(520);
    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(24, 20, 24, 16);
    lay->setSpacing(12);

    const QString where = QDir::toNativeSeparators(st.path);
    QString title, text, primary;
    switch (st.found) {
    case Found::Nothing:
        title = QStringLiteral("Install finterm %1").arg(env.version);
        text = QStringLiteral("finterm is not installed yet. Installing puts it in %1 and adds it to your "
                              "applications.")
                   .arg(where);
        primary = QStringLiteral("Install");
        break;
    case Found::Unknown:
        title = QStringLiteral("Replace the installed finterm?");
        text = QStringLiteral("finterm is installed in %1, but its version can't be told (it was not installed by "
                              "this installer). If it is newer than %2, replacing it means going back to an older "
                              "version.")
                   .arg(where, env.version);
        primary = QStringLiteral("Replace It");
        break;
    case Found::Older:
        title = QStringLiteral("Update finterm to %1").arg(env.version);
        text = QStringLiteral("finterm %1 is installed in %2. Updating replaces it and keeps the version it replaces "
                              "beside it.")
                   .arg(st.version, where);
        primary = QStringLiteral("Update");
        break;
    case Found::Same:
        title = QStringLiteral("finterm %1 is installed").arg(env.version);
        text = QStringLiteral("This version is already installed in %1.").arg(where);
        primary = QStringLiteral("Open Installed");
        break;
    case Found::Newer:
        title = QStringLiteral("A newer finterm is installed");
        text = QStringLiteral("finterm %1 is installed in %2; this download is %3, which is older.")
                   .arg(st.version, where, env.version);
        primary = QStringLiteral("Open finterm %1").arg(st.version);
        break;
    }
    if (running_now)
        text += QStringLiteral("\n\nfinterm is running now: quit it to use the installed version.");
    if (has_data() && st.found != Found::Newer)
        text += QStringLiteral("\n\nYour data is kept, and brought up to date (a copy is made first) when it needs "
                               "to be.");
#if defined(Q_OS_LINUX)
    if (st.foreign_launcher && st.found != Found::Newer)
        text += QStringLiteral("\n\nThere is already a finterm launcher (from a source checkout). It is kept as "
                               "finterm.desktop.bak.");
#endif

    auto* head = new QLabel(title);
    QFont f = head->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    f.setBold(true);
    head->setFont(f);
    lay->addWidget(head);
    auto* body = new QLabel(text);
    body->setWordWrap(true);
    lay->addWidget(body);

    const bool installing = st.found != Found::Same && st.found != Found::Newer;
    QCheckBox* favourite = nullptr;
    if (installing && st.found == Found::Nothing && !install::favourite_offered()) {
        favourite = new QCheckBox(favourite_label());
        favourite->setChecked(true);
        lay->addWidget(favourite);
    }
    QCheckBox* fresh = nullptr;
    if (has_data() && st.found != Found::Newer) {
        fresh = new QCheckBox(QStringLiteral("Start fresh: move my data to a backup folder and begin empty"));
        lay->addWidget(fresh);
    }
    // A new computer: start from the data exported on the old one.
    QCheckBox* import = nullptr;
    QString import_file;
    if (!has_data() && st.found != Found::Newer) {
        import = new QCheckBox(QStringLiteral("Start with my data from an exported file…"));
        lay->addWidget(import);
        QObject::connect(import, &QCheckBox::toggled, &dlg, [&dlg, import, &import_file](bool on) {
            if (!on) {
                import_file.clear();
                import->setText(QStringLiteral("Start with my data from an exported file…"));
                return;
            }
            const QString f = QFileDialog::getOpenFileName(&dlg, QStringLiteral("Exported finterm data"),
                                                           QDir::homePath(), QStringLiteral("finterm data (*.tar.gz)"));
            const QString problem = f.isEmpty() ? QString() : screens::data_transfer_dialog::check_file(&dlg, f);
            if (f.isEmpty() || !problem.isEmpty()) {
                if (!problem.isEmpty())
                    QMessageBox::warning(&dlg, QStringLiteral("finterm"),
                                         QStringLiteral("This file can't be imported: %1.").arg(problem));
                QSignalBlocker block(import);
                import->setChecked(false);
                return;
            }
            import_file = f;
            import->setText(QStringLiteral("Start with my data from %1").arg(QFileInfo(f).fileName()));
        });
    }
    auto* dont_ask = new QCheckBox(QStringLiteral("Don't ask again when I run this file"));
    lay->addWidget(dont_ask);

    auto* buttons = new QDialogButtonBox;
    QPushButton* main_btn = buttons->addButton(primary, QDialogButtonBox::AcceptRole);
    QPushButton* reinstall = nullptr;
    if (st.found == Found::Same)
        reinstall = buttons->addButton(QStringLiteral("Install Again"), QDialogButtonBox::ActionRole);
    // With finterm already running, this process can only hand over to it.
    QPushButton* run_this = buttons->addButton(running_now ? QStringLiteral("Open the Running finterm")
                                                           : QStringLiteral("Run Without Installing"),
                                               QDialogButtonBox::ActionRole);
    QPushButton* quit = buttons->addButton(QStringLiteral("Quit"), QDialogButtonBox::RejectRole);
    main_btn->setDefault(true);
    lay->addWidget(buttons);

    Answer a;
    QObject::connect(main_btn, &QPushButton::clicked, &dlg, [&] {
        a.choice = installing ? Choice::Install : Choice::OpenInstalled;
        dlg.accept();
    });
    if (reinstall)
        QObject::connect(reinstall, &QPushButton::clicked, &dlg, [&] {
            a.choice = Choice::Install;
            dlg.accept();
        });
    QObject::connect(run_this, &QPushButton::clicked, &dlg, [&] {
        a.choice = Choice::RunThis;
        dlg.accept();
    });
    QObject::connect(quit, &QPushButton::clicked, &dlg, &QDialog::reject);
    // "Don't ask again" means: this file runs as it is.
    QObject::connect(dont_ask, &QCheckBox::toggled, &dlg, [main_btn, reinstall](bool on) {
        main_btn->setEnabled(!on);
        if (reinstall)
            reinstall->setEnabled(!on);
    });
    dlg.exec();
    a.favourite = favourite && favourite->isChecked();
    a.favourite_offered = favourite != nullptr;
    a.start_fresh = fresh && fresh->isChecked();
    a.dont_ask = dont_ask->isChecked();
    a.import_file = import && import->isChecked() ? import_file : QString();
    return a;
}

} // namespace

int handle_command_line() {
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--install"))) {
        install::Options opt;
        // Offered on a first install only (an update shows no such choice).
        const auto env = install::current_env();
        const bool first = install::state(env).found == install::Found::Nothing;
        opt.favourite = first && !args.contains(QStringLiteral("--no-favourite"));
        opt.favourite_offered = first;
        opt.replace_unknown = args.contains(QStringLiteral("--replace"));
        const auto r = install::install(env, opt);
        log_install(r);
        print_json(report(r, "install"));
        return r.ok ? 0 : 1;
    }
    if (args.contains(QStringLiteral("--uninstall"))) {
        const auto r = install::uninstall(install::current_env());
        log_install(r);
        QJsonObject o = report(r, "uninstall");
        o[QStringLiteral("data_kept")] = QDir::toNativeSeparators(AppPaths::root());
        print_json(o);
        return r.ok ? 0 : 1;
    }
    return -1;
}

bool offer(bool another_running) {
    if (QCoreApplication::arguments().contains(QStringLiteral("--portable")) ||
        QCoreApplication::arguments().contains(QStringLiteral("--selftest")))
        return true;
    // An update (or rollback) being put in place: not now — the package may
    // be half replaced; the updater's own start check says what is going on.
    if (services::update_install::helper_in_flight())
        return true;
    const install::Env env = install::current_env();
    const install::State st = install::state(env);
    if (st.running_installed) {
        install::refresh_launcher(env);
        install::record_running_install(env);
        return true;
    }
    if (!install::should_offer(env, st))
        return true;

    const Answer a = ask(env, st, another_running);
    if (a.dont_ask)
        install::set_dont_ask(env.package);
    if (a.choice == Choice::Quit)
        return false;
    if (a.start_fresh) {
        QDir().mkpath(ProfileManager::instance().profile_root());
        if (const QString err = start_fresh::schedule({}); !err.isEmpty())
            QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                                 QStringLiteral("Could not ask for a fresh start: %1.\n\nYour data is kept.").arg(err));
    }
    // Done by whichever copy starts next (this one, or the installed one),
    // before anything opens a database.
    if (!a.import_file.isEmpty())
        if (const QString err = data_transfer::schedule_import(setup_paths::current_where(), a.import_file);
            !err.isEmpty())
            QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                                 QStringLiteral("Could not import %1: %2.").arg(a.import_file, err));
    switch (a.choice) {
    case Choice::Quit:
        return false;
    case Choice::RunThis:
        return true;
    case Choice::OpenInstalled:
        break;
    case Choice::Install: {
        install::Options opt;
        opt.favourite = a.favourite;
        opt.favourite_offered = a.favourite_offered;
        opt.replace_unknown = true; // the user saw it can't be told, and chose Replace
        // Copying a few hundred MB: off the GUI thread, behind a note.
        QProgressDialog note(QStringLiteral("Installing finterm…"), QString(), 0, 0);
        note.setWindowTitle(QStringLiteral("finterm"));
        note.setCancelButton(nullptr);
        note.setMinimumDuration(0);
        note.show();
        QFutureWatcher<install::Result> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<install::Result>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([env, opt] { return install::install(env, opt); }));
        if (!watcher.isFinished())
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        note.close();
        const install::Result r = watcher.result();
        log_install(r);
        if (!r.ok) {
            QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                                 QStringLiteral("finterm could not be installed: %1.\n\nThis copy runs instead.")
                                     .arg(r.error));
            return true;
        }
        break;
    }
    }
    if (!install::launch_installed(st.path, profile_args())) {
        QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                             QStringLiteral("finterm is installed in %1 but could not be started from there. This copy "
                                            "runs instead.")
                                 .arg(QDir::toNativeSeparators(st.path)));
        return true;
    }
    eject_disk_image_later(env.package);
    return false;
}

} // namespace finterm::setup_assistant
