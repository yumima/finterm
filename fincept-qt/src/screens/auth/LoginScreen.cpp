#include "screens/auth/LoginScreen.h"

#include "auth/AuthManager.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QIntValidator>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPainter>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

QString panel_style() {
    return QString(
        "QWidget#authPanel { background:%1; border:1px solid %2; border-radius:6px; }"
    ).arg(fincept::ui::colors::BG_SURFACE(), fincept::ui::colors::BORDER_MED());
}

QString user_list_style() {
    return QString(
        "QListWidget { background:%1; color:%2; border:1px solid %3; border-radius:3px;"
        "  font-size:14px; outline:none; }"
        "QListWidget::item { padding:10px 14px; border-bottom:1px solid %3; }"
        "QListWidget::item:hover { background:%4; }"
        "QListWidget::item:selected { background:%5; color:%6; }"
    ).arg(fincept::ui::colors::BG_BASE(), fincept::ui::colors::TEXT_PRIMARY(),
          fincept::ui::colors::BORDER_DIM(), fincept::ui::colors::BG_HOVER(),
          fincept::ui::colors::AMBER_DIM(), fincept::ui::colors::AMBER());
}

QString pin_input_style() {
    return QString(
        "QLineEdit { background:%1; color:%2; border:1px solid %3; border-radius:3px;"
        "  padding:12px 16px; font-size:24px; font-family:Consolas,monospace;"
        "  letter-spacing:12px; }"
        "QLineEdit:focus { border:1px solid %4; }"
    ).arg(fincept::ui::colors::BG_BASE(), fincept::ui::colors::TEXT_PRIMARY(),
          fincept::ui::colors::BORDER_MED(), fincept::ui::colors::AMBER());
}

QString primary_btn_style() {
    return QString(
        "QPushButton { background:%1; color:#fff; border:none; border-radius:3px;"
        "  padding:10px 20px; font-size:13px; font-weight:700; letter-spacing:0.6px; }"
        "QPushButton:hover { background:%2; }"
        "QPushButton:disabled { background:%3; color:%4; }"
    ).arg(fincept::ui::colors::AMBER(), fincept::ui::colors::AMBER_DIM(),
          fincept::ui::colors::BG_RAISED(), fincept::ui::colors::TEXT_TERTIARY());
}

QString secondary_btn_style() {
    return QString(
        "QPushButton { background:transparent; color:%1; border:1px solid %2;"
        "  border-radius:3px; padding:8px 16px; font-size:12px; }"
        "QPushButton:hover { color:%3; border-color:%3; }"
    ).arg(fincept::ui::colors::TEXT_SECONDARY(),
          fincept::ui::colors::BORDER_MED(),
          fincept::ui::colors::AMBER());
}

QString danger_btn_style() {
    return QString(
        "QPushButton { background:transparent; color:%1; border:none;"
        "  padding:6px 10px; font-size:11px; }"
        "QPushButton:hover { color:%2; text-decoration:underline; }"
    ).arg(fincept::ui::colors::TEXT_TERTIARY(), fincept::ui::colors::NEGATIVE());
}

} // namespace

LoginScreen::LoginScreen(QWidget* parent) : QWidget(parent) {
    pages_ = new QStackedWidget(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(pages_);

    build_picker_page();
    build_pin_page();
    pages_->addWidget(picker_page_);
    pages_->addWidget(pin_page_);

    auto& auth = auth::AuthManager::instance();
    connect(&auth, &auth::AuthManager::login_succeeded, this, &LoginScreen::on_login_succeeded);
    connect(&auth, &auth::AuthManager::login_failed,    this, &LoginScreen::on_login_failed);
}

void LoginScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(fincept::ui::colors::BG_BASE()));
}

void LoginScreen::showEvent(QShowEvent* event) {
    refresh_user_list();
    show_picker();
    QWidget::showEvent(event);
}

void LoginScreen::hideEvent(QHideEvent* event) {
    if (pin_input_) pin_input_->clear();
    if (pin_error_) pin_error_->hide();
    QWidget::hideEvent(event);
}

