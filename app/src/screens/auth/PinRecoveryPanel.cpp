#include "screens/auth/PinRecoveryPanel.h"

#include "auth/AuthManager.h"
#include "auth/PinPolicy.h"
#include "core/config/StartFresh.h"
#include "screens/auth/StartFreshDialog.h"
#include "ui/theme/Theme.h"

#include <QDir>
#include <QFrame>
#include <QIntValidator>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

namespace finterm::screens {

namespace {

QString mono() {
    return QStringLiteral("font-family:'Consolas','Courier New',monospace;");
}

QString title_style() {
    return QStringLiteral("color:%1; font-size:14px; font-weight:700; letter-spacing:1px; background:transparent;")
               .arg(ui::colors::AMBER()) +
           mono();
}

QString text_style() {
    return QStringLiteral("color:%1; font-size:12px; background:transparent;").arg(ui::colors::TEXT_SECONDARY()) +
           mono();
}

QString error_style() {
    return QStringLiteral("color:%1; font-size:12px; background:rgba(220,38,38,0.08); border:1px solid #7f1d1d;"
                          " padding:6px 8px;")
               .arg(ui::colors::NEGATIVE()) +
           mono();
}

QString input_style() {
    return QStringLiteral("QLineEdit { background:%1; color:%2; border:1px solid %3; padding:8px 10px;"
                          " font-size:16px; letter-spacing:2px; %5 }"
                          "QLineEdit:focus { border:1px solid %4; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
             ui::colors::BORDER_BRIGHT(), mono());
}

/// A choice: a bold first line and a quieter, wrapping explanation; the
/// whole card is the button (mouse, or Tab then Enter/Space). A frame with a
/// layout, so its height follows the wrapped text — a QPushButton would not.
class ChoiceCard : public QFrame {
  public:
    ChoiceCard(const QString& label, const QString& detail, bool danger, std::function<void()> on_click)
        : on_click_(std::move(on_click)) {
        setObjectName(QStringLiteral("choiceCard"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        const QString accent = danger ? ui::colors::NEGATIVE() : ui::colors::AMBER();
        setStyleSheet(QStringLiteral("QFrame#choiceCard { background:%1; border:1px solid %2; }"
                                     "QFrame#choiceCard:hover, QFrame#choiceCard:focus { border-color:%3; }")
                          .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM(), accent));
        auto* vl = new QVBoxLayout(this);
        vl->setContentsMargins(12, 8, 12, 8);
        vl->setSpacing(2);
        auto* head = new QLabel(label, this);
        auto* body = new QLabel(detail, this);
        body->setWordWrap(true);
        head->setStyleSheet(QStringLiteral("color:%1; font-size:13px; font-weight:700; background:transparent; %2")
                                .arg(accent, mono()));
        body->setStyleSheet(QStringLiteral("color:%1; font-size:12px; background:transparent; %2")
                                .arg(ui::colors::TEXT_SECONDARY(), mono()));
        for (QLabel* l : {head, body}) {
            l->setAttribute(Qt::WA_TransparentForMouseEvents); // the card takes the click
            l->setTextInteractionFlags(Qt::NoTextInteraction);
            vl->addWidget(l);
        }
    }

  protected:
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && on_click_)
            on_click_();
        QFrame::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) && on_click_) {
            on_click_();
            return;
        }
        QFrame::keyPressEvent(e);
    }

  private:
    std::function<void()> on_click_;
};

QPushButton* plain_button(const QString& label) {
    auto* b = new QPushButton(label);
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedHeight(32);
    b->setStyleSheet(QStringLiteral("QPushButton { background:transparent; color:%1; border:1px solid %2;"
                                    " padding:0 14px; font-size:12px; %4 }"
                                    "QPushButton:hover { color:%3; border-color:%3; }")
                         .arg(ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(), mono()));
    return b;
}

QPushButton* primary_button(const QString& label) {
    auto* b = new QPushButton(label);
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedHeight(34);
    b->setStyleSheet(QStringLiteral("QPushButton { background:rgba(217,119,6,0.1); color:%1; border:1px solid %2;"
                                    " padding:0 16px; font-size:13px; font-weight:700; %4 }"
                                    "QPushButton:hover { background:%1; color:%3; }")
                         .arg(ui::colors::AMBER(), ui::colors::AMBER_DIM(), ui::colors::BG_BASE(), mono()));
    return b;
}

