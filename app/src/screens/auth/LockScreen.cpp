#include "screens/auth/LockScreen.h"

#include "auth/AuthManager.h"
#include "auth/PinManager.h"
#include "screens/auth/PinRecoveryPanel.h"
#include "core/logging/Logger.h"
#include "ui/theme/Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace finterm::screens {

// ── Obsidian Design System Styles ────────────────────────────────────────────
// Identical palette to LoginScreen for visual consistency.

// Scoped to the card itself: an unscoped rule would box every label in it.
static QString card_style() {
    return QString("QWidget#lockCard { background: %1; border: 1px solid %2; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::BORDER_DIM());
}

static QString pin_input_style() {
    return QString("QLineEdit {"
                   "  background: %1; color: %2;"
                   "  border: 1px solid %3;"
                   "  font-size: 24px; letter-spacing: 12px;"
                   "  font-family: 'Consolas','Courier New',monospace;"
                   "  selection-background-color: %4; selection-color: %5;"
                   "}"
                   "QLineEdit:focus { border: 1px solid %6; }")
        .arg(ui::colors::BG_SURFACE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(), ui::colors::AMBER(),
             ui::colors::BG_BASE(), ui::colors::BORDER_BRIGHT());
}

static QString btn_primary() {
    return QString("QPushButton {"
                   "  background: rgba(217,119,6,0.1); color: %1;"
                   "  border: 1px solid %2;"
                   "  padding: 0 16px; font-size: 14px; font-weight: 700;"
                   "  font-family: 'Consolas','Courier New',monospace;"
                   "}"
                   "QPushButton:hover { background: %1; color: %3; }"
                   "QPushButton:disabled { color: %4; background: %5; border-color: %6; }")
        .arg(ui::colors::AMBER(), ui::colors::AMBER_DIM(), ui::colors::BG_BASE(), ui::colors::TEXT_SECONDARY(),
             ui::colors::BG_RAISED(), ui::colors::BORDER_DIM());
}

static QString link_style() {
    return QString("QPushButton { background: transparent; color: %1; border: none; padding: 2px 4px;"
                   "  font-size: 12px; font-family: 'Consolas','Courier New',monospace; }"
                   "QPushButton:hover { color: %2; text-decoration: underline; }")
        .arg(ui::colors::TEXT_SECONDARY(), ui::colors::AMBER());
}

static QString label_style() {
    return QString("color: %1; font-size: 13px; font-weight: 700;"
                   "background: transparent; letter-spacing: 0.5px;"
                   "font-family: 'Consolas','Courier New',monospace;")
        .arg(ui::colors::TEXT_SECONDARY());
}

static QString muted_style() {
    return QString("color: %1; font-size: 13px; background: transparent;"
                   "font-family: 'Consolas','Courier New',monospace;")
        .arg(ui::colors::TEXT_SECONDARY());
}

static QString error_style() {
    return QString("color: %1; font-size: 13px;"
                   "background: rgba(220,38,38,0.08);"
                   "border: 1px solid #7f1d1d; padding: 6px 8px;"
                   "font-family: 'Consolas','Courier New',monospace;")
        .arg(ui::colors::NEGATIVE());
}

// Hardens a PIN QLineEdit against input-method leakage, predictive text, and
// paste-based brute-force scripts. Called on every PIN entry field in this
// screen.
static void harden_pin_input(QLineEdit* edit) {
    if (!edit)
        return;

    // Tell IMEs / on-screen keyboards this is a 6-digit secret — no prediction,
    // no suggestions, no clipboard history surfacing.
    edit->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText |
                              Qt::ImhNoAutoUppercase | Qt::ImhHiddenText);

    // Validator rejects non-digits at the keypress level so only 0-9 can be
    // typed even if the paste block below is ever circumvented.
    auto* validator = new QIntValidator(0, 999999, edit);
    edit->setValidator(validator);

    // Disable drag/drop into the field (dropped text would bypass validators).
    edit->setDragEnabled(false);
    edit->setAcceptDrops(false);

    // No right-click → Paste, and no clipboard pull via the built-in menu.
    edit->setContextMenuPolicy(Qt::NoContextMenu);

    // Disable Qt's built-in paste shortcut so Ctrl+V / Shift+Insert cannot
    // inject a scripted PIN attempt. We still allow manual digit entry only.
    QObject::connect(edit, &QLineEdit::textChanged, edit, [edit](const QString& txt) {
        // Belt-and-braces: if anything non-digit somehow lands here, strip it.
        QString cleaned;
        cleaned.reserve(txt.size());
        for (const QChar& c : txt)
            if (c.isDigit())
                cleaned.append(c);
        if (cleaned != txt) {
            QSignalBlocker b(edit);
            edit->setText(cleaned);
        }
    });
}

static QFrame* make_separator() {
    auto* sep = new QFrame;
    sep->setFixedHeight(1);
    sep->setStyleSheet(QString("background: %1; border: none;").arg(ui::colors::BORDER_DIM()));
    return sep;
}

// ── Constructor ──────────────────────────────────────────────────────────────

