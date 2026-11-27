#include "screens/auth/StartFreshDialog.h"

#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/config/StartFresh.h"
#include "core/logging/Logger.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace finterm::screens::StartFreshDialog {

bool ask(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Start fresh"));
    dlg.setMinimumWidth(520);
    dlg.setMaximumWidth(640);
    auto* vl = new QVBoxLayout(&dlg);
    vl->setSpacing(10);

    const QString profile = ProfileManager::instance().active();
    auto* what = new QLabel(&dlg);
    what->setWordWrap(true);
    what->setTextFormat(Qt::RichText);
    what->setText(
        QStringLiteral(
            "<p><b>finterm will restart and set itself up as on its first start.</b></p>"
            "<p><b>Moved to a backup folder</b> (not deleted — you can copy it back):<br>"
            "every user, with their portfolios, watchlists, notes, chats and workspaces%1.</p>"
            "<p><b>Set aside in the system keyring</b> (kept under a backup name the new setup doesn't "
            "use): the API keys and passwords saved in finterm.</p>"
            "<p><b>Removed:</b> the cache.</p>"
            "<p><b>Kept:</b> logs, downloaded public data%2.</p>"
            "<p>Backup folder: <code>%3</code></p>")
            .arg(profile == QLatin1String("default") ? QStringLiteral(", and the app settings") : QString(),
                 profile == QLatin1String("default") ? QString()
                                                     : QStringLiteral(", and the other profiles"),
                 QDir::toNativeSeparators(start_fresh::backup_root()).toHtmlEscaped()));
    vl->addWidget(what);

    auto* runtime = new QCheckBox(QStringLiteral("Also remove the Python environment and downloaded models"), &dlg);
    vl->addWidget(runtime);
    auto* runtime_note = new QLabel(QStringLiteral("Several GB. The next start downloads them again, which takes a "
                                                   "while and needs the network — leave this off unless the Python "
                                                   "setup itself is broken."),
                                    &dlg);
    runtime_note->setWordWrap(true);
    runtime_note->setContentsMargins(22, 0, 0, 0);
    vl->addWidget(runtime_note);

    auto* confirm_lbl = new QLabel(QStringLiteral("Type <b>RESET</b> to confirm:"), &dlg);
    vl->addWidget(confirm_lbl);
    auto* confirm = new QLineEdit(&dlg);
    vl->addWidget(confirm);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    auto* go = buttons->addButton(QStringLiteral("Start fresh"), QDialogButtonBox::DestructiveRole);
    go->setEnabled(false);
    QObject::connect(confirm, &QLineEdit::textChanged, go,
                     [go](const QString& t) { go->setEnabled(t.trimmed() == QLatin1String("RESET")); });
    QObject::connect(go, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(buttons);
    confirm->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return false;

    start_fresh::Options options;
    options.remove_runtime = runtime->isChecked();
    if (const QString why = start_fresh::schedule(options); !why.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Start fresh"), why);
        return false;
    }
    LOG_INFO("StartFresh", QStringLiteral("fresh start scheduled (runtime too: %1) — restarting")
                               .arg(options.remove_runtime ? "yes" : "no"));
    if (!start_fresh::relaunch()) {
        QMessageBox::information(parent, QStringLiteral("Start fresh"),
                                 QStringLiteral("finterm will now close. Start it again to finish the fresh start."));
    }
    QApplication::quit();
    return true;
}

} // namespace finterm::screens::StartFreshDialog