void LoginScreen::build_picker_page() {
    picker_page_ = new QWidget(this);
    auto* outer = new QVBoxLayout(picker_page_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addStretch();

    auto* panel = new QWidget(picker_page_);
    panel->setObjectName("authPanel");
    panel->setStyleSheet(panel_style());
    panel->setFixedWidth(420);
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(28, 28, 28, 28);
    pl->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Sign in"), panel);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:700; letter-spacing:0.4px;")
                             .arg(fincept::ui::colors::TEXT_PRIMARY()));

    auto* subtitle = new QLabel(
        QStringLiteral("Pick your local user, then enter your PIN."), panel);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString("color:%1; font-size:12px;").arg(fincept::ui::colors::TEXT_SECONDARY()));

    user_list_ = new QListWidget(panel);
    user_list_->setStyleSheet(user_list_style());
    user_list_->setMinimumHeight(180);
    user_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(user_list_, &QListWidget::itemActivated, this, &LoginScreen::on_user_selected);
    connect(user_list_, &QListWidget::itemClicked,   this, &LoginScreen::on_user_selected);

    picker_empty_ = new QLabel(
        QStringLiteral("No local users yet \xe2\x80\x94 create one to get started."),
        panel);
    picker_empty_->setAlignment(Qt::AlignCenter);
    picker_empty_->setWordWrap(true);
    picker_empty_->setStyleSheet(
        QString("color:%1; font-size:12px; padding:24px; border:1px dashed %2;"
                " border-radius:3px;")
            .arg(fincept::ui::colors::TEXT_TERTIARY(), fincept::ui::colors::BORDER_DIM()));
    picker_empty_->hide();

    create_btn_ = new QPushButton(QStringLiteral("CREATE ACCOUNT"), panel);
    create_btn_->setStyleSheet(primary_btn_style());
    create_btn_->setCursor(Qt::PointingHandCursor);
    connect(create_btn_, &QPushButton::clicked, this, &LoginScreen::navigate_register);

    pl->addWidget(title);
    pl->addWidget(subtitle);
    pl->addSpacing(8);
    pl->addWidget(user_list_);
    pl->addWidget(picker_empty_);
    pl->addWidget(create_btn_);

    auto* centerer = new QHBoxLayout;
    centerer->addStretch();
    centerer->addWidget(panel);
    centerer->addStretch();
    outer->addLayout(centerer);
    outer->addStretch();
}

