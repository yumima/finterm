#include "screens/profile/ProfileScreen.h"

#include "auth/AuthManager.h"
#include "auth/AuthService.h"
#include "auth/PinManager.h"
#include "core/config/AppPaths.h"
#include "core/config/ProfileManager.h"
#include "core/logging/Logger.h"
#include "ui/theme/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QUrl>

namespace fincept::screens {

static const char* MF = "font-family:'Consolas',monospace;";
static QString PANEL_SS() {
    return QString("background:%1;border:1px solid %2;").arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM());
}
static QString HDR_SS() {
    return QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM());
}

QWidget* ProfileScreen::make_panel(const QString& title) {
    auto* w = new QWidget(this);
    w->setStyleSheet(PANEL_SS());
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    auto* hdr = new QWidget(this);
    hdr->setFixedHeight(34);
    hdr->setStyleSheet(HDR_SS());
    auto* hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(12, 0, 12, 0);
    auto* t = new QLabel(title);
    t->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;background:transparent;letter-spacing:0.5px;%2")
                         .arg(ui::colors::AMBER(), MF));
    hl->addWidget(t);
    hl->addStretch();
    vl->addWidget(hdr);
    return w;
}

QWidget* ProfileScreen::make_data_row(const QString& label, QLabel*& value_out) {
    auto* row = new QWidget(this);
    row->setStyleSheet(QString("background:transparent;border-bottom:1px solid %1;").arg(ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(12, 6, 12, 6);
    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(QString("color:%1;font-size:12px;font-weight:700;background:transparent;letter-spacing:0.5px;%2")
                           .arg(ui::colors::TEXT_SECONDARY(), MF));
    hl->addWidget(lbl);
    hl->addStretch();
    value_out = new QLabel("\xe2\x80\x94");
    value_out->setStyleSheet(QString("color:%1;font-size:13px;font-weight:700;background:transparent;%2")
                                 .arg(ui::colors::TEXT_PRIMARY(), MF));
    hl->addWidget(value_out);
    return row;
}


static QString button_ss() {
    return QString("QPushButton{background:%1;color:%2;border:1px solid %3;padding:0 10px;"
                   "font-size:12px;font-weight:700;font-family:'Consolas',monospace;}QPushButton:hover{color:%4;}")
        .arg(ui::colors::BG_RAISED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
             ui::colors::TEXT_PRIMARY());
}

static QString danger_button_ss(const char* bg_alpha) {
    return QString("QPushButton{background:rgba(220,38,38,%3);color:%1;border:1px solid #7f1d1d;padding:0 12px;"
                   "font-size:12px;font-weight:700;font-family:'Consolas',monospace;}QPushButton:hover{background:%1;"
                   "color:%2;}")
        .arg(ui::colors::NEGATIVE(), ui::colors::TEXT_PRIMARY(), QString::fromLatin1(bg_alpha));
}

static QLabel* make_note(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QString("color:%1;font-size:12px;background:transparent;padding:8px 12px;%2")
                         .arg(ui::colors::TEXT_SECONDARY(), MF));
    return l;
}

static const QString kMaskedKey = QString(20, QChar(0x2022));

ProfileScreen::ProfileScreen(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    build_header(root);
    sections_ = new QStackedWidget;
    sections_->addWidget(build_overview());
    sections_->addWidget(build_security());
    build_tab_nav(root);
    root->addWidget(sections_, 1);
    connect(&auth::AuthManager::instance(), &auth::AuthManager::auth_state_changed, this, &ProfileScreen::refresh_all);
    // Style the first tab as active on load (also runs refresh_all)
    on_section_changed(0);
}

void ProfileScreen::build_header(QVBoxLayout* root) {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(38);
    bar->setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(14, 0, 14, 0);
    hl->setSpacing(8);
    auto* title = new QLabel("ACCOUNT");
    title->setStyleSheet(
        QString("color:%1;font-size:14px;font-weight:700;background:transparent;%2").arg(ui::colors::AMBER(), MF));
    hl->addWidget(title);
    username_header_ = new QLabel;
    username_header_->setStyleSheet(
        QString("color:%1;font-size:13px;background:transparent;%2").arg(ui::colors::TEXT_PRIMARY(), MF));
    hl->addWidget(username_header_);
    hl->addStretch();
    auto* rb = new QPushButton("REFRESH");
    rb->setFixedHeight(22);
    rb->setCursor(Qt::PointingHandCursor);
    rb->setStyleSheet(button_ss());
    // refresh_user_data emits auth_state_changed → refresh_all.
    connect(rb, &QPushButton::clicked, this, []() { auth::AuthManager::instance().refresh_user_data(); });
    hl->addWidget(rb);
    root->addWidget(bar);
}

void ProfileScreen::build_tab_nav(QVBoxLayout* root) {
    auto* nav = new QWidget(this);
    nav->setFixedHeight(32);
    nav->setStyleSheet(
        QString("background:%1;border-bottom:1px solid %2;").arg(ui::colors::BG_RAISED(), ui::colors::BORDER_DIM()));
    auto* hl = new QHBoxLayout(nav);
    hl->setContentsMargins(4, 0, 4, 0);
    hl->setSpacing(0);
    const QStringList tabs = {"OVERVIEW", "SECURITY"};
    for (int i = 0; i < tabs.size(); i++) {
        auto* btn = new QPushButton(tabs[i]);
        btn->setFixedHeight(32);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, i]() { on_section_changed(i); });
        hl->addWidget(btn);
        nav_buttons_.append(btn);
    }
    hl->addStretch();
    root->addWidget(nav);
}