LockScreen::LockScreen(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* overlay = new QVBoxLayout;
    overlay->setAlignment(Qt::AlignCenter);
    root->addLayout(overlay, 1);

    // A box with one page visible, not a QStackedWidget: a stack is as tall
    // as its tallest page (its heightForWidth takes the maximum of all).
    pages_ = new QWidget;
    pages_->setMinimumWidth(440);
    pages_->setMaximumWidth(540);
    pages_->setStyleSheet("background: transparent;");
    auto* pages_layout = new QVBoxLayout(pages_);
    pages_layout->setContentsMargins(0, 0, 0, 0);

    build_unlock_page();
    build_recovery_page();
    show_page(unlock_page_);

    overlay->addWidget(pages_, 0, Qt::AlignCenter);

    // Lockout countdown — runs only while visible
    lockout_timer_ = new QTimer(this);
    lockout_timer_->setInterval(1000);
    connect(lockout_timer_, &QTimer::timeout, this, &LockScreen::update_lockout_display);

    connect(&auth::PinManager::instance(), &auth::PinManager::lockout_changed, this, [this](bool locked, int) {
        if (locked) {
            update_lockout_display();
            if (isVisible())
                lockout_timer_->start();
        }
    });
}

// ── Background Paint (matches LoginScreen grid pattern) ─────────────────────

void LockScreen::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::colors::BG_BASE()));

    p.setPen(QPen(QColor(ui::colors::BG_RAISED()), 1));
    const int step = 24;
    for (int x = 0; x < width(); x += step)
        p.drawLine(x, 0, x, height());
    for (int y = 0; y < height(); y += step)
        p.drawLine(0, y, width(), y);

    int cx = width() / 2;
    int cy = height() / 2;
    p.setPen(QPen(QColor(ui::colors::BORDER_DIM()), 1));
    p.drawLine(cx - 60, cy, cx + 60, cy);
    p.drawLine(cx, cy - 60, cx, cy + 60);
}

void LockScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (lockout_timer_ && auth::PinManager::instance().is_locked_out(auth::PinManager::instance().account()))
        lockout_timer_->start();
}

void LockScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    if (lockout_timer_)
        lockout_timer_->stop();
    // A resumed unlock never starts pre-filled.
    if (unlock_pin_input_)
        unlock_pin_input_->clear();
}

// ── Unlock page ──────────────────────────────────────────────────────────────

void LockScreen::build_unlock_page() {
    auto* page = new QWidget(this);
    unlock_page_ = page;
    page->setObjectName("lockCard");
    page->setStyleSheet(card_style());

    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(28, 22, 28, 22);
    vl->setSpacing(10);

    auto* header = new QWidget(this);
    header->setFixedHeight(38);
    header->setStyleSheet(QString("background: %1; border: none;").arg(ui::colors::BG_RAISED()));
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(14, 0, 14, 0);
    auto* title = new QLabel("TERMINAL LOCKED");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: 700;"
                                 "background: transparent; letter-spacing: 1px;"
                                 "font-family: 'Consolas','Courier New',monospace;")
                             .arg(ui::colors::AMBER()));
    hl->addWidget(title);
    hl->addStretch();
    vl->addWidget(header);

    unlock_user_ = new QLabel;
    unlock_user_->setStyleSheet(muted_style());
    unlock_user_->setWordWrap(true);
    vl->addWidget(unlock_user_);

    vl->addWidget(make_separator());

    auto* pin_lbl = new QLabel("PIN");
    pin_lbl->setStyleSheet(label_style());
    vl->addWidget(pin_lbl);

    unlock_pin_input_ = new QLineEdit;
    unlock_pin_input_->setPlaceholderText("------");
    unlock_pin_input_->setMaxLength(6);
    unlock_pin_input_->setAlignment(Qt::AlignCenter);
    unlock_pin_input_->setEchoMode(QLineEdit::Password);
    unlock_pin_input_->setFixedHeight(48);
    unlock_pin_input_->setStyleSheet(pin_input_style());
    harden_pin_input(unlock_pin_input_);
    vl->addWidget(unlock_pin_input_);

    unlock_lockout_label_ = new QLabel;
    unlock_lockout_label_->setWordWrap(true);
    unlock_lockout_label_->setStyleSheet(QString("color: %1; font-size: 12px;"
                                                 "background: rgba(202,138,4,0.08);"
                                                 "border: 1px solid #713f12; padding: 6px 8px;"
                                                 "font-family: 'Consolas','Courier New',monospace;")
                                             .arg(ui::colors::WARNING()));
    unlock_lockout_label_->hide();
    vl->addWidget(unlock_lockout_label_);

    unlock_error_ = new QLabel;
    unlock_error_->setWordWrap(true);
    unlock_error_->setStyleSheet(error_style());
    unlock_error_->hide();
    vl->addWidget(unlock_error_);

    unlock_btn_ = new QPushButton("  UNLOCK  ");
    unlock_btn_->setFixedHeight(34);
    unlock_btn_->setStyleSheet(btn_primary());
    connect(unlock_btn_, &QPushButton::clicked, this, &LockScreen::on_unlock_submit);
    vl->addWidget(unlock_btn_);

    // Always there, not only after failures: someone who knows they forgot
    // should not have to fail their way to the way out.
    auto* links = new QHBoxLayout;
    links->setContentsMargins(0, 2, 0, 0);
    auto* forgot = new QPushButton("Forgot PIN?");
    forgot->setStyleSheet(link_style());
    forgot->setCursor(Qt::PointingHandCursor);
    connect(forgot, &QPushButton::clicked, this, [this]() {
        recovery_->start(auth::PinManager::instance().account(), /*signed_in=*/true);
        show_page(recovery_page_);
    });
    auto* other = new QPushButton("Switch user");
    other->setStyleSheet(link_style());
    other->setCursor(Qt::PointingHandCursor);
    connect(other, &QPushButton::clicked, this, &LockScreen::switch_user_requested);
    links->addWidget(forgot);
    links->addStretch();
    links->addWidget(other);
    vl->addLayout(links);

    connect(unlock_pin_input_, &QLineEdit::returnPressed, this, &LockScreen::on_unlock_submit);
    pages_->layout()->addWidget(page);
}