void LoginScreen::build_pin_page() {
    pin_page_ = new QWidget(this);
    auto* outer = new QVBoxLayout(pin_page_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addStretch();

    auto* panel = new QWidget(pin_page_);
    panel->setObjectName("authPanel");
    panel->setStyleSheet(panel_style());
    panel->setFixedWidth(420);
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(28, 28, 28, 28);
    pl->setSpacing(14);

    pin_username_lbl_ = new QLabel(panel);
    pin_username_lbl_->setAlignment(Qt::AlignCenter);
    pin_username_lbl_->setStyleSheet(
        QString("color:%1; font-size:18px; font-weight:700; letter-spacing:0.4px;")
            .arg(fincept::ui::colors::TEXT_PRIMARY()));

    auto* hint = new QLabel(QStringLiteral("Enter PIN"), panel);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(QString("color:%1; font-size:12px;")
                            .arg(fincept::ui::colors::TEXT_SECONDARY()));

    pin_input_ = new QLineEdit(panel);
    pin_input_->setPlaceholderText(QStringLiteral("\xe2\x80\xa2 \xe2\x80\xa2 \xe2\x80\xa2 \xe2\x80\xa2"));
    pin_input_->setEchoMode(QLineEdit::Password);
    pin_input_->setMaxLength(6);
    pin_input_->setValidator(new QIntValidator(0, 999999, this));
    pin_input_->setAlignment(Qt::AlignCenter);
    pin_input_->setStyleSheet(pin_input_style());

    pin_error_ = new QLabel(panel);
    pin_error_->setStyleSheet(QString("color:%1; font-size:12px; padding:2px;")
                                  .arg(fincept::ui::colors::NEGATIVE()));
    pin_error_->setAlignment(Qt::AlignCenter);
    pin_error_->setWordWrap(true);
    pin_error_->hide();

    pin_unlock_btn_ = new QPushButton(QStringLiteral("UNLOCK"), panel);
    pin_unlock_btn_->setStyleSheet(primary_btn_style());
    pin_unlock_btn_->setCursor(Qt::PointingHandCursor);
    connect(pin_unlock_btn_, &QPushButton::clicked, this, &LoginScreen::on_unlock);
    connect(pin_input_, &QLineEdit::returnPressed, this, &LoginScreen::on_unlock);

    pin_back_btn_ = new QPushButton(QStringLiteral("\xe2\x86\x90  Different user"), panel);
    pin_back_btn_->setStyleSheet(secondary_btn_style());
    pin_back_btn_->setCursor(Qt::PointingHandCursor);
    connect(pin_back_btn_, &QPushButton::clicked, this, [this]() { show_picker(); });

    pin_delete_btn_ = new QPushButton(QStringLiteral("Forgot PIN? Reset this user"), panel);
    pin_delete_btn_->setStyleSheet(danger_btn_style());
    pin_delete_btn_->setCursor(Qt::PointingHandCursor);
    connect(pin_delete_btn_, &QPushButton::clicked, this, &LoginScreen::on_delete_user);

    pl->addWidget(pin_username_lbl_);
    pl->addWidget(hint);
    pl->addWidget(pin_input_);
    pl->addWidget(pin_error_);
    pl->addWidget(pin_unlock_btn_);
    pl->addWidget(pin_back_btn_);
    pl->addSpacing(4);
    pl->addWidget(pin_delete_btn_);

    auto* centerer = new QHBoxLayout;
    centerer->addStretch();
    centerer->addWidget(panel);
    centerer->addStretch();
    outer->addLayout(centerer);
    outer->addStretch();
}

void LoginScreen::refresh_user_list() {
    if (!user_list_) return;
    user_list_->clear();
    const auto users = auth::AuthManager::instance().list_local_users();
    if (users.isEmpty()) {
        user_list_->hide();
        if (picker_empty_) picker_empty_->show();
        return;
    }
    if (picker_empty_) picker_empty_->hide();
    user_list_->show();

    for (const auto& u : users) {
        const QString username = u["username"].toString();
        const QString last = u["last_login_at"].toString();
        auto* item = new QListWidgetItem(user_list_);
        const QString display = last.isEmpty()
            ? username
            : QString("%1   \xe2\x80\xa2   last login %2").arg(username, last.left(10));
        item->setText(display);
        item->setData(Qt::UserRole, username);
    }
}

void LoginScreen::show_picker() {
    active_username_.clear();
    if (pin_input_) pin_input_->clear();
    if (pin_error_) pin_error_->hide();
    if (pages_)     pages_->setCurrentWidget(picker_page_);
}

void LoginScreen::show_pin_pad(const QString& username) {
    active_username_ = username;
    if (pin_username_lbl_) pin_username_lbl_->setText(username);
    if (pin_input_)        { pin_input_->clear(); pin_input_->setFocus(); }
    if (pin_error_)        pin_error_->hide();
    if (pages_)            pages_->setCurrentWidget(pin_page_);
}

void LoginScreen::on_user_selected(QListWidgetItem* item) {
    if (!item) return;
    const QString username = item->data(Qt::UserRole).toString();
    if (!username.isEmpty())
        show_pin_pad(username);
}

void LoginScreen::on_unlock() {
    const QString pin = pin_input_->text();
    if (pin.size() < 4 || pin.size() > 6) {
        pin_error_->setText(QStringLiteral("PIN must be 4 to 6 digits."));
        pin_error_->show();
        return;
    }
    if (active_username_.isEmpty()) {
        pin_error_->setText(QStringLiteral("Pick a user first."));
        pin_error_->show();
        return;
    }
    pin_error_->hide();
    pin_unlock_btn_->setEnabled(false);
    auth::AuthManager::instance().login_local(active_username_, pin);
}

void LoginScreen::on_delete_user() {
    if (active_username_.isEmpty()) return;
    auto ans = QMessageBox::warning(
        this, QStringLiteral("Reset user"),
        QString(QStringLiteral("Delete local user \"%1\"? This removes their "
                               "saved data on this machine. You will need to "
                               "create the account again."))
            .arg(active_username_),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ans != QMessageBox::Yes) return;
    auth::AuthManager::instance().delete_local_user(active_username_);
    active_username_.clear();
    show_picker();
    refresh_user_list();
}

void LoginScreen::on_login_succeeded() {
    if (pin_input_)      pin_input_->clear();
    if (pin_unlock_btn_) pin_unlock_btn_->setEnabled(true);
}

void LoginScreen::on_login_failed(const QString& error) {
    if (pin_unlock_btn_) pin_unlock_btn_->setEnabled(true);
    if (pin_error_) {
        pin_error_->setText(error);
        pin_error_->show();
    }
    if (pin_input_) pin_input_->selectAll();
}

} // namespace fincept::screens