void ProfileScreen::on_section_changed(int index) {
    sections_->setCurrentIndex(index);
    for (int i = 0; i < nav_buttons_.size(); ++i) {
        nav_buttons_[i]->setStyleSheet(
            i == index
                ? QString("QPushButton{background:%1;color:%2;border:none;padding:0 14px;"
                          "font-size:12px;font-weight:700;letter-spacing:0.5px;font-family:'Consolas',monospace;}")
                      .arg(ui::colors::AMBER(), ui::colors::BG_BASE())
                : QString("QPushButton{background:transparent;color:%1;border:none;padding:0 14px;"
                          "font-size:12px;font-weight:700;letter-spacing:0.5px;font-family:'Consolas',monospace;}"
                          "QPushButton:hover{color:%2;}")
                      .arg(ui::colors::TEXT_SECONDARY(), ui::colors::TEXT_SECONDARY()));
    }
    // The PIN can change from Settings → Security & Lock without an auth
    // event, so re-read on every tab switch.
    refresh_all();
}

QWidget* ProfileScreen::build_overview() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");
    auto* page = new QWidget(this);
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(14, 14, 14, 14);
    vl->setSpacing(10);
    auto* grid = new QGridLayout;
    grid->setSpacing(10);

    auto* acct = make_panel("LOCAL ACCOUNT");
    auto* avl = qobject_cast<QVBoxLayout*>(acct->layout());
    avl->addWidget(make_data_row("USERNAME", ov_username_));
    ov_email_row_ = make_data_row("EMAIL", ov_email_);
    avl->addWidget(ov_email_row_);
    avl->addWidget(make_data_row("MEMBER SINCE", ov_member_since_));
    avl->addWidget(make_note("finterm has no online account. This user exists only in this install's auth.db "
                             "on this machine; nothing about it is sent anywhere."));
    grid->addWidget(acct, 0, 0);

    auto* inst = make_panel("THIS INSTALL");
    auto* ivl = qobject_cast<QVBoxLayout*>(inst->layout());
    ivl->addWidget(make_data_row("DATA PROFILE", ov_data_profile_));
    ivl->addWidget(make_data_row("DATA FOLDER", ov_data_folder_));
    ov_data_folder_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ivl->addWidget(make_data_row("ACCOUNT DATABASES", ov_account_dbs_));
    ov_account_dbs_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* fr = new QWidget(this);
    fr->setStyleSheet("background:transparent;");
    auto* frl = new QHBoxLayout(fr);
    frl->setContentsMargins(12, 8, 12, 8);
    auto* ob = new QPushButton("OPEN DATA FOLDER");
    ob->setFixedHeight(26);
    ob->setCursor(Qt::PointingHandCursor);
    ob->setStyleSheet(button_ss());
    connect(ob, &QPushButton::clicked, this,
            []() { QDesktopServices::openUrl(QUrl::fromLocalFile(fincept::AppPaths::user())); });
    frl->addWidget(ob);
    frl->addStretch();
    ivl->addWidget(fr);
    grid->addWidget(inst, 0, 1);

    vl->addLayout(grid);
    vl->addStretch();
    scroll->setWidget(page);
    return scroll;
}

