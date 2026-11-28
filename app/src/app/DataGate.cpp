#include "app/DataGate.h"

#include "core/config/AppPaths.h"
#include "core/config/LayoutMigrations.h"
#include "core/logging/Logger.h"
#include "core/setup/DataUpgrade.h"
#include "core/setup/SetupPaths.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QLabel>
#include <QLockFile>
#include <QtConcurrent/QtConcurrentRun>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>
#include <QVBoxLayout>

#include <cstdio>

#ifdef Q_OS_WIN
#    include <windows.h>
#endif

namespace finterm::data_gate {

namespace {

data_upgrade::Plan plan() {
    return setup_paths::current_plan();
}

/// Data a newer finterm wrote: say so; it is never opened.
void newer_data(const data_upgrade::Plan& p) {
    const QString writer = data_upgrade::read_stamp(p.profile_root).value(QStringLiteral("version")).toString();
    LOG_WARN("DataGate", QString("Data in %1 is newer than this build (%2): not opened").arg(p.profile_root, p.app_version));
    QMessageBox::warning(
        nullptr, QStringLiteral("finterm"),
        QStringLiteral("Your data was last used by a newer finterm%1.\n\nThis is finterm %2. It does not open that "
                       "data, because it could damage it. Start the newer finterm instead, or install it again from "
                       "github.com/yumima/finterm.\n\nCopies of your data from before each upgrade are in %3.")
            .arg(writer.isEmpty() || writer == p.app_version ? QString() : QStringLiteral(" (%1)").arg(writer),
                 p.app_version, QDir::toNativeSeparators(p.backup_root)));
}

void say_json(const QJsonObject& o) {
#ifdef Q_OS_WIN
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* console = nullptr;
        freopen_s(&console, "CONOUT$", "w", stdout);
    }
#endif
    std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
}

/// The data folder is this process's alone: no other finterm has it open.
/// `lock` holds it while it lives.
bool take_data(QLockFile& lock, const QString& data_dir) {
    QDir().mkpath(data_dir);
    lock.setStaleLockTime(0);
    return !AppPaths::data_dir_in_use(data_dir) && lock.tryLock(0);
}

} // namespace

void import_if_scheduled() {
    const data_transfer::Where w = setup_paths::current_where();
    if (!QFileInfo::exists(QDir(w.profile_root).filePath(QStringLiteral("import.json"))))
        return;
    // Like Start fresh: never while another finterm (or its helper) still has
    // the data open — the import waits for the next start. (Another instance
    // of this profile can't be starting: this is past SingleApplication's
    // check; another build starting in the same instant is not guarded.)
    if (AppPaths::data_dir_in_use(w.data_dir)) {
        QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                             QStringLiteral("Your data could not be imported yet: another finterm is still using it.\n\n"
                                            "Close every finterm window; the import is done at the next start."));
        return;
    }
    const auto out = data_transfer::run_if_scheduled(w);
    if (!out.ran)
        return;
    if (!out.result.ok) {
        QMessageBox::warning(nullptr, QStringLiteral("finterm"),
                             QStringLiteral("Your data could not be imported: %1.\n\nNothing was changed.")
                                 .arg(out.result.error));
        return;
    }
    QString text = QStringLiteral("Your data was imported (%1 account%2).").arg(out.result.databases).arg(out.result.databases == 1 ? "" : "s");
    if (!out.result.backup_dir.isEmpty())
        text += QStringLiteral("\n\nThe data it replaced was moved, not deleted, to:\n%1")
                    .arg(QDir::toNativeSeparators(out.result.backup_dir));
    text += QStringLiteral("\n\nAPI keys and service passwords kept in the system keyring are not part of an "
                           "export: enter them again in Settings › Credentials.");
    QMessageBox::information(nullptr, QStringLiteral("finterm"), text);
}