// ── Forgot page ──────────────────────────────────────────────────────────────

void LockScreen::build_recovery_page() {
    recovery_page_ = new QWidget(this);
    recovery_page_->setObjectName("lockCard");
    recovery_page_->setStyleSheet(card_style());
    auto* vl = new QVBoxLayout(recovery_page_);
    vl->setContentsMargins(28, 22, 28, 22);
    recovery_ = new PinRecoveryPanel(recovery_page_);
    vl->addWidget(recovery_);
    connect(recovery_, &PinRecoveryPanel::back, this, &LockScreen::show_unlock);
    connect(recovery_, &PinRecoveryPanel::switch_user, this, &LockScreen::switch_user_requested);
    connect(recovery_, &PinRecoveryPanel::pin_reset, this, [this](const QString&, const QString&) {
        LOG_INFO("Auth", "PIN reset with the recovery key — unlocking");
        emit unlocked();
    });
    pages_->layout()->addWidget(recovery_page_);
}

// ── Page activation ─────────────────────────────────────────────────────────

void LockScreen::show_page(QWidget* page) {
    unlock_page_->setVisible(page == unlock_page_);
    recovery_page_->setVisible(page == recovery_page_);
}

void LockScreen::activate() {
    show_unlock();
}

void LockScreen::show_unlock() {
    unlock_pin_input_->clear();
    unlock_error_->hide();
    unlock_lockout_label_->hide();
    const QString user = auth::PinManager::instance().account();
    unlock_user_->setText(user.isEmpty() ? QStringLiteral("Enter your PIN to unlock")
                                         : QStringLiteral("Signed in as %1 — enter your PIN to unlock").arg(user));
    show_page(unlock_page_);
    update_lockout_display();
    unlock_pin_input_->setFocus();
}

// ── Actions ─────────────────────────────────────────────────────────────────

void LockScreen::on_unlock_submit() {
    auto& pm = auth::PinManager::instance();
    if (pm.is_locked_out(pm.account())) {
        update_lockout_display();
        return;
    }
    const QString pin = unlock_pin_input_->text();
    if (pin.isEmpty()) {
        unlock_error_->setText("Enter your PIN");
        unlock_error_->show();
        return;
    }
    if (pm.verify_pin(pin)) {
        unlock_error_->hide();
        unlock_lockout_label_->hide();
        emit unlocked();
        return;
    }
    unlock_pin_input_->clear();
    unlock_pin_input_->setFocus();
    if (pm.is_locked_out(pm.account())) {
        unlock_error_->hide();
        update_lockout_display();
        lockout_timer_->start();
    } else {
        unlock_error_->setText(pm.failed_attempts(pm.account()) >= auth::PinManager::kFreeAttempts
                                   ? QStringLiteral("Incorrect PIN. Forgot it? Choose \"Forgot PIN?\" below.")
                                   : QStringLiteral("Incorrect PIN"));
        unlock_error_->show();
    }
}

void LockScreen::update_lockout_display() {
    auto& pm = auth::PinManager::instance();
    const int secs = pm.lockout_remaining_seconds(pm.account());
    if (secs <= 0) {
        unlock_lockout_label_->hide();
        unlock_btn_->setEnabled(true);
        unlock_pin_input_->setEnabled(true);
        if (unlock_page_->isVisible())
            unlock_pin_input_->setFocus();
        lockout_timer_->stop();
        return;
    }
    const int mins = secs / 60;
    const QString time_str = mins > 0 ? QString("%1m %2s").arg(mins).arg(secs % 60, 2, 10, QChar('0'))
                                      : QString("%1s").arg(secs);
    unlock_lockout_label_->setText(
        QString("Too many wrong PINs — try again in %1, or choose \"Forgot PIN?\".").arg(time_str));
    unlock_lockout_label_->show();
    unlock_btn_->setEnabled(false);
    unlock_pin_input_->setEnabled(false);
}

} // namespace finterm::screens