QWidget* ProfileScreen::build_security() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}");
    auto* page = new QWidget(this);
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(14, 14, 14, 14);
    vl->setSpacing(10);

    auto* kp = make_panel("LOCAL API KEY");
    auto* kvl = qobject_cast<QVBoxLayout*>(kp->layout());
    auto* kr = new QWidget(this);
    kr->setStyleSheet("background:transparent;");
    auto* krl = new QHBoxLayout(kr);
    krl->setContentsMargins(12, 10, 12, 10);
    krl->setSpacing(8);
    sec_api_key_ = new QLabel(kMaskedKey);
    sec_api_key_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sec_api_key_->setStyleSheet(
        QString("color:%1;font-size:13px;background:transparent;%2").arg(ui::colors::TEXT_PRIMARY(), MF));
    krl->addWidget(sec_api_key_, 1);
    sec_show_key_btn_ = new QPushButton("SHOW");
    sec_show_key_btn_->setFixedHeight(22);
    sec_show_key_btn_->setCursor(Qt::PointingHandCursor);
    sec_show_key_btn_->setStyleSheet(button_ss());
    connect(sec_show_key_btn_, &QPushButton::clicked, this, [this]() {
        if (api_key_visible_) {
            hide_api_key();
            return;
        }
        api_key_visible_ = true;
        sec_show_key_btn_->setText("HIDE");
        sec_api_key_->setText(auth::AuthManager::instance().session().api_key);
    });
    krl->addWidget(sec_show_key_btn_);
    auto* cb = new QPushButton("COPY");
    cb->setFixedHeight(22);
    cb->setCursor(Qt::PointingHandCursor);
    cb->setStyleSheet(button_ss());
    connect(cb, &QPushButton::clicked, this, [cb]() {
        auto key = auth::AuthManager::instance().session().api_key;
        if (!key.isEmpty()) {
            QApplication::clipboard()->setText(key);
            cb->setText("COPIED");
            QTimer::singleShot(1500, cb, [cb]() { cb->setText("COPY"); });
        }
    });
    krl->addWidget(cb);
    auto* rg = new QPushButton("REGENERATE");
    rg->setFixedHeight(22);
    rg->setCursor(Qt::PointingHandCursor);
    rg->setStyleSheet(QString("QPushButton{background:rgba(217,119,6,0.1);color:%1;border:1px solid %2;padding:0 10px;"
                              "font-size:12px;font-weight:700;font-family:'Consolas',monospace;}QPushButton:hover{"
                              "background:%1;color:%3;}")
                          .arg(ui::colors::AMBER(), ui::colors::AMBER_DIM(), ui::colors::BG_BASE()));
    connect(rg, &QPushButton::clicked, this, &ProfileScreen::show_regen_confirm);
    krl->addWidget(rg);
    kvl->addWidget(kr);
    kvl->addWidget(make_note("Issued by this install and checked only against auth.db on this machine."));
    vl->addWidget(kp);

    auto* pp = make_panel("DEVICE PIN");
    auto* pvl = qobject_cast<QVBoxLayout*>(pp->layout());
    pvl->addWidget(make_data_row("PIN", sec_pin_));
    pvl->addWidget(make_note("Change the PIN under Settings \xe2\x86\x92 Security & Lock."));
    vl->addWidget(pp);

    auto* sp = make_panel("SESSION");
    auto* svl = qobject_cast<QVBoxLayout*>(sp->layout());
    auto* ar = new QWidget(this);
    ar->setStyleSheet("background:transparent;");
    auto* arl = new QHBoxLayout(ar);
    arl->setContentsMargins(12, 8, 12, 8);
    arl->setSpacing(10);
    auto* lb = new QPushButton("SIGN OUT");
    lb->setFixedHeight(26);
    lb->setCursor(Qt::PointingHandCursor);
    lb->setStyleSheet(danger_button_ss("0.1"));
    connect(lb, &QPushButton::clicked, this, &ProfileScreen::show_logout_confirm);
    arl->addWidget(lb);
    auto* dab = new QPushButton("DELETE LOCAL ACCOUNT");
    dab->setFixedHeight(26);
    dab->setCursor(Qt::PointingHandCursor);
    dab->setStyleSheet(danger_button_ss("0.15"));
    connect(dab, &QPushButton::clicked, this, &ProfileScreen::show_delete_account_dialog);
    arl->addWidget(dab);
    arl->addStretch();
    svl->addWidget(ar);
    vl->addWidget(sp);

    vl->addStretch();
    scroll->setWidget(page);
    return scroll;
}

