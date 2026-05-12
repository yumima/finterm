#include "screens/auth/RegisterScreen.h"

#include "auth/AuthManager.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QIntValidator>
#include <QPainter>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

QString panel_style() {
    return QString(
        "QWidget#authPanel { background:%1; border:1px solid %2; border-radius:6px; }"
    ).arg(fincept::ui::colors::BG_SURFACE(), fincept::ui::colors::BORDER_MED());
}

QString input_style() {
    return QString(
        "QLineEdit { background:%1; color:%2; border:1px solid %3; border-radius:3px;"
        "  padding:8px 12px; font-size:14px; }"
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

QString link_btn_style() {
    return QString(
        "QPushButton { background:transparent; color:%1; border:none;"
        "  padding:8px; font-size:12px; }"
        "QPushButton:hover { color:%2; text-decoration:underline; }"
    ).arg(fincept::ui::colors::TEXT_SECONDARY(), fincept::ui::colors::AMBER());
}

} // namespace

RegisterScreen::RegisterScreen(QWidget* parent) : QWidget(parent) {
    build_form();

    auto& auth = auth::AuthManager::instance();
    connect(&auth, &auth::AuthManager::signup_succeeded, this, [this]() {
        emit navigate_login();
    });
    connect(&auth, &auth::AuthManager::signup_failed, this, [this](const QString& err) {
        error_label_->setText(err);
        error_label_->show();
        create_btn_->setEnabled(true);
    });
}

void RegisterScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(fincept::ui::colors::BG_BASE()));
}

void RegisterScreen::hideEvent(QHideEvent* event) {
    if (username_)    username_->clear();
    if (pin_)         pin_->clear();
    if (pin_confirm_) pin_confirm_->clear();
    if (error_label_) error_label_->hide();
    QWidget::hideEvent(event);
}

void RegisterScreen::build_form() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addStretch();

    auto* panel = new QWidget(this);
    panel->setObjectName("authPanel");
    panel->setStyleSheet(panel_style());
    panel->setFixedWidth(420);
    auto* pl = new QVBoxLayout(panel);
    pl->setContentsMargins(28, 28, 28, 28);
    pl->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Create local account"), panel);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700; letter-spacing:0.4px;")
                             .arg(fincept::ui::colors::TEXT_PRIMARY()));

    auto* subtitle = new QLabel(
        QStringLiteral("Pick a username and a 4\xe2\x80\x936 digit PIN. "
                       "Everything stays on this machine."),
        panel);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QString("color:%1; font-size:12px;").arg(fincept::ui::colors::TEXT_SECONDARY()));

    username_ = new QLineEdit(panel);
    username_->setPlaceholderText(QStringLiteral("Username"));
    username_->setMaxLength(32);
    username_->setStyleSheet(input_style());
    username_->setValidator(new QRegularExpressionValidator(
        QRegularExpression("^[a-zA-Z0-9_-]{1,32}$"), this));

    pin_ = new QLineEdit(panel);
    pin_->setPlaceholderText(QStringLiteral("PIN (4\xe2\x80\x936 digits)"));
    pin_->setEchoMode(QLineEdit::Password);
    pin_->setMaxLength(6);
    pin_->setValidator(new QIntValidator(0, 999999, this));
    pin_->setStyleSheet(input_style());

    pin_confirm_ = new QLineEdit(panel);
    pin_confirm_->setPlaceholderText(QStringLiteral("Confirm PIN"));
    pin_confirm_->setEchoMode(QLineEdit::Password);
    pin_confirm_->setMaxLength(6);
    pin_confirm_->setValidator(new QIntValidator(0, 999999, this));
    pin_confirm_->setStyleSheet(input_style());

    error_label_ = new QLabel(panel);
    error_label_->setStyleSheet(QString("color:%1; font-size:12px; padding:4px 0;")
                                     .arg(fincept::ui::colors::NEGATIVE()));
    error_label_->setWordWrap(true);
    error_label_->hide();

    create_btn_ = new QPushButton(QStringLiteral("CREATE ACCOUNT"), panel);
    create_btn_->setStyleSheet(primary_btn_style());
    create_btn_->setCursor(Qt::PointingHandCursor);
    connect(create_btn_, &QPushButton::clicked, this, &RegisterScreen::on_create);

    back_btn_ = new QPushButton(QStringLiteral("\xe2\x86\x90  Back to login"), panel);
    back_btn_->setStyleSheet(link_btn_style());
    back_btn_->setCursor(Qt::PointingHandCursor);
    connect(back_btn_, &QPushButton::clicked, this, &RegisterScreen::navigate_login);

    pl->addWidget(title);
    pl->addWidget(subtitle);
    pl->addSpacing(8);
    pl->addWidget(username_);
    pl->addWidget(pin_);
    pl->addWidget(pin_confirm_);
    pl->addWidget(error_label_);
    pl->addWidget(create_btn_);
    pl->addWidget(back_btn_);

    auto* centerer = new QHBoxLayout;
    centerer->addStretch();
    centerer->addWidget(panel);
    centerer->addStretch();
    root->addLayout(centerer);
    root->addStretch();

    connect(pin_confirm_, &QLineEdit::returnPressed, this, &RegisterScreen::on_create);
}

void RegisterScreen::on_create() {
    const QString user = username_->text().trimmed().toLower();
    const QString pin  = pin_->text();
    const QString pin2 = pin_confirm_->text();

    auto show_error = [this](const QString& msg) {
        error_label_->setText(msg);
        error_label_->show();
    };

    if (user.size() < 2) {
        show_error(QStringLiteral("Username must be at least 2 characters."));
        return;
    }
    if (pin.size() < 4 || pin.size() > 6) {
        show_error(QStringLiteral("PIN must be 4 to 6 digits."));
        return;
    }
    if (pin != pin2) {
        show_error(QStringLiteral("PIN entries don't match."));
        return;
    }
    error_label_->hide();
    create_btn_->setEnabled(false);
    auth::AuthManager::instance().signup_local(user, pin);
}

} // namespace fincept::screens