QLineEdit* pin_field(const QString& placeholder, QWidget* parent) {
    auto* e = new QLineEdit(parent);
    e->setPlaceholderText(placeholder);
    e->setEchoMode(QLineEdit::Password);
    e->setMaxLength(6);
    e->setValidator(new QIntValidator(0, 999999, e));
    e->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText);
    e->setStyleSheet(input_style());
    return e;
}

} // namespace

PinRecoveryPanel::PinRecoveryPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    // One page visible in a box (a QStackedWidget would be as tall as both).
    build_choices();
    build_key_form();
    root->addWidget(choices_);
    root->addWidget(key_form_);
    show_page(choices_);
}

void PinRecoveryPanel::build_choices() {
    choices_ = new QWidget(this);
    auto* vl = new QVBoxLayout(choices_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("FORGOT YOUR PIN?"), choices_);
    title->setStyleSheet(title_style());
    vl->addWidget(title);
    for_user_ = new QLabel(choices_);
    for_user_->setWordWrap(true);
    for_user_->setStyleSheet(text_style());
    vl->addWidget(for_user_);
    vl->addSpacing(4);

    key_btn_ = new ChoiceCard(QStringLiteral("Use my recovery key"),
                              QStringLiteral("Set a new PIN. Everything you have in finterm stays."), false, [this]() {
                                  key_input_->clear();
                                  new_pin_->clear();
                                  new_pin2_->clear();
                                  key_error_->hide();
                                  show_page(key_form_);
                                  key_input_->setFocus();
                              });
    vl->addWidget(key_btn_);
    key_note_ = new QLabel(QStringLiteral("This user has no recovery key (users created before version 4.2.2 can "
                                          "make one in Settings › Security once signed in)."),
                           choices_);
    key_note_->setWordWrap(true);
    key_note_->setStyleSheet(text_style());
    vl->addWidget(key_note_);

    auto* other = new ChoiceCard(QStringLiteral("Sign in as someone else"),
                                 QStringLiteral("Pick another user on this computer, or create a new one."), false,
                                 [this]() { emit switch_user(); });
    vl->addWidget(other);

    auto* reset_user = new ChoiceCard(QStringLiteral("Reset this user"),
                                      QStringLiteral("Remove this user — their data is moved to a backup folder — then "
                                                     "create the user again."),
                                      true, [this]() { on_reset_user(); });
    vl->addWidget(reset_user);

    auto* fresh = new ChoiceCard(QStringLiteral("Start fresh"),
                                 QStringLiteral("Set finterm up again from the beginning. Your current data is moved "
                                                "to a backup folder, not deleted."),
                                 true, [this]() { on_start_fresh(); });
    vl->addWidget(fresh);

    vl->addSpacing(4);
    auto* back = plain_button(QStringLiteral("←  Back to the PIN"));
    connect(back, &QPushButton::clicked, this, &PinRecoveryPanel::back);
    vl->addWidget(back, 0, Qt::AlignLeft);
}

void PinRecoveryPanel::build_key_form() {
    key_form_ = new QWidget(this);
    auto* vl = new QVBoxLayout(key_form_);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("SET A NEW PIN"), key_form_);
    title->setStyleSheet(title_style());
    vl->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("Type the recovery key you saved when the user was created, then choose a "
                                           "new PIN (4 to 6 digits)."),
                            key_form_);
    hint->setWordWrap(true);
    hint->setStyleSheet(text_style());
    vl->addWidget(hint);

    key_input_ = new QLineEdit(key_form_);
    key_input_->setPlaceholderText(QStringLiteral("XXXXX-XXXXX-XXXXX-XXXXX"));
    key_input_->setMaxLength(32);
    key_input_->setInputMethodHints(Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhPreferUppercase);
    key_input_->setStyleSheet(input_style());
    vl->addWidget(key_input_);
    new_pin_ = pin_field(QStringLiteral("New PIN"), key_form_);
    vl->addWidget(new_pin_);
    new_pin2_ = pin_field(QStringLiteral("New PIN again"), key_form_);
    vl->addWidget(new_pin2_);

    key_error_ = new QLabel(key_form_);
    key_error_->setWordWrap(true);
    key_error_->setStyleSheet(error_style());
    key_error_->hide();
    vl->addWidget(key_error_);

    auto* submit = primary_button(QStringLiteral("SET NEW PIN"));
    connect(submit, &QPushButton::clicked, this, &PinRecoveryPanel::on_reset_with_key);
    connect(key_input_, &QLineEdit::returnPressed, new_pin_, qOverload<>(&QWidget::setFocus));
    connect(new_pin_, &QLineEdit::returnPressed, new_pin2_, qOverload<>(&QWidget::setFocus));
    connect(new_pin2_, &QLineEdit::returnPressed, this, &PinRecoveryPanel::on_reset_with_key);
    vl->addWidget(submit);

    auto* back = plain_button(QStringLiteral("←  Other options"));
    connect(back, &QPushButton::clicked, this, [this]() { show_page(choices_); });
    vl->addWidget(back, 0, Qt::AlignLeft);
}