void ProfileScreen::hide_api_key() {
    api_key_visible_ = false;
    if (sec_show_key_btn_)
        sec_show_key_btn_->setText("SHOW");
    if (sec_api_key_)
        sec_api_key_->setText(kMaskedKey);
}

void ProfileScreen::refresh_all() {
    // Install facts hold whether or not anyone is signed in.
    ov_data_profile_->setText(fincept::ProfileManager::instance().active());
    ov_data_folder_->setText(QDir::toNativeSeparators(fincept::AppPaths::user()));
    // auth.db and users/<name>.db live under AppPaths::data(), which can be a
    // legacy <profile>/data outside the folder above.
    ov_account_dbs_->setText(QDir::toNativeSeparators(fincept::AppPaths::data()));

    const bool has_pin = auth::PinManager::instance().has_pin();
    sec_pin_->setText(has_pin ? "SET" : "NOT SET");
    sec_pin_->setStyleSheet(QString("color:%1;font-size:13px;font-weight:700;background:transparent;%2")
                                .arg(has_pin ? ui::colors::POSITIVE() : ui::colors::NEGATIVE())
                                .arg(MF));

    const auto& s = auth::AuthManager::instance().session();
    if (!s.authenticated) {
        // Nothing of the previous user may linger once signed out.
        hide_api_key();
        username_header_->clear();
        ov_username_->setText("\xe2\x80\x94");
        ov_email_->clear();
        ov_email_row_->setVisible(false);
        ov_member_since_->setText("\xe2\x80\x94");
        return;
    }
    const auto& u = s.user_info;
    username_header_->setText(u.username);
    ov_username_->setText(u.username.isEmpty() ? "\xe2\x80\x94" : u.username);
    const QString email = auth::displayable_email(u.email);
    ov_email_row_->setVisible(!email.isEmpty());
    ov_email_->setText(email);
    // created_at is ISO-8601 ("2026-01-02T…") or SQLite ("2026-01-02 …"):
    // the date is the first ten characters either way.
    ov_member_since_->setText(u.created_at.isEmpty() ? "\xe2\x80\x94" : u.created_at.left(10));
    if (api_key_visible_)
        sec_api_key_->setText(s.api_key);
}