int transfer_command_line() {
    const QStringList args = QCoreApplication::arguments();
    const qsizetype ex = args.indexOf(QStringLiteral("--export"));
    const qsizetype im = args.indexOf(QStringLiteral("--import"));
    if (ex < 0 && im < 0)
        return -1;
    register_all_migrations();
    const bool exporting = ex >= 0;
    const qsizetype at = exporting ? ex : im;
    QJsonObject o{{QStringLiteral("action"), exporting ? QStringLiteral("export") : QStringLiteral("import")}};
    if (at + 1 >= args.size()) {
        o[QStringLiteral("ok")] = false;
        o[QStringLiteral("error")] = QStringLiteral("give the file: --%1 FILE").arg(exporting ? "export" : "import");
        say_json(o);
        return 2;
    }
    const QString file = QFileInfo(args.at(at + 1)).absoluteFilePath();
    data_transfer::Result r;
    if (exporting) {
        // Reads only — beside a running finterm, too (no layout steps here).
        r = data_transfer::export_to(setup_paths::current_where(), file);
    } else if (AppPaths::data_dir_in_use(setup_paths::current_where().data_dir)) {
        r.error = QStringLiteral("finterm is running on this data: quit it first");
    } else {
        // As a start does first: the data where this build expects it.
        LayoutMigrations::remember_before(plan().app_build);
        LayoutMigrations::run();
        const data_transfer::Where w = setup_paths::current_where();
        // Checked again (the layout step may have moved it). No lock is held
        // across the move: the lock file is inside the folder that moves
        // aside (Windows can't rename a folder holding an open file).
        if (AppPaths::data_dir_in_use(w.data_dir)) {
            r.error = QStringLiteral("finterm is running on this data: quit it first");
        } else {
            r = data_transfer::import_now(w, file);
            // What it put in place, ours alone while it is brought up to date
            // (plan() looks the data folder up again: it is the imported one).
            const QString imported = plan().data_dir;
            QLockFile lock(QDir(imported).filePath(QStringLiteral("finterm.db.lock")));
            if (r.ok && !take_data(lock, imported)) {
                o[QStringLiteral("problems")] =
                    QJsonArray{QStringLiteral("finterm started meanwhile: the imported data is upgraded at its next "
                                              "start")};
            } else if (r.ok) {
                const auto up = data_upgrade::upgrade(plan());
                o[QStringLiteral("upgraded")] = QJsonArray::fromStringList(up.migrated);
                if (!up.problems.isEmpty())
                    o[QStringLiteral("problems")] = QJsonArray::fromStringList(up.problems);
            }
        }
    }
    o[QStringLiteral("ok")] = r.ok;
    o[QStringLiteral("file")] = QDir::toNativeSeparators(file);
    o[QStringLiteral("accounts")] = r.databases;
    if (!r.backup_dir.isEmpty())
        o[QStringLiteral("replaced_data_in")] = QDir::toNativeSeparators(r.backup_dir);
    if (!r.error.isEmpty())
        o[QStringLiteral("error")] = r.error;
    say_json(o);
    return r.ok ? 0 : 1;
}