void PinRecoveryPanel::show_page(QWidget* page) {
    choices_->setVisible(page == choices_);
    key_form_->setVisible(page == key_form_);
}

void PinRecoveryPanel::start(const QString& username, bool signed_in) {
    username_ = username;
    signed_in_ = signed_in;
    for_user_->setText(username.isEmpty() ? QString() : QStringLiteral("User: %1").arg(username));
    const bool has_key = !username.isEmpty() && auth::AuthManager::instance().has_recovery_key(username);
    key_btn_->setVisible(has_key);
    key_note_->setVisible(!has_key);
    show_page(choices_);
}

void PinRecoveryPanel::on_reset_with_key() {
    const QString pin = new_pin_->text();
    auto fail = [this](const QString& why) {
        key_error_->setText(why);
        key_error_->show();
    };
    if (pin != new_pin2_->text()) {
        new_pin2_->clear();
        new_pin2_->setFocus();
        return fail(QStringLiteral("The two PINs differ."));
    }
    if (const QString why = auth::pin_policy::problem(pin); !why.isEmpty())
        return fail(why);
    const QString why =
        auth::AuthManager::instance().reset_pin_with_recovery_key(username_, key_input_->text(), pin);
    if (!why.isEmpty())
        return fail(why);
    key_error_->hide();
    emit pin_reset(username_, pin);
}

void PinRecoveryPanel::on_reset_user() {
    if (username_.isEmpty())
        return;
    // Moved aside, not deleted: this runs without the PIN (that is what it is
    // for), so whoever is at the screen must not be able to destroy anything.
    const QString keep_in = start_fresh::deleted_user_backup_dir(username_);
    const auto ans = QMessageBox::warning(
        window(), QStringLiteral("Reset this user"),
        QStringLiteral("Remove the user \"%1\" from finterm on this computer — with their portfolios, watchlists, "
                       "notes, chats and settings?\n\nTheir data is moved to a backup folder, not deleted:\n%2\n"
                       "(their saved notification keys are kept too, under a backup name)\n\n"
                       "Other users are not affected. Afterwards you can create the user again.")
            .arg(username_, QDir::toNativeSeparators(keep_in)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ans != QMessageBox::Yes)
        return;
    auto& auth = auth::AuthManager::instance();
    // The lock screen: the user's database is open — close the session first.
    // The device PIN stays until the delete succeeds (delete_local_user
    // retires it then, when it was this user's): a failed reset must leave
    // the user able to unlock as before.
    if (signed_in_)
        auth.logout(/*wipe_device_pin=*/false);
    const QString who = username_;
    QPointer<PinRecoveryPanel> self(this);
    QPointer<QWidget> win(window());
    auth.delete_local_user(
        who,
        [self, win, who](bool ok, const QString& error) {
            if (!ok) {
                QMessageBox::warning(win, QStringLiteral("Reset this user"),
                                     QStringLiteral("Could not reset \"%1\": %2").arg(who, error));
                return;
            }
            if (self)
                emit self->user_reset(who);
        },
        keep_in);
}

void PinRecoveryPanel::on_start_fresh() {
    StartFreshDialog::ask(window());
}

} // namespace finterm::screens