void ProfileScreen::show_logout_confirm() {
    if (QMessageBox::question(this, "Confirm Logout",
                              "Sign out of finterm?\n\nYour device PIN is kept — the next "
                              "sign-in unlocks with it instead of asking you to create a new one.",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        auth::AuthManager::instance().logout();
}

void ProfileScreen::show_regen_confirm() {
    if (QMessageBox::warning(this, "Regenerate API Key",
                             "Your current local API key stops working immediately and a new one replaces it. "
                             "Continue?",
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QPointer<ProfileScreen> self = this;
    auth::AuthManager::instance().regenerate_api_key([self](bool ok, const QString& err) {
        if (!self)
            return;
        if (ok) {
            self->hide_api_key();
        } else {
            LOG_WARN("Profile", "API key regeneration failed: " + err);
            QMessageBox::warning(self, "Regenerate API Key", "Could not regenerate the API key: " + err);
        }
    });
}

void ProfileScreen::show_delete_account_dialog() {
    const auto& s = auth::AuthManager::instance().session();
    const QString username = s.user_info.username;
    const QString api_key = s.api_key;
    if (!s.authenticated || username.isEmpty())
        return;

    // First confirmation
    auto first = QMessageBox::warning(
        this, "Delete Local Account",
        QString("This permanently deletes the local user \"%1\" on this machine: its sign-in, its database "
                "(%2 — watchlists, portfolios and other per-user data), its saved notification "
                "credentials and the device PIN. Other files in the data folders are not touched.\n\n"
                "This action CANNOT be undone. Are you sure?")
            .arg(username, QDir::toNativeSeparators(fincept::AppPaths::data() + "/users/" + username + ".db")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (first != QMessageBox::Yes)
        return;

    // Second confirmation — type the username to confirm
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("Confirm Account Deletion");
    dlg->setFixedSize(400, 200);
    dlg->setStyleSheet(QString("background:%1;color:%2;font-family:'Consolas',monospace;")
                           .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY()));
    auto* vl = new QVBoxLayout(dlg);
    vl->setContentsMargins(20, 16, 20, 16);
    vl->setSpacing(10);

    auto* warn = new QLabel("TYPE THE USERNAME TO CONFIRM:");
    warn->setStyleSheet(
        QString("color:%1;font-size:12px;font-weight:700;background:transparent;").arg(ui::colors::NEGATIVE()));
    vl->addWidget(warn);

    auto* hint = new QLabel(username);
    hint->setStyleSheet(QString("color:%1;font-size:12px;background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(hint);

    auto* input = new QLineEdit;
    input->setFixedHeight(28);
    input->setPlaceholderText(username);
    vl->addWidget(input);

    auto* brl = new QHBoxLayout;
    brl->addStretch();
    auto* cancel = new QPushButton("CANCEL");
    cancel->setFixedHeight(26);
    connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    brl->addWidget(cancel);

    auto* confirm = new QPushButton("DELETE MY ACCOUNT");
    confirm->setFixedHeight(26);
    confirm->setEnabled(false);
    confirm->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:none;padding:0 14px;"
                                   "font-size:12px;font-weight:700;font-family:'Consolas',monospace;}"
                                   "QPushButton:disabled{background:#3f1515;color:#7f3333;}")
                               .arg(ui::colors::NEGATIVE(), ui::colors::TEXT_PRIMARY()));
    connect(input, &QLineEdit::textChanged, this,
            [confirm, username](const QString& text) { confirm->setEnabled(text.trimmed() == username); });
    connect(confirm, &QPushButton::clicked, dlg, &QDialog::accept);
    brl->addWidget(confirm);
    vl->addLayout(brl);

    const bool accepted = dlg->exec() == QDialog::Accepted;
    dlg->deleteLater();
    if (!accepted)
        return;

    // Close the user's database before its file is removed (Windows refuses
    // to delete an open file, and an orphaned users/<name>.db would come back
    // under a re-created account of the same name). Delete before signing out
    // so the login screen that logout() shows no longer lists this user.
    auth::AuthService::instance().close_user_db();
    QPointer<ProfileScreen> self = this;
    auth::AuthManager::instance().delete_local_user(username, [self, username, api_key](bool ok, const QString& err) {
        if (!ok) {
            LOG_ERROR("Profile", "Local account deletion failed: " + err);
            auth::AuthService::instance().open_user_db(api_key);
            if (self)
                QMessageBox::critical(self, "Delete Failed", "Account deletion failed: " + err);
            return;
        }
        LOG_INFO("Profile", "Local account deleted: " + username);
        auth::AuthManager::instance().logout(/*wipe_device_pin=*/true);
    });
}

} // namespace fincept::screens