bool run(bool interactive, bool sole) {
    const data_upgrade::Plan p = plan();

    const bool layout_newer = LayoutMigrations::newer_than_build();
    const auto newer = data_upgrade::inventory(p.data_dir, p.known_version).newer();
    if (layout_newer || !newer.isEmpty()) {
        if (!interactive) {
            LOG_ERROR("DataGate", "The data is newer than this build (" + p.app_version + "): not opened");
            return false;
        }
        newer_data(p);
        return false;
    }

    // Upgrading takes a moment only when there is something to do: the window
    // shows only then (inventory() is quick, upgrade() returns at once).
    if (!sole) {
        // Not upgraded now — and not at a login either, with no copy first.
        if (data_upgrade::needs_upgrade(p)) {
            LOG_WARN("DataGate", "Another finterm has the data open: the data is not upgraded");
            MigrationRunner::set_hold(QStringLiteral(
                "This account's data needs upgrading for this version of finterm, and another finterm is using it. "
                "Close every finterm window and start it again."));
        }
        return true;
    }
    if (!data_upgrade::needs_upgrade(p))
        return true;

    QWidget note(nullptr, Qt::SplashScreen | Qt::WindowStaysOnTopHint);
    auto* lay = new QVBoxLayout(&note);
    lay->setContentsMargins(28, 22, 28, 22);
    auto* label = new QLabel(QStringLiteral("finterm %1\nUpgrading your data — a copy is kept first…").arg(p.app_version));
    lay->addWidget(label);
    if (interactive) {
        note.show();
        QApplication::processEvents();
    }

    // On a worker thread, so the note stays painted (and the desktop does not
    // call the app frozen) while large databases are copied and migrated.
    QFutureWatcher<data_upgrade::Outcome> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<data_upgrade::Outcome>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([p] { return data_upgrade::upgrade(p); }));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    const data_upgrade::Outcome out = watcher.result();
    note.close();
    if (out.ran)
        LOG_INFO("DataGate", QString("Data upgraded: %1 database(s)%2")
                                 .arg(out.migrated.size())
                                 .arg(out.backup_dir.isEmpty() ? "" : ", backup " + out.backup_dir));
    for (const QString& problem : out.problems)
        LOG_WARN("DataGate", problem);
    // No copy could be made: nothing is upgraded at a login either.
    if (out.ran && out.backup_dir.isEmpty())
        MigrationRunner::set_hold(QStringLiteral("This account's data needs upgrading for this version of finterm, "
                                                 "but a copy of it could not be made first (%1). Free some disk space "
                                                 "and start finterm again.")
                                      .arg(out.problems.join(QStringLiteral(" "))));
    if (interactive && !out.problems.isEmpty()) {
        QString text = out.problems.join(QStringLiteral("\n\n"));
        if (!out.backup_dir.isEmpty())
            text += QStringLiteral("\n\nA copy of the data from before the upgrade is in:\n%1")
                        .arg(QDir::toNativeSeparators(out.backup_dir));
        QMessageBox::warning(nullptr, QStringLiteral("finterm"), text);
    }
    return true;
}

int run_command_line() {
    register_all_migrations();
    const bool check_only = QCoreApplication::arguments().contains(QStringLiteral("--check-only"));
    // As a start does first: the data where this build expects it (the move
    // checks for itself that no other finterm has it open). Not for a check,
    // which changes nothing.
    if (!check_only) {
        LayoutMigrations::remember_before(setup_paths::current_plan().app_build);
        LayoutMigrations::run();
    }
    const data_upgrade::Plan p = plan();
    const auto say = say_json;
    // An upgrade needs the data to itself (as main() checks before the gate).
    QLockFile lock(QDir(p.data_dir).filePath(QStringLiteral("finterm.db.lock")));
    if (!check_only && !take_data(lock, p.data_dir)) {
        say(QJsonObject{{QStringLiteral("action"), QStringLiteral("upgrade-data")},
                        {QStringLiteral("ok"), false},
                        {QStringLiteral("error"), QStringLiteral("finterm is running on this data: quit it first")}});
        return 2;
    }
    const auto inv = data_upgrade::inventory(p.data_dir, p.known_version);
    QJsonArray dbs;
    for (const auto& d : inv.user_dbs)
        dbs.append(QJsonObject{{QStringLiteral("file"), QDir::toNativeSeparators(d.path)},
                               {QStringLiteral("version"), d.readable ? d.version : -1}});
    QJsonObject o{{QStringLiteral("action"), QStringLiteral("upgrade-data")},
                  {QStringLiteral("profile"), p.profile},
                  {QStringLiteral("known_version"), p.known_version},
                  {QStringLiteral("databases"), dbs},
                  {QStringLiteral("newer_data"), LayoutMigrations::newer_than_build() || !inv.newer().isEmpty()},
                  {QStringLiteral("needs_upgrade"), data_upgrade::needs_upgrade(p)}};
    bool ok = !o.value(QStringLiteral("newer_data")).toBool();
    if (ok && !check_only) {
        const auto out = data_upgrade::upgrade(p);
        o[QStringLiteral("ran")] = out.ran;
        o[QStringLiteral("backup")] = QDir::toNativeSeparators(out.backup_dir);
        o[QStringLiteral("migrated")] = QJsonArray::fromStringList(out.migrated);
        o[QStringLiteral("problems")] = QJsonArray::fromStringList(out.problems);
        ok = out.ok;
    }
    o[QStringLiteral("ok")] = ok;
    say(o);
    return ok ? 0 : 1;
}

} // namespace finterm::data_gate
