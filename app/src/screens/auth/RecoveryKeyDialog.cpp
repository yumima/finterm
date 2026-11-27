#include "screens/auth/RecoveryKeyDialog.h"

#include "auth/AuthManager.h"
#include "auth/PinManager.h"
#include "storage/repositories/SettingsRepository.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace finterm::screens::RecoveryKeyDialog {

namespace {
constexpr const char* kOfferedKey = "security.recovery_key_offered";
}

void show_key(QWidget* parent, const QString& username, const QString& key) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Your recovery key"));
    dlg.setMinimumWidth(500);
    auto* vl = new QVBoxLayout(&dlg);
    vl->setSpacing(10);

    auto* intro = new QLabel(
        QStringLiteral("If you forget the PIN of <b>%1</b>, this key sets a new one and keeps all your data. "
                       "It is shown only now: save it or write it down, and keep it somewhere other than this "
                       "computer.")
            .arg(username.toHtmlEscaped()),
        &dlg);
    intro->setWordWrap(true);
    vl->addWidget(intro);

    auto* key_lbl = new QLabel(key, &dlg);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPixelSize(22);
    f.setBold(true);
    key_lbl->setFont(f);
    key_lbl->setAlignment(Qt::AlignCenter);
    key_lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    key_lbl->setStyleSheet(QStringLiteral("padding:14px; border:1px dashed palette(mid);"));
    vl->addWidget(key_lbl);

    auto* row = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("Copy"), &dlg);
    auto* save = new QPushButton(QStringLiteral("Save to a file…"), &dlg);
    row->addWidget(copy);
    row->addWidget(save);
    row->addStretch();
    vl->addLayout(row);
    QObject::connect(copy, &QPushButton::clicked, &dlg, [key, copy]() {
        QApplication::clipboard()->setText(key);
        copy->setText(QStringLiteral("Copied"));
    });
    QObject::connect(save, &QPushButton::clicked, &dlg, [&dlg, key, username]() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString path = QFileDialog::getSaveFileName(
            &dlg, QStringLiteral("Save the recovery key"),
            QDir(dir).filePath(QStringLiteral("finterm recovery key - %1.txt").arg(username)),
            QStringLiteral("Text (*.txt)"));
        if (path.isEmpty())
            return;
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(&dlg, QStringLiteral("Save the recovery key"), out.errorString());
            return;
        }
        out.write(QStringLiteral("finterm recovery key for the user \"%1\"\n\n%2\n\n"
                                 "On the lock or sign-in screen choose \"Forgot PIN?\" > \"Use my recovery key\".\n")
                      .arg(username, key)
                      .toUtf8());
    });

    auto* kept = new QCheckBox(QStringLiteral("I have saved this key"), &dlg);
    vl->addWidget(kept);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Continue"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    QObject::connect(kept, &QCheckBox::toggled, buttons->button(QDialogButtonBox::Ok), &QWidget::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    vl->addWidget(buttons);
    // Closing the window is a choice too: the key was on screen.
    dlg.exec();
}

bool create_and_show(QWidget* parent) {
    auto& auth = auth::AuthManager::instance();
    // A recovery key resets the PIN: making one needs the PIN, as changing
    // it does — someone at an unattended, unlocked terminal must not be able
    // to mint a key and take the account over later. Wrong answers count
    // against the same lockout as the lock screen.
    auto& gate = auth::PinManager::instance();
    for (;;) {
        if (gate.is_locked_out(gate.account())) {
            QMessageBox::warning(parent, QStringLiteral("Recovery key"),
                                 QStringLiteral("Too many wrong PINs — try again in %1 s.")
                                     .arg(gate.lockout_remaining_seconds(gate.account())));
            return false;
        }
        bool ok = false;
        const QString pin = QInputDialog::getText(parent, QStringLiteral("Recovery key"),
                                                  QStringLiteral("Enter your PIN to make a recovery key:"),
                                                  QLineEdit::Password, QString(), &ok);
        if (!ok)
            return false;
        if (gate.verify_pin(pin))
            break;
        QMessageBox::warning(parent, QStringLiteral("Recovery key"), QStringLiteral("That PIN is not right."));
    }
    const QString key = auth.create_recovery_key();
    if (key.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Recovery key"),
                             QStringLiteral("The recovery key could not be saved. Try again from Settings › "
                                            "Security."));
        return false;
    }
    show_key(parent, auth.session().user_info.username, key);
    return true;
}

void offer_if_missing(QWidget* parent) {
    auto& auth = auth::AuthManager::instance();
    const QString user = auth.session().user_info.username;
    if (!auth.is_authenticated() || user.isEmpty() || auth.has_recovery_key(user))
        return;
    auto& settings = SettingsRepository::instance();
    if (const auto r = settings.get(QString::fromLatin1(kOfferedKey)); r.is_ok() && r.value() == QLatin1String("1"))
        return;
    settings.set(QString::fromLatin1(kOfferedKey), QStringLiteral("1"), QStringLiteral("security"));
    const auto ans = QMessageBox::question(
        parent, QStringLiteral("Make a recovery key?"),
        QStringLiteral("A recovery key lets you set a new PIN if you forget yours, without losing your data.\n\n"
                       "Make one now? (You can also do it later in Settings › Security.)"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (ans == QMessageBox::Yes)
        create_and_show(parent);
}

} // namespace finterm::screens::RecoveryKeyDialog
