#include "screens/settings/DataTransferDialog.h"

#include "core/config/StartFresh.h"
#include "core/logging/Logger.h"
#include "core/setup/DataTransfer.h"
#include "core/setup/SetupPaths.h"

#include <QApplication>
#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

namespace finterm::screens::data_transfer_dialog {

namespace {

/// Run `work` off the GUI thread behind a busy note; its result.
template <typename T, typename F>
T busy(QWidget* parent, const QString& label, F work) {
    QProgressDialog note(label, QString(), 0, 0, parent);
    note.setWindowTitle(QStringLiteral("finterm"));
    note.setWindowModality(Qt::WindowModal);
    note.setMinimumDuration(0);
    note.setCancelButton(nullptr);
    note.show();
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(work));
    if (!watcher.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    note.close();
    return watcher.result();
}

QString documents() {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return d.isEmpty() ? QDir::homePath() : d;
}

} // namespace

QString check_file(QWidget* parent, const QString& file) {
    const data_transfer::Where w = setup_paths::current_where();
    return busy<QString>(parent, QStringLiteral("Checking the file…"),
                         [w, file] { return data_transfer::check_archive(w, file); });
}

void export_data(QWidget* parent) {
    const data_transfer::Where w = setup_paths::current_where();
    const QString name = QStringLiteral("finterm-data-%1%2.tar.gz")
                             .arg(w.profile == QLatin1String("default") ? QString() : w.profile + QLatin1Char('-'),
                                  QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    QString file = QFileDialog::getSaveFileName(parent, QStringLiteral("Export my data"),
                                                QDir(documents()).filePath(name),
                                                QStringLiteral("finterm data (*.tar.gz)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".tar.gz"))) {
        file += QStringLiteral(".tar.gz");
        // The dialog asked about the name as typed, not this one.
        if (QFileInfo::exists(file) &&
            QMessageBox::question(parent, QStringLiteral("Export my data"),
                                  QStringLiteral("%1 already exists. Replace it?").arg(QDir::toNativeSeparators(file)),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
    }
    const auto r = busy<data_transfer::Result>(parent, QStringLiteral("Exporting your data…"),
                                               [w, file] { return data_transfer::export_to(w, file); });
    if (!r.ok) {
        QMessageBox::warning(parent, QStringLiteral("Export my data"),
                             QStringLiteral("Your data could not be exported: %1.").arg(r.error));
        return;
    }
    QMessageBox::information(
        parent, QStringLiteral("Export my data"),
        QStringLiteral("Exported %1 account%2 to:\n%3\n\nIt holds your personal data in full, with each account's "
                       "sign-in details (its PIN is checked against what is in it): keep it as private as this "
                       "computer. API keys and service passwords kept in the system keyring are not in it.")
            .arg(r.databases)
            .arg(r.databases == 1 ? "" : "s")
            .arg(QDir::toNativeSeparators(r.file)));
}

void import_data(QWidget* parent) {
    const data_transfer::Where w = setup_paths::current_where();
    const QString file = QFileDialog::getOpenFileName(parent, QStringLiteral("Import data"), documents(),
                                                      QStringLiteral("finterm data (*.tar.gz)"));
    if (file.isEmpty())
        return;
    const QString problem = check_file(parent, file);
    if (!problem.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Import data"),
                             QStringLiteral("This file can't be imported: %1.").arg(problem));
        return;
    }
    const auto answer = QMessageBox::question(
        parent, QStringLiteral("Import data"),
        QStringLiteral("Replace this profile's data with the data in\n%1?\n\nYour current data is moved, not deleted, "
                       "to a folder in %2. finterm restarts to do it.")
            .arg(QDir::toNativeSeparators(file), QDir::toNativeSeparators(w.backup_root)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    if (const QString err = data_transfer::schedule_import(w, file); !err.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Import data"), err);
        return;
    }
    LOG_INFO("DataTransfer", "Import scheduled — restarting: " + file);
    if (!start_fresh::relaunch())
        QMessageBox::information(parent, QStringLiteral("Import data"),
                                 QStringLiteral("finterm will now close. Start it again to finish the import."));
    QApplication::quit();
}

} // namespace finterm::screens::data_transfer_dialog
